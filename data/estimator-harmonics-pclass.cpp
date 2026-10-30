// Deterministic estimator regression tests, not IEC/IEEE PMU conformance tests.
#include "../Source/C37118Estimator.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <vector>

namespace {
const double pi = 3.141592653589793238462643383279502884;
const uint64_t epoch = 1800000000000000000ULL;
int failures = 0;

void Check(bool condition, const char *message, int line) {
  if(!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, message); ++failures; }
}
#define CHECK(condition) Check((condition), #condition, __LINE__)

void Near(double actual, double expected, double tolerance, const char *message, int line) {
  if(!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
    std::fprintf(stderr, "FAIL line %d: %s actual=%.12g expected=%.12g tolerance=%.4g\n",
                 line, message, actual, expected, tolerance);
    ++failures;
  }
}
#define NEAR(actual, expected, tolerance) Near((actual), (expected), (tolerance), #actual, __LINE__)

struct Wave {
  unsigned Nominal = 50;
  uint64_t Origin = epoch;
  double Offset = 0, Ramp = 0, Phase = 0, H2 = 0, H3 = 0, DC = 0;
  double AmplitudeSlope = 0, AmplitudeCurve = 0;
  double HighOrder = 0, High = 0; // One more harmonic, of this order and relative amplitude
  double Amplitudes[7] = {230, 231, 229, 10, 11, 9, .75};
  double Phases[7] = {.37, -1.71, 2.43, -.13, -2.21, 1.93, .81};

  double Time(uint64_t timestamp) const {
    return timestamp >= Origin ? static_cast<double>(timestamp - Origin) / 1e9 :
                                 -static_cast<double>(Origin - timestamp) / 1e9;
  }
  double Scale(double t) const { return 1 + AmplitudeSlope * t + AmplitudeCurve * t * t; }
  C37118::EstimatorSample Sample(uint64_t timestamp, uint64_t sequence, uint32_t rate,
                                uint32_t denominator = 1) const {
    C37118::EstimatorSample sample;
    sample.TimestampNs = timestamp;
    sample.Sequence = sequence;
    sample.Generation = 1;
    sample.SampleRate = rate;
    sample.SampleRateDenominator = denominator;
    sample.VoltageChannels = 3;
    sample.CurrentChannels = 4;
    sample.TimeSource = 3; // A GPS-like enum deliberately cannot qualify timing.
    double t = Time(timestamp);
    double nominalPhase = 2 * pi * Nominal * (timestamp % 1000000000ULL) / 1e9;
    double phase = nominalPhase + 2 * pi * (Offset * t + .5 * Ramp * t * t);
    for(unsigned channel = 0; channel < 7; ++channel) {
      sample.Values[channel] = std::sqrt(2.0) * Amplitudes[channel] *
        (Scale(t) * std::cos(phase + Phase + Phases[channel]) +
         H2 * std::cos(2 * phase + .83) + H3 * std::cos(3 * phase - .71) +
         High * std::cos(HighOrder * phase + .29)) + DC;
    }
    return sample;
  }
  std::complex<double> Expected(uint64_t reportUs, unsigned channel) const {
    double t = Time(reportUs * 1000);
    return std::polar(Amplitudes[channel] * Scale(t), Phase + Phases[channel] +
                      2 * pi * (Offset * t + .5 * Ramp * t * t));
  }
};

std::vector<C37118::Phasor> Run(C37118::Estimator &estimator, const Wave &wave, uint64_t start,
                               unsigned samples, uint32_t rate, uint64_t sequence = 0,
                               bool quantizeMicroseconds = false, bool jitter = false,
                               uint32_t denominator = 1) {
  std::vector<C37118::Phasor> frames;
  for(unsigned i = 0; i < samples; ++i) {
    // Absolute index, not a rounded period; each product fits uint64 for the
    // uint32 ratio and the bounded durations exercised here.
    const uint64_t steps = static_cast<uint64_t>(i) * denominator;
    uint64_t delta = (steps / rate) * 1000000000ULL +
                     (steps % rate) * 1000000000ULL / rate;
    if(jitter && i % 3 == 1) delta += 1300;
    if(quantizeMicroseconds) delta = delta / 1000 * 1000;
    C37118::EstimatorSample sample = wave.Sample(start + delta, sequence + i, rate, denominator);
    C37118::Phasor phasor;
    if(estimator.Push(sample, phasor)) frames.push_back(phasor);
  }
  return frames;
}

void CheckQuality(const C37118::Phasor &phasor) {
  CHECK(phasor.Qualification == C37118::MeasurementQualification::Unqualified);
  CHECK(!phasor.Timing.UtcTraceable && !phasor.Timing.Locked && !phasor.Timing.ErrorBoundKnown);
  CHECK(C37118::TimeQualityByte(phasor.Timing) == 0x0F);
  CHECK(C37118::StatusWord(phasor.Timing, phasor.Qualification) == 0xA1F0);
}

void TestNominalAndAllChannels() {
  for(unsigned nominal : {50U, 60U}) {
    Wave wave; wave.Nominal = nominal; wave.DC = 31;
    C37118::Estimator estimator(nominal, nominal);
    auto frames = Run(estimator, wave, epoch, 6400, 32000);
    CHECK(frames.size() >= 8);
    for(const auto &phasor : frames) {
      CheckQuality(phasor);
      CHECK(phasor.VoltageChannels == 3 && phasor.CurrentChannels == 4);
      for(unsigned ch = 0; ch < 7; ++ch) {
        auto value = ch < 3 ? phasor.Voltage[ch] : phasor.Current[ch - 3];
        NEAR(std::abs(value - wave.Expected(phasor.Timestamp, ch)), 0, 1e-8);
        NEAR(std::abs(value), wave.Amplitudes[ch], 1e-8);
      }
      NEAR(phasor.Frequency, nominal, 1e-8);
      NEAR(phasor.ROCOF, 0, 1e-6);
    }
    CHECK(estimator.GetStats().RejectedWindows == 0);
    CHECK(estimator.GetStats().BufferHighWater < 1400);
    CHECK(estimator.GetStats().LastCondition < 1000);
  }
  std::puts("PASS nominal/DC/RMS/all-seven-channel UTC phase and unknown quality");
}

