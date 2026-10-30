// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "PB2SampleBatch.h"
#include "correlation.h"
#include "text.h"

// Bounded causal PRU/PPS/NMEA time labels; no hardware access or clock setting
// (live_clock.py). All mappings describe observed DRDY, not analogue
// aperture/effective input time. A GPS-relative label and a separately
// confirmed civil epoch do not qualify PMU time accuracy. Newer-than-anchor
// samples are explicitly holdover/extrapolation. Position returned by
// Diag() belongs only in a private/authenticated API.
namespace ozmv { namespace pb2 { namespace broker {

using TimeModel = ozmv::pb2::TimeModel; // PB2SampleBatch.h: the model the service applies per sample

constexpr int64_t kNs = 1000000000;
constexpr int64_t kLatencyNs = 900000000;
constexpr int64_t kHoldoverNs = 2 * kNs;
constexpr size_t kMaxEdges = 8;
constexpr size_t kMaxPendingNmea = 64;
constexpr size_t kMaxMapBatch = 256;

// How the anchors' civil epoch may be confirmed (platform/pb2/timing/sample-time.md, step 1).
enum class EpochPolicy {
    ReferenceOnly,   // Only a fresh independent coarse reference confirms it
    ReceiverProfile, // A fresh reference decides; without one, kReceiverEpochAnchors consecutive anchors do
};
// Gate T1's receiver-specific association: RMC/GGA follow their own pulse within
// kLatencyNs (verified for the bench receiver against NTP/SSH on 2026-10-09).
constexpr uint64_t kReceiverEpochAnchors = 10;
// The three-anchor model's error, measured at every new anchor over this many anchors;
// the clock uncertainty is declared once kModelErrorMinimum 2 s errors exist.
constexpr size_t kModelErrorWindow = 64;
constexpr size_t kModelErrorMinimum = 16;
// Assumed 1-sigma error of the receiver's PPS against UTC: u-blox standard
// precision datasheets give 30 ns RMS. Not measured for the bench receiver.
constexpr int64_t kReceiverUncertaintyNs = 30;

// A Python Counter: keys in the order of their first increment.
class Counters {
public:
    void Add(const std::string &key, uint64_t count = 1);
    uint64_t Get(std::string_view key) const;
    const std::vector<std::pair<std::string, uint64_t>> &Items() const { return items; }
    Json ToJson() const;
    bool operator==(const Counters &other) const { return items == other.items; }
private:
    std::vector<std::pair<std::string, uint64_t>> items;
};

struct ClockEvent {
    std::string reason;
    std::optional<uint64_t> pps_sequence;
    uint64_t generation = 0;
    bool operator==(const ClockEvent &o) const {
        return reason == o.reason && pps_sequence == o.pps_sequence && generation == o.generation;
    }
};

struct Position {
    double latitude = 0, longitude = 0;
    bool operator==(const Position &o) const { return latitude == o.latitude && longitude == o.longitude; }
};

// One tick labelled with a model (map_with_model()). Python's constant fields
// (absolute_uncertainty_ns, effective_measurement_utc_ns: None;
// pmu_time_qualified: False) are not repeated here.
struct Mapping {
    uint64_t generation = 0;
    bool valid = false;
    std::string quality = "unavailable", tracking_state = "acquiring";
    std::optional<i128> candidate_utc_ns, utc_ns, extrapolation_ns;
    bool epoch_confirmed = false;
    uint64_t tick = 0;
    bool operator==(const Mapping &o) const {
        return generation == o.generation && valid == o.valid && quality == o.quality &&
               tracking_state == o.tracking_state && candidate_utc_ns == o.candidate_utc_ns && utc_ns == o.utc_ns &&
               extrapolation_ns == o.extrapolation_ns && epoch_confirmed == o.epoch_confirmed && tick == o.tick;
    }
};

// The model's prediction error at new anchors, 1 s and 2 s past its newest
// anchor (ns): RMS and largest magnitude over the last kModelErrorWindow.
struct ModelError {
    uint64_t samples_1s = 0, samples_2s = 0;
    double rms_1s_ns = 0, max_1s_ns = 0, rms_2s_ns = 0, max_2s_ns = 0;
    bool operator==(const ModelError &o) const {
        return samples_1s == o.samples_1s && samples_2s == o.samples_2s && rms_1s_ns == o.rms_1s_ns &&
               max_1s_ns == o.max_1s_ns && rms_2s_ns == o.rms_2s_ns && max_2s_ns == o.max_2s_ns;
    }
};
// {Rms1sNs, Max1sNs, Rms2sNs, Max2sNs, Samples}: null values until measured.
Json ModelErrorJson(const ModelError &error);

// LiveClock.diagnostic(). The defaults are what the broker assumes for a
// field a clock does not report (the Python broker's dict.get defaults).
struct Diagnostic {
    std::string boot_id, producer_session;
    uint64_t generation = 0;
    std::string quality = "unavailable", tracking_state = "acquiring";
    std::string confirmation_basis; // "reference", "receiver_profile" or empty while unconfirmed
    uint64_t consecutive_anchors = 0;
    ModelError model_error;
    std::optional<int64_t> clock_uncertainty_ns; // Expanded (k = 2); unknown until enough model errors
    bool epoch_confirmed = false, requires_new_session = false, fix = false;
    std::optional<Position> position;
    std::optional<int64_t> satellites;
    bool gps_stale = true;
    std::optional<double> nmea_age_ms, pps_age_ms, packet_age_ms;
    uint64_t anchor_count = 0, pps_count = 0, pending_nmea_count = 0, retained_pps_count = 0;
    std::optional<i128> reference_age_ns;
    Counters counters;
    std::vector<ClockEvent> events;
    // The private report's "timing" leaves out position and both identities.
    Json ToJson(bool private_fields) const;
    bool operator==(const Diagnostic &o) const;
};

struct AnchorView {
    uint64_t sequence = 0, tick = 0;
    int64_t utc_ns = 0;
    correlation::Bounds bounds;
};

// What the broker needs from its clock. LiveClock is the implementation;
// tests substitute their own.
class Clock {
public:
    virtual ~Clock() = default;
    // request_bracketed is ONLY for a nonce-matched request/response: before
    // must precede sending the request, after must follow its response.
    virtual void ObservePacket(uint64_t current_ticks, uint64_t pps_count, uint64_t pps_ticks, int64_t before_ns,
                               int64_t after_ns, bool request_bracketed) = 0;
    virtual bool RequiresNewSession() const = 0;
    virtual void ObserveNmea(std::string_view line, int64_t received_ns) = 0;
    virtual TimeModel MappingModel(int64_t now_ns) = 0;
    virtual Mapping MapTick(uint64_t tick, int64_t now_ns) = 0;
    virtual Diagnostic Diag(int64_t now_ns) = 0;
    virtual std::optional<AnchorView> LatestAnchor() const = 0;
    virtual uint64_t AcceptedAnchors() const = 0;
    // nullptr is Python's None: clears the reference. Throws ValueError.
    virtual void SetReference(const Json *reference) = 0;
};

// The only tick->UTC rule: LiveClock::MapTicks() uses it, and the service
// applies the same model fields per sample (PB2SampleBatch.h TimeModel::Map).
Mapping MapWithModel(const TimeModel &model, uint64_t tick);
// Exact nearest integer, ties to even; the denominator must be positive.
i128 RoundedRatio(i128 numerator, i128 denominator);

class LiveClock final : public Clock {
public:
    // Throws ValueError without both identities.
    LiveClock(std::string boot_id, std::string producer_session, const Json *reference = nullptr,
              EpochPolicy policy = EpochPolicy::ReferenceOnly);

