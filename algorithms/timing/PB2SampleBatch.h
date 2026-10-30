// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

// Binary sample batches from the PB2 acquisition broker (platform/pb2/acquisition/ipc.py).
// One SOCK_SEQPACKET message: a 128-byte little-endian header with the session's
// acquisition profile and one tick->UTC model, then PRU stream ABI v4 DATA packets
// exactly as the PRU sent them (platform/pb2/acquisition/protocol.h). The broker
// checks only PRU headers; every packet header and record is checked here, before
// any consumer state changes. ipc.decode_batch() states the same rules in Python,
// and both are pinned to platform/pb2/acquisition/testdata/ipc-batch-golden.json.
namespace ozmv { namespace pb2 {

enum class TimeQuality { Unavailable, UnconfirmedEpoch, GpsRelative, Holdover };

namespace wire {
inline uint16_t Le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
inline uint32_t Le32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline uint64_t Le64(const uint8_t *p) { return uint64_t(Le32(p)) | uint64_t(Le32(p + 4)) << 32; }
inline uint64_t Distance(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }
// CRC-16/CCITT-FALSE (polynomial 0x1021, initial 0xFFFF): the ADS131M08 frame
// CRC, binascii.crc_hqx(data, 0xffff) in protocol.py.
inline uint16_t AdcCrc(const uint8_t *data, size_t size) {
    static const std::array<uint16_t, 256> table = [] {
        std::array<uint16_t, 256> t{};
        for(unsigned i = 0; i < 256; ++i) {
            uint16_t c = uint16_t(i << 8);
            for(int bit = 0; bit < 8; ++bit) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
            t[i] = c;
        }
        return t;
    }();
    uint16_t crc = 0xffff;
    for(size_t i = 0; i < size; ++i) crc = uint16_t((crc << 8) ^ table[((crc >> 8) ^ data[i]) & 0xff]);
    return crc;
}
} // namespace wire

// The session's acquisition profile (protocol.py PROFILES): ADC OSR, exact rate
// and the PRU firmware's cadence gate, which is a build constant (1% for
// OSR4096, 10% for OSR128), never derived from the rate.
struct InputProfile {
    uint32_t Osr = 0, RateNumerator = 0, RateDenominator = 0, PruHz = 0, CadenceToleranceTicks = 0;
    uint64_t TicksPerSample = 0;
    bool Known() const { return TicksPerSample != 0; }
    uint64_t MinimumTicks() const { return TicksPerSample - CadenceToleranceTicks; }
    uint64_t MaximumTicks() const { return TicksPerSample + CadenceToleranceTicks; }
    double Hz() const { return Known() ? double(RateNumerator) / RateDenominator : 0.0; }
    bool operator==(const InputProfile &o) const {
        return Osr == o.Osr && RateNumerator == o.RateNumerator && RateDenominator == o.RateDenominator &&
               PruHz == o.PruHz && CadenceToleranceTicks == o.CadenceToleranceTicks;
    }
    bool operator!=(const InputProfile &o) const { return !(*this == o); }
    static constexpr uint32_t SupportedPruHz() { return 250000000; } // eCAP tick clock of every PRU build
    static bool ValidOsr(uint64_t osr) {
        static const uint64_t codes[] = {128, 256, 512, 1024, 2048, 4096, 8192, 16256}; // ADS131M08 CLOCK.OSR
        for(uint64_t value : codes) if(osr == value) return true;
        return false;
    }
    // Throws on anything the consumer cannot gate exactly. A zero tolerance
    // selects the 1% gate of a schema-1 JSON envelope that declares none.
    static InputProfile Make(uint64_t osr, uint64_t numerator, uint64_t denominator, uint64_t pruHz,
                             uint64_t toleranceTicks) {
        if(!ValidOsr(osr) || !numerator || numerator > UINT32_MAX || !denominator || denominator > UINT32_MAX ||
           pruHz != SupportedPruHz())
            throw std::runtime_error("Unsupported physical input profile");
        const uint64_t product = pruHz * denominator; // < 2^28 * 2^32
        if(product % numerator || !(product / numerator) || product / numerator > UINT32_MAX)
            throw std::runtime_error("Physical input profile has no integer PRU tick cadence");
        InputProfile profile;
        profile.Osr = uint32_t(osr);
        profile.RateNumerator = uint32_t(numerator);
        profile.RateDenominator = uint32_t(denominator);
        profile.PruHz = uint32_t(pruHz);
        profile.TicksPerSample = product / numerator;
        if(!toleranceTicks) toleranceTicks = (profile.TicksPerSample + 99) / 100;
        if(toleranceTicks >= profile.TicksPerSample) throw std::runtime_error("Invalid physical cadence tolerance");
        profile.CadenceToleranceTicks = uint32_t(toleranceTicks);
        return profile;
    }
};

// live_clock.LiveClock.mapping_model(): one causal snapshot labelling every
// sample of a batch, with map_with_model()'s exact rule. Invalid when unknown:
// an unavailable model, or a tick outside [LowTick, HighTick], has no epoch.
struct TimeModel {
    bool Available = false, Confirmed = false, Poisoned = false;
    uint64_t Generation = 0, LowTick = 0, HighTick = 0, AnchorTick = 0, AnchorUTCNs = 0, TickSpan = 0, UTCSpanNs = 0;
    struct Label {
        TimeQuality Quality = TimeQuality::Unavailable;
        uint64_t CandidateUTCNs = 0;
        bool HasCandidate = false, EpochConfirmed = false;
    };
    // ipc.check_model(): these bounds keep Map() exact in 64-bit arithmetic.
    static constexpr uint64_t SpanLimit() { return 1ULL << 31; }
    static constexpr uint64_t UTCMinimum() { return 1ULL << 35; }
    static constexpr uint64_t UTCMaximum() { return 1ULL << 62; }
    void Validate(uint32_t pruHz) const {
        if(!Generation || (Poisoned && Available)) throw std::runtime_error("Invalid physical clock model");
        if(!Available) {
            if(Confirmed || LowTick || HighTick || AnchorTick || AnchorUTCNs || TickSpan || UTCSpanNs)
                throw std::runtime_error("Unavailable physical clock model carries a mapping");
            return;
        }
        if(!TickSpan || TickSpan > SpanLimit() || !UTCSpanNs || UTCSpanNs > SpanLimit() ||
           wire::Distance(LowTick, AnchorTick) > SpanLimit() || wire::Distance(HighTick, AnchorTick) > SpanLimit())
            throw std::runtime_error("Physical clock model outside its bounds");
        // Within 1% of the PRU clock's nominal nanoseconds per tick.
        const uint64_t actual = UTCSpanNs * pruHz, nominal = TickSpan * 1000000000ULL;
        if(wire::Distance(actual, nominal) > nominal / 100 || AnchorUTCNs < UTCMinimum() || AnchorUTCNs > UTCMaximum())
            throw std::runtime_error("Physical clock model outside its bounds");
    }
    // live_clock._rounded_ratio: nearest integer, ties to even, positive denominator.
    static int64_t RoundedRatio(int64_t numerator, int64_t denominator) {
        int64_t quotient = numerator / denominator, remainder = numerator % denominator;
        if(remainder < 0) { --quotient; remainder += denominator; } // Floor division, as in Python
        const int64_t doubled = 2 * remainder;
        return quotient + ((doubled > denominator || (doubled == denominator && (quotient & 1))) ? 1 : 0);
    }
    Label Map(uint64_t tick) const {
        Label label;
        if(!Available || tick < LowTick || tick > HighTick) return label;
        const int64_t offset = tick >= AnchorTick ? int64_t(tick - AnchorTick) : -int64_t(AnchorTick - tick);
        const int64_t delta = RoundedRatio(offset * int64_t(UTCSpanNs), int64_t(TickSpan));
        label.CandidateUTCNs = uint64_t(int64_t(AnchorUTCNs) + delta);
        label.HasCandidate = true;
        label.EpochConfirmed = Confirmed;
        label.Quality = !Confirmed ? TimeQuality::UnconfirmedEpoch :
                        tick > AnchorTick ? TimeQuality::Holdover : TimeQuality::GpsRelative;
        return label;
    }
};

// One decoded PRU record (ozmv_stream_sample): Raw[0:30) is the ADS131M08
// response, Raw[30] the PRU's validity flag, Raw[31] its DRDY glitch count.
struct BatchRecord {
    uint64_t Sequence = 0, Tick = 0;
    std::array<int32_t, 8> Values{};
    bool Valid = false;
    uint8_t Glitches = 0;
};

// A checked, non-owning view of one batch message. Parse() checks the header,
// profile and model; ForEachRecord() checks and decodes the packets.
struct BatchView {
    enum : size_t { HeaderBytes = 128, PruHeaderBytes = 96, RecordBytes = 48 };
    std::array<uint8_t, 16> Session{};
    uint64_t Sequence = 0;
    bool ClockSynchronized = false;
    uint32_t StartupRecords = 0; // Records with a lower sequence are checked, never delivered
    InputProfile Profile;
    TimeModel Model;
    const uint8_t *Packets = nullptr;
    size_t PacketBytes = 0;
    uint32_t PacketCount = 0;