void TestFrequencyAndHarmonics() {
  double largestTve = 0, largestFe = 0, largestRfe = 0;
  for(unsigned nominal : {50U, 60U}) for(double offset : {-2., -1., -.1, 0., .1, 1., 2.})
    for(double phase : {0., pi / 6, pi / 2, pi}) {
      Wave wave; wave.Nominal = nominal; wave.Offset = offset; wave.Phase = phase;
      wave.H2 = wave.H3 = .01;
      C37118::Estimator estimator(nominal, nominal);
      auto frames = Run(estimator, wave, epoch + 370000000, 3840, 32000);
      CHECK(frames.size() >= 3);
      for(const auto &phasor : frames) {
        double tve = std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)) / wave.Amplitudes[0];
        double fe = std::abs(phasor.Frequency - nominal - offset);
        double rfe = std::abs(phasor.ROCOF);
        largestTve = std::max(largestTve, tve);
        largestFe = std::max(largestFe, fe);
        largestRfe = std::max(largestRfe, rfe);
        NEAR(tve, 0, 1e-6);
        NEAR(fe, 0, 1e-5);
        // This wider 50/60 Hz, 56-case sweep includes finite three-fit
        // harmonic-carrier mismatch. This budget is not a standards limit.
        NEAR(rfe, 0, 3e-3);
        if(rfe > 1e-3) CHECK(estimator.GetStats().Fits == frames.size() * 3);
      }
    }
  std::printf("PASS steady offset/harmonic sweep: max relative TVE %.4g, FE %.4g Hz, RFE %.4g Hz/s\n",
              largestTve, largestFe, largestRfe);
}

void TestDynamicsAndNoise() {
  for(unsigned nominal : {50U, 60U}) {
    Wave wave; wave.Nominal = nominal; wave.AmplitudeSlope = .1; wave.AmplitudeCurve = .02;
    C37118::Estimator amplitude(nominal, nominal);
    auto frames = Run(amplitude, wave, epoch, 6400, 32000);
    CHECK(!frames.empty());
    for(const auto &phasor : frames) {
      NEAR(std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)), 0, 1e-8);
      NEAR(phasor.Frequency, nominal, 1e-8);
      NEAR(phasor.ROCOF, 0, 1e-6);
    }
    // Real amplitude derivatives combined with a rotating envelope create
    // nonzero Im(X''/X). Subtracting Im((X'/X)^2) must cancel it for a tone.
    wave.Offset = .5;
    C37118::Estimator mixed(nominal, nominal);
    frames = Run(mixed, wave, epoch, 6400, 32000);
    CHECK(!frames.empty());
    for(const auto &phasor : frames) {
      NEAR(std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)) / 230, 0, 1e-6);
      NEAR(phasor.Frequency, nominal + .5, 1e-5);
      NEAR(phasor.ROCOF, 0, 1e-3);
    }
    wave.Offset = 0;
    wave.AmplitudeSlope = wave.AmplitudeCurve = 0;
    wave.Ramp = 1;
    wave.H3 = .01;
    C37118::Estimator chirp(nominal, nominal);
    frames = Run(chirp, wave, epoch + 370000000, 6400, 32000);
    CHECK(!frames.empty());
    for(const auto &phasor : frames) {
      double t = wave.Time(phasor.Timestamp * 1000);
      NEAR(std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)) / 230, 0, 1e-4);
      NEAR(phasor.Frequency, nominal + t, 1e-3);
      NEAR(phasor.ROCOF, 1, .06); // Measured finite-case approximation budget, not normative.
    }
    C37118::Estimator noisy(nominal, nominal);
    unsigned emitted = 0;
    for(unsigned i = 0; i < 6400; ++i) {
      auto sample = wave.Sample(epoch + i * 31250ULL, i, 32000);
      sample.Values[0] += .1 * std::sin(2 * pi * 719 * i / 32000.0);
      C37118::Phasor phasor;
      if(noisy.Push(sample, phasor)) {
        ++emitted;
        CHECK(std::isfinite(phasor.Frequency) && std::isfinite(phasor.ROCOF));
      }
    }
    CHECK(emitted >= 8);
  }
  std::puts("PASS amplitude modulation, chirp characterization and finite noisy estimates");
}

void TestZeroAndOutOfBand() {
  Wave wave;
  for(double &amplitude : wave.Amplitudes) amplitude = 0;
  wave.DC = 5;
  C37118::Estimator zero(50, 50);
  CHECK(Run(zero, wave, epoch, 6400, 32000).empty());
  CHECK(zero.GetStats().ReferenceFailures >= 8);
  CHECK(zero.GetStats().FramesEmitted == 0);
  C37118::Estimator noise(50, 50);
  uint32_t random = 0x3175489;
  unsigned noiseFrames = 0;
  for(unsigned i = 0; i < 6400; ++i) {
    auto sample = wave.Sample(epoch + i * 31250ULL, i, 32000);
    random = random * 1664525U + 1013904223U;
    sample.Values[0] = .02 * (static_cast<double>(random) / 4294967296.0 - .5);
    C37118::Phasor phasor;
    if(noise.Push(sample, phasor)) ++noiseFrames;
  }
  CHECK(noiseFrames == 0);
  CHECK(noise.GetStats().ReferenceFailures >= 8);
  wave = Wave(); wave.Offset = 9;
  C37118::Estimator outside(50, 50);
  CHECK(Run(outside, wave, epoch, 6400, 32000).empty());
  CHECK(outside.GetStats().CarrierFailures > 0);
  std::puts("PASS zero/DC-only, pure noise and out-of-band input produce no invented f/ROCOF");
}

