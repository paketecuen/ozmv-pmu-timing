// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include "PB2SampleBatch.h"

// The conversion time of each PB2 sample (platform/pb2/timing/sample-time.md,
// step 1). Host-buildable: no service or hardware dependency.
namespace ozmv { namespace pb2 {

// The PRU polls DRDY, so every observed tick is late by a non-negative,
// bounded amount, while the conversions themselves are strictly periodic on
// the ADC's clock. The lower envelope of the observed ticks against the
// sample sequence is therefore the conversion-ready time.
//
// It is kept incrementally, at a few operations per sample: each sample's
// lateness against the current envelope updates its block's lowest point and
// a coarse histogram; each completed block refits the envelope by least
// squares through the last Blocks lowest points, lowered by the expected
// excess of a block minimum. Choosing a block's lowest point needs only the
// envelope's slope; until a first envelope exists the nominal cadence stands
// in for it, so the labels start once every point was chosen against a
// fitted envelope (two windows: 0.52 s at OSR128, 4 s at OSR4096). The state
// is about 1.3 KB: the service copies its sample state for every message.
class CadenceFit {
public:
    enum : size_t { Blocks = 16, MinimumBlock = 64, MaximumBlock = 512 };
    // Polling is never early: a sample observed this many ticks (32 ns)
    // before the envelope means the fit no longer describes the stream, and
    // EarlyLimit of them within one block restart it.
    static constexpr double EarlyTicks() { return 8.0; }
    enum : uint64_t { EarlyLimit = 4 };
    // Lateness histogram: Bins of BinTicks from -FloorTicks, for the status
    // and for the correction's p99; published once per window of samples.
    enum : size_t { Bins = 512, BinTicks = 8 };
    static constexpr double FloorTicks() { return 64.0; }
    struct Stats {
        bool Fitted = false;                 // An envelope labels the samples
        size_t WindowSamples = 0, BlockSamples = 0;
        uint64_t Fits = 0, Restarts = 0, EarlySamples = 0, FittedSamples = 0, UnfittedSamples = 0;
        // In PRU ticks: each observed tick's lateness above the envelope over
        // the last full window (p50/p99 to one histogram bin), the block
        // minima's scatter around the latest fit, the subtracted excess and
        // the envelope's 1-sigma uncertainty.
        double LatenessMin = 0, LatenessP50 = 0, LatenessP99 = 0, LatenessMax = 0;
        double Scatter = 0, Correction = 0, Uncertainty = 0;
    };

    // An eighth of a second of samples per block, within 64..512: a 0.26 s
    // window at OSR128 and a 2 s one at OSR4096. Restarts the fit.
    void Configure(const InputProfile &profile) {
        const double perBlock = profile.Hz() / 8.0;
        pBlock = size_t(std::min<double>(MaximumBlock, std::max<double>(MinimumBlock, std::floor(perBlock))));
        pNominal = profile.TicksPerSample;
        pConfigured = true;
        pStats.BlockSamples = pBlock;
        pStats.WindowSamples = pBlock * Blocks;
        Clear();
    }
    // The next sample starts a new window (a sequence or cadence gap, a new
    // session); counted as a restart when it discards an envelope or samples.
    void Restart() {
        if(pHaveReference) ++pStats.Restarts;
        Clear();
    }
    // One sample, in delivery order. Returns the tick to label it with: the
    // envelope's when fitted is set, else the observed one.
    uint64_t Push(uint64_t sequence, uint64_t tick, bool &fitted) {
        fitted = false;
        if(!pConfigured) { ++pStats.UnfittedSamples; return tick; }
        if(pHaveReference && (pLastSequence == UINT64_MAX || sequence != pLastSequence + 1 || tick <= pLastTick))
            Restart();
        pLastSequence = sequence;
        pLastTick = tick;
        if(!pHaveReference) { // The nominal cadence through this sample, until a fit
            pHaveReference = true;
            pOriginSequence = sequence;
            pOriginTick = tick;
            pOffset = pSlope = 0;
        }
        const int64_t steps = int64_t(sequence - pOriginSequence);
        const uint64_t predicted = pOriginTick + uint64_t(int64_t(pNominal) * steps + std::llround(pOffset + pSlope * double(steps)));
        const double lateness = double(int64_t(tick - predicted));
        if(pStats.Fitted && lateness < -EarlyTicks()) {
            ++pStats.EarlySamples;
            if(++pEarlyInBlock >= EarlyLimit) {
                Restart();
                return Push(sequence, tick, fitted); // This sample starts the new window
            }
        }
        if(!pInBlock || lateness < pBlockLowest) {
            pBlockLowest = lateness;
            pBlockSequence = sequence;
            pBlockTick = tick;
        }
        if(pFittedReference) Count(lateness);
        const uint64_t label = pStats.Fitted ? predicted : tick;
        fitted = pStats.Fitted;
        if(fitted) ++pStats.FittedSamples;
        else ++pStats.UnfittedSamples;
        if(++pInBlock == pBlock) CloseBlock();
        return label;
    }
    const Stats &GetStats() const { return pStats; }

private:
    struct Point { uint64_t Sequence = 0, Tick = 0; bool Settled = false; };

