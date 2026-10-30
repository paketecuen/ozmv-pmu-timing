// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include "nlohmann/json.hpp"
#include "PB2SampleClock.h"

namespace ozmv { namespace pb2 {

// The PQ data's timeline (#190, platform/pb2/timing/sample-time.md, step 4).
// - GPS: a sample whose epoch is confirmed is labelled with its GPS input time,
//   the instant the PMU and the WMU carry too, floored to the microsecond.
// - Holdover: when the confirmation is lost (the broker invalidates its model
//   on a late NMEA sentence or a missed pulse, and confirms it again about 10 s
//   later), the timeline goes on from the last confirmed sample at the sample
//   period measured while confirmed. GPS coming back within RejoinToleranceNs()
//   of it continues the same timeline: no re-anchor.
// - Linux: after HoldoverLimitNs() without GPS, or when the time was released
//   to it (below), the Linux-anchored SampleClock, labelled as before.
// - Provisional (#195, step 5): a session whose first sample has no known time
//   (no confirmed epoch, and the system clock not synchronized or a receiver
//   still confirming) opens a provisional window. Its labels count the nominal
//   sample period from the first sample's Linux label, and carry the window's
//   number: the analyzer measures them at once and holds what it would store.
//   The first sample with a known time closes the window with a Commit that
//   gives every provisional label its final time. The sequence counts every
//   conversion, so the mapping is exact: the GPS input time at the period GPS
//   measured, or the synchronized Linux clock when there is no receiver. Past
//   ProvisionalLimitNs() the Linux clock closes it, synchronized or not; a new
//   producer session or a broken cadence closes it as it is.
// The analyzer re-anchors (ResetSampleStream) on a sample discontinuity, as
// before, and otherwise only on a change of basis: GPS acquired outside a
// window, holdover expired, or GPS returning farther from the holdover than
// the tolerance. A commit is not a re-anchor: the labels continue through it.
class Timeline {
public:
    enum class Basis : uint8_t { Linux, Gps, Holdover };
    struct Label {
        int64_t TimeUs = 0;
        uint8_t TimeSource = SampleClock::TimeSourceSyncing();
        uint64_t Generation = 0;   // Changes at every re-anchor
        bool WallClockSet = false; // TimeUs is at or after 2020-01-01
        Basis On = Basis::Linux;
        uint64_t Window = 0;       // Non-zero: a provisional label of that window
        // The time the PMU and the WMU take (#197): TimeUs before its floor,
        // on the GPS and Holdover bases only (0 otherwise), with its expanded
        // (k = 2) bound: the sample's own under GPS, HoldoverBoundNs() since
        // the last confirmed sample in holdover (0: unknown).
        uint64_t TimeNs = 0;
        uint32_t UncertaintyNs = 0;
        uint64_t HoldoverNs = 0;   // Time since the last confirmed sample; 0 under GPS
    };
    enum class CommitKind : uint8_t { None, Gps, Linux, AsIs };
    // Closes a provisional window: the final time of each of its labels.
    struct Commit {
        uint64_t Window = 0;
        CommitKind Kind = CommitKind::None;
        int64_t ProvisionalUs = 0, FinalUs = 0; // One label of the window and its final time
        double Rate = 1.0;                      // Final microseconds per provisional microsecond
        uint8_t TimeSource = SampleClock::TimeSourceSyncing(); // Of the final labels
        int64_t Map(int64_t provisionalUs) const {
            return FinalUs + int64_t(std::llround(double(provisionalUs - ProvisionalUs) * Rate));
        }
    };
    static const char *CommitKindName(CommitKind kind) {
        return kind == CommitKind::Gps ? "gps" : kind == CommitKind::Linux ? "linux" : kind == CommitKind::AsIs ? "as-is" : "none";
    }
    // AnalyzerAggreg::TIMESOURCE_GPS (DevicePB2 static_asserts it).
    static constexpr uint8_t TimeSourceGps() { return 1; }
    // A third of the OSR128 sample period, so a rejoin keeps the labels increasing.
    static constexpr uint64_t RejoinToleranceNs() { return 10000; }
    // IEC 61000-4-30 Class A allows 20 ms. An hour at the measured period,
    // drifting with temperature well under 1 ppm, stays within a few ms.
    static constexpr uint64_t HoldoverLimitNs() { return 3600ULL * 1000000000ULL; }
    // The period is measured over successive windows of this much confirmed time.
    static constexpr uint64_t PeriodWindowNs() { return 10ULL * 1000000000ULL; }
    // The holdover's expanded bound after sinceNs without GPS (#197), once the
    // period is measured: its origin's own, plus a rate error that grows with
    // the period's age (the time from its last measurement to the holdover's
    // origin, hours after an expiry or a restart) and a frequency drift.
    // Conservative until measured (E4): on the bench, ~10 s holdovers rejoined
    // within 1.3 us (0.13 ppm), an 8-minute one 15.2 us off, and the crystal
    // moved 2.7 ppm over an afternoon (0.125 ppb/s). With a fresh period, about
    // 5 us after 10 s, 0.4 ms after 10 min, 5 ms after the hour HoldoverLimitNs()
    // allows. Status() counts the rejoins that came back outside it.
    static constexpr double HoldoverRatePpb() { return 500; }
    static constexpr double HoldoverDriftPpbPerS() { return 0.5; }
    static uint32_t HoldoverBoundNs(uint32_t originNs, uint64_t sinceNs, uint64_t periodAgeNs = 0) {
        if(!originNs) return 0; // An unknown origin bounds nothing
        const double s = double(sinceNs) / 1e9, age = double(periodAgeNs) / 1e9;
        const double rate = HoldoverRatePpb() + HoldoverDriftPpbPerS() * age;
        const double bound = double(originNs) + rate * s + 0.5 * HoldoverDriftPpbPerS() * s * s;
        return bound >= 4e9 ? 4000000000U : uint32_t(std::ceil(bound));
    }
    // A provisional window lasts at most this long: a cold receiver confirms
    // its epoch in about a minute, and the analyzer holds 5 blocks a second.
    static constexpr uint64_t ProvisionalLimitNs() { return 120ULL * 1000000000ULL; }
    // The shortest span of candidate GPS times that measures the period for a commit.
    static constexpr uint64_t CommitPeriodSpanNs() { return 1000000000ULL; }