bool OnGrid(uint64_t timestamp, unsigned rate) {
  uint64_t fraction = timestamp % 1000000;
  for(unsigned k = 0; k < rate; ++k)
    if(fraction == (k * 1000000ULL + rate / 2) / rate) return true;
  return false;
}

void TestGridsAndLargeEpochs() {
  for(unsigned nominal : {50U, 60U}) for(int rate : {10, 12, 15, 20, 25, 30, 50, 60}) {
    if(!C37118::Estimator::SupportedRate(nominal, rate)) continue;
    Wave wave; wave.Nominal = nominal; wave.Origin = 8000000000000000000ULL;
    C37118::Estimator estimator(nominal, rate);
    uint64_t start = wave.Origin + 913700000;
    auto frames = Run(estimator, wave, start, 1500, 1000);
    CHECK(frames.size() >= static_cast<unsigned>(rate));
    uint64_t previous = 0;
    for(const auto &phasor : frames) {
      CHECK(OnGrid(phasor.Timestamp, rate));
      CHECK(phasor.Timestamp > previous);
      CHECK(phasor.Timestamp * 1000 >= start + (1000000000ULL + nominal - 1) / nominal);
      if(previous) CHECK(phasor.Timestamp - previous == 1000000U / rate ||
                         phasor.Timestamp - previous == (1000000U + rate - 1) / rate);
      NEAR(std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)), 0, 1e-8);
      previous = phasor.Timestamp;
    }
    CHECK(estimator.GetStats().RejectedWindows == 0);
  }
  for(auto pair : {std::pair<unsigned, int>(50, 12), {60, 25}, {49, 10}, {50, 0}, {60, -1}}) {
    CHECK(!C37118::Estimator::SupportedRate(pair.first, pair.second));
    bool threw = false;
    try { C37118::Estimator bad(pair.first, pair.second); } catch(const std::invalid_argument &) { threw = true; }
    CHECK(threw);
  }
  std::puts("PASS all supported report grids, late starts, second rollover and large integer epochs");
}

void TestQuantizedAndJitteredSamples() {
  for(unsigned rate : {984U, 5000U, 32000U, 65000U}) {
    Wave wave; wave.Offset = 1; wave.H3 = .01;
    C37118::Estimator estimator(50, 50);
    auto frames = Run(estimator, wave, epoch + 170000000, rate / 5, rate, 0, true, true);
    CHECK(frames.size() >= 7);
    CHECK(estimator.GetStats().Resets == 0);
    for(const auto &phasor : frames) {
      NEAR(std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)) / 230, 0, 1e-6);
      NEAR(phasor.Frequency, 51, 1e-5);
      NEAR(phasor.ROCOF, 0, 1e-3);
    }
  }
  std::puts("PASS 984/5k/32k/65k sample rates, microsecond quantization and interval jitter");
}

void TestFractionalRate() {
  // PB2's present 124 ns PWM / OSR4096 yields one sample every 1015808 ns.
  // These are analytical input vectors, not ADC, X1 or timing qualifications.
  const uint32_t numerator = 1000000000U, denominator = 1015808U;
  for(unsigned nominal : {50U, 60U}) for(double offset : {-1., 0., 1.}) {
    Wave wave; wave.Nominal = nominal; wave.Offset = offset;
    wave.DC = 7; wave.H2 = wave.H3 = .01;
    C37118::Estimator estimator(nominal, nominal);
    auto frames = Run(estimator, wave, epoch + 913700000, 600, numerator, 0,
                      false, false, denominator);
    CHECK(frames.size() >= nominal / 2);
    uint64_t previous = 0;
    for(const auto &phasor : frames) {
      CheckQuality(phasor);
      CHECK(OnGrid(phasor.Timestamp, nominal));
      CHECK(phasor.Timestamp > previous);
      for(unsigned ch = 0; ch < 7; ++ch) {
        auto value = ch < 3 ? phasor.Voltage[ch] : phasor.Current[ch - 3];
        NEAR(std::abs(value - wave.Expected(phasor.Timestamp, ch)) / wave.Amplitudes[ch], 0, 1e-6);
      }
      NEAR(phasor.Frequency, nominal + offset, 1e-5);
      NEAR(phasor.ROCOF, 0, 3e-3);
      previous = phasor.Timestamp;
    }
    CHECK(estimator.GetStats().Resets == 0);
    CHECK(estimator.GetStats().RejectedWindows == 0);
    CHECK(estimator.GetStats().SamplesAccepted == 600);
    CHECK(estimator.GetStats().BufferHighWater < 64);
  }

  // Representation changes are explicit metadata epochs even when their
  // mathematical rates agree; no previously buffered fit may straddle them.
  Wave wave;
  C37118::Estimator changed(50, 50);
  auto before = Run(changed, wave, epoch, 100, numerator, 0, false, false, denominator);
  CHECK(!before.empty());
  const uint64_t changedAt = epoch + 100ULL * denominator;
  auto after = Run(changed, wave, changedAt, 150, numerator / 2, 100,
                   false, false, denominator / 2);
  CHECK(changed.GetStats().MetadataChanges == 1);
  CHECK(changed.GetStats().Resets == 1);
  CHECK(!after.empty());
  if(!before.empty() && !after.empty()) {
    CHECK(after.front().Timestamp > before.back().Timestamp);
    CHECK(after.front().Timestamp * 1000 >= changedAt + 20000000ULL);
  }

  // Change only the denominator: 32 kHz -> 16 kHz. The previous epoch's
  // cadence cannot be retained just because the numerator is unchanged.
  C37118::Estimator halved(50, 50);
  Run(halved, wave, epoch, 3200, 32000);
  after = Run(halved, wave, epoch + 100000000, 1600, 32000, 3200, false, false, 2);
  CHECK(halved.GetStats().MetadataChanges == 1);
  CHECK(halved.GetStats().CadenceDiscontinuities == 0);
  CHECK(!after.empty() && after.front().Timestamp >= epoch / 1000 + 120000);
  std::puts("PASS fractional 984.438 SPS at 50/60 Hz, seven channels, UTC grid and denominator epochs");
}

