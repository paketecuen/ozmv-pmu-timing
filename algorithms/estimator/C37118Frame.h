// This file is part of openZmeter, licensed under GPL-3.0-or-later.
#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

// Pure serializers for the existing legacy C37.118 stream (Annex A of
// C37.118.2-2024). The existing AA01/AA31/AA11 SYNC words are retained.
// Serializing these frames does not qualify the measurement pipeline as a PMU.
namespace C37118 {

struct TimingQuality {
  // These are evidence supplied by a timing authority, never inferred from a
  // GPS/NTP source enum or from a plausible-looking Unix timestamp. Traceability
  // and an error bound may survive loss of lock only under a qualified holdover
  // model. MaximumErrorNs must include the timestamp's entire error budget.
  bool UtcTraceable = false;
  bool ErrorBoundKnown = false;
  uint64_t MaximumErrorNs = 0;
  bool Locked = false;
  bool UnlockDurationKnown = false;
  uint64_t UnlockedDurationUs = 0;

  // The timing authority owns the standard's pending/occurred time windows.
  // Unknown leap information must not fabricate a pending/occurred indication.
  bool LeapFlagsKnown = false;
  bool LeapPending = false;
  bool LeapOccurred = false;
  bool LeapDelete = false;
};

enum class MeasurementQualification { Unqualified, Qualified, Invalid };

struct Phasor {
  std::string Analyzer;
  std::complex<double> Voltage[3];
  std::complex<double> Current[4];
  double Frequency = 0;              // Actual Hz.
  double ROCOF = 0;                  // Hz/s; floating DFREQ uses actual units.
  uint64_t Timestamp = 0;            // Unix microseconds; quality is separate.
  uint8_t VoltageChannels = 0;
  uint8_t CurrentChannels = 0;
  TimingQuality Timing;
  // Qualified requires validation of the complete PMU measurement pipeline,
  // including phase reference, latency compensation and reporting instants.
  MeasurementQualification Qualification = MeasurementQualification::Unqualified;
};

struct FrameConfig {
  uint16_t IDCODE = 1;
  int16_t DataRate = 50;
  uint16_t NominalFrequency = 50;
  uint8_t PhasorFormat = 0x0E;
  uint32_t TimeBase = 1000000;
};

inline bool HasUtcBound(const TimingQuality &quality) {
  return quality.UtcTraceable && quality.ErrorBoundKnown;
}

inline bool IsUtcLocked(const TimingQuality &quality) {
  // A bound outside the largest representable MSG_TQ bin is a clock fault,
  // even if the producer leaves a stale lock indication set.
  return HasUtcBound(quality) && quality.Locked && quality.MaximumErrorNs <= 10000000000ULL;
}

inline uint8_t MessageTimeQuality(const TimingQuality &quality) {
  if(!HasUtcBound(quality)) return 0x0F;
  if(IsUtcLocked(quality)) return 0;
  // Table A.3 uses inclusive "within" bounds, from 1 ns through 10 s.
  uint64_t bound = 1;
  for(uint8_t code = 1; code <= 11; ++code, bound *= 10)
    if(quality.MaximumErrorNs <= bound) return code;
  return 0x0F;
}

inline uint8_t PmuTimeQuality(const TimingQuality &quality) {
  if(!HasUtcBound(quality)) return 7;
  // Table A.6 uses strict upper bounds. In particular, exactly 100 ns must
  // not advertise <100 ns, and exactly 10 ms uses the conservative code 7.
  uint64_t bound = 100;
  for(uint8_t code = 1; code <= 6; ++code, bound *= 10)
    if(quality.MaximumErrorNs < bound) return code;
  return 7;
}

inline uint8_t UnlockDurationCode(const TimingQuality &quality) {
  if(IsUtcLocked(quality)) return 0;
  if(!quality.UnlockDurationKnown) return 3; // No unknown code: use worst bin.
  if(quality.UnlockedDurationUs < 10000000ULL) return 0;
  if(quality.UnlockedDurationUs < 100000000ULL) return 1;
  // Table A.5 leaves exactly 100 s between adjacent bins; use the worse bin.
  if(quality.UnlockedDurationUs <= 1000000000ULL) return 2;
  return 3;
}

inline uint8_t TimeQualityByte(const TimingQuality &quality) {
  uint8_t flags = MessageTimeQuality(quality);
  if(quality.LeapFlagsKnown) {
    if(quality.LeapPending) flags |= 0x10;
    if(quality.LeapOccurred) flags |= 0x20;
    if(quality.LeapDelete && (quality.LeapPending || quality.LeapOccurred)) flags |= 0x40;
  }
  return flags; // Reserved bit 7 always zero.
}

inline uint16_t StatusWord(const TimingQuality &quality, MeasurementQualification qualification) {
  uint16_t dataError = qualification == MeasurementQualification::Qualified ? 0 :
                       qualification == MeasurementQualification::Invalid ? 3 : 2;
  return static_cast<uint16_t>((dataError << 14) |
         (IsUtcLocked(quality) ? 0 : 0x2000) |
         (PmuTimeQuality(quality) << 6) | (UnlockDurationCode(quality) << 4));
}

inline uint16_t CRC_CCITT(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for(uint16_t i = 0; i < len; ++i) {
    uint16_t temp = (data[i] ^ (crc >> 8)) & 0xFF;
    temp ^= temp >> 4;
    crc = (crc << 8) ^ (temp << 12) ^ (temp << 5) ^ temp;
  }
  return crc;
}

inline void WriteBE16(uint8_t *buf, uint16_t value) {
  buf[0] = value >> 8; buf[1] = value;
}
inline void WriteBE32(uint8_t *buf, uint32_t value) {
  buf[0] = value >> 24; buf[1] = value >> 16; buf[2] = value >> 8; buf[3] = value;
}
inline void WriteBEFloat(uint8_t *buf, float value) {
  static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE 754 binary32 required");
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  WriteBE32(buf, bits);
}

inline bool ValidConfig(const FrameConfig &config) {
  return config.IDCODE > 0 && config.IDCODE < 65535 && config.DataRate != 0 &&
         (config.NominalFrequency == 50 || config.NominalFrequency == 60) &&
         (config.PhasorFormat == 0x0E || config.PhasorFormat == 0x0F) &&
         config.TimeBase > 0 && config.TimeBase <= 0x00FFFFFF;
}

inline void WriteTimestamp(uint8_t *buf, uint64_t timestamp, uint32_t timeBase,
                           const TimingQuality &quality) {
  uint64_t seconds = timestamp / 1000000ULL;
  uint64_t fraction = ((timestamp % 1000000ULL) * timeBase + 500000ULL) / 1000000ULL;
  // Rounding with a coarser TIME_BASE can advance the timestamp to the next
  // second. SOC then wraps modulo 2^32 as defined in Table A.1 (year 2106).
  if(fraction >= timeBase) { fraction -= timeBase; ++seconds; }
  WriteBE32(buf, static_cast<uint32_t>(seconds));
  WriteBE32(buf + 4, static_cast<uint32_t>(fraction) |
                     (static_cast<uint32_t>(TimeQualityByte(quality)) << 24));
}

inline void WriteHeader(uint8_t *buf, uint8_t sync, uint16_t size, const FrameConfig &config,
                        uint64_t timestamp, const TimingQuality &quality) {
  buf[0] = 0xAA; buf[1] = sync;
  WriteBE16(buf + 2, size);
  WriteBE16(buf + 4, config.IDCODE);
  WriteTimestamp(buf + 6, timestamp, config.TimeBase, quality);
}

inline uint16_t BuildDataFrame(uint8_t *buf, uint16_t maxLen, const FrameConfig &config,
                              const Phasor &phasor) {
  const uint16_t frameSize = 82; // Header14 + STAT2 + 7*8 + FREQ4 + DFREQ4 + CHK2.
  if(!buf || maxLen < frameSize || !ValidConfig(config)) return 0;
  WriteHeader(buf, 0x01, frameSize, config, phasor.Timestamp, phasor.Timing);
  MeasurementQualification qualification = phasor.Qualification;
  // CFG2 advertises seven channels. Padded channels cannot masquerade as a
  // qualified measurement, even if a caller marks the available channels good.
  if((phasor.VoltageChannels != 3 || phasor.CurrentChannels != 4) &&
     qualification == MeasurementQualification::Qualified)
    qualification = MeasurementQualification::Unqualified;
  if(phasor.VoltageChannels > 3 || phasor.CurrentChannels > 4)
    qualification = MeasurementQualification::Invalid;
  bool polar = (config.PhasorFormat & 1) != 0;
  uint16_t pos = 16;
  for(unsigned channel = 0; channel < 7; ++channel) {
    std::complex<double> value(0, 0);
    if(channel < 3 && channel < phasor.VoltageChannels) value = phasor.Voltage[channel];
    if(channel >= 3 && channel - 3 < phasor.CurrentChannels) value = phasor.Current[channel - 3];
    float first = static_cast<float>(polar ? std::abs(value) : value.real());
    float second = static_cast<float>(polar ? std::arg(value) : value.imag());
    if(!std::isfinite(first) || !std::isfinite(second)) qualification = MeasurementQualification::Invalid;
    WriteBEFloat(buf + pos, first); pos += 4;
    WriteBEFloat(buf + pos, second); pos += 4;
  }
  float frequency = static_cast<float>(phasor.Frequency);
  // Floating frequency fields carry actual units. The x100 DFREQ scale applies
  // only to the signed 16-bit representation, not IEEE binary32.
  float rocof = static_cast<float>(phasor.ROCOF);
  if(!std::isfinite(frequency) || !std::isfinite(rocof)) qualification = MeasurementQualification::Invalid;
  WriteBEFloat(buf + pos, frequency); pos += 4;
  WriteBEFloat(buf + pos, rocof); pos += 4;
  WriteBE16(buf + 14, StatusWord(phasor.Timing, qualification));
  WriteBE16(buf + pos, CRC_CCITT(buf, pos));
  return frameSize;
}

inline uint16_t BuildCFG2Frame(uint8_t *buf, uint16_t maxLen, const FrameConfig &config,
                              uint64_t timestamp, const TimingQuality &quality = TimingQuality()) {
  const uint16_t frameSize = 194;
  if(!buf || maxLen < frameSize || !ValidConfig(config)) return 0;
  WriteHeader(buf, 0x31, frameSize, config, timestamp, quality);
  WriteBE32(buf + 14, config.TimeBase);
  WriteBE16(buf + 18, 1);
  std::memcpy(buf + 20, "ozmV_PMU        ", 16);
  WriteBE16(buf + 36, config.IDCODE);
  WriteBE16(buf + 38, config.PhasorFormat);
  WriteBE16(buf + 40, 7);
  WriteBE16(buf + 42, 0);
  WriteBE16(buf + 44, 0);
  const char *names[] = {"VA              ", "VB              ", "VC              ",
                         "IA              ", "IB              ", "IC              ", "IN              "};
  uint16_t pos = 46;
  for(unsigned channel = 0; channel < 7; ++channel) {
    std::memcpy(buf + pos, names[channel], 16); pos += 16;
  }
  for(unsigned channel = 0; channel < 7; ++channel) {
    WriteBE32(buf + pos, channel < 3 ? 0x00000001 : 0x01000001); pos += 4;
  }
  WriteBE16(buf + pos, config.NominalFrequency == 50 ? 1 : 0); pos += 2;
  WriteBE16(buf + pos, 1); pos += 2;
  WriteBE16(buf + pos, static_cast<uint16_t>(config.DataRate)); pos += 2;
  WriteBE16(buf + pos, CRC_CCITT(buf, pos));
  return frameSize;
}

inline uint16_t BuildHeaderFrame(uint8_t *buf, uint16_t maxLen, const FrameConfig &config,
                                uint64_t timestamp, const TimingQuality &quality = TimingQuality()) {
  const char info[] = "ozmV Smart Meter - IEEE C37.118.2 PMU - openZmeter.com";
  const uint16_t infoLen = sizeof(info) - 1;
  const uint16_t frameSize = 14 + infoLen + 2;
  if(!buf || maxLen < frameSize || !ValidConfig(config)) return 0;
  WriteHeader(buf, 0x11, frameSize, config, timestamp, quality);
  std::memcpy(buf + 14, info, infoLen);
  WriteBE16(buf + frameSize - 2, CRC_CCITT(buf, frameSize - 2));
  return frameSize;
}

} // namespace C37118