    explicit Timeline(signal::SampleRateRatio rate = SampleAdapter::Rate(), uint64_t toleranceTicks = 0)
        : pLinux(rate, toleranceTicks), pNominalPeriodNs(1e9 * rate.Denominator / rate.Numerator),
          pPeriodNs(pNominalPeriodNs) {}
    explicit Timeline(const InputProfile &profile)
        : Timeline(signal::SampleRateRatio(profile.RateNumerator, profile.RateDenominator),
                   profile.CadenceToleranceTicks) {}

    // One call per decoded sample, with SampleClock::Map's arguments. Returns
    // true when the timeline re-anchored on this sample; Discontinuity() then
    // says whether samples are missing or new (the analyzer drops its history)
    // or only the labels moved.
    // A commit, when this sample closed a window, is ready in TakeCommit()
    // and must reach the analyzer before this sample does.
    bool Map(const SampleState::Sample &sample, const std::string &session,
             uint64_t monotonicUs, uint64_t wallUs, bool hostClockSynchronized, Label &label) {
        SampleClock::Label linux;
        const bool linuxReset = pLinux.Map(sample, session, monotonicUs, wallUs, hostClockSynchronized, linux);
        const char *why = nullptr;
        pDiscontinuity = false;
        bool start = !pInitialized, committedGps = false, committedLinux = false;
        if(linuxReset && SamplesBroken(pLinux.Reason())) {
            why = pLinux.ResetReasonName();
            pDiscontinuity = true;
            // Sequence numbers still count conversions across lost samples, so
            // a sequence gap keeps the holdover and the window; a new session
            // or a broken cadence does not, and this sample starts from nothing.
            if(pLinux.Reason() != SampleClock::ResetReason::SequenceGap) {
                if(pOpen) Close(CommitKind::AsIs, pLastTimeUs, pLastTimeUs, 1.0, SampleClock::TimeSourceSyncing(), pProvisionalLastSeq);
                Forget();
                pBasis = Basis::Linux;
                pRateValid = false;
                start = true;
            }
        }
        TrackRate(sample);
        const bool gps = sample.epochConfirmed && sample.inputUTCNs;
        if(start && !gps && !(hostClockSynchronized && !sample.hasCandidate)) Open(sample.seq, linux.TimeUs);
        else if(pOpen) {
            const int64_t provisional = ProvisionalUs(sample.seq);
            const bool expired = double(sample.seq - pProvisionalSeq) * pNominalPeriodNs >= double(ProvisionalLimitNs());
            const double period = gps ? CommitPeriodNs(sample) : 0;
            if(period > 0) { // A confirmed epoch with no measured period yet waits for a second of it
                Close(CommitKind::Gps, provisional, int64_t(sample.inputUTCNs / 1000), period / pNominalPeriodNs,
                      TimeSourceGps(), sample.seq);
                committedGps = true;
            } else if(expired || (hostClockSynchronized && !sample.hasCandidate)) {
                Close(CommitKind::Linux, provisional, linux.TimeUs, 1.0, linux.TimeSource, sample.seq);
                committedLinux = true;
            }
        }
        if(pOpen) {
            // Provisional: no basis yet, and the Linux clock's own re-anchors do not move these labels.
            pInitialized = true;
            label.TimeUs = ProvisionalUs(sample.seq);
            label.TimeSource = SampleClock::TimeSourceSyncing();
            label.WallClockSet = SampleClock::WallClockSet(label.TimeUs);
            label.On = Basis::Linux;
            label.Window = pOpen;
            // A confirmed sample waiting for the commit has its GPS time already: the
            // PMU and the WMU take it on the same generation as after the commit.
            label.TimeNs = gps ? sample.inputUTCNs : 0;
            label.UncertaintyNs = gps ? sample.timeUncertaintyNs : 0;
            label.HoldoverNs = 0;
            pLastTimeUs = label.TimeUs;
            pProvisionalLastSeq = sample.seq;
            if(why) {
                ++pGeneration;
                pReason = why;
            }
            label.Generation = pGeneration;
            return why != nullptr;
        }
        uint64_t timeNs = 0;
        if(gps) {
            const uint64_t now = sample.inputUTCNs;
            if(pBasis == Basis::Linux) {
                if(!why && !committedGps) why = "GPS time acquired"; // A GPS commit continues the labels
                ++pAcquisitions;
            } else if(pBasis == Basis::Holdover) {
                pLastRejoinNs = Distance(now, HoldoverNs(sample.seq));
                // The bound the holdover declared here, widened by this sample's own
                const uint32_t declared = DeclaredBoundNs(HoldoverNs(sample.seq));
                if(pLastRejoinNs > uint64_t(declared) + sample.timeUncertaintyNs) ++pRejoinsOverBound;
                if(declared) pWorstRejoinOfBound = std::max(pWorstRejoinOfBound, double(pLastRejoinNs) / declared);
                if(pLastRejoinNs > RejoinToleranceNs()) {
                    if(!why) why = "GPS time returned off the holdover";
                    ++pSteps;
                    pWindowSeq = UINT64_MAX; // Do not measure the period across the step
                } else ++pRejoins;
            }
            pBasis = Basis::Gps;
            Confirm(sample.seq, now, sample.timeUncertaintyNs);
            timeNs = now;
        } else if(pBasis != Basis::Linux && pHaveConfirmed && sample.seq >= pLastSeq &&
                  double(sample.seq - pLastSeq) * pPeriodNs <= double(HoldoverLimitNs())) {
            if(pBasis == Basis::Gps) ++pHoldovers;
            pBasis = Basis::Holdover;
            ++pHoldoverSamples;
            timeNs = HoldoverNs(sample.seq);
        } else {
            if(pBasis != Basis::Linux) {
                if(!why) why = pHaveConfirmed ? "GPS holdover expired" : "GPS holdover lost";
                if(pHaveConfirmed) ++pExpiries;
                Forget();
                pBasis = Basis::Linux;
            } else if(linuxReset && !why && !committedLinux) why = pLinux.ResetReasonName(); // Its own re-anchors, as before
        }
        if(why) {
            ++pGeneration;
            pReason = why;
        }
        pInitialized = true;
        if(pBasis == Basis::Linux) {
            label.TimeUs = linux.TimeUs;
            label.TimeSource = linux.TimeSource;
            label.TimeNs = 0;
            label.UncertaintyNs = 0;
            label.HoldoverNs = 0;
        } else {
            label.TimeUs = int64_t(timeNs / 1000);
            label.TimeSource = TimeSourceGps();
            label.TimeNs = timeNs;
            label.HoldoverNs = pBasis == Basis::Holdover && timeNs > pLastNs ? timeNs - pLastNs : 0;
            // Before the period is measured the holdover runs at the nominal one,
            // off by the crystal's tolerance (the bench's by 40 ppm): no bound.
            label.UncertaintyNs = pBasis == Basis::Gps ? sample.timeUncertaintyNs : DeclaredBoundNs(timeNs);
        }
        label.WallClockSet = SampleClock::WallClockSet(label.TimeUs);
        label.Generation = pGeneration;
        label.On = pBasis;
        pLastTimeUs = label.TimeUs;
        return why != nullptr;
    }

