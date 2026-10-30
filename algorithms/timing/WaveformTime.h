// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// The WMU's time track (#190, platform/pb2/timing/sample-time.md). The FLAC
// audio carries raw samples and only their count; beside it, in the same Ogg
// stream, a second logical stream carries one record per FLAC frame with the
// UTC of the frame's first and last samples, the largest uncertainty and
// flags. The samples are periodic on the ADC clock, so a receiver times every
// one of them linearly between those two. tools/wmu-time.py reads it.
// Host-buildable: no FLAC or Ogg dependency.
namespace ozmv { namespace wmu {

// What a producer knows about one sample's time.
struct SampleTime {
    uint64_t UTCNs = 0;         // Its input time, UTC ns; 0 = unknown
    uint32_t UncertaintyNs = 0; // Expanded (k = 2) uncertainty of UTCNs; 0 = unknown
    uint8_t TimeSource = 0;     // AnalyzerAggreg::TIMESOURCE_*
    bool Confirmed = false;     // Its UTC epoch is confirmed
    bool Fitted = false;        // PB2: timed from the fitted DRDY envelope
    bool Gap = false;           // It follows a discontinuity (missing samples or a restart)
    bool Invalid = false;       // Its values are not a measurement (zeros in the FLAC)
    bool Holdover = false;      // Its time was extrapolated through a loss of GPS (#197); the bound grows
};

// Record flags; bits 8-15 carry the frame's first sample's time source.
enum : uint32_t { FlagConfirmed = 1, FlagFitted = 2, FlagGap = 4, FlagTimeKnown = 8, FlagInvalid = 16, FlagHoldover = 32 };
enum : size_t { IdentBytes = 24, RecordBytes = 40 };
enum : uint16_t { Version = 1 };

// The time stream's first (beginning-of-stream) packet: "OZWTIME\0", the
// version, the record size, then the samples' exact rate as a ratio.
inline std::array<uint8_t, IdentBytes> Ident(uint32_t rateNumerator, uint32_t rateDenominator) {
    std::array<uint8_t, IdentBytes> out{};
    const char magic[8] = {'O', 'Z', 'W', 'T', 'I', 'M', 'E', 0};
    std::copy(magic, magic + 8, out.begin());
    auto put = [&](size_t at, uint64_t value, unsigned bytes) {
        for(unsigned i = 0; i < bytes; ++i) out[at + i] = uint8_t(value >> (8 * i));
    };
    put(8, Version, 2);
    put(10, RecordBytes, 2);
    put(12, rateNumerator, 4);
    put(16, rateDenominator, 4);
    return out;
}

struct Record {
    uint64_t FirstSample = 0;    // The frame's first sample, counted from the stream's start
    uint32_t Count = 0;          // Its samples
    uint32_t Flags = 0;
    uint64_t FirstNs = 0, LastNs = 0;
    uint32_t UncertaintyNs = 0;  // The frame's largest; 0 when any is unknown
};

// The record of `count` (>= 1) consecutive samples from `first`; at(index)
// returns the SampleTime of a sample by its index in the stream.
template<class At> Record Summarize(uint64_t first, uint32_t count, At at) {
    Record record;
    record.FirstSample = first;
    record.Count = count;
    bool confirmed = true, fitted = true, known = true, bounded = true, gap = false, invalid = false, holdover = false;
    uint32_t worst = 0;
    for(uint32_t i = 0; i < count; ++i) {
        const SampleTime &time = at(first + i);
        confirmed = confirmed && time.Confirmed;
        fitted = fitted && time.Fitted;
        known = known && time.UTCNs != 0;
        bounded = bounded && time.UncertaintyNs != 0;
        gap = gap || time.Gap;
        invalid = invalid || time.Invalid;
        holdover = holdover || time.Holdover;
        worst = std::max(worst, time.UncertaintyNs);
    }
    const SampleTime &firstTime = at(first);
    record.FirstNs = firstTime.UTCNs;
    record.LastNs = at(first + count - 1).UTCNs;
    record.UncertaintyNs = bounded ? worst : 0;
    record.Flags = (confirmed ? FlagConfirmed : 0) | (fitted ? FlagFitted : 0) | (gap ? FlagGap : 0) |
                   (known ? FlagTimeKnown : 0) | (invalid ? FlagInvalid : 0) | (holdover ? FlagHoldover : 0) |
                   (uint32_t(firstTime.TimeSource) << 8);
    return record;
}

// Little-endian: first sample u64, count u32, flags u32, first and last UTC
// ns u64, uncertainty ns u32, reserved u32.
inline std::array<uint8_t, RecordBytes> Encode(const Record &record) {
    std::array<uint8_t, RecordBytes> out{};
    auto put = [&](size_t at, uint64_t value, unsigned bytes) {
        for(unsigned i = 0; i < bytes; ++i) out[at + i] = uint8_t(value >> (8 * i));
    };
    put(0, record.FirstSample, 8);
    put(8, record.Count, 4);
    put(12, record.Flags, 4);
    put(16, record.FirstNs, 8);
    put(24, record.LastNs, 8);
    put(32, record.UncertaintyNs, 4);
    return out;
}

}} // namespace ozmv::wmu