    static bool IsBatch(const void *data, size_t size) { return size >= 4 && std::memcmp(data, "OZB1", 4) == 0; }
    std::string SessionHex() const {
        static const char digits[] = "0123456789abcdef";
        std::string hex(32, '0');
        for(size_t i = 0; i < Session.size(); ++i) {
            hex[2 * i] = digits[Session[i] >> 4];
            hex[2 * i + 1] = digits[Session[i] & 15];
        }
        return hex;
    }
    static BatchView Parse(const void *data, size_t size) {
        using namespace wire;
        const uint8_t *b = static_cast<const uint8_t*>(data);
        if(size < HeaderBytes) throw std::runtime_error("Short physical batch");
        if(std::memcmp(b, "OZB1", 4) || Le16(b + 4) != 1 || Le16(b + 6) != HeaderBytes || Le32(b + 8) != size)
            throw std::runtime_error("Unknown physical batch format or length");
        BatchView view;
        view.PacketCount = Le32(b + 12);
        std::memcpy(view.Session.data(), b + 16, view.Session.size());
        bool session = false;
        for(uint8_t byte : view.Session) session |= byte != 0;
        const uint32_t flags = Le32(b + 40), modelFlags = Le32(b + 68);
        if(!view.PacketCount || !session || (flags & ~1U) || (modelFlags & ~7U) || !Le32(b + 64))
            throw std::runtime_error("Invalid physical batch header");
        view.Sequence = Le64(b + 32);
        view.ClockSynchronized = flags & 1U;
        view.StartupRecords = Le32(b + 44);
        view.Profile = InputProfile::Make(Le32(b + 48), Le32(b + 52), Le32(b + 56), Le32(b + 60), Le32(b + 64));
        TimeModel &model = view.Model;
        model.Available = modelFlags & 1U;
        model.Confirmed = modelFlags & 2U;
        model.Poisoned = modelFlags & 4U;
        model.Generation = Le64(b + 72);
        model.LowTick = Le64(b + 80);
        model.HighTick = Le64(b + 88);
        model.AnchorTick = Le64(b + 96);
        model.AnchorUTCNs = Le64(b + 104);
        model.TickSpan = Le64(b + 112);
        model.UTCSpanNs = Le64(b + 120);
        model.Validate(view.Profile.PruHz);
        view.Packets = b + HeaderBytes;
        view.PacketBytes = size - HeaderBytes;
        return view;
    }
};

// Checks every PRU packet header (ABI v4 DATA, RUNNING, no error or loss, this
// session, consecutive PacketSequence, NextSample after its last record) and
// record (tick not after its packet's Now, consecutive sequence and rising tick
// across the whole batch, known flag, CRC-16 and STATUS of a valid frame), then
// calls visit(record) in order. Cadence is the consumer's gate, not a reject.
template<class Visit> void ForEachRecord(const BatchView &batch, Visit &&visit) {
    using namespace wire;
    const uint8_t *packet = batch.Packets, *const end = batch.Packets + batch.PacketBytes;
    bool havePacket = false, haveRecord = false;
    uint32_t lastPacket = 0;
    uint64_t lastSequence = 0, lastTick = 0;
    BatchRecord record;
    for(uint32_t n = 0; n < batch.PacketCount; ++n) {
        if(size_t(end - packet) < BatchView::PruHeaderBytes) throw std::runtime_error("Truncated PRU packet");
        const uint32_t count = Le32(packet + 44);
        const uint64_t size = Le32(packet + 12);
        if(Le32(packet) != 0x32534150U || Le32(packet + 4) != 4 || Le32(packet + 8) != 2 || !count ||
           size != BatchView::PruHeaderBytes + uint64_t(count) * BatchView::RecordBytes || size > uint64_t(end - packet))
            throw std::runtime_error("Invalid PRU DATA packet");
        if(Le32(packet + 20) != 1 || Le32(packet + 88) || Le32(packet + 92) ||
           std::memcmp(packet + 24, batch.Session.data(), batch.Session.size()))
            throw std::runtime_error("Foreign or faulty PRU DATA packet");
        const uint32_t number = Le32(packet + 40);
        if(havePacket && number != uint32_t(lastPacket + 1)) throw std::runtime_error("Lost or replayed PRU packet");
        const uint64_t now = Le64(packet + 56);
        for(const uint8_t *at = packet + BatchView::PruHeaderBytes; at < packet + size; at += BatchView::RecordBytes) {
            record.Tick = Le64(at);
            record.Sequence = Le64(at + 8);
            const uint8_t *raw = at + 16;
            if(record.Tick > now) throw std::runtime_error("Record after its packet time");
            if(haveRecord && (lastSequence == UINT64_MAX || record.Sequence != lastSequence + 1 || record.Tick <= lastTick))
                throw std::runtime_error("Discontinuous batch records");
            if(raw[30] > 1) throw std::runtime_error("Unknown ADC sample flag");
            record.Valid = raw[30] == 0;
            record.Glitches = raw[31];
            if(record.Valid) {
                if(AdcCrc(raw, 27) != (uint16_t(raw[27]) << 8 | raw[28])) throw std::runtime_error("ADC CRC mismatch");
                if(raw[0] != 0x81 || raw[1] != 0xff) throw std::runtime_error("ADC STATUS mismatch");
                for(unsigned ch = 0; ch < 8; ++ch) {
                    const uint8_t *v = raw + 3 + 3 * ch;
                    const int32_t value = int32_t(uint32_t(v[0]) << 16 | uint32_t(v[1]) << 8 | v[2]);
                    record.Values[ch] = (value ^ 0x800000) - 0x800000; // Signed 24-bit, big-endian
                }
            } else {
                record.Values.fill(0); // Whatever the PRU shifted in failed its CRC: never a measurement
            }
            lastSequence = record.Sequence;
            lastTick = record.Tick;
            haveRecord = true;
            visit(static_cast<const BatchRecord&>(record));
        }
        if(lastSequence == UINT64_MAX || Le64(packet + 48) != lastSequence + 1)
            throw std::runtime_error("Inconsistent producer sample counter");
        lastPacket = number;
        havePacket = true;
        packet += size;
    }
    if(packet != end) throw std::runtime_error("Trailing physical batch bytes");
}

}} // namespace ozmv::pb2
