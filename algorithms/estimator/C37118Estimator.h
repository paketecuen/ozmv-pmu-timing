// This file is part of openZmeter, licensed under GPL-3.0-or-later.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include "C37118Decimator.h"
#include "C37118Frame.h"
#include "MeasurementSample.h"

namespace C37118 {

using EstimatorSample = ozmv::signal::EngineeringSample;

struct EstimatorStats {
  uint64_t SamplesSeen = 0;
  uint64_t SamplesAccepted = 0;
  uint64_t FramesEmitted = 0;
  uint64_t Resets = 0;
  uint64_t InvalidSamples = 0;
  uint64_t MetadataChanges = 0;
  uint64_t SequenceDiscontinuities = 0;
  uint64_t TimestampDiscontinuities = 0;
  uint64_t CadenceDiscontinuities = 0;
  uint64_t RejectedWindows = 0;
  uint64_t CoverageFailures = 0;
  uint64_t ConditioningFailures = 0;
  uint64_t ReferenceFailures = 0;
  uint64_t CarrierFailures = 0;
  uint64_t Fits = 0;
  uint64_t BufferHighWater = 0;
  double LastCondition = 0;
  // The current run's decimation: 1 = the fit takes every sample.
  unsigned DecimationFactor = 1, FilterTaps = 0;
  double PointRateHz = 0, FilterLatencyNs = 0;
};

// Experimental, sample-based Taylor-Fourier estimator. This is neither an
// IEC/IEEE 60255-118-1 P/M profile nor a substitute for calibrated acquisition.
// A two-nominal-cycle Hann window needs one nominal cycle of lookahead.
// DC and 2nd/3rd harmonic nuisance terms improve steady-state selectivity;
// quadratic-envelope truncation still limits dynamic and interharmonic response.
// One weighted fit serves two models. Frequency and ROCOF come from the full
// one, whose envelope is quadratic: from the positive sequence when three
// voltages form one (Annex D.4), otherwise from phase A. The phasors come from
// its first-order sub-model. A quadratic envelope's zeroth-order filter has
// negative lobes near the window's edges and rings 7-11 % before and after a
// step (P class allows 5 %, Table 8); the first order's is non-negative, at
// the cost of a small bias under curved envelopes (~1.7e-4 TVE at 1 Hz/s).
// Engineering-unit samples and their aperture/latency compensation belong to
// the caller. Every emitted result remains explicitly unqualified; its time
// quality is the worst its window's samples declare: UTC-traceable only when
// every one has a confirmed epoch and a known uncertainty, with the largest of
// those as its bound, and locked only if none was extrapolated through a
// holdover (#197): then it is unlocked, with the time since the last locked
// report (C37118Frame.h writes the Annex A fields).
// With Decimation::Automatic a fast source is first low-pass filtered and
// decimated to about 2 kS/s (C37118Decimator.h), and the phasors are divided
// by the filter's gain at the measured frequency; continuity and validity are
// still checked on every input sample.
class Estimator {
 public:
  enum class Decimation { Off, Automatic };

  static bool SupportedRate(uint16_t nominalFrequency, int16_t dataRate) {
    return (nominalFrequency == 50 && (dataRate == 10 || dataRate == 25 || dataRate == 50)) ||
           (nominalFrequency == 60 && (dataRate == 10 || dataRate == 12 || dataRate == 15 ||
                                      dataRate == 20 || dataRate == 30 || dataRate == 60));
  }

  Estimator(uint16_t nominalFrequency, int16_t dataRate, Decimation decimation = Decimation::Off)
      : pNominal(nominalFrequency), pRate(dataRate), pDecimation(decimation), pSamples(kCapacity) {
    if(!SupportedRate(nominalFrequency, dataRate))
      throw std::invalid_argument("Unsupported PMU nominal frequency/report rate");
    pHalfWindowNs = (kNs + pNominal - 1) / pNominal;
  }