void TestFinalPB2Rate() {
  // PB2's final X1 profile: one sample every 31744 ns (31502.016 SPS).
  const uint32_t numerator = 1000000000U, denominator = 31744U;
  double largestFe = 0, largestTve = 0;
  for(double offset : {-.1, 0., .1}) {
    Wave wave; wave.Offset = offset; wave.H2 = wave.H3 = .01; wave.DC = 3;
    C37118::Estimator estimator(50, 50);
    auto frames = Run(estimator, wave, epoch + 913700000, 15751, numerator, 0, false, false, denominator);
    CHECK(frames.size() >= 22); // 0.5 s at 50 fps less the first report's 20 ms lookahead
    for(const auto &phasor : frames) {
      CheckQuality(phasor);
      for(unsigned ch = 0; ch < 7; ++ch) {
        auto value = ch < 3 ? phasor.Voltage[ch] : phasor.Current[ch - 3];
        const double tve = std::abs(value - wave.Expected(phasor.Timestamp, ch)) / wave.Amplitudes[ch];
        largestTve = std::max(largestTve, tve);
        NEAR(tve, 0, 1e-6);
      }
      largestFe = std::max(largestFe, std::abs(phasor.Frequency - 50 - offset));
      NEAR(phasor.Frequency, 50 + offset, 1e-5);
      NEAR(phasor.ROCOF, 0, 3e-3);
    }
    CHECK(estimator.GetStats().Resets == 0 && estimator.GetStats().RejectedWindows == 0);
    CHECK(estimator.GetStats().CadenceDiscontinuities == 0);
    // Two nominal cycles plus the cadence margin: ~1262 of the 4096 slots.
    CHECK(estimator.GetStats().BufferHighWater > 1200 && estimator.GetStats().BufferHighWater < 1300);
  }
  // PB2 declares three voltage channels and no current channel. Its unused
  // slots are zero, and the fit skips them: the voltage results must be the
  // same bits as a stream that declares four (zero) current channels.
  Wave wave; wave.Offset = .1; wave.H3 = .01;
  for(unsigned ch = 3; ch < 7; ++ch) wave.Amplitudes[ch] = 0;
  C37118::Estimator voltageOnly(50, 50), declared(50, 50);
  std::vector<C37118::Phasor> a, b;
  for(unsigned i = 0; i < 15751; ++i) {
    const uint64_t delta = static_cast<uint64_t>(i) * denominator;
    auto sample = wave.Sample(epoch + delta, i, numerator, denominator);
    C37118::Phasor out;
    if(declared.Push(sample, out)) b.push_back(out);
    sample.CurrentChannels = 0;
    if(voltageOnly.Push(sample, out)) a.push_back(out);
  }
  bool same = a.size() == b.size() && !a.empty();
  for(size_t n = 0; same && n < a.size(); ++n) {
    same = a[n].Timestamp == b[n].Timestamp && a[n].Frequency == b[n].Frequency && a[n].ROCOF == b[n].ROCOF;
    for(unsigned ch = 0; same && ch < 3; ++ch) same = a[n].Voltage[ch] == b[n].Voltage[ch];
  }
  CHECK(same);
  std::printf("PASS final PB2 31502.016 SPS at 49.9/50/50.1 Hz: max FE %.3g Hz, TVE %.3g; voltage-only fit identical\n",
              largestFe, largestTve);
}

// #190: the publisher decimates a fast source before the fit (C37118Decimator.h).
// Fitting ~1969 points a second instead of 31,502 must give the direct fit's
// results on the same report instants, and must not alias any harmonic.
struct Worst { double Tve = 0, Fe = 0, Rfe = 0; };
Worst Measure(const std::vector<C37118::Phasor> &frames, const Wave &wave, double frequency) {
  Worst worst;
  for(const auto &phasor : frames) {
    worst.Tve = std::max(worst.Tve, std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)) / wave.Amplitudes[0]);
    worst.Fe = std::max(worst.Fe, std::abs(phasor.Frequency - frequency));
    worst.Rfe = std::max(worst.Rfe, std::abs(phasor.ROCOF));
  }
  return worst;
}