    // A stopped or faulted stream: the next sample starts over ("initialization").
    // An open window closes as it is (TakeCommit()).
    void Invalidate() {
        if(pOpen) Close(CommitKind::AsIs, pLastTimeUs, pLastTimeUs, 1.0, SampleClock::TimeSourceSyncing(), pProvisionalLastSeq);
        pLinux.Invalidate();
        Forget();
        pBasis = Basis::Linux;
        pRateValid = false;
        pInitialized = false;
    }
    // The commit of the window that the last Map() or Invalidate() closed, once.
    bool TakeCommit(Commit &commit) {
        if(!pCommitReady) return false;
        commit = pCommit;
        pCommitReady = false;
        return true;
    }
    uint64_t OpenWindow() const { return pOpen; }
    bool Discontinuity() const { return pDiscontinuity; }
    bool Initialized() const { return pInitialized; }
    uint64_t Generation() const { return pGeneration; }
    int64_t LastTimeUs() const { return pLastTimeUs; }
    const char *ResetReasonName() const { return pReason; }
    Basis On() const { return pBasis; }
    static const char *BasisName(Basis basis) {
        return basis == Basis::Gps ? "gps-input-time" : basis == Basis::Holdover ? "gps-holdover" : SampleClock::TimelineBasis();
    }
    static const char *TimelineBasis() { return "gps-input-time, holdover, linux-anchored-pru-ticks without GPS, provisional until known"; }
    // The newest label's basis, "provisional" while a window is open.
    const char *BasisNow() const { return pOpen ? "provisional" : BasisName(pBasis); }
    const SampleClock &Linux() const { return pLinux; }
    nlohmann::json Status() const {
        return {{"Basis", BasisNow()}, {"GpsAcquisitions", pAcquisitions}, {"Holdovers", pHoldovers},
                {"HoldoverSamples", pHoldoverSamples}, {"Rejoins", pRejoins}, {"GpsSteps", pSteps},
                {"HoldoverExpiries", pExpiries},
                {"LastRejoinNs", pRejoins || pSteps ? nlohmann::json(pLastRejoinNs) : nlohmann::json(nullptr)},
                // #197: how the holdover's declared bound held at each return of the GPS
                {"RejoinsOverBound", pRejoinsOverBound},
                {"WorstRejoinOfBound", pRejoins || pSteps ? nlohmann::json(pWorstRejoinOfBound) : nlohmann::json(nullptr)},
                {"MeasuredPeriodNs", pPeriodMeasured ? nlohmann::json(pPeriodNs) : nlohmann::json(nullptr)},
                {"NominalPeriodNs", pNominalPeriodNs}, {"Provisional", ProvisionalStatus()}};
    }
    nlohmann::json ProvisionalStatus() const {
        nlohmann::json last = nullptr;
        if(pLastCommit.Kind != CommitKind::None)
            last = {{"Window", pLastCommit.Window}, {"Kind", CommitKindName(pLastCommit.Kind)},
                    {"OffsetUs", pLastCommit.FinalUs - pLastCommit.ProvisionalUs},
                    {"RatePpm", (pLastCommit.Rate - 1.0) * 1e6}, {"WindowS", pLastWindowNs / 1e9},
                    {"TimeSource", pLastCommit.TimeSource}};
        return {{"Open", pOpen ? nlohmann::json(pOpen) : nlohmann::json(nullptr)}, {"Windows", pWindows},
                {"Commits", {{"Gps", pCommits[1]}, {"Linux", pCommits[2]}, {"AsIs", pCommits[3]}}}, {"LastCommit", last}};
    }

private:
    static bool SamplesBroken(SampleClock::ResetReason reason) {
        return reason == SampleClock::ResetReason::Initial || reason == SampleClock::ResetReason::SessionChange ||
               reason == SampleClock::ResetReason::SequenceGap || reason == SampleClock::ResetReason::CadenceGap;
    }
    static uint64_t Distance(uint64_t a, uint64_t b) { return a >= b ? a - b : b - a; }
    // Signed: a sample before the origin (a sequence gap backwards) maps before it.
    uint64_t HoldoverNs(uint64_t seq) const {
        return pLastNs + uint64_t(std::llround(double(int64_t(seq - pLastSeq)) * pPeriodNs));
    }
    // A confirmed sample: the holdover's origin, and the period measured over
    // the window since the last one closed.
    void Confirm(uint64_t seq, uint64_t ns, uint32_t uncertaintyNs) {
        if(pWindowSeq == UINT64_MAX || seq <= pWindowSeq || ns <= pWindowNs) {
            pWindowSeq = seq;
            pWindowNs = ns;
        } else if(ns - pWindowNs >= PeriodWindowNs()) {
            pPeriodNs = double(ns - pWindowNs) / double(seq - pWindowSeq);
            pPeriodMeasured = true;
            pPeriodAtNs = ns;
            pWindowSeq = seq;
            pWindowNs = ns;
        }
        pHaveConfirmed = true;
        pLastSeq = seq;
        pLastNs = ns;
        pLastUncertaintyNs = uncertaintyNs;
    }
    // The holdover's bound at a holdover time: none before the period is measured.
    uint32_t DeclaredBoundNs(uint64_t timeNs) const {
        if(!pPeriodMeasured) return 0;
        const uint64_t since = timeNs > pLastNs ? timeNs - pLastNs : 0;
        return HoldoverBoundNs(pLastUncertaintyNs, since, pLastNs > pPeriodAtNs ? pLastNs - pPeriodAtNs : 0);
    }
    // Nothing left to extrapolate from; the measured period is the board's, so it
    // stays, and the holdover's bound grows with its age (HoldoverBoundNs).
    void Forget() {
        pHaveConfirmed = false;
        pWindowSeq = UINT64_MAX;
    }
    // Provisional labels count the nominal period from the window's first sample.
    int64_t ProvisionalUs(uint64_t seq) const {
        return pProvisionalUs + int64_t(std::llround(double(int64_t(seq - pProvisionalSeq)) * pNominalPeriodNs / 1000.0));
    }
    void Open(uint64_t seq, int64_t linuxUs) {
        pOpen = ++pWindows;
        pProvisionalSeq = pProvisionalLastSeq = seq;
        pProvisionalUs = linuxUs;
    }
    void Close(CommitKind kind, int64_t provisionalUs, int64_t finalUs, double rate, uint8_t source, uint64_t seq) {
        pCommit.Window = pOpen;
        pCommit.Kind = kind;
        pCommit.ProvisionalUs = provisionalUs;
        pCommit.FinalUs = finalUs;
        pCommit.Rate = rate;
        pCommit.TimeSource = source;
        pCommitReady = true;
        pLastCommit = pCommit;
        pLastWindowNs = double(seq - pProvisionalSeq) * pNominalPeriodNs;
        ++pCommits[unsigned(kind)];
        pOpen = 0;
    }
    // The first candidate GPS time of the broker's current clock generation:
    // the origin of the period a GPS commit maps the window with.
    void TrackRate(const SampleState::Sample &sample) {
        if(!sample.hasCandidate || !sample.inputUTCNs) return;
        if(!pRateValid || sample.clockGeneration != pRateGeneration || sample.seq <= pRateSeq) {
            pRateValid = true;
            pRateGeneration = sample.clockGeneration;
            pRateSeq = sample.seq;
            pRateNs = sample.inputUTCNs;
        }
    }
    // The sample period a GPS commit maps the window with: measured from the
    // candidate times of this clock generation (a crystal's period does not
    // change with the broker's model), else the holdover's; 0 while neither is
    // known. The broker confirms an epoch after 10 s of it, so the first is the rule.
    double CommitPeriodNs(const SampleState::Sample &sample) const {
        if(pRateValid && pRateGeneration == sample.clockGeneration && sample.seq > pRateSeq &&
           sample.inputUTCNs >= pRateNs + CommitPeriodSpanNs()) {
            const double period = double(sample.inputUTCNs - pRateNs) / double(sample.seq - pRateSeq);
            if(std::fabs(period - pNominalPeriodNs) <= pNominalPeriodNs / 100) return period;
        }
        return pPeriodMeasured ? pPeriodNs : 0;
    }