  // A reset clears all fit history. Already emitted report instants are never
  // reused, including after backward clock steps or generation changes.
  void Reset() {
    ++pStats.Resets;
    pStarted = pSlotted = false;
    pHead = pCount = 0;
  }

  const EstimatorStats &GetStats() const { return pStats; }

  bool Push(const EstimatorSample &sample, Phasor &result) {
    ++pStats.SamplesSeen;
    if(!ValidSample(sample)) {
      ++pStats.InvalidSamples;
      Reset();
      return false;
    }
    if(pStarted) {
      if(sample.Generation != pPrevious.Generation || sample.SampleRate != pPrevious.SampleRate ||
         sample.SampleRateDenominator != pPrevious.SampleRateDenominator ||
         sample.VoltageChannels != pPrevious.VoltageChannels || sample.CurrentChannels != pPrevious.CurrentChannels ||
         sample.TimeSource != pPrevious.TimeSource || sample.ClockGeneration != pPrevious.ClockGeneration ||
         sample.Epoch != pPrevious.Epoch || sample.Scale != pPrevious.Scale || sample.ChannelMap != pPrevious.ChannelMap) {
        ++pStats.MetadataChanges;
        Reset();
      } else if(pPrevious.Sequence == std::numeric_limits<uint64_t>::max() ||
                sample.Sequence != pPrevious.Sequence + 1) {
        ++pStats.SequenceDiscontinuities;
        Reset();
      } else if(sample.TimestampNs <= pPrevious.TimestampNs) {
        ++pStats.TimestampDiscontinuities;
        Reset();
      } else {
        uint64_t interval = sample.TimestampNs - pPrevious.TimestampNs;
        if(interval < pMinimumIntervalNs || interval > pMaximumIntervalNs) {
          ++pStats.CadenceDiscontinuities;
          Reset();
        }
      }
    }
    if(!pStarted) {
      pStarted = true;
      StartRun(sample);
    }
    pPrevious = sample;
    ++pStats.SamplesAccepted;

    Point input;
    input.TimestampNs = sample.TimestampNs;
    input.UncertaintyNs = sample.TimeUncertaintyNs;
    input.Traceable = sample.Epoch == ozmv::signal::EpochAssociation::Confirmed;
    input.Holdover = sample.Holdover;
    std::copy(sample.Values, sample.Values + 7, input.Values);
    if(pFactor == 1) return Store(input, result);
    Point decimated;
    return pDecimator.Push(input, decimated) && Store(decimated, result);
  }

 private:
  static constexpr uint64_t kNs = 1000000000ULL;
  static constexpr unsigned kColumns = 11;
  static constexpr unsigned kCapacity = 4096;
  static constexpr double kPi = 3.141592653589793238462643383279502884;
  struct Fit {
    double Coefficients[kColumns][7] = {};
    double ResidualRms[7] = {};
    double Peaks[7] = {};
    // The weighted normal equations (peak-normalised): the phasors' sub-model
    // is solved from the same sums.
    double Gram[kColumns][kColumns] = {}, Rhs[kColumns][7] = {};
  };
  uint16_t pNominal;
  int16_t pRate;
  Decimation pDecimation;
  uint64_t pHalfWindowNs = 0;
  std::vector<Point> pSamples;
  unsigned pHead = 0, pCount = 0;
  bool pStarted = false, pSlotted = false, pHaveEmitted = false;
  EstimatorSample pPrevious;
  // This run's cadence bounds (its rate is fixed: a change restarts it), its
  // decimation (1 = none) and the largest interval between its points.
  uint64_t pMinimumIntervalNs = 0, pMaximumIntervalNs = 0;
  Decimator pDecimator;
  unsigned pFactor = 1;
  uint64_t pPointIntervalNs = 0;
  uint64_t pSlotSecond = 0, pLastEmittedUs = 0;
  // The last report that was locked, for the unlocked-time code; it outlives resets.
  uint64_t pLastLockedUs = 0;
  bool pHaveLocked = false;
  unsigned pSlotIndex = 0;
  EstimatorStats pStats;