void TestDecimated() {
  using Decimation = C37118::Estimator::Decimation;
  const uint32_t numerator = 1000000000U, denominator = 31744U;
  double largestTve = 0, largestFe = 0, largestRfe = 0, largestDifference = 0;
  for(unsigned nominal : {50U, 60U}) for(double offset : {-4.5, -2., -.1, 0., .1, 2., 4.5}) {
    Wave wave; wave.Nominal = nominal; wave.Offset = offset; wave.Phase = .4;
    wave.H2 = wave.H3 = .01; wave.DC = 3;
    C37118::Estimator decimated(nominal, nominal, Decimation::Automatic), direct(nominal, nominal);
    auto frames = Run(decimated, wave, epoch + 913700000, 15751, numerator, 0, false, false, denominator);
    auto reference = Run(direct, wave, epoch + 913700000, 15751, numerator, 0, false, false, denominator);
    const auto &stats = decimated.GetStats();
    CHECK(stats.DecimationFactor == 16 && stats.FilterTaps == (nominal == 50 ? 259U : 273U));
    NEAR(stats.PointRateHz, 1e9 / 31744 / 16, 1e-9);
    CHECK(stats.Resets == 0 && stats.RejectedWindows == 0 && stats.BufferHighWater < 100);
    // The filter's 4 ms may cost the first report.
    CHECK(frames.size() + 1 >= reference.size() && frames.size() <= reference.size() && frames.size() >= nominal / 2 - 4);
    size_t matched = 0;
    for(const auto &phasor : frames) {
      CheckQuality(phasor);
      CHECK(OnGrid(phasor.Timestamp, nominal));
      for(const auto &other : reference) if(other.Timestamp == phasor.Timestamp) {
        ++matched;
        for(unsigned ch = 0; ch < 7; ++ch) {
          auto value = ch < 3 ? phasor.Voltage[ch] : phasor.Current[ch - 3];
          auto expected = ch < 3 ? other.Voltage[ch] : other.Current[ch - 3];
          const double difference = std::abs(value - expected) / wave.Amplitudes[ch];
          largestDifference = std::max(largestDifference, difference);
          NEAR(difference, 0, 1e-8);
        }
        NEAR(phasor.Frequency, other.Frequency, 1e-7);
        NEAR(phasor.ROCOF, other.ROCOF, 1e-4);
      }
      // The direct fit's own budgets, within its +-2 Hz sweep (TestFrequencyAndHarmonics).
      if(std::abs(offset) > 2) continue;
      for(unsigned ch = 0; ch < 7; ++ch) {
        auto value = ch < 3 ? phasor.Voltage[ch] : phasor.Current[ch - 3];
        const double tve = std::abs(value - wave.Expected(phasor.Timestamp, ch)) / wave.Amplitudes[ch];
        largestTve = std::max(largestTve, tve);
        NEAR(tve, 0, 1e-6);
      }
      largestFe = std::max(largestFe, std::abs(phasor.Frequency - nominal - offset));
      largestRfe = std::max(largestRfe, std::abs(phasor.ROCOF));
      NEAR(phasor.Frequency, nominal + offset, 1e-5);
      NEAR(phasor.ROCOF, 0, 3e-3);
    }
    CHECK(matched == frames.size());
  }
  // Any one harmonic from the 4th to the 50th at 10 % (M class's test level).
  // Below the output's Nyquist frequency (984 Hz) the filter only attenuates
  // it, so the result stays the direct fit's or better; above, it must not
  // alias: unfiltered, the 39th at 50 Hz (1950 Hz) would land at 18.9 Hz.
  Worst aliased, ratio;
  for(unsigned nominal : {50U, 60U}) for(unsigned order = 4; order <= 50; ++order) for(double offset : {0., .1}) {
    Wave wave; wave.Nominal = nominal; wave.Offset = offset; wave.HighOrder = order; wave.High = .1;
    C37118::Estimator decimated(nominal, nominal, Decimation::Automatic), direct(nominal, nominal);
    auto frames = Run(decimated, wave, epoch + 913700000, 9451, numerator, 0, false, false, denominator);
    auto reference = Run(direct, wave, epoch + 913700000, 9451, numerator, 0, false, false, denominator);
    CHECK(frames.size() >= nominal / 4 && decimated.GetStats().RejectedWindows == 0);
    const Worst a = Measure(frames, wave, nominal + offset), b = Measure(reference, wave, nominal + offset);
    // About the direct fit's, beyond the aliasing floor checked below:
    // fewer points sample the window's response to a harmonic near 1 kHz a
    // little differently (its ROCOF error up to ~1.3x at 60 Hz, 8th-10th).
    if(b.Tve > 1e-6) ratio.Tve = std::max(ratio.Tve, a.Tve / b.Tve);
    if(b.Fe > 1e-5) ratio.Fe = std::max(ratio.Fe, a.Fe / b.Fe);
    if(b.Rfe > 1e-3) ratio.Rfe = std::max(ratio.Rfe, a.Rfe / b.Rfe);
    const bool close = a.Tve <= std::max(1.2 * b.Tve, 1e-6) && a.Fe <= std::max(1.2 * b.Fe, 1e-5) &&
                       a.Rfe <= std::max(1.5 * b.Rfe, 1e-3);
    if(!close) std::fprintf(stderr, "  %u Hz %+.1f, order %u: decimated TVE %.3g FE %.3g RFE %.3g, direct %.3g %.3g %.3g\n",
                            nominal, offset, order, a.Tve, a.Fe, a.Rfe, b.Tve, b.Fe, b.Rfe);
    CHECK(close);
    if(order * nominal < 1e9 / 31744 / 16 / 2) continue;
    aliased.Tve = std::max(aliased.Tve, a.Tve);
    aliased.Fe = std::max(aliased.Fe, a.Fe);
    aliased.Rfe = std::max(aliased.Rfe, a.Rfe);
  }
  CHECK(aliased.Tve < 1e-6 && aliased.Fe < 1e-5 && aliased.Rfe < 1e-3);
  // At OSR4096's 984 S/s there is nothing to decimate: the same bits as Off.
  Wave wave; wave.Offset = .3; wave.H3 = .01;
  C37118::Estimator automatic(50, 50, Decimation::Automatic), off(50, 50);
  auto a = Run(automatic, wave, epoch + 913700000, 600, numerator, 0, false, false, 1015808U);
  auto b = Run(off, wave, epoch + 913700000, 600, numerator, 0, false, false, 1015808U);
  bool same = a.size() == b.size() && !a.empty() && automatic.GetStats().DecimationFactor == 1;
  for(size_t n = 0; same && n < a.size(); ++n)
    same = a[n].Timestamp == b[n].Timestamp && a[n].Voltage[0] == b[n].Voltage[0] && a[n].Frequency == b[n].Frequency;
  CHECK(same);
  std::printf("PASS decimated PB2 stream, 16:1 at 50/60 Hz: within +-2 Hz TVE %.3g, FE %.3g Hz, RFE %.3g Hz/s; "
              "to +-4.5 Hz %.3g from the direct fit\n", largestTve, largestFe, largestRfe, largestDifference);
  std::printf("PASS one harmonic 4th-50th at 10 %%: at most %.3gx the direct fit's TVE, %.3gx its FE, %.3gx its RFE; "
              "above 984 Hz TVE %.3g, FE %.3g Hz, RFE %.3g Hz/s; nothing decimated at 984 S/s\n",
              ratio.Tve, ratio.Fe, ratio.Rfe, aliased.Tve, aliased.Fe, aliased.Rfe);
}

