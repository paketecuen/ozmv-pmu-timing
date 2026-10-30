// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_clock.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "errors.h"
#include "nmea.h"

namespace ozmv { namespace pb2 { namespace broker {
namespace {

constexpr i128 kPruHz = correlation::kPruHz;

std::string_view Last3(std::string_view text) { return text.size() > 3 ? text.substr(text.size() - 3) : text; }

const std::string &Field(const std::vector<std::string> &fields, size_t index) {
    if(index >= fields.size()) throw ValueError("list index out of range"); // Python's IndexError, same handling
    return fields[index];
}

Position PositionFrom(const std::vector<std::string> &fields, size_t latitude) {
    const std::string &north = Field(fields, latitude + 1);
    if(north != "N" && north != "S") throw ValueError("Wrong coordinate hemisphere");
    const std::string &east = Field(fields, latitude + 3);
    if(east != "E" && east != "W") throw ValueError("Wrong coordinate hemisphere");
    Position position;
    position.latitude = nmea::Coordinate(Field(fields, latitude), north);
    position.longitude = nmea::Coordinate(Field(fields, latitude + 2), east);
    return position;
}

// The integer milliseconds age as Python's int / 1e6.
double Milliseconds(i128 nanoseconds) { return double(nanoseconds) / 1e6; }

// A JSON integer the reference may carry: Python accepts any int, JSON here
// any 64-bit one (an integer beyond 64 bits arrives as a float and is refused).
bool JsonInteger(const Json &value, i128 &out) {
    if(value.is_number_unsigned()) {
        out = i128(value.get<uint64_t>());
        return true;
    }
    if(value.is_number_integer()) {
        out = i128(value.get<int64_t>());
        return true;
    }
    return false;
}

} // namespace

void Counters::Add(const std::string &key, uint64_t count) {
    for(auto &item : items)
        if(item.first == key) {
            item.second += count;
            return;
        }
    items.emplace_back(key, count);
}

uint64_t Counters::Get(std::string_view key) const {
    for(const auto &item : items)
        if(item.first == key) return item.second;
    return 0;
}

Json Counters::ToJson() const {
    Json out = Json::object();
    for(const auto &item : items) out[item.first] = item.second;
    return out;
}

Json Diagnostic::ToJson(bool private_fields) const {
    Json out = Json::object();
    auto optional = [](const std::optional<double> &value) { return value ? Json(*value) : Json(nullptr); };
    if(private_fields) {
        out["boot_id"] = boot_id;
        out["producer_session"] = producer_session;
    }
    out["generation"] = generation;
    out["quality"] = quality;
    out["tracking_state"] = tracking_state;
    out["epoch_confirmed"] = epoch_confirmed;
    out["confirmation_basis"] = confirmation_basis.empty() ? Json(nullptr) : Json(confirmation_basis);
    out["consecutive_anchors"] = consecutive_anchors;
    out["model_error"] = ModelErrorJson(model_error);
    out["clock_uncertainty_ns"] = clock_uncertainty_ns ? Json(*clock_uncertainty_ns) : Json(nullptr);
    out["absolute_uncertainty_ns"] = nullptr;
    out["pmu_time_qualified"] = false;
    out["requires_new_session"] = requires_new_session;
    out["fix"] = fix;
    if(private_fields)
        out["position"] = position ? Json({{"latitude", position->latitude}, {"longitude", position->longitude}})
                                   : Json(nullptr);
    out["satellites"] = satellites ? Json(*satellites) : Json(nullptr);
    out["gps_stale"] = gps_stale;
    out["nmea_age_ms"] = optional(nmea_age_ms);
    out["pps_age_ms"] = optional(pps_age_ms);
    out["packet_age_ms"] = optional(packet_age_ms);
    out["anchor_count"] = anchor_count;
    out["pps_count"] = pps_count;
    out["pending_nmea_count"] = pending_nmea_count;
    out["retained_pps_count"] = retained_pps_count;
    out["reference_age_ns"] = reference_age_ns ? IntegerJson(*reference_age_ns) : Json(nullptr);
    out["counters"] = counters.ToJson();
    Json list = Json::array();
    for(const auto &event : events)
        list.push_back(Json({{"reason", event.reason},
                             {"pps_sequence", event.pps_sequence ? Json(*event.pps_sequence) : Json(nullptr)},
                             {"generation", event.generation}}));
    out["events"] = list;
    return out;
}

Json ModelErrorJson(const ModelError &e) {
    auto value = [](uint64_t samples, double ns) { return samples ? Json(ns) : Json(nullptr); };
    Json out = Json::object();
    out["Rms1sNs"] = value(e.samples_1s, e.rms_1s_ns);
    out["Max1sNs"] = value(e.samples_1s, e.max_1s_ns);
    out["Rms2sNs"] = value(e.samples_2s, e.rms_2s_ns);
    out["Max2sNs"] = value(e.samples_2s, e.max_2s_ns);
    out["Samples"] = e.samples_2s;
    return out;
}

bool Diagnostic::operator==(const Diagnostic &o) const {
    return boot_id == o.boot_id && producer_session == o.producer_session && generation == o.generation &&
           quality == o.quality && tracking_state == o.tracking_state && epoch_confirmed == o.epoch_confirmed &&
           confirmation_basis == o.confirmation_basis && consecutive_anchors == o.consecutive_anchors &&
           model_error == o.model_error && clock_uncertainty_ns == o.clock_uncertainty_ns &&
           requires_new_session == o.requires_new_session && fix == o.fix && position == o.position &&
           satellites == o.satellites && gps_stale == o.gps_stale && nmea_age_ms == o.nmea_age_ms &&
           pps_age_ms == o.pps_age_ms && packet_age_ms == o.packet_age_ms && anchor_count == o.anchor_count &&
           pps_count == o.pps_count && pending_nmea_count == o.pending_nmea_count &&
           retained_pps_count == o.retained_pps_count && reference_age_ns == o.reference_age_ns &&
           counters == o.counters && events == o.events;
}

i128 RoundedRatio(i128 numerator, i128 denominator) {
    const i128 quotient = FloorDiv(numerator, denominator);
    const i128 doubled = (numerator - quotient * denominator) * 2;
    return quotient + ((doubled > denominator || (doubled == denominator && (quotient & 1))) ? 1 : 0);
}

Mapping MapWithModel(const TimeModel &model, uint64_t tick) {
    // Endpoint rate fit over the anchors: no absolute epoch enters float math,
    // negative offsets keep ties-to-even, and a tick outside [LowTick,
    // HighTick] or an unavailable model is invalid.
    Mapping result;
    result.generation = model.Generation;
    result.tracking_state = model.Poisoned ? "fault" : "acquiring";
    result.tick = tick;
    if(model.Available && model.LowTick <= tick && tick <= model.HighTick) {
        if(!model.TickSpan) throw ValueError("Invalid clock model"); // Python: ZeroDivisionError
        const i128 delta = RoundedRatio((i128(tick) - i128(model.AnchorTick)) * i128(model.UTCSpanNs), i128(model.TickSpan));
        const i128 candidate = i128(model.AnchorUTCNs) + delta;
        const char *state = tick > model.AnchorTick ? "holdover" : "gps_relative";
        result.valid = true;
        result.candidate_utc_ns = candidate;
        if(model.Confirmed) result.utc_ns = candidate;
        result.epoch_confirmed = model.Confirmed;
        result.tracking_state = state;
        result.quality = model.Confirmed ? state : "unconfirmed_epoch";
        result.extrapolation_ns = std::max<i128>(0, delta);
    }
    return result;
}

LiveClock::LiveClock(std::string boot_id, std::string producer_session, const Json *reference, EpochPolicy policy)
    : boot_id_(std::move(boot_id)), producer_session_(std::move(producer_session)), policy_(policy) {
    if(boot_id_.empty() || producer_session_.empty())
        throw ValueError("Boot and producer session identities are required");
    SetReference(reference);
}

void LiveClock::SetReference(const Json *reference) {
    // The caller may refresh an atomic root-private file periodically. A bad,
    // foreign or missing replacement cannot preserve a previous confirmation.
    anchor_confirmation_.reset();
    reference_.reset();
    if(reference == nullptr) return;
    if(!reference->is_object()) throw ValueError("Invalid coarse reference");
    auto get = [&](const char *key) -> const Json * {
        const auto found = reference->find(key);
        return found == reference->end() ? nullptr : &*found;
    };
    const Json *status = get("status"), *boot = get("boot_id"), *board = get("board_monotonic_ns");
    const Json *bounds = get("utc_minus_board_monotonic_ns");
    correlation::Reference value;
    bool valid = status && status->is_string() && status->get<std::string>() == "passed" && boot &&
                 boot->is_string() && boot->get<std::string>() == boot_id_ && board &&
                 JsonInteger(*board, value.board_monotonic_ns) && value.board_monotonic_ns >= 0 && bounds &&
                 bounds->is_array() && bounds->size() == 2 && JsonInteger((*bounds)[0], value.offset_lo) &&
                 JsonInteger((*bounds)[1], value.offset_hi);
    valid = valid && value.offset_hi - value.offset_lo >= 0 && value.offset_hi - value.offset_lo <= 250000000;
    if(!valid) throw ValueError("Invalid or foreign coarse reference");
    value.status = "passed";
    value.boot_id = boot_id_;
    reference_ = value;
}

void LiveClock::Invalidate(const std::string &reason, std::optional<uint64_t> sequence, bool poison) {
    ++generation_;
    anchors_.clear();
    history_.clear();
    run_anchors_ = 0;
    anchor_confirmation_.reset();
    counters_.Add(reason);
    events_.push_back(ClockEvent{reason, sequence, generation_});
    if(events_.size() > 8) events_.pop_front();
    poisoned_ = poisoned_ || poison;
}

void LiveClock::ClearFix() {
    fix_ = false;
    position_.reset();
    fix_received_ns_.reset();
}

LiveClock::Edge *LiveClock::FindEdge(uint64_t sequence) {
    for(auto &edge : edges_)
        if(edge.sequence == sequence) return &edge;
    return nullptr;
}

void LiveClock::ObservePacket(uint64_t current_ticks, uint64_t pps_count, uint64_t pps_ticks, int64_t before_ns,
                              int64_t after_ns, bool request_bracketed, const std::string *boot_id,
                              const std::string *producer_session) {
    // read() start/end around an unsolicited packet is not a request bracket.
    // Unsolicited packets tighten only a causal upper bound; they never invent
    // a lower bound for a snapshot already queued before read() began.
    if((boot_id && *boot_id != boot_id_) || (producer_session && *producer_session != producer_session_))
        throw ValueError("Foreign clock producer");
    if(before_ns < 0 || after_ns < 0 || pps_count > 0xffffffffULL || pps_ticks > current_ticks || after_ns < before_ns ||
       (pps_count == 0 && pps_ticks != 0))
        throw ValueError("Malformed counter observation");
    now_ = std::max(now_, after_ns);
    last_packet_ns_ = after_ns;
    if(poisoned_) return;
    // The capture tick is immutable for a given PPS count. Reordering can
    // explain an older count, never a different capture for the same count.
    if(pps_count == pps_count_ && pps_ticks != pps_tick_) {
        Invalidate("conflicting_pps_capture", pps_count, true);
        ClearFix();
        return;
    }
    if((current_tick_ && current_ticks < *current_tick_) || pps_count < pps_count_) {
        // A queued DATA header or overlapping old response can legitimately
        // predate a newer observation. Only a request sent after the latest
        // accepted packet was received proves that a regression is fresh.
        if(!request_bracketed || !last_ordered_receipt_ns_ || before_ns < *last_ordered_receipt_ns_) {
            counters_.Add("stale_packet_observations");
            Advance(now_);
            return;
        }
        Invalidate("ambiguous_counter_domain", pps_count, true);
        ClearFix();
        return;
    }
    current_tick_ = current_ticks;
    last_ordered_receipt_ns_ = after_ns;
    if(pps_count > pps_count_ && pps_count_) {
        const i128 delta = i128(pps_ticks) - i128(pps_tick_);
        if(pps_count != pps_count_ + 1 || delta <= 0 ||
           Abs(delta - kPruHz) * 1000000 > kPruHz * correlation::kMaxClockPpm) {
            Invalidate("pps_discontinuity", pps_count);
            edges_.clear();
            pending_.clear();
        }
    }
    pps_count_ = pps_count;
    pps_tick_ = pps_ticks;
    std::optional<correlation::Bounds> bounds;
    if(request_bracketed && after_ns - before_ns <= kNs / 10) {
        bounds = correlation::Bounds{before_ns, after_ns};
        if(bridge_) {
            const auto reachable = reachable_bounds_(bridge_->second, i128(current_ticks) - i128(bridge_->first), kPruHz,
                                                     correlation::kMaxClockPpm);
            if(std::max(bounds->lo, reachable.lo) > std::min(bounds->hi, reachable.hi)) {
                Invalidate("counter_bridge_discontinuity", pps_count);
                edges_.clear();
                pending_.clear();
            }
        }
        bridge_ = std::make_pair(current_ticks, *bounds);
    } else if(bridge_) {
        const auto projected = reachable_bounds_(bridge_->second, i128(current_ticks) - i128(bridge_->first), kPruHz,
                                                 correlation::kMaxClockPpm);
        bounds = correlation::Bounds{projected.lo, std::min(projected.hi, i128(after_ns))};
        if(bounds->hi < bounds->lo) {
            bounds.reset();
            counters_.Add("inconsistent_packet_receipt");
        }
    }
    if(pps_count && bounds) {
        std::optional<correlation::Bounds> edge_bounds;
        try {
            edge_bounds = edge_bounds_(bounds->lo, bounds->hi, current_ticks, pps_ticks, kPruHz, correlation::kMaxClockPpm);
        } catch(const ValueError &) {
        }
        if(edge_bounds && edge_bounds->hi - edge_bounds->lo <= kNs / 10) {
            Edge *edge = FindEdge(pps_count);
            if(edge == nullptr) {
                if(edges_.size() == kMaxEdges) edges_.erase(edges_.begin());
                Edge added;
                added.sequence = pps_count;
                added.tick = pps_ticks;
                added.bounds = *edge_bounds;
                edges_.push_back(added);
            } else {
                const correlation::Bounds intersection{std::max(edge->bounds.lo, edge_bounds->lo),
                                                       std::min(edge->bounds.hi, edge_bounds->hi)};
                if(intersection.lo > intersection.hi) {
                    Invalidate("inconsistent_pps_bounds", pps_count);
                    edge->invalid = true;
                } else {
                    edge->bounds = intersection;
                }
            }
        }
    }
    Advance(now_);
}

void LiveClock::ObserveNmea(std::string_view line, int64_t received_ns) {
    if(received_ns < 0) throw ValueError("Malformed NMEA receipt time");
    if(line.size() > 256) {
        counters_.Add("invalid_nmea");
        return;
    }
    auto fields = nmea::Sentence(line);
    if(!fields) {
        counters_.Add("invalid_nmea");
        return;
    }
    const std::string_view kind = Last3((*fields)[0]);
    if(kind != "RMC" && kind != "GGA") return;
    now_ = std::max(now_, received_ns);
    if(pending_.size() == kMaxPendingNmea) {
        pending_.pop_front();
        Invalidate("nmea_queue_overflow");
    }
    pending_.push_back(Pending{std::move(*fields), received_ns});
    Advance(now_);
}

void LiveClock::Consume(Edge &edge, const std::vector<std::string> &fields, int64_t received) {
    const std::string kind(Last3(fields[0]));
    const bool rmc = kind == "RMC";
    Rmc rmc_record;
    Gga gga_record;
    try {
        if(rmc) {
            rmc_record.utc_ns = nmea::RmcUtcNs(fields);
            rmc_record.position = PositionFrom(fields, 3);
        } else {
            const auto quality = ParseInt(Field(fields, 6));
            if(!quality) throw ValueError("invalid literal for int()");
            if(*quality < 1 || *quality > 5) throw ValueError("No measured GGA fix");
            const auto satellites = ParseInt(Field(fields, 7));
            if(!satellites) throw ValueError("invalid literal for int()");
            if(*satellites < 0 || *satellites > 128) throw ValueError("No measured GGA fix");
            const int64_t utc = nmea::RmcUtcNs({"GPRMC", Field(fields, 1), "A", "", "", "", "", "", "", "010100"});
            gga_record.second_of_day = utc / kNs % 86400;
            gga_record.position = PositionFrom(fields, 2);
            gga_record.satellites = *satellites;
        }
    } catch(const ValueError &) {
        edge.invalid = true;
        ClearFix();
        Invalidate(rmc ? "invalid_rmc" : "invalid_gga", edge.sequence);
        return;
    }
    const std::string key = rmc ? "rmc" : "gga";
    const bool present = rmc ? bool(edge.rmc) : bool(edge.gga);
    const bool differs = rmc ? (present && !(*edge.rmc == rmc_record)) : (present && !(*edge.gga == gga_record));
    if(differs) {
        edge.invalid = true;
        ClearFix();
        Invalidate("conflicting_" + key, edge.sequence);
    } else if(edge.closed && !present) {
        edge.invalid = true;
        Invalidate("late_nmea", edge.sequence);
    } else if(rmc) {
        edge.rmc = rmc_record;
    } else {
        edge.gga = gga_record;
    }
    if(edge.invalid || !edge.rmc || !edge.gga) return;
    const Rmc &r = *edge.rmc;
    const Gga &g = *edge.gga;
    if(r.utc_ns / kNs % 86400 != g.second_of_day || std::fabs(r.position.latitude - g.position.latitude) > 1e-7 ||
       std::fabs(r.position.longitude - g.position.longitude) > 1e-7) {
        edge.invalid = true;
        ClearFix();
        Invalidate("rmc_gga_conflict", edge.sequence);
        return;
    }
    if(!last_valid_pair_ns_ || received >= *last_valid_pair_ns_) {
        fix_ = true;
        position_ = r.position;
        satellites_ = g.satellites;
        fix_received_ns_ = received;
        last_valid_pair_ns_ = received;
    }
}

void LiveClock::Advance(int64_t now) {
    now_ = std::max(now_, now);
    if(poisoned_) return;
    std::deque<Pending> retained;
    while(!pending_.empty()) {
        Pending item = std::move(pending_.front());
        pending_.pop_front();
        std::vector<Edge *> candidates;
        for(auto &edge : edges_)
            if(i128(item.received) - edge.bounds.hi >= 0 && i128(item.received) - edge.bounds.lo <= kLatencyNs)
                candidates.push_back(&edge);
        if(candidates.size() == 1) {
            Consume(*candidates[0], item.fields, item.received);
        } else if(candidates.size() > 1) {
            Invalidate("ambiguous_nmea");
            for(Edge *edge : candidates) edge->invalid = true;
        } else if(i128(now_) - item.received <= 2 * kNs) {
            retained.push_back(std::move(item));
        } else {
            counters_.Add("unassociated_nmea");
        }
    }
    pending_ = std::move(retained);
    for(auto &edge : edges_) {
        if(edge.closed || i128(now_) <= edge.bounds.hi + kLatencyNs) continue;
        edge.closed = true;
        if(edge.invalid || !edge.rmc || !edge.gga) {
            Invalidate("missing_or_invalid_nmea", edge.sequence);
            continue;
        }
        const Anchor anchor{edge.sequence, edge.tick, edge.rmc->utc_ns, edge.bounds};
        if(!anchors_.empty()) {
            const Anchor &previous = anchors_.back();
            if(anchor.sequence != previous.sequence + 1 || anchor.utc_ns != previous.utc_ns + kNs)
                Invalidate("anchor_discontinuity", anchor.sequence);
        }
        MeasureModelError(anchor); // Against the anchors before it, before it joins them
        anchors_.push_back(anchor);
        if(anchors_.size() > 3) anchors_.pop_front();
        history_.push_back(anchor);
        if(history_.size() > 4) history_.pop_front();
        ++run_anchors_;
        anchor_confirmation_.reset();
        counters_.Add("accepted_anchors");
    }
    if(fix_received_ns_ && i128(now_) - *fix_received_ns_ > 2 * kNs) {
        ClearFix();
        Invalidate("fix_stale");
    }
    if(anchors_.size() == 3 && current_tick_) {
        const Anchor &first = anchors_.front(), &last = anchors_.back();
        if((i128(*current_tick_) - i128(last.tick)) * 2 * kNs > kHoldoverNs * (i128(last.tick) - i128(first.tick)))
            Invalidate("holdover_expired", last.sequence);
    }
}

bool LiveClock::Confirmed(int64_t now) {
    basis_.clear();
    if(anchors_.size() < 3) return false;
    if(reference_ && Abs(i128(now) - reference_->board_monotonic_ns) <= 120 * kNs) {
        // A fresh reference decides, under either policy. Only expiry depends
        // on query time. These immutable anchor/reference checks change
        // solely on append, invalidation or reference replacement.
        if(!anchor_confirmation_) {
            bool all = true;
            for(const Anchor &anchor : anchors_) {
                ++reference_checks_;
                if(!correlation::ReferenceConfirms(*reference_, boot_id_, anchor.bounds, anchor.utc_ns)) {
                    all = false;
                    break;
                }
            }
            anchor_confirmation_ = all;
            if(!all && policy_ == EpochPolicy::ReceiverProfile) counters_.Add("reference_disagreements");
        }
        if(*anchor_confirmation_) basis_ = "reference";
        return *anchor_confirmation_;
    }
    // Without one, the receiver profile: every anchor of an unbroken run
    // already paired its pulse with a valid, agreeing RMC/GGA at +1 s.
    if(policy_ == EpochPolicy::ReceiverProfile && run_anchors_ >= kReceiverEpochAnchors) {
        basis_ = "receiver_profile";
        return true;
    }
    return false;
}

void LiveClock::MeasureModelError(const Anchor &anchor) {
    // The model labels with the endpoint rate of three consecutive anchors,
    // through the newest (MappingModel()). Its real error is visible at each
    // new anchor: as predicted 1 s ahead (newest anchor k-1) and 2 s ahead
    // (newest anchor k-2). history_ holds only anchors consecutive with this one.
    const size_t n = history_.size();
    auto error = [&](const Anchor &first, const Anchor &last) {
        const i128 predicted = i128(last.utc_ns) + RoundedRatio((i128(anchor.tick) - i128(last.tick)) *
                                                                    (i128(last.utc_ns) - i128(first.utc_ns)),
                                                                i128(last.tick) - i128(first.tick));
        return int64_t(predicted - i128(anchor.utc_ns));
    };
    if(n >= 3) {
        errors_1s_.push_back(error(history_[n - 3], history_[n - 1]));
        if(errors_1s_.size() > kModelErrorWindow) errors_1s_.pop_front();
    }
    if(n >= 4) {
        errors_2s_.push_back(error(history_[n - 4], history_[n - 2]));
        if(errors_2s_.size() > kModelErrorWindow) errors_2s_.pop_front();
    }
}

TimeModel LiveClock::MappingModel(int64_t now_ns) {
    // The broker computes it once per IPC batch and the service applies it to
    // every sample, so candidate/confirmed epoch, time quality, clock
    // generation and the invalid-when-unknown rule are those of MapTicks()
    // exactly. The UTC span is the anchors' own: three consecutive anchors are
    // exactly two seconds apart.
    if(now_ns < 0) throw ValueError("Malformed sample clock query");
    Advance(now_ns);
    TimeModel model;
    model.Generation = generation_;
    model.Poisoned = poisoned_;
    if(!poisoned_ && anchors_.size() == 3 && current_tick_) {
        const Anchor &first = anchors_.front(), &last = anchors_.back();
        const i128 span = i128(last.tick) - i128(first.tick);
        // Consecutive PPS captures are 250e6 +- 1000 ppm ticks apart; anything
        // else would fail the batch encoder's model check (ipc.check_model).
        if(span <= 0 || last.utc_ns < 0) throw ValueError("Invalid clock model");
        model.Available = true;
        model.Confirmed = Confirmed(now_);
        model.LowTick = first.tick;
        model.HighTick = uint64_t(std::min<i128>(i128(*current_tick_), i128(last.tick) + kHoldoverNs * span / (2 * kNs)));
        model.AnchorTick = last.tick;
        model.AnchorUTCNs = uint64_t(last.utc_ns);
        model.TickSpan = uint64_t(span);
        model.UTCSpanNs = uint64_t(2 * kNs);
    }
    return model;
}

std::vector<Mapping> LiveClock::MapTicks(const std::vector<uint64_t> &ticks, int64_t now_ns) {
    // State advancement and civil-epoch confirmation run once per batch; all
    // arithmetic remains exact at large counter/UTC epochs.
    if(now_ns < 0 || ticks.size() > kMaxMapBatch) throw ValueError("Malformed sample clock query");
    const TimeModel model = MappingModel(now_ns);
    std::vector<Mapping> result;
    result.reserve(ticks.size());
    for(uint64_t tick : ticks) result.push_back(MapWithModel(model, tick));
    return result;
}

Mapping LiveClock::MapTick(uint64_t tick, int64_t now_ns) { return MapTicks({tick}, now_ns)[0]; }

std::optional<AnchorView> LiveClock::LatestAnchor() const {
    // A read-only snapshot for a caller that correlates it with an external
    // clock (the chrony feed). The sequence only increases while anchors keep
    // forming; any invalidation clears the anchors until a fresh one forms.
    if(anchors_.empty()) return std::nullopt;
    const Anchor &anchor = anchors_.back();
    return AnchorView{anchor.sequence, anchor.tick, anchor.utc_ns, anchor.bounds};
}

Diagnostic LiveClock::Diag(int64_t now_ns) {
    if(now_ns < 0) throw ValueError("Malformed diagnostic clock query");
    const Mapping mapping = MapTick(current_tick_.value_or(0), now_ns);
    Diagnostic d;
    d.boot_id = boot_id_;
    d.producer_session = producer_session_;
    d.generation = generation_;
    d.quality = mapping.quality;
    d.tracking_state = mapping.tracking_state;
    d.epoch_confirmed = mapping.epoch_confirmed;
    d.confirmation_basis = mapping.epoch_confirmed ? basis_ : std::string();
    d.consecutive_anchors = run_anchors_;
    auto summarize = [](const std::deque<int64_t> &errors, uint64_t &samples, double &rms, double &max) {
        samples = errors.size();
        double squares = 0;
        for(int64_t e : errors) {
            squares += double(e) * double(e);
            max = std::max(max, std::fabs(double(e)));
        }
        if(samples) rms = std::sqrt(squares / double(samples));
    };
    summarize(errors_1s_, d.model_error.samples_1s, d.model_error.rms_1s_ns, d.model_error.max_1s_ns);
    summarize(errors_2s_, d.model_error.samples_2s, d.model_error.rms_2s_ns, d.model_error.max_2s_ns);
    if(errors_2s_.size() >= kModelErrorMinimum) {
        const double receiver = double(kReceiverUncertaintyNs), model = d.model_error.rms_2s_ns;
        d.clock_uncertainty_ns = int64_t(std::ceil(2 * std::sqrt(receiver * receiver + model * model)));
    }
    d.requires_new_session = poisoned_;
    d.fix = fix_;
    d.position = position_;
    d.satellites = fix_ ? satellites_ : std::nullopt;
    if(last_valid_pair_ns_) d.nmea_age_ms = Milliseconds(std::max<i128>(0, i128(now_) - *last_valid_pair_ns_));
    d.gps_stale = !fix_ || !d.nmea_age_ms || *d.nmea_age_ms > 2000;
    for(const auto &edge : edges_)
        if(edge.sequence == pps_count_)
            d.pps_age_ms = Milliseconds(std::max<i128>(0, i128(now_) - FloorDiv(edge.bounds.lo + edge.bounds.hi, 2)));
    if(last_packet_ns_) d.packet_age_ms = Milliseconds(std::max<i128>(0, i128(now_) - *last_packet_ns_));
    d.anchor_count = anchors_.size();
    d.pps_count = pps_count_;
    d.pending_nmea_count = pending_.size();
    d.retained_pps_count = edges_.size();
    if(reference_) d.reference_age_ns = Abs(i128(now_) - reference_->board_monotonic_ns);
    d.counters = counters_;
    d.events.assign(events_.begin(), events_.end());
    return d;
}

}}} // namespace ozmv::pb2::broker