  const Point &At(unsigned index) const { return pSamples[(pHead + index) % kCapacity]; }
  void Pop() { pHead = (pHead + 1) % kCapacity; --pCount; }

  // The declared channels: the others carry zeros, whose fit is exactly zero.
  static unsigned ActiveChannels(const EstimatorSample &sample, unsigned (&channels)[7]) {
    unsigned active = 0;
    for(unsigned channel = 0; channel < sample.VoltageChannels; ++channel) channels[active++] = channel;
    for(unsigned channel = 0; channel < sample.CurrentChannels; ++channel) channels[active++] = 3 + channel;
    return active;
  }

  // A run's first sample (valid: see ValidSample) sets its decimation.
  void StartRun(const EstimatorSample &sample) {
    const double inputHz = static_cast<double>(sample.SampleRate) / sample.SampleRateDenominator;
    pFactor = pDecimation == Decimation::Automatic ? Decimator::FactorFor(inputHz) : 1;
    if(pFactor > 1) {
      unsigned channels[7];
      pDecimator.Configure(inputHz, pFactor, pNominal, channels, ActiveChannels(sample, channels));
    }
    pMinimumIntervalNs = MinimumInterval(sample);
    pMaximumIntervalNs = MaximumInterval(sample);
    pPointIntervalNs = pMaximumIntervalNs * pFactor;
    pStats.DecimationFactor = pFactor;
    pStats.FilterTaps = pFactor > 1 ? pDecimator.Taps() : 0;
    pStats.PointRateHz = inputHz / pFactor;
    pStats.FilterLatencyNs = pFactor > 1 ? pDecimator.LatencyNs() : 0;
  }

  // One point into the window buffer; a report when it completes one.
  bool Store(const Point &point, Phasor &result) {
    if(!pSlotted) {
      pSlotted = true;
      uint64_t earliest = point.TimestampNs + pHalfWindowNs;
      if(pHaveEmitted) earliest = std::max(earliest, (pLastEmittedUs + 1) * 1000);
      FirstSlot(earliest);
    }
    uint64_t retention = 2 * pHalfWindowNs + 2 * pPointIntervalNs;
    uint64_t oldest = point.TimestampNs > retention ? point.TimestampNs - retention : 0;
    while(pCount && At(0).TimestampNs < oldest) Pop();
    if(pCount == kCapacity) {
      // The declared sample-rate/cadence limits make this unreachable under a
      // valid stream; never silently overwrite part of a fit window.
      ++pStats.CoverageFailures;
      Reset();
      return false;
    }
    pSamples[(pHead + pCount) % kCapacity] = point;
    ++pCount;
    pStats.BufferHighWater = std::max(pStats.BufferHighWater, static_cast<uint64_t>(pCount));

    uint64_t reportUs = SlotUs();
    uint64_t centre = reportUs * 1000;
    if(point.TimestampNs < centre + pHalfWindowNs) return false;
    NextSlot(); // A rejected estimate is skipped, never retried with stale data.
    if(!Estimate(centre, reportUs, result)) {
      ++pStats.RejectedWindows;
      return false;
    }
    pLastEmittedUs = reportUs;
    pHaveEmitted = true;
    ++pStats.FramesEmitted;
    return true;
  }

  // Called only after ValidSample: the ratio is between 12*nominal and 65000 Hz.
  // uint32 numerator/denominator keep both products and the ceil adjustment
  // below UINT64_MAX; the minimum period remains greater than the 2000 ns guard.
  static uint64_t MinimumInterval(const EstimatorSample &sample) {
    return 800000000ULL * sample.SampleRateDenominator / sample.SampleRate - 2000;
  }
  static uint64_t MaximumInterval(const EstimatorSample &sample) {
    return (1200000000ULL * sample.SampleRateDenominator + sample.SampleRate - 1) /
           sample.SampleRate + 2000;
  }