void TestRationalBounds() {
  struct RateCase { uint32_t Numerator, Denominator; bool Valid; };
  const uint32_t max32 = std::numeric_limits<uint32_t>::max();
  const RateCase cases[] = {
    {0, 1, false}, {1000000000U, 0, false}, {0, 0, false},
    {max32, 1, false}, {1, max32, false}, {max32, max32, false},
    {599999999U, 1000000U, false}, {600000000U, 1000000U, true},
    {4225000000U, 65000U, true}, {4225000001U, 65000U, false},
    {max32, 7158278U, true}, {max32, 7158279U, false},
    {1000000000U, 1015808U, true}, {984U, 1, true}
  };
  Wave wave;
  for(const auto &rate : cases) {
    C37118::Estimator estimator(50, 50);
    auto sample = wave.Sample(epoch, 0, rate.Numerator, rate.Denominator);
    C37118::Phasor output;
    CHECK(!estimator.Push(sample, output)); // A lone sample cannot make a window.
    CHECK(estimator.GetStats().InvalidSamples == (rate.Valid ? 0U : 1U));
    CHECK(estimator.GetStats().SamplesAccepted == (rate.Valid ? 1U : 0U));
  }

  // Independent numerical boundaries for (1e9 / 1015808) SPS: 80% of
  // 1015808 ns rounded down minus 2000, and 120% rounded up plus 2000.
  // Rounding the sample rate to 984 Hz incorrectly rejects the lower edge.
  for(uint64_t interval : {810645ULL, 810646ULL, 1220970ULL, 1220971ULL}) {
    C37118::Estimator estimator(50, 50);
    C37118::Phasor output;
    CHECK(!estimator.Push(wave.Sample(epoch, 0, 1000000000U, 1015808U), output));
    CHECK(!estimator.Push(wave.Sample(epoch + interval, 1, 1000000000U, 1015808U), output));
    const bool acceptedCadence = interval >= 810646ULL && interval <= 1220970ULL;
    CHECK(estimator.GetStats().CadenceDiscontinuities == (acceptedCadence ? 0U : 1U));
  }
  std::puts("PASS exact rational rate/cadence boundaries, zero denominators and uint32 extremes");
}

void TestReset(const std::function<void(C37118::EstimatorSample &)> &change, bool invalid,
               C37118::Estimator::Decimation decimation) {
  Wave wave;
  C37118::Estimator estimator(50, 50, decimation);
  auto first = Run(estimator, wave, epoch, 3200, 32000);
  // Decimated, the first report waits for the filter's 4 ms too.
  CHECK(first.size() >= (decimation == C37118::Estimator::Decimation::Off ? 3U : 2U));
  uint64_t changedAt = epoch + 100000000;
  auto changed = wave.Sample(changedAt, 3200, 32000);
  change(changed);
  C37118::Phasor output;
  CHECK(!estimator.Push(changed, output));
  CHECK(estimator.GetStats().Resets >= 1);
  uint64_t nextAt = changed.TimestampNs + 31250;
  unsigned emitted = 0;
  for(unsigned i = 0; i < 3200; ++i) {
    auto sample = wave.Sample(nextAt + i * 31250ULL, changed.Sequence + i + 1, 32000);
    if(!invalid) {
      sample.Generation = changed.Generation;
      sample.VoltageChannels = changed.VoltageChannels;
      sample.CurrentChannels = changed.CurrentChannels;
      sample.TimeSource = changed.TimeSource;
      sample.ClockGeneration = changed.ClockGeneration;
      sample.Epoch = changed.Epoch;
      sample.Scale = changed.Scale;
      sample.ChannelMap = changed.ChannelMap;
    }
    if(estimator.Push(sample, output)) {
      ++emitted;
      CHECK(output.Timestamp > first.back().Timestamp);
      CHECK(output.Timestamp * 1000 >= changed.TimestampNs + 20000000ULL);
      CHECK(output.Timestamp * 1000 + 20000000ULL <= sample.TimestampNs);
    }
  }
  CHECK(emitted >= 2);
}