    void Clear() {
        pHaveReference = pFittedReference = false;
        pInBlock = pPoints = pNextPoint = 0;
        pEarlyInBlock = 0;
        pWindowCount = 0;
        pWindowLowest = pWindowHighest = 0;
        std::fill(std::begin(pHistogram), std::end(pHistogram), uint16_t(0));
        pP99 = 0;
        pStats.Fitted = false;
    }
    void Count(double lateness) {
        const double at = std::floor((lateness + FloorTicks()) / BinTicks);
        ++pHistogram[size_t(std::min<double>(Bins - 1, std::max(0.0, at)))];
        if(!pWindowCount || lateness < pWindowLowest) pWindowLowest = lateness;
        if(!pWindowCount || lateness > pWindowHighest) pWindowHighest = lateness;
        if(++pWindowCount < pBlock * Blocks) return;
        // A full window: publish its lateness and keep its p99 for the correction.
        auto quantile = [&](double q) {
            const uint64_t rank = uint64_t(std::ceil(q * double(pWindowCount)));
            uint64_t seen = 0;
            for(size_t bin = 0; bin < Bins; ++bin)
                if((seen += pHistogram[bin]) >= rank) return double(bin) * BinTicks - FloorTicks() + BinTicks / 2.0;
            return pWindowHighest;
        };
        pStats.LatenessP50 = quantile(0.50);
        pStats.LatenessP99 = pP99 = std::min(quantile(0.99), pWindowHighest);
        pStats.LatenessMin = pWindowLowest;
        pStats.LatenessMax = pWindowHighest;
        std::fill(std::begin(pHistogram), std::end(pHistogram), uint16_t(0));
        pWindowCount = 0;
    }
    void CloseBlock() {
        pInBlock = 0;
        pEarlyInBlock = 0;
        Point &point = pRing[pNextPoint];
        point.Sequence = pBlockSequence;
        point.Tick = pBlockTick;
        point.Settled = pFittedReference; // Chosen against a fitted slope
        pNextPoint = (pNextPoint + 1) % Blocks;
        if(pPoints < Blocks) ++pPoints;
        if(pPoints == Blocks) Fit();
    }
    void Fit() {
        // The oldest point is the origin; x: samples after it, y: ticks after
        // it less the nominal cadence. Least squares through the 16 points.
        const Point &origin = pRing[pNextPoint];
        double x[Blocks], y[Blocks], sx = 0, sy = 0;
        bool settled = true;
        for(size_t j = 0; j < Blocks; ++j) {
            const Point &point = pRing[(pNextPoint + j) % Blocks];
            const int64_t steps = int64_t(point.Sequence - origin.Sequence);
            x[j] = double(steps);
            y[j] = double(int64_t(point.Tick - origin.Tick) - int64_t(pNominal) * steps);
            sx += x[j];
            sy += y[j];
            settled = settled && point.Settled;
        }
        const double mx = sx / Blocks, my = sy / Blocks;
        double sxx = 0, sxy = 0;
        for(size_t j = 0; j < Blocks; ++j) {
            sxx += (x[j] - mx) * (x[j] - mx);
            sxy += (x[j] - mx) * (y[j] - my);
        }
        const double b = sxx > 0 ? sxy / sxx : 0, a = my - b * mx;
        double squares = 0;
        for(size_t j = 0; j < Blocks; ++j) squares += (y[j] - a - b * x[j]) * (y[j] - a - b * x[j]);
        const double scatter = std::sqrt(squares / double(Blocks - 2));
        // A block minimum of uniform lateness over [0, G] exceeds the true
        // floor by G / (block + 1) on average.
        const double correction = std::max(0.0, pP99) / double(pBlock + 1);
        pOriginSequence = origin.Sequence;
        pOriginTick = origin.Tick;
        pOffset = a - correction;
        pSlope = b;
        pFittedReference = true;
        pStats.Fitted = settled;
        ++pStats.Fits;
        pStats.Scatter = scatter;
        pStats.Correction = correction;
        // A single minimum's scatter, not their mean's: the minima are chosen
        // against the envelope they then refit, so no 1/Blocks averaging is
        // claimed. The integer ticks (the first observable one, and the
        // label's) add a half-tick bias and a uniform tick: 1/4 + 1/12.
        pStats.Uncertainty = std::sqrt(scatter * scatter + correction * correction / 4 + 1.0 / 3);
    }