  bool ValidSample(const EstimatorSample &sample) const {
    if(!sample.Valid || sample.SampleRateDenominator == 0 ||
       sample.Epoch == ozmv::signal::EpochAssociation::Unavailable ||
       (sample.Epoch != ozmv::signal::EpochAssociation::Unspecified && sample.TimestampNs == 0) ||
       sample.SampleRate < 12ULL * pNominal * sample.SampleRateDenominator ||
       sample.SampleRate > 65000ULL * sample.SampleRateDenominator ||
       sample.VoltageChannels == 0 || sample.VoltageChannels > 3 || sample.CurrentChannels > 4 ||
       sample.TimestampNs > std::numeric_limits<uint64_t>::max() - 2 * kNs) return false;
    for(double value : sample.Values) if(!std::isfinite(value)) return false;
    return true;
  }

  uint64_t SlotUs() const {
    // Round each absolute per-second slot once to the existing microsecond
    // wire grid. A 60 fps stream therefore uses 16666/16667 us intervals rather
    // than accumulating a rounded period and drifting across UTC seconds.
    return pSlotSecond * 1000000ULL + (pSlotIndex * 1000000ULL + pRate / 2) / pRate;
  }

  void FirstSlot(uint64_t earliestNs) {
    pSlotSecond = earliestNs / kNs;
    pSlotIndex = 0;
    while(SlotUs() * 1000 < earliestNs) NextSlot();
  }

  void NextSlot() {
    if(++pSlotIndex == static_cast<unsigned>(pRate)) { pSlotIndex = 0; ++pSlotSecond; }
  }

  double RelativeU(uint64_t timestamp, uint64_t centre) const {
    // Subtract integer epochs first; converting their absolute values to
    // double would lose resolution at contemporary (and larger) Unix epochs.
    double delta = timestamp >= centre ? static_cast<double>(timestamp - centre) :
                                        -static_cast<double>(centre - timestamp);
    return delta * pNominal / static_cast<double>(kNs);
  }

  void Basis(double u, double carrier, double (&basis)[kColumns]) const {
    double angle = 2 * kPi * carrier * u / pNominal;
    double cosine = std::cos(angle), sine = std::sin(angle);
    basis[0] = 1;
    basis[1] = cosine; basis[2] = sine;
    basis[3] = u * cosine; basis[4] = u * sine;
    basis[5] = u * u * cosine; basis[6] = u * u * sine;
    // Second and third carrier harmonics by the double/triple-angle identities:
    // the same basis to a few ulp, without four more libm calls per sample.
    const double cosine2 = cosine * cosine - sine * sine, sine2 = 2 * sine * cosine;
    basis[7] = cosine2; basis[8] = sine2;
    basis[9] = cosine * cosine2 - sine * sine2; basis[10] = sine * cosine2 + cosine * sine2;
  }

  // A model's i-th column: all kColumns for the full model; the first-order
  // sub-model (kColumns - 2) drops the u^2 terms, columns 5 and 6.
  static unsigned Column(unsigned columns, unsigned i) { return columns == kColumns || i < 5 ? i : i + 2; }