void TestDiscontinuities() {
  for(auto mode : {C37118::Estimator::Decimation::Off, C37118::Estimator::Decimation::Automatic}) {
    TestReset([](C37118::EstimatorSample &s) { ++s.Generation; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { ++s.Sequence; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.TimestampNs -= 31250; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.TimestampNs += 1000000; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.TimeSource = 1; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { ++s.ClockGeneration; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.Epoch = ozmv::signal::EpochAssociation::Candidate; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.Scale = ozmv::signal::ScaleBasis::NominalCircuit; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.ChannelMap = ozmv::signal::ChannelMapBasis::Documented; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.Epoch = ozmv::signal::EpochAssociation::Unavailable; }, true, mode);
    TestReset([](C37118::EstimatorSample &s) { s.CurrentChannels = 3; }, false, mode);
    TestReset([](C37118::EstimatorSample &s) { s.Values[0] = std::numeric_limits<double>::quiet_NaN(); }, true, mode);
    TestReset([](C37118::EstimatorSample &s) { s.Valid = false; }, true, mode);
    TestReset([](C37118::EstimatorSample &s) { s.SampleRate = 0; }, true, mode);
    TestReset([](C37118::EstimatorSample &s) { s.SampleRate = 65001; }, true, mode);
    TestReset([](C37118::EstimatorSample &s) { s.SampleRateDenominator = 0; }, true, mode);
    TestReset([](C37118::EstimatorSample &s) { s.SampleRateDenominator = std::numeric_limits<uint32_t>::max(); }, true, mode);
  }
  for(auto mode : {C37118::Estimator::Decimation::Off, C37118::Estimator::Decimation::Automatic}) {
    Wave wave;
    C37118::Estimator changedRate(50, 50, mode);
    Run(changedRate, wave, epoch, 3200, 32000);
    auto frames = Run(changedRate, wave, epoch + 100000000, 6500, 65000, 3200, true);
    CHECK(changedRate.GetStats().MetadataChanges == 1);
    CHECK(!frames.empty() && frames.front().Timestamp >= epoch / 1000 + 120000);
    // Decimated, the new rate gets its own filter: 33:1 at 65 kS/s.
    if(mode != C37118::Estimator::Decimation::Off)
      CHECK(changedRate.GetStats().DecimationFactor == 33 && changedRate.GetStats().FilterTaps > 259);
    for(const auto &phasor : frames) NEAR(std::abs(phasor.Voltage[0] - wave.Expected(phasor.Timestamp, 0)) / 230, 0, 1e-6);
    changedRate.Reset();
    CHECK(!frames.empty() && !changedRate.Push(wave.Sample(epoch + 200000000, 10000, 32000), frames.front()));
  }
  std::puts("PASS generation/config/source changes, gaps, invalid samples and explicit reset rewarm");
}

void Benchmark() {
  Wave wave; wave.Offset = 1; wave.H2 = wave.H3 = .01;
  C37118::Estimator estimator(50, 50);
  auto start = std::chrono::steady_clock::now();
  auto frames = Run(estimator, wave, epoch, 64000, 32000);
  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(frames.size() >= 95);
  std::printf("BENCH host only: 2 s at32ksps/50fps, %zu estimates, %.3f s elapsed, %llu fit passes, buffer high-water %llu\n",
              frames.size(), elapsed, static_cast<unsigned long long>(estimator.GetStats().Fits),
              static_cast<unsigned long long>(estimator.GetStats().BufferHighWater));
  // The PB2's stream (three voltages at 31,502 S/s), synthesized beforehand so
  // that only Push is timed: the direct fit against the decimated one.
  const uint32_t numerator = 1000000000U, denominator = 31744U;
  std::vector<C37118::EstimatorSample> samples;
  for(unsigned i = 0; i < 63004; ++i) {
    auto sample = wave.Sample(epoch + static_cast<uint64_t>(i) * denominator, i, numerator, denominator);
    sample.CurrentChannels = 0;
    samples.push_back(sample);
  }
  double best[2] = {1e9, 1e9};
  size_t reports[2] = {0, 0};
  for(unsigned repeat = 0; repeat < 5; ++repeat) for(unsigned mode = 0; mode < 2; ++mode) {
    C37118::Estimator timed(50, 50, mode ? C37118::Estimator::Decimation::Automatic : C37118::Estimator::Decimation::Off);
    C37118::Phasor phasor;
    size_t count = 0;
    auto begin = std::chrono::steady_clock::now();
    for(const auto &sample : samples) count += timed.Push(sample, phasor);
    best[mode] = std::min(best[mode], std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count());
    reports[mode] = count;
  }
  std::printf("BENCH host only: 2 s of the PB2 stream, direct %.1f ms (%zu reports), decimated %.1f ms (%zu reports): %.1fx less\n",
              best[0] * 1e3, reports[0], best[1] * 1e3, reports[1], best[0] / best[1]);
}
} // namespace

// #190: the reports' time quality from the samples' declared timing
// (platform/pb2/timing/sample-time.md). Annex A: MSG_TQ in FRACSEC, STAT bit 13
// (sync), bits 8-6 (PMU time quality) and 5-4 (unlocked time); data stays in
// test mode (bits 15-14 = 10) because the measurement is not qualified.
void TestTimeQuality(C37118::Estimator::Decimation decimation) {
  const uint32_t numerator = 1000000000U, denominator = 31744U;
  Wave wave;
  auto run = [&](C37118::Estimator &estimator, unsigned first, unsigned count,
                 ozmv::signal::EpochAssociation association, uint32_t uncertainty, unsigned zeroAt = ~0U) {
    std::vector<C37118::Phasor> frames;
    for(unsigned i = first; i < first + count; ++i) {
      auto sample = wave.Sample(epoch + static_cast<uint64_t>(i) * denominator, i, numerator, denominator);
      sample.Epoch = association;
      sample.TimeUncertaintyNs = i == zeroAt ? 0 : uncertainty;
      sample.CurrentChannels = 0;
      C37118::Phasor out;
      if(estimator.Push(sample, out)) frames.push_back(out);
    }
    return frames;
  };
  using ozmv::signal::EpochAssociation;
  C37118::Estimator estimator(50, 50, decimation);
  // Confirmed GPS time, 87 ns (k = 2): locked, MSG_TQ 0, sync bit clear, "< 100 ns".
  auto locked = run(estimator, 0, 15751, EpochAssociation::Confirmed, 87);
  CHECK(locked.size() >= 21);
  for(const auto &phasor : locked) {
    CHECK(phasor.Timing.UtcTraceable && phasor.Timing.ErrorBoundKnown && phasor.Timing.Locked);
    CHECK(phasor.Timing.MaximumErrorNs == 87);
    CHECK(C37118::TimeQualityByte(phasor.Timing) == 0x00);
    CHECK(C37118::StatusWord(phasor.Timing, phasor.Qualification) == 0x8040);
  }
  // A sample of unknown uncertainty in the window: no bound, so not locked.
  auto unbounded = run(estimator, 15751, 3151, EpochAssociation::Confirmed, 87, 15751 + 1575);
  bool sawUnbounded = false;
  for(const auto &phasor : unbounded)
    if(!phasor.Timing.ErrorBoundKnown) {
      sawUnbounded = true;
      CHECK(!phasor.Timing.Locked && C37118::TimeQualityByte(phasor.Timing) == 0x0F);
      CHECK((C37118::StatusWord(phasor.Timing, phasor.Qualification) & 0x2000) != 0);
    }
  CHECK(sawUnbounded);
  // A larger bound: still locked, "< 1 us".
  auto wider = run(estimator, 18902, 3151, EpochAssociation::Confirmed, 300);
  CHECK(!wider.empty() && wider.back().Timing.Locked && wider.back().Timing.MaximumErrorNs == 300);
  CHECK(!wider.empty() && C37118::StatusWord(wider.back().Timing, wider.back().Qualification) == 0x8080);
  // The epoch falls back to a candidate (the estimator restarts on that change):
  // not traceable, sync bit set, MSG_TQ fault, and the unlocked time since the
  // last locked report, under 10 s here.
  auto candidate = run(estimator, 22053, 6301, EpochAssociation::Candidate, 87);
  CHECK(!candidate.empty());
  for(const auto &phasor : candidate) {
    CHECK(!phasor.Timing.UtcTraceable && !phasor.Timing.Locked);
    CHECK(phasor.Timing.UnlockDurationKnown && phasor.Timing.UnlockedDurationUs < 10000000ULL);
    CHECK(C37118::TimeQualityByte(phasor.Timing) == 0x0F);
    CHECK(C37118::StatusWord(phasor.Timing, phasor.Qualification) == (0x8000 | 0x2000 | (7 << 6)));
  }
  std::printf("PASS PMU time quality%s: locked at 87 ns (STAT 0x8040), 300 ns (0x8080), unbounded and candidate unlocked\n",
              decimation == C37118::Estimator::Decimation::Off ? "" : ", decimated");
}


// P class, IEC/IEEE 60255-118-1 Table 2 and 3: one harmonic at 1 % from the 2nd to the 50th, at f0,
// limits TVE 1 %, FE 0.005 Hz, RFE 0.4 Hz/s. Our decimated estimator on the PB2 stream.
int main(int argc, char **argv) {
  const uint32_t numerator = 1000000000U, denominator = 31744U;
  const double level = argc > 1 ? atof(argv[1]) : 0.01;
  for(unsigned nominal : {50U, 60U}) {
    Worst all; unsigned worstFe = 0, worstRfe = 0;
    for(unsigned order = 2; order <= 50; ++order) {
      Wave wave; wave.Nominal = nominal; wave.Offset = 0;
      if(order == 2) wave.H2 = level; else if(order == 3) wave.H3 = level; else { wave.HighOrder = order; wave.High = level; }
      C37118::Estimator decimated(nominal, nominal, C37118::Estimator::Decimation::Automatic);
      auto frames = Run(decimated, wave, epoch + 913700000, 31502 * 2, numerator, 0, false, false, denominator);
      const Worst w = Measure(frames, wave, nominal);
      if(w.Fe > all.Fe) { all.Fe = w.Fe; worstFe = order; }
      if(w.Rfe > all.Rfe) { all.Rfe = w.Rfe; worstRfe = order; }
      all.Tve = std::max(all.Tve, w.Tve);
      if(order <= 12 || w.Fe > 0.005 || w.Rfe > 0.4)
        std::printf("%u Hz, harmonic %2u at %.0f %%: TVE %.2e  FE %.2e Hz  RFE %.3f Hz/s%s\n", nominal, order, level * 100,
                    w.Tve, w.Fe, w.Rfe, (w.Fe > 0.005 || w.Rfe > 0.4) ? "  <- beyond P class" : "");
    }
    std::printf("%u Hz worst: TVE %.2e, FE %.2e Hz (harmonic %u), RFE %.3f Hz/s (harmonic %u)\n\n", nominal, all.Tve,
                all.Fe, worstFe, all.Rfe, worstRfe);
  }
  return 0;
}
