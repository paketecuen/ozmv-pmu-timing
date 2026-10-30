// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace ozmv { namespace signal {

// The cadence is metadata, never a replacement for the sample's time axis.
// Keeping the ratio avoids turning PB2's 1e9/1015808 SPS into an invented 984 Hz.
struct SampleRateRatio {
    uint32_t Numerator, Denominator;
    SampleRateRatio(uint32_t numerator = 0, uint32_t denominator = 1)
        : Numerator(numerator), Denominator(denominator) {}
    bool Valid() const { return Numerator != 0 && Denominator != 0; }
    double Hz() const { return Valid() ? double(Numerator) / Denominator : 0; }
    double NyquistHz() const { return Hz() / 2; }
    // Necessary sampling bound only: does not certify the analogue response,
    // anti-alias filter, FFT window or any particular harmonic measurement.
    unsigned HarmonicCeiling(uint32_t fundamentalHz, unsigned requested = 50) const {
        if(!Valid() || !fundamentalHz) return 0;
        // Strictly BELOW Nyquist, including an exact equality boundary. Divide
        // in stages so arbitrary uint32 inputs cannot overflow their products.
        const uint64_t ceiling = ((uint64_t(Numerator)-1) / (2ULL*Denominator)) / fundamentalHz;
        return ceiling < requested ? static_cast<unsigned>(ceiling) : requested;
    }
};

enum class EpochAssociation : uint8_t { Unspecified, Unavailable, Candidate, Confirmed };
enum class ScaleBasis : uint8_t { Unspecified, DeviceConfiguration, NominalCircuit, Calibrated };
enum class ChannelMapBasis : uint8_t { Unspecified, DeviceConfiguration, Documented, Verified };

// Shared by the existing AnalyzerSoft tap and PB2. Voltage slots [0,3) are
// volts; current slots [3,7) are amperes ONLY within the declared channel count.
// An absent sensor has zero channels, not a measured zero or an assumed scale.
// Producers must keep every slot finite (unused slots stay zero), and change
// Generation when their mapping, scale or acquisition continuity changes.
// Epoch association does not assert UTC accuracy, calibrated latency or PMU
// qualification. Unspecified preserves the existing analyzer's provenance.
struct EngineeringSample {
    uint64_t TimestampNs = 0;
    uint64_t Sequence = 0;
    uint64_t Generation = 0;
    uint32_t SampleRate = 0;
    uint32_t SampleRateDenominator = 1;
    uint8_t VoltageChannels = 0;
    uint8_t CurrentChannels = 0;
    uint8_t TimeSource = 0;
    double Values[7] = {};
    bool Valid = true;
    uint64_t ClockGeneration = 0;
    EpochAssociation Epoch = EpochAssociation::Unspecified;
    // Expanded (k = 2) uncertainty of TimestampNs against UTC, from the
    // producer's timing authority (PB2: platform/pb2/timing/sample-time.md);
    // 0 = unknown. Only with a Confirmed epoch can it make a PMU report locked.
    uint32_t TimeUncertaintyNs = 0;
    // The time was extrapolated through a loss of the producer's lock (#197):
    // UTC-traceable within TimeUncertaintyNs, but not locked. A PMU report
    // whose window holds one says so (C37.118 STAT bit 13 and unlocked time).
    bool Holdover = false;
    ScaleBasis Scale = ScaleBasis::Unspecified;
    ChannelMapBasis ChannelMap = ChannelMapBasis::Unspecified;

    SampleRateRatio Rate() const { return SampleRateRatio(SampleRate, SampleRateDenominator); }
};

}} // namespace ozmv::signal