  template <unsigned N>
  bool Solve(const double (&gram)[kColumns][kColumns], const double (&rhs)[kColumns][7],
             double (&answer)[N][7]) {
    double scaled[N][N], inverse[N][N] = {}, scale[N];
    double originalNorm = 0;
    for(unsigned i = 0; i < N; ++i) {
      const double diagonal = gram[Column(N, i)][Column(N, i)];
      if(!(diagonal > 0) || !std::isfinite(diagonal)) return false;
      scale[i] = std::sqrt(diagonal);
    }
    for(unsigned i = 0; i < N; ++i) {
      double rowNorm = 0;
      for(unsigned j = 0; j < N; ++j) {
        scaled[i][j] = gram[Column(N, i)][Column(N, j)] / scale[i] / scale[j];
        rowNorm += std::abs(scaled[i][j]);
      }
      originalNorm = std::max(originalNorm, rowNorm);
      inverse[i][i] = 1;
    }
    // Partial-pivoted Gauss-Jordan inversion of the column-scaled Gram matrix.
    // Its infinity-norm condition estimate is checked before accepting any fit.
    for(unsigned column = 0; column < N; ++column) {
      unsigned pivot = column;
      for(unsigned row = column + 1; row < N; ++row)
        if(std::abs(scaled[row][column]) > std::abs(scaled[pivot][column])) pivot = row;
      if(!std::isfinite(scaled[pivot][column]) || std::abs(scaled[pivot][column]) < 1e-10) return false;
      if(pivot != column) for(unsigned j = 0; j < N; ++j) {
        std::swap(scaled[column][j], scaled[pivot][j]);
        std::swap(inverse[column][j], inverse[pivot][j]);
      }
      double diagonal = scaled[column][column];
      for(unsigned j = 0; j < N; ++j) {
        scaled[column][j] /= diagonal;
        inverse[column][j] /= diagonal;
      }
      for(unsigned row = 0; row < N; ++row) if(row != column) {
        double factor = scaled[row][column];
        for(unsigned j = 0; j < N; ++j) {
          scaled[row][j] -= factor * scaled[column][j];
          inverse[row][j] -= factor * inverse[column][j];
        }
      }
    }
    double inverseNorm = 0;
    for(unsigned i = 0; i < N; ++i) {
      double rowNorm = 0;
      for(unsigned j = 0; j < N; ++j) rowNorm += std::abs(inverse[i][j]);
      inverseNorm = std::max(inverseNorm, rowNorm);
    }
    const double condition = originalNorm * inverseNorm;
    if(N == kColumns) pStats.LastCondition = condition; // The full model's
    if(!std::isfinite(condition) || condition > 1e6) return false;
    for(unsigned i = 0; i < N; ++i) for(unsigned channel = 0; channel < 7; ++channel) {
      double value = 0;
      for(unsigned j = 0; j < N; ++j) value += inverse[i][j] * rhs[Column(N, j)][channel] / scale[j];
      answer[i][channel] = value / scale[i];
      if(!std::isfinite(answer[i][channel])) return false;
    }
    return true;
  }

  bool FitWindow(uint64_t centre, double carrier, Fit &fit) {
    ++pStats.Fits;
    // Only the declared channels carry data: MeasurementSample keeps the other
    // slots zero, whose fit is exactly zero, so the per-channel work skips them.
    // At 31.5 kS/s a two-cycle window holds ~1260 samples per fit pass, and
    // ~79 points decimated.
    unsigned channels[7];
    const unsigned active = ActiveChannels(pPrevious, channels);
    unsigned used = 0;
    for(unsigned index = 0; index < pCount; ++index) {
      const Point &point = At(index);
      if(std::abs(RelativeU(point.TimestampNs, centre)) >= 1) continue;
      ++used;
      for(unsigned k = 0; k < active; ++k) {
        const unsigned channel = channels[k];
        fit.Peaks[channel] = std::max(fit.Peaks[channel], std::abs(point.Values[channel]));
      }
    }
    if(used < 2 * kColumns || At(0).TimestampNs > centre - pHalfWindowNs ||
       At(pCount - 1).TimestampNs < centre + pHalfWindowNs) {
      ++pStats.CoverageFailures;
      return false;
    }
    // Local sums, copied into the fit once: accumulating into the fit itself,
    // which the compiler must assume may alias the samples, made the direct
    // (undecimated) fit ~10 % slower.
    double gram[kColumns][kColumns] = {}, rhs[kColumns][7] = {}, energy[7] = {}, weightSum = 0;
    // Peak normalization by one reciprocal per channel instead of a division
    // per sample (within an ulp; the A53 divider is not pipelined).
    double normalize[7] = {};
    for(unsigned k = 0; k < active; ++k)
      if(fit.Peaks[channels[k]] > 0) normalize[channels[k]] = 1 / fit.Peaks[channels[k]];
    for(unsigned index = 0; index < pCount; ++index) {
      const Point &point = At(index);
      double u = RelativeU(point.TimestampNs, centre);
      if(std::abs(u) >= 1) continue;
      double basis[kColumns], weighted[kColumns];
      Basis(u, carrier, basis);
      double weight = .5 * (1 + std::cos(kPi * u));
      weightSum += weight;
      for(unsigned i = 0; i < kColumns; ++i) weighted[i] = weight * basis[i];
      for(unsigned i = 0; i < kColumns; ++i)
        for(unsigned j = 0; j <= i; ++j) gram[i][j] += weighted[i] * basis[j];
      for(unsigned k = 0; k < active; ++k) {
        const unsigned channel = channels[k];
        double value = point.Values[channel] * normalize[channel];
        energy[channel] += weight * value * value;
        for(unsigned i = 0; i < kColumns; ++i) rhs[i][channel] += weighted[i] * value;
      }
    }
    for(unsigned i = 0; i < kColumns; ++i)
      for(unsigned j = 0; j < i; ++j) gram[j][i] = gram[i][j];
    std::copy(&gram[0][0], &gram[0][0] + kColumns * kColumns, &fit.Gram[0][0]);
    std::copy(&rhs[0][0], &rhs[0][0] + kColumns * 7, &fit.Rhs[0][0]);
    if(!Solve(gram, rhs, fit.Coefficients)) { ++pStats.ConditioningFailures; return false; }
    for(unsigned channel = 0; channel < 7; ++channel) {
      double explained = 0;
      for(unsigned i = 0; i < kColumns; ++i) explained += fit.Coefficients[i][channel] * rhs[i][channel];
      double residual = std::max(0.0, energy[channel] - explained);
      fit.ResidualRms[channel] = std::sqrt(residual / weightSum) * fit.Peaks[channel];
      for(unsigned i = 0; i < kColumns; ++i) fit.Coefficients[i][channel] *= fit.Peaks[channel];
    }
    return true;
  }

