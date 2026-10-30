// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"
#include "PB2CadenceFit.h"
#include "PB2SampleBatch.h"

// Diagnostic raw-input consumer. No phase map, mains scaling, PQ persistence or
// PMU publication is implied by accepting physical ADC samples here.
// Two inputs from the acquisition broker: binary sample batches (ConsumeBatch,
// PB2SampleBatch.h) and the schema-1 JSON envelope (Consume), which now carries
// GPS/timing/transport status with no samples; an envelope that still carries
// samples (at most one PRU packet's worth, the earlier broker) is accepted too.
namespace ozmv { namespace pb2 {
class SampleState {
public:
    using TimeQuality = ozmv::pb2::TimeQuality;
    struct Sample {
        uint64_t seq = 0, tick = 0;
        std::array<int32_t, 8> values{};
        uint64_t clockGeneration = 0;
        TimeQuality quality = TimeQuality::Unavailable;
        uint64_t candidateUTCNs = 0, utcNs = 0;
        bool hasCandidate = false, epochConfirmed = false;
        // Binary batches (platform/pb2/timing/sample-time.md): candidateUTCNs
        // and utcNs are the conversion-ready time of the fitted DRDY envelope
        // once drdyFitted, the observed DRDY tick before. inputUTCNs is that
        // time less the ADC filter delay, what the filtered value represents
        // (analogue front end still uncalibrated); 0 with no candidate.
        // timeUncertaintyNs: expanded (k = 2) uncertainty of utcNs against
        // UTC; 0 = unknown (unconfirmed epoch, unfitted DRDY, or no measured
        // clock uncertainty from the broker yet).
        uint64_t inputUTCNs = 0;
        uint32_t timeUncertaintyNs = 0;
        bool drdyFitted = false;
        // false: the PRU tolerated an isolated ADC CRC error for this slot
        // (protocol.py SAMPLE_INVALID_CRC). Its sequence/tick are still real
        // and part of the continuous producer stream; values are zero and
        // must never be read as a measurement (see PB2AnalyzerFeed/PhysicalProcessor).
        bool valid = true;
    };
    // The unit PhysicalProcessor and AnalyzerFeed consume. A batch of any size
    // reaches them as consecutive chunks of up to this many samples; it is not
    // the PRU packet size, which each packet header declares (protocol.h Count).
    enum : size_t { PacketCapacity = 8 };
    struct Packet {
        std::array<Sample, PacketCapacity> samples{};
        size_t count = 0;
        std::string session;
        bool sessionChanged = false, discontinuity = false, transportRunning = false;
        InputProfile profile; // The session's declared profile; unknown only for a hand-built packet
    };
    // ConsumeBatch() result; the samples themselves go to the caller's vector.
    struct Batch {
        std::string session;
        bool sessionChanged = false, discontinuity = false, clockSynchronized = false;
        InputProfile profile;
        std::vector<size_t> discontinuities; // Indexes of samples that start a sequence or cadence gap
        size_t excluded = 0;                 // Records below the startup exclusion: checked, not delivered
    };
private:
    using json = nlohmann::json;
    struct Channel {
        int32_t last = 0, min = 0, max = 0;
        double mean = 0, m2 = 0;
        uint64_t count = 0;
        void Add(int32_t value) {
            last = value;
            if(!count) min = max = value;
            if(value < min) min = value;
            if(value > max) max = value;
            ++count;
            const double delta = value - mean;
            mean += delta / count;
            m2 += delta * (value - mean);
        }
    };
    std::array<Channel, 8> channels{};
    std::string session;
    InputProfile profile;
    uint64_t frames = 0, generation = 0, discontinuities = 0, rejected = 0;
    uint64_t lastSequence = 0, firstTick = 0, lastTick = 0, lastReceiveUs = 0, lastDataUs = 0;
    uint64_t cadenceErrors = 0, disconnects = 0;
    // Samples the PRU flagged invalid (tolerated isolated ADC CRC error):
    // excluded from the raw channel statistics below, never from Frames or
    // the sequence/cadence continuity checks.
    uint64_t invalidSamples = 0;
    uint64_t startupExcluded = 0, batchSequence = 0;
    bool connected = false, haveBatch = false;
    const char *inputFormat = nullptr; // Format of the last message that carried samples
    json gps = json::object(), timing = json::object(), transport = json::object();
    CadenceFit fit;                    // Of the current session's binary batches
    InputProfile fitProfile;           // The profile fit was configured for
    uint32_t lastTimeUncertaintyNs = 0;