    SampleClock pLinux;
    Basis pBasis = Basis::Linux;
    bool pInitialized = false, pDiscontinuity = false, pHaveConfirmed = false, pPeriodMeasured = false;
    uint64_t pGeneration = 0, pLastSeq = 0, pLastNs = 0, pWindowSeq = UINT64_MAX, pWindowNs = 0;
    uint32_t pLastUncertaintyNs = 0; // The last confirmed sample's: the holdover's origin
    uint64_t pPeriodAtNs = 0;        // The confirmed time that completed the last period measurement
    uint64_t pRejoinsOverBound = 0;  // Rejoins farther than the bound the holdover declared
    double pWorstRejoinOfBound = 0;  // The largest rejoin distance over its declared bound
    double pNominalPeriodNs = 0, pPeriodNs = 0;
    int64_t pLastTimeUs = 0;
    const char *pReason = "initialization";
    uint64_t pAcquisitions = 0, pHoldovers = 0, pHoldoverSamples = 0, pRejoins = 0, pSteps = 0, pExpiries = 0,
             pLastRejoinNs = 0;
    // The provisional window (#195): its number (0 when none is open), first sample and label.
    uint64_t pOpen = 0, pWindows = 0, pProvisionalSeq = 0, pProvisionalLastSeq = 0;
    int64_t pProvisionalUs = 0;
    bool pCommitReady = false;
    Commit pCommit, pLastCommit;
    double pLastWindowNs = 0;
    uint64_t pCommits[4] = {0, 0, 0, 0};
    bool pRateValid = false;
    uint64_t pRateGeneration = 0, pRateSeq = 0, pRateNs = 0;
};

}} // namespace ozmv::pb2