  // A channel's complex coefficient from its (cosine, sine) columns, or the
  // positive sequence of the three voltages' (phase B lagging A by 120 deg).
  static std::complex<double> Coefficient(const Fit &fit, unsigned column, unsigned channel) {
    return std::complex<double>(fit.Coefficients[column][channel], -fit.Coefficients[column + 1][channel]);
  }
  static std::complex<double> Positive(const Fit &fit, unsigned column) {
    const std::complex<double> a(-.5, std::sqrt(3.0) / 2);
    return (Coefficient(fit, column, 0) + a * Coefficient(fit, column, 1) + a * a * Coefficient(fit, column, 2)) / 3.0;
  }

  bool Reference(const Fit &fit, double carrier, double &frequency, double &rocof) {
    const double rmsScale = 1 / std::sqrt(2.0);
    std::complex<double> x = Coefficient(fit, 1, 0) * rmsScale;
    double amplitude = std::abs(x);
    if(!std::isfinite(amplitude) || amplitude <= std::max(1e-6, fit.Peaks[0] * 1e-9) ||
       amplitude < 5 * fit.ResidualRms[0]) {
      ++pStats.ReferenceFailures;
      return false;
    }
    // The positive sequence when it holds more signal for its noise than phase
    // A alone (a mean of three channels carries 1/sqrt(3) of one's noise): a
    // balanced set, never one voltage, a reversed rotation or one live phase.
    // Its phases' 2nd-harmonic images cancel too. ROCOF is the noise-limited
    // estimate: phase A alone needs ~5 dB more SNR for P class's RFE.
    bool positive = false;
    if(pPrevious.VoltageChannels == 3) {
      const std::complex<double> sequence = Positive(fit, 1) * rmsScale;
      positive = std::sqrt(3.0) * std::abs(sequence) > amplitude;
      if(positive) x = sequence;
    }
    std::complex<double> first = (positive ? Positive(fit, 3) : Coefficient(fit, 3, 0)) * rmsScale * double(pNominal);
    std::complex<double> second = (positive ? Positive(fit, 5) : Coefficient(fit, 5, 0)) * rmsScale * 2.0 *
                                  double(pNominal) * double(pNominal);
    std::complex<double> ratio = first / x;
    frequency = carrier + ratio.imag() / (2 * kPi);
    rocof = (second / x - ratio * ratio).imag() / (2 * kPi);
    if(!std::isfinite(frequency) || !std::isfinite(rocof) ||
       std::abs(frequency - pNominal) > 5 ||
       3 * frequency * pPrevious.SampleRateDenominator * pFactor >= .45 * pPrevious.SampleRate) {
      ++pStats.CarrierFailures;
      return false;
    }
    return true;
  }

