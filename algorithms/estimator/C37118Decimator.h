// This file is part of openZmeter, licensed under GPL-3.0-or-later.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace C37118 {

// One point of the PMU estimator's fit: an input sample or a decimated one.
struct Point {
  uint64_t TimestampNs = 0;
  uint32_t UncertaintyNs = 0; // Expanded (k = 2) uncertainty of TimestampNs; 0 = unknown
  bool Traceable = false;     // Its UTC epoch is confirmed
  bool Holdover = false;      // Its time was extrapolated through a loss of lock (#197)
  double Values[7] = {};
};

// The PMU estimator's anti-alias decimation (#190, platform/pb2/timing/sample-time.md).
// A fast source (the PB2's 31,502 S/s) is low-pass filtered and keeps one
// sample in Factor(), down to the lowest rate at or above OutputMinimumHz()
// (1969 S/s on the PB2), because the Taylor-Fourier fit costs in proportion to
// its points. The filter is a Kaiser-windowed sinc with an odd, symmetric set
// of taps and unity gain at DC:
// - its passband reaches PassbandHarmonics() times the nominal frequency, the
//   band of the fit's DC, fundamental and 2nd/3rd harmonic terms;
// - its stopband starts at the output's Nyquist frequency with StopbandDb()
//   of attenuation, so nothing above it aliases into the fit.
// Each output's time is the taps-weighted mean of its inputs' times: on a
// uniform stream exactly the centre input's, so the filter adds no time error,
// only half its span of latency. Its uncertainty is the worst its inputs
// declare (0 if any is unknown), and it is traceable only if all of them are.
// The fundamental leaves with the gain Gain(f) (1 + 7e-6 at 50 Hz on the
// PB2), which the estimator divides out at the measured frequency.
// Continuity belongs to the caller: Configure() or Clear() at every gap.
class Decimator {
 public:
  static constexpr double OutputMinimumHz() { return 1920; } // 32 samples a cycle at 60 Hz
  static constexpr double StopbandDb() { return 100; }
  static constexpr unsigned PassbandHarmonics() { return 4; }

  // The factor for an input rate: 1 (none) below twice OutputMinimumHz().
  static unsigned FactorFor(double inputHz) {
    return inputHz >= 2 * OutputMinimumHz() ? static_cast<unsigned>(inputHz / OutputMinimumHz()) : 1;
  }

  // Designs the filter for this rate, factor (>= 2) and nominal frequency,
  // unless it already has, takes the channels to filter (indices into
  // Point::Values; the others stay zero) and clears it.
  void Configure(double inputHz, unsigned factor, unsigned nominal, const unsigned *channels, unsigned count) {
    if(inputHz != pInputHz || factor != pFactor || nominal != pNominal) Design(inputHz, factor, nominal);
    pActive = std::min(count, 7U);
    std::copy(channels, channels + pActive, pChannels);
    Clear();
  }
  void Clear() {
    pAt = 0;
    pUntilOutput = static_cast<unsigned>(pTaps.size());
  }

  // One input in stream order; true when it completes an output.
  bool Push(const Point &input, Point &output) {
    const unsigned taps = static_cast<unsigned>(pTaps.size());
    if(!taps) return false; // Never configured
    // Each input goes in twice, taps apart, so the last `taps` of them are
    // always contiguous from pAt (after the increment), oldest first.
    for(unsigned k = 0; k < pActive; ++k) {
      double *values = &pValues[pChannels[k] * 2 * taps];
      values[pAt] = values[pAt + taps] = input.Values[pChannels[k]];
    }
    pTimes[pAt] = pTimes[pAt + taps] = input.TimestampNs;
    pUncertainties[pAt] = pUncertainties[pAt + taps] = input.UncertaintyNs;
    pTraceable[pAt] = pTraceable[pAt + taps] = input.Traceable;
    pHoldover[pAt] = pHoldover[pAt + taps] = input.Holdover;
    if(++pAt == taps) pAt = 0;
    if(--pUntilOutput) return false;
    pUntilOutput = pFactor;

    // The taps are symmetric: each pair of inputs around the centre shares
    // one, and four partial sums keep the in-order A53's adder busy.
    output = Point();
    const unsigned middle = taps / 2;
    const double *half = &pTaps[middle]; // half[k]: the tap k inputs from the centre
    for(unsigned c = 0; c < pActive; ++c) {
      const double *values = &pValues[pChannels[c] * 2 * taps + pAt + middle];
      output.Values[pChannels[c]] = Paired(half, middle, [values](unsigned k) { return values[k] + values[-int(k)]; },
                                           half[0] * values[0]);
    }
    // The weighted mean time, from the centre input's: the pairs' deviation
    // from a uniform spacing, exact in int64 and zero on a uniform stream.
    const uint64_t *times = &pTimes[pAt + middle];
    const double offset = Paired(half, middle, [times](unsigned k) {
      return static_cast<double>(static_cast<int64_t>(times[k] + times[-int(k)] - 2 * times[0]));
    }, 0.0);
    const int64_t shift = std::llround(offset);
    output.TimestampNs = times[0] + static_cast<uint64_t>(shift); // Two's complement: a negative shift subtracts
    uint32_t worst = 0;
    bool bounded = true;
    uint8_t traceable = 1, holdover = 0;
    for(unsigned j = 0; j < taps; ++j) {
      const uint32_t uncertainty = pUncertainties[pAt + j];
      bounded &= uncertainty != 0;
      worst = std::max(worst, uncertainty);
      traceable &= pTraceable[pAt + j];
      holdover |= pHoldover[pAt + j];
    }
    output.UncertaintyNs = bounded ? worst : 0;
    output.Traceable = traceable != 0;
    output.Holdover = holdover != 0;
    return true;
  }