    size_t pBlock = MaximumBlock;
    uint64_t pNominal = 0;
    bool pConfigured = false, pHaveReference = false, pFittedReference = false;
    uint64_t pLastSequence = 0, pLastTick = 0;
    // The envelope: tick = OriginTick + Nominal * k + Offset + Slope * k, k samples after OriginSequence.
    uint64_t pOriginSequence = 0, pOriginTick = 0;
    double pOffset = 0, pSlope = 0;
    // The block being filled and the last Blocks lowest points.
    size_t pInBlock = 0;
    uint64_t pEarlyInBlock = 0;
    double pBlockLowest = 0;
    uint64_t pBlockSequence = 0, pBlockTick = 0;
    Point pRing[Blocks];
    size_t pPoints = 0, pNextPoint = 0;
    // Lateness over the current window, against a fitted envelope.
    uint16_t pHistogram[Bins] = {};
    uint64_t pWindowCount = 0;
    double pWindowLowest = 0, pWindowHighest = 0, pP99 = 0;
    Stats pStats;
};

// The ADS131M08 digital filter's group delay for a profile, in HR mode: sinc3
// at R = min(OSR, 1024), then for a larger OSR a sinc1 over OSR/R of its
// outputs. In modulator periods tMOD = 1 / (rate * OSR):
// 1.5 (R - 1) + (OSR/R - 1) R / 2. Computed from the filter structure; the
// pipeline after the filter is not documented, so one tMOD of uncertainty
// stays until the CH3 self-calibration measures it (#191).
inline double ModulatorPeriodNs(const InputProfile &profile) {
    return 1e9 * double(profile.RateDenominator) / (double(profile.RateNumerator) * double(profile.Osr));
}
inline uint64_t FilterDelayNs(const InputProfile &profile) {
    if(!profile.Known()) return 0;
    const uint64_t r = std::min<uint64_t>(profile.Osr, 1024), stages = profile.Osr / r;
    const double halfPeriods = double(3 * (r - 1) + (stages - 1) * r);
    return uint64_t(std::llround(halfPeriods / 2.0 * ModulatorPeriodNs(profile)));
}
inline uint64_t FilterDelayUncertaintyNs(const InputProfile &profile) {
    return profile.Known() ? uint64_t(std::llround(ModulatorPeriodNs(profile))) : 0;
}

}} // namespace ozmv::pb2