  // The window's worst declared timing (the samples FitWindow uses).
  void TimeQuality(uint64_t centre, uint64_t reportUs, TimingQuality &timing) {
    bool traceable = true, bounded = true, holdover = false;
    uint64_t worst = 0, used = 0;
    for(unsigned index = 0; index < pCount; ++index) {
      const Point &point = At(index);
      if(std::abs(RelativeU(point.TimestampNs, centre)) >= 1) continue;
      ++used;
      traceable = traceable && point.Traceable;
      bounded = bounded && point.UncertaintyNs > 0;
      holdover = holdover || point.Holdover;
      worst = std::max<uint64_t>(worst, point.UncertaintyNs);
    }
    timing.UtcTraceable = used && traceable;
    timing.ErrorBoundKnown = used && bounded;
    timing.MaximumErrorNs = timing.ErrorBoundKnown ? worst : 0;
    timing.Locked = timing.UtcTraceable && timing.ErrorBoundKnown && !holdover;
    if(timing.Locked) {
      pLastLockedUs = reportUs;
      pHaveLocked = true;
    } else if(pHaveLocked && reportUs >= pLastLockedUs) {
      timing.UnlockDurationKnown = true;
      timing.UnlockedDurationUs = reportUs - pLastLockedUs;
    }
  }

  bool Estimate(uint64_t centre, uint64_t reportUs, Phasor &result) {
    double carrier = pNominal, frequency = 0, rocof = 0;
    Fit finalFit;
    for(unsigned pass = 0; pass < 3; ++pass) {
      Fit fit;
      if(!FitWindow(centre, carrier, fit) || !Reference(fit, carrier, frequency, rocof)) return false;
      finalFit = fit;
      if(std::abs(frequency - carrier) < 1e-9) break;
      carrier = frequency;
    }
    // The phasors: the final fit's first-order sub-model (see the class comment).
    double phasors[kColumns - 2][7];
    if(!Solve(finalFit.Gram, finalFit.Rhs, phasors)) {
      ++pStats.ConditioningFailures;
      return false;
    }
    Phasor estimate;
    estimate.Timestamp = reportUs;
    TimeQuality(centre, reportUs, estimate.Timing);
    estimate.VoltageChannels = pPrevious.VoltageChannels;
    estimate.CurrentChannels = pPrevious.CurrentChannels;
    estimate.Frequency = frequency;
    estimate.ROCOF = rocof;
    // Only the fractional second is needed: nominal frequency is integer Hz.
    uint64_t phaseUs = ((reportUs % 1000000ULL) * pNominal) % 1000000ULL;
    // Decimated, the fundamental passed the filter with its gain at this frequency.
    const double gain = pFactor > 1 ? pDecimator.Gain(frequency) : 1;
    std::complex<double> rotation = std::polar(1 / std::sqrt(2.0) / gain, -2 * kPi * phaseUs / 1000000.0);
    for(unsigned channel = 0; channel < 7; ++channel) {
      std::complex<double> local(phasors[1][channel] * finalFit.Peaks[channel], -phasors[2][channel] * finalFit.Peaks[channel]);
      std::complex<double> value = local * rotation;
      if(!std::isfinite(value.real()) || !std::isfinite(value.imag())) return false;
      if(channel < estimate.VoltageChannels) estimate.Voltage[channel] = value;
      if(channel >= 3 && channel - 3 < estimate.CurrentChannels) estimate.Current[channel - 3] = value;
    }
    result = estimate;
    return true;
  }
};

} // namespace C37118