    // Fixed input paths (PRU GPI synchronizer, PPS path), 1 sigma, until the
    // CH3 self-calibration measures them (sample-time.md, #191).
    static constexpr double PathUncertaintyNs() { return 8.0; }
    // The broker's expanded clock uncertainty (Timing.ClockUncertaintyNs); 0 while unknown.
    double ClockUncertaintyNs() const {
        const auto found = timing.find("ClockUncertaintyNs");
        return (found != timing.end() && found->is_number() && found->get<double>() > 0) ? found->get<double>() : 0.0;
    }
    // Expanded (k = 2) uncertainty of a fitted sample's time; 0 when unknown.
    uint32_t SampleTimeUncertaintyNs(double clockNs) const {
        const auto &stats = fit.GetStats();
        if(clockNs <= 0 || !stats.Fitted) return 0;
        const double fitNs = stats.Uncertainty * 1e9 / InputProfile::SupportedPruHz();
        return uint32_t(std::ceil(2 * std::sqrt(std::pow(clockNs / 2, 2) + fitNs * fitNs +
                                                    PathUncertaintyNs() * PathUncertaintyNs())));
    }

    static uint64_t TimestampNs(const json &value) {
        const uint64_t n = Decimal(value);
        // Leave the same arithmetic margin as the sample-based PMU estimator.
        if(!n || n > UINT64_MAX - 2000000000ULL)
            throw std::runtime_error("Invalid physical sample epoch");
        return n;
    }
    static uint64_t Unsigned(const json &value, const char *error) {
        if(!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
            throw std::runtime_error(error);
        return value.get<uint64_t>();
    }
    static InputProfile DecodeProfile(const json &message) {
        for(const char *key : {"Schema", "OSR", "RateNumerator", "RateDenominator"})
            if(!message.at(key).is_number_integer()) throw std::runtime_error("Invalid profile field type");
        if(message.at("Schema") != 1) throw std::runtime_error("Unsupported physical input profile");
        // Absent: the 250 MHz PRU clock and the 1% gate of the earlier, OSR4096-only broker.
        const auto pruHz = message.find("PruHz"), tolerance = message.find("CadenceToleranceTicks");
        uint64_t toleranceTicks = 0; // InputProfile::Make: zero selects that 1% gate
        if(tolerance != message.end()) {
            toleranceTicks = Unsigned(*tolerance, "Invalid physical cadence tolerance");
            if(!toleranceTicks) throw std::runtime_error("Invalid physical cadence tolerance");
        }
        return InputProfile::Make(Unsigned(message.at("OSR"), "Unsupported physical input profile"),
            Unsigned(message.at("RateNumerator"), "Unsupported physical input profile"),
            Unsigned(message.at("RateDenominator"), "Unsupported physical input profile"),
            pruHz == message.end() ? InputProfile::SupportedPruHz() :
                Unsigned(*pruHz, "Unsupported physical input profile"), toleranceTicks);
    }
    static void DecodeClock(const json &input, Sample &sample) {
        const auto &generation = input.at("ClockGeneration");
        if(!generation.is_number_integer() ||
           (!generation.is_number_unsigned() && generation.get<int64_t>() <= 0) ||
           generation.get<uint64_t>() == 0)
            throw std::runtime_error("Invalid physical clock generation");
        sample.clockGeneration = generation.get<uint64_t>();
        const auto &quality = input.at("TimeQuality");
        if(!quality.is_string()) throw std::runtime_error("Invalid physical time quality");
        const std::string name = quality.get<std::string>();
        if(name == "unavailable") sample.quality = TimeQuality::Unavailable;
        else if(name == "unconfirmed_epoch") sample.quality = TimeQuality::UnconfirmedEpoch;
        else if(name == "gps_relative") sample.quality = TimeQuality::GpsRelative;
        else if(name == "holdover") sample.quality = TimeQuality::Holdover;
        else throw std::runtime_error("Unknown physical time quality");
        const auto &candidate = input.at("CandidateUTCNs");
        const auto &utc = input.at("UTCNs");
        if(sample.quality == TimeQuality::Unavailable) {
            if(!candidate.is_null() || !utc.is_null())
                throw std::runtime_error("Unavailable physical time carries an epoch");
            return;
        }
        sample.candidateUTCNs = TimestampNs(candidate);
        sample.hasCandidate = true;
        if(sample.quality == TimeQuality::UnconfirmedEpoch) {
            if(!utc.is_null()) throw std::runtime_error("Unconfirmed physical time claims UTC");
            return;
        }
        sample.utcNs = TimestampNs(utc);
        if(sample.utcNs != sample.candidateUTCNs)
            throw std::runtime_error("Physical UTC and candidate disagree");
        sample.epochConfirmed = true; // Civil epoch only; no accuracy qualification.
    }
    void StartSession(const std::string &incoming) {
        session = incoming; ++generation; frames = firstTick = lastTick = lastSequence = 0;
        channels = std::array<Channel,8>{};
        haveBatch = false;
    }
    // Continuity, cadence and channel statistics of one accepted sample.
    bool Account(const Sample &sample) {
        bool gap = false;
        if(frames) {
            if(sample.seq != lastSequence + 1) { ++discontinuities; gap = true; }
            const uint64_t delta = sample.tick - lastTick;
            if(delta < profile.MinimumTicks() || delta > profile.MaximumTicks()) { ++cadenceErrors; gap = true; }
        } else firstTick = sample.tick;
        // A flagged sample still advances Frames/sequence/cadence (the producer
        // stream stays continuous); only its zeroed values are excluded from
        // the raw channel statistics.
        if(sample.valid) for(size_t ch = 0; ch < 8; ++ch) channels[ch].Add(sample.values[ch]);
        else ++invalidSamples;
        ++frames; lastSequence = sample.seq; lastTick = sample.tick;
        return gap;
    }
public:
    static uint64_t Decimal(const json &value) {
        if(!value.is_string()) throw std::runtime_error("Counter must be a decimal string");
        const std::string s = value.get<std::string>();
        if(s.empty() || s.size() > 20) throw std::runtime_error("Invalid counter");
        uint64_t n = 0;
        for(char c : s) {
            if(c < '0' || c > '9' || n > (UINT64_MAX - (c - '0')) / 10)
                throw std::runtime_error("Invalid counter");
            n = n * 10 + c - '0';
        }
        return n;
    }
    const InputProfile &Profile() const { return profile; }
    void Disconnect() {
        if(connected) ++disconnects;
        connected = false;
    }
    void Reject() { ++rejected; Disconnect(); }
    Packet Consume(const json &message, uint64_t nowUs) {
        const InputProfile declared = DecodeProfile(message);
        const std::string incoming = message.at("Session").get<std::string>();
        if(incoming.size() != 32 || incoming.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("Invalid producer session");
        if(incoming == session && profile.Known() && declared != profile)
            throw std::runtime_error("Inconsistent physical input profile");
        const auto &samples = message.at("Samples");
        if(!samples.is_array() || samples.size() > PacketCapacity || !message.at("GPS").is_object() ||
           !message.at("Timing").is_object() || !message.at("Transport").is_object())
            throw std::runtime_error("Invalid input envelope");
        for(const char *key : {"CrcErrors", "StatusErrors", "InvalidSamples"}) {
            const auto &value = message.at("Transport").at(key);
            if(!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
                throw std::runtime_error("Invalid transport counter");
        }
        const auto &incomingTransport = message.at("Transport");
        if(!incomingTransport.at("State").is_string()) throw std::runtime_error("Invalid transport state");
        const std::string state = incomingTransport.at("State").get<std::string>();
        if(state != "starting" && state != "idle" && state != "running" && state != "fault")
            throw std::runtime_error("Unknown transport state");
        const auto &error = incomingTransport.at("Error");
        bool noError = false;
        if(error.is_number_integer()) {
            // PRU stream ABI v4 error codes 0..19 (acquisition/protocol.h; 19 = DRDY glitch burst).
            if((!error.is_number_unsigned() && error.get<int64_t>() < 0) || error.get<uint64_t>() > 19)
                throw std::runtime_error("Invalid transport error");
            noError = error.get<uint64_t>() == 0;
        } else if(!error.is_string() ||
                  (error != "host_packet_validation" && error != "host_acquisition_failure")) {
            throw std::runtime_error("Unknown transport error");
        }
        Packet decoded;
        decoded.count = samples.size();
        decoded.session = incoming;
        decoded.sessionChanged = incoming != session;
        decoded.transportRunning = state == "running" && noError;
        decoded.profile = declared;
        for(size_t i = 0; i < samples.size(); ++i) {
            auto &sample = decoded.samples[i];
            sample.seq = Decimal(samples[i].at("Sequence"));
            sample.tick = Decimal(samples[i].at("Ticks"));
            DecodeClock(samples[i], sample);
            const auto &validField = samples[i].at("Valid");
            if(!validField.is_boolean()) throw std::runtime_error("Invalid physical sample validity flag");
            sample.valid = validField.get<bool>();
            const auto &raw = samples[i].at("Raw");
            if(sample.valid) {
                if(!raw.is_array() || raw.size() != 8) throw std::runtime_error("Invalid ADC channels");
                for(size_t ch = 0; ch < 8; ++ch) {
                    if(!raw[ch].is_number_integer() ||
                       (raw[ch].is_number_unsigned() ? raw[ch].get<uint64_t>() > 8388607 :
                        (raw[ch].get<int64_t>() < -8388608 || raw[ch].get<int64_t>() > 8388607)))
                        throw std::runtime_error("Invalid signed 24-bit ADC value");
                    sample.values[ch] = raw[ch].get<int32_t>();
                }
            } else {
                // A tolerated isolated ADC CRC error (see Sample::valid): the
                // producer never sends channel data for a slot it already
                // knows is corrupt, so there is nothing here to validate.
                if(!raw.is_null()) throw std::runtime_error("Invalid sample carries ADC channels");
                sample.values.fill(0);
            }
            if(i && (decoded.samples[i-1].seq == UINT64_MAX || sample.seq != decoded.samples[i-1].seq + 1 ||
                     sample.tick <= decoded.samples[i-1].tick))
                throw std::runtime_error("Discontinuous packet");
        }
        if(incoming == session && frames && !samples.empty() &&
           (decoded.samples[0].seq <= lastSequence || decoded.samples[0].tick <= lastTick))
            throw std::runtime_error("Replayed physical sample");
        // Validate the complete message before changing any sample statistics.
        if(incoming != session) StartSession(incoming);
        profile = declared;
        for(size_t i = 0; i < samples.size(); ++i) decoded.discontinuity |= Account(decoded.samples[i]);
        gps = message.at("GPS"); timing = message.at("Timing"); transport = message.at("Transport");
        connected = true; lastReceiveUs = nowUs;
        if(!samples.empty()) { lastDataUs = nowUs; inputFormat = "json-envelope-v1"; }
        return decoded;
    }
    // One binary batch (PB2SampleBatch.h). Every record is checked and labelled
    // with the batch's clock model before any state changes; a batch implies a
    // RUNNING producer, so GPS/timing/transport stay those of the last status.
    Batch ConsumeBatch(const BatchView &batch, uint64_t nowUs, std::vector<Sample> &out) {
        out.clear();
        Batch result;
        result.session = batch.SessionHex();
        result.sessionChanged = result.session != session;
        result.clockSynchronized = batch.ClockSynchronized;
        result.profile = batch.Profile;
        if(!result.sessionChanged && profile.Known() && batch.Profile != profile)
            throw std::runtime_error("Inconsistent physical input profile");
        if(!result.sessionChanged && haveBatch && batch.Sequence <= batchSequence)
            throw std::runtime_error("Replayed physical batch");
        const TimeModel &model = batch.Model;
        ForEachRecord(batch, [&](const BatchRecord &record) {
            if(record.Sequence < batch.StartupRecords) { ++result.excluded; return; }
            out.emplace_back();
            Sample &sample = out.back();
            sample.seq = record.Sequence;
            sample.tick = record.Tick;
            sample.values = record.Values;
            sample.valid = record.Valid;
            sample.clockGeneration = model.Generation;
        });
        if(!result.sessionChanged && frames && !out.empty() && (out[0].seq <= lastSequence || out[0].tick <= lastTick))
            throw std::runtime_error("Replayed physical sample");
        // Everything is valid: commit.
        if(result.sessionChanged) {
            StartSession(result.session);
            gps = json::object(); timing = json::object(); transport = json::object(); // Another producer's
        }
        profile = batch.Profile;
        if(result.sessionChanged || fitProfile != profile) {
            fit.Configure(profile);
            fitProfile = profile;
        }
        // Each sample in order: continuity (the observed tick), the DRDY fit,
        // which restarts at any gap, then its labels from the fitted tick.
        const double clockNs = ClockUncertaintyNs();
        const uint64_t filterDelayNs = FilterDelayNs(profile);
        // A fitted sample's uncertainty changes only with the fit (once a
        // block) or the clock (once a batch): computed then, not per sample.
        uint64_t uncertaintyFit = UINT64_MAX;
        uint32_t uncertaintyNs = 0;
        for(size_t i = 0; i < out.size(); ++i) {
            Sample &sample = out[i];
            if(Account(sample)) {
                result.discontinuity = true;
                result.discontinuities.push_back(i);
                fit.Restart();
            }
            const uint64_t tick = fit.Push(sample.seq, sample.tick, sample.drdyFitted);
            const TimeModel::Label label = model.Map(tick);
            sample.quality = label.Quality;
            sample.hasCandidate = label.HasCandidate;
            sample.epochConfirmed = label.EpochConfirmed;
            sample.candidateUTCNs = label.CandidateUTCNs;
            sample.utcNs = label.EpochConfirmed ? label.CandidateUTCNs : 0;
            sample.inputUTCNs = label.HasCandidate && label.CandidateUTCNs > filterDelayNs ?
                label.CandidateUTCNs - filterDelayNs : 0;
            if(label.EpochConfirmed && sample.drdyFitted && fit.GetStats().Fits != uncertaintyFit) {
                uncertaintyFit = fit.GetStats().Fits;
                uncertaintyNs = SampleTimeUncertaintyNs(clockNs);
            }
            sample.timeUncertaintyNs = label.EpochConfirmed && sample.drdyFitted ? uncertaintyNs : 0;
            lastTimeUncertaintyNs = sample.timeUncertaintyNs;
        }
        startupExcluded += result.excluded;
        batchSequence = batch.Sequence;
        haveBatch = true;
        connected = true; lastReceiveUs = nowUs;
        if(!out.empty()) lastDataUs = nowUs;
        inputFormat = "binary-batch-v1";
        return result;
    }
    json Status(uint64_t nowUs) const {
        const bool disconnected = !connected || nowUs < lastReceiveUs || nowUs - lastReceiveUs > 1500000;
        const bool stale = disconnected || !frames || nowUs < lastDataUs || nowUs - lastDataUs > 1500000;
        json rows = json::array();
        for(size_t ch = 0; ch < 8; ++ch) {
            const auto &c = channels[ch];
            rows.push_back({{"AIN", ch}, {"Count", c.count}, {"Last", c.last}, {"Min", c.min},
                {"Max", c.max}, {"Mean", c.mean}, {"StdDev", c.count ? std::sqrt(c.m2 / c.count) : 0},
                {"ADCInputVolts", c.last * (1.2 / 8388608.0)}});
        }
        json currentGPS = gps, currentTiming = timing;
        if(disconnected) {
            currentGPS["Fix"] = false; currentGPS["Stale"] = true;
            currentGPS["Latitude"] = nullptr; currentGPS["Longitude"] = nullptr;
            currentTiming["Association"] = "unavailable";
            currentTiming["UTC"] = nullptr; currentTiming["CandidateUTC"] = nullptr;
        }
        // The profile is the producer's declaration; null until a message arrived.
        const bool known = profile.Known();
        // How each binary-batch sample is timed (platform/pb2/timing/sample-time.md).
        const auto &f = fit.GetStats();
        const double tickNs = 1e9 / InputProfile::SupportedPruHz();
        auto ns = [&](bool available, double ticks) { return available ? json(ticks * tickNs) : json(nullptr); };
        const json cadence = {{"State", f.Fitted ? "fitted" : "warming"}, {"WindowSamples", f.WindowSamples},
            {"BlockSamples", f.BlockSamples}, {"Fits", f.Fits}, {"Restarts", f.Restarts},
            {"EarlySamples", f.EarlySamples}, {"FittedSamples", f.FittedSamples}, {"UnfittedSamples", f.UnfittedSamples},
            {"LatenessNs", f.Fits ? json{{"Min", f.LatenessMin * tickNs}, {"P50", f.LatenessP50 * tickNs},
                                         {"P99", f.LatenessP99 * tickNs}, {"Max", f.LatenessMax * tickNs}} : json(nullptr)},
            {"EnvelopeScatterNs", ns(f.Fits, f.Scatter)}, {"UncertaintyNs", ns(f.Fits, f.Uncertainty)}};
        const double clockNs = ClockUncertaintyNs();
        const uint64_t filterUncertaintyNs = FilterDelayUncertaintyNs(profile);
        const uint32_t sampleNs = lastTimeUncertaintyNs;
        const json sampleTime = {
            {"Basis", "Conversion-ready time from the fitted DRDY envelope; the input time is that less the computed "
                      "ADC filter delay; analogue front-end delay not calibrated"},
            {"CadenceFit", cadence},
            {"FilterDelayNs", known ? json(FilterDelayNs(profile)) : json(nullptr)},
            {"FilterDelayUncertaintyNs", known ? json(filterUncertaintyNs) : json(nullptr)},
            {"PathUncertaintyNs", PathUncertaintyNs()},
            {"ClockUncertaintyNs", clockNs > 0 ? json(clockNs) : json(nullptr)},
            {"SampleTimeUncertaintyNs", sampleNs ? json(sampleNs) : json(nullptr)},
            {"InputTimeUncertaintyNs", sampleNs && known ?
                json(std::ceil(2 * std::sqrt(std::pow(sampleNs / 2.0, 2) + double(filterUncertaintyNs * filterUncertaintyNs)))) :
                json(nullptr)},
            {"Within250ns", sampleNs ? json(sampleNs <= 250) : json(nullptr)}};
        return {{"Connected", connected}, {"Stale", stale}, {"TransportStale", disconnected}, {"Session", session},
            {"Generation", generation}, {"Frames", frames}, {"RejectedPackets", rejected},
            {"SequenceDiscontinuities", discontinuities}, {"CadenceDiscontinuities", cadenceErrors},
            {"Disconnects", disconnects}, {"LastSequence", std::to_string(lastSequence)},
            {"LastPRUTicks", std::to_string(lastTick)},
            {"CrcErrors", transport.value("CrcErrors", uint64_t(0))},
            {"StatusErrors", transport.value("StatusErrors", uint64_t(0))},
            {"InvalidSamples", invalidSamples}, {"StartupExcluded", startupExcluded},
            {"SampleRateNominal", known ? json(profile.Hz()) : json(nullptr)},
            {"SampleRateNumerator", known ? json(profile.RateNumerator) : json(nullptr)},
            {"SampleRateDenominator", known ? json(profile.RateDenominator) : json(nullptr)},
            {"TicksPerSample", known ? json(profile.TicksPerSample) : json(nullptr)},
            {"CadenceToleranceTicks", known ? json(profile.CadenceToleranceTicks) : json(nullptr)},
            {"SampleRateObserved", frames > 1 && lastTick > firstTick ?
                double(frames - 1) * 250000000.0 / double(lastTick - firstTick) : 0.0},
            {"OSR", known ? json(profile.Osr) : json(nullptr)}, {"PGA", 1}, {"ClockSource", "PWM"}, {"X1Present", false},
            {"InputFormat", inputFormat ? json(inputFormat) : json(nullptr)},
            {"InputUnits", "ADC differential volts (nominal)"}, {"Channels", rows},
            {"GPS", currentGPS}, {"Timing", currentTiming}, {"SampleTime", sampleTime}, {"Transport", transport},
            {"ChannelMapVerified", false}, {"CalibrationVerified", false},
            {"MeasurementQualified", false}, {"TimingQualified", false},
            // The decoded samples feed the PB2_3P_NOMINAL analyzer (DevicePB2); that
            // output is unqualified physical PQ. DevicePB2 reports the
            // configured feed state over this constant.
            {"PhysicalPQFeed", true}, {"PhysicalPQQualified", false}, {"PhysicalPMU", false}};
    }
};

// When a sample was captured, for its age (DevicePB2's DataAge, #137): the
// confirmed GPS epoch, or else the candidate one, which is all a board without
// a time reference ever has. 0 when the sample carries neither.
inline uint64_t CaptureUTCNs(const SampleState::Sample &sample, bool &confirmed) {
    confirmed = sample.epochConfirmed;
    if(sample.epochConfirmed) return sample.utcNs;
    return sample.hasCandidate ? sample.candidateUTCNs : 0;
}
}} // namespace ozmv::pb2