    void ObservePacket(uint64_t current_ticks, uint64_t pps_count, uint64_t pps_ticks, int64_t before_ns,
                       int64_t after_ns, bool request_bracketed) override {
        ObservePacket(current_ticks, pps_count, pps_ticks, before_ns, after_ns, request_bracketed, nullptr, nullptr);
    }
    // With the producer's identities, which must be this clock's.
    void ObservePacket(uint64_t current_ticks, uint64_t pps_count, uint64_t pps_ticks, int64_t before_ns,
                       int64_t after_ns, bool request_bracketed, const std::string *boot_id,
                       const std::string *producer_session);
    bool RequiresNewSession() const override { return poisoned_; }
    void ObserveNmea(std::string_view line, int64_t received_ns) override;
    TimeModel MappingModel(int64_t now_ns) override;
    Mapping MapTick(uint64_t tick, int64_t now_ns) override;
    // At most kMaxMapBatch ticks under one snapshot, in input order.
    std::vector<Mapping> MapTicks(const std::vector<uint64_t> &ticks, int64_t now_ns);
    Diagnostic Diag(int64_t now_ns) override;
    std::optional<AnchorView> LatestAnchor() const override;
    uint64_t AcceptedAnchors() const override { return counters_.Get("accepted_anchors"); }
    void SetReference(const Json *reference) override;