  // The filter's gain at a frequency (real: its phase is a pure delay).
  double Gain(double frequencyHz) const {
    const unsigned middle = static_cast<unsigned>(pTaps.size() / 2);
    double gain = pTaps[middle];
    for(unsigned k = 1; k <= middle; ++k)
      gain += 2 * pTaps[middle + k] * std::cos(2 * kPi * frequencyHz * k / pInputHz);
    return gain;
  }
  unsigned Factor() const { return pFactor; }
  unsigned Taps() const { return static_cast<unsigned>(pTaps.size()); }
  double OutputHz() const { return pInputHz / pFactor; }
  // How much later than its newest input an output's time is: half the span.
  double LatencyNs() const { return (pTaps.size() - 1) / 2.0 * 1e9 / pInputHz; }
  const std::vector<double> &Coefficients() const { return pTaps; }

 private:
  static constexpr double kPi = 3.141592653589793238462643383279502884;
  double pInputHz = 0;
  unsigned pFactor = 0, pNominal = 0;
  std::vector<double> pTaps, pValues;
  std::vector<uint64_t> pTimes;
  std::vector<uint32_t> pUncertainties;
  std::vector<uint8_t> pTraceable, pHoldover;
  unsigned pChannels[7] = {}, pActive = 0;
  unsigned pAt = 0, pUntilOutput = 0;

  // centre + the sum over k = 1..middle of half[k] * pair(k), in four partial sums.
  template<class Pair> static double Paired(const double *half, unsigned middle, Pair pair, double centre) {
    double sums[4] = {centre, 0, 0, 0};
    unsigned k = 1;
    for(; k + 3 <= middle; k += 4)
      for(unsigned lane = 0; lane < 4; ++lane) sums[lane] += half[k + lane] * pair(k + lane);
    for(; k <= middle; ++k) sums[0] += half[k] * pair(k);
    return (sums[0] + sums[1]) + (sums[2] + sums[3]);
  }

  static double BesselI0(double x) {
    double sum = 1, term = 1;
    for(unsigned k = 1; k < 100 && term > 1e-17 * sum; ++k) {
      const double half = x / (2.0 * k);
      term *= half * half;
      sum += term;
    }
    return sum;
  }
  // Kaiser's estimates: beta for the attenuation and the length for the
  // transition from the passband edge to the output's Nyquist frequency.
  void Design(double inputHz, unsigned factor, unsigned nominal) {
    pInputHz = inputHz;
    pFactor = factor;
    pNominal = nominal;
    const double pass = PassbandHarmonics() * static_cast<double>(nominal), stop = inputHz / factor / 2;
    const double beta = 0.1102 * (StopbandDb() - 8.7);
    unsigned taps = static_cast<unsigned>(std::ceil((StopbandDb() - 7.95) / (14.36 * (stop - pass) / inputHz))) + 1;
    taps |= 1U;
    const double cutoff = (pass + stop) / 2 / inputHz, middle = (taps - 1) / 2.0, scale = 1 / BesselI0(beta);
    pTaps.assign(taps, 0);
    double sum = 0;
    for(unsigned j = 0; j <= taps / 2; ++j) {
      const double n = j - middle, ratio = n / middle;
      const double sinc = j == taps / 2 ? 2 * cutoff : std::sin(2 * kPi * cutoff * n) / (kPi * n);
      pTaps[j] = pTaps[taps - 1 - j] = sinc * BesselI0(beta * std::sqrt(std::max(0.0, 1 - ratio * ratio))) * scale;
    }
    for(double tap : pTaps) sum += tap;
    for(double &tap : pTaps) tap /= sum;
    pValues.assign(7 * 2 * taps, 0);
    pTimes.assign(2 * taps, 0);
    pUncertainties.assign(2 * taps, 0);
    pTraceable.assign(2 * taps, 0);
    pHoldover.assign(2 * taps, 0);
  }
};

} // namespace C37118