    uint64_t Generation() const { return generation_; }
    const Counters &CounterValues() const { return counters_; }

    // Test seams: the bound formulas (to compare with an independent
    // reference) and the number of civil-epoch checks made.
    using ReachableFunction = correlation::Bounds (*)(correlation::Bounds, i128, i128, i128);
    using EdgeFunction = correlation::Bounds (*)(i128, i128, i128, i128, i128, i128);
    void SetBoundFunctions(ReachableFunction reachable, EdgeFunction edge) {
        reachable_bounds_ = reachable;
        edge_bounds_ = edge;
    }
    uint64_t ReferenceChecks() const { return reference_checks_; }

private:
    struct Rmc {
        int64_t utc_ns = 0;
        Position position;
        bool operator==(const Rmc &o) const { return utc_ns == o.utc_ns && position == o.position; }
    };
    struct Gga {
        int64_t second_of_day = 0;
        Position position;
        int64_t satellites = 0;
        bool operator==(const Gga &o) const {
            return second_of_day == o.second_of_day && position == o.position && satellites == o.satellites;
        }
    };
    struct Edge {
        uint64_t sequence = 0, tick = 0;
        correlation::Bounds bounds;
        std::optional<Rmc> rmc;
        std::optional<Gga> gga;
        bool invalid = false, closed = false;
    };
    struct Anchor {
        uint64_t sequence = 0, tick = 0;
        int64_t utc_ns = 0;
        correlation::Bounds bounds;
    };
    struct Pending {
        std::vector<std::string> fields;
        int64_t received = 0;
    };

    void Invalidate(const std::string &reason, std::optional<uint64_t> sequence = std::nullopt, bool poison = false);
    void ClearFix();
    void Advance(int64_t now);
    void Consume(Edge &edge, const std::vector<std::string> &fields, int64_t received);
    bool Confirmed(int64_t now);
    void MeasureModelError(const Anchor &anchor);
    Edge *FindEdge(uint64_t sequence);

    std::string boot_id_, producer_session_;
    EpochPolicy policy_;
    std::string basis_;                      // Of the last Confirmed() answer
    uint64_t run_anchors_ = 0;               // Consecutive anchors since the last invalidation
    std::deque<Anchor> history_;             // The last 4 of them, for the model error
    std::deque<int64_t> errors_1s_, errors_2s_;
    uint64_t generation_ = 1;
    Counters counters_;
    std::deque<ClockEvent> events_;          // At most 8
    std::vector<Edge> edges_;                // Insertion order, at most kMaxEdges
    std::deque<Pending> pending_;
    std::deque<Anchor> anchors_;             // At most 3
    std::optional<std::pair<uint64_t, correlation::Bounds>> bridge_;
    std::optional<uint64_t> current_tick_;
    uint64_t pps_count_ = 0, pps_tick_ = 0;
    std::optional<int64_t> last_packet_ns_, last_ordered_receipt_ns_;
    int64_t now_ = 0;
    bool poisoned_ = false, fix_ = false;
    std::optional<Position> position_;
    std::optional<int64_t> satellites_, fix_received_ns_, last_valid_pair_ns_;
    std::optional<correlation::Reference> reference_;
    std::optional<bool> anchor_confirmation_;
    ReachableFunction reachable_bounds_ = &correlation::ReachableBounds;
    EdgeFunction edge_bounds_ = &correlation::EdgeBounds;
    uint64_t reference_checks_ = 0;
};

}}} // namespace ozmv::pb2::broker
