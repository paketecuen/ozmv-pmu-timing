// E5: IEC/IEEE 60255-118-1:2018 P-class compliance of the production estimator, in simulation.
// The PB2 stream (1e9/31744 S/s, ideal sample times, optional ADC noise and time jitter), balanced
// three-phase test signals as in the standard (Eqs. 15-43), reports at Fs = f0 frames/s, reference
// phasors, frequency and ROCOF from the standard's signal definitions. Limits: Tables 2-10 (P class).
//
// Every test is evaluated on the three phase phasors and on the positive sequence (5.1: a PMU
// reports single-phase or positive-sequence synchrophasors, or both). Frequency and ROCOF are the
// estimator's own (it derives them from phase A).
//
// Steps follow 5.2.3, 5.2.4 and Annex C.4: equivalent-time sampling with the step at n = 20
// fractions of a reporting interval, the reports of all tests interleaved by their offset from the
// step, linear interpolation between neighbours; state levels from the first and last 1 s of a
// 2 s + 4 x (response-time limit) epoch, state boundaries at +-0.5 % of the step, pre- and
// post-transition overshoot and undershoot.
//
// Build (from the paper folder): c++ -std=c++17 -O2 -I algorithms/estimator -o analysis/e5 analysis/e5_pclass.cpp
// Run:   analysis/e5 [f0=50] [noise_db_below_rms=none] [jitter_ns_rms=0] [csv folder for the step waveforms]
#include "C37118Estimator.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr uint32_t Numerator = 1000000000U, Denominator = 31744U;
constexpr uint64_t Epoch = 1800000000000000000ULL; // A UTC second boundary, ns
constexpr unsigned Phasors = 4;                    // Phases A, B, C and the positive sequence
const char *PhasorName[Phasors] = {"A", "B", "C", "PS"};
const std::complex<double> A3 = std::polar(1.0, 2 * Pi / 3);

double F0 = 50;
double NoiseRms = 0;       // Relative to the amplitude
double JitterNs = 0;       // RMS of a Gaussian sample-time error

// A test signal: magnitude factor m(t), phase deviation p(t) (rad, beyond 2 pi f0 t), and their
// derivatives define the reference phasor, frequency and ROCOF at any time t (s since Epoch).
struct Signal {
  std::function<double(double)> Mag, Phase, Freq, Rocof; // Freq: Hz deviation from f0; Rocof: Hz/s
  double Harmonic = 0; unsigned HarmonicOrder = 0;
};

struct Report { double T; std::complex<double> V[Phasors]; double F, R; };

std::vector<Report> Run(const Signal &s, double seconds) {
  C37118::Estimator est(unsigned(F0), unsigned(F0), C37118::Estimator::Decimation::Automatic);
  std::mt19937_64 rng(12345);
  std::normal_distribution<double> gauss(0, 1);
  std::vector<Report> out;
  const uint64_t count = uint64_t(seconds * 1e9 / (1e9 * Denominator / Numerator));
  for(uint64_t i = 0; i < count; ++i) {
    const uint64_t steps = i * Denominator;
    const uint64_t delta = (steps / Numerator) * 1000000000ULL + (steps % Numerator) * 1000000000ULL / Numerator;
    const double t = double(delta) / 1e9; // True sample time
    C37118::EstimatorSample x;
    const int64_t jitter = JitterNs > 0 ? int64_t(std::llround(JitterNs * gauss(rng))) : 0;
    x.TimestampNs = uint64_t(int64_t(Epoch + delta) + jitter);
    x.Sequence = i; x.Generation = 1; x.SampleRate = Numerator; x.SampleRateDenominator = Denominator;
    x.VoltageChannels = 3; x.CurrentChannels = 0; x.TimeSource = 1;
    const double base = 2 * Pi * F0 * t + s.Phase(t), m = s.Mag(t);
    for(unsigned c = 0; c < 3; ++c) {
      const double shift = -2 * Pi * c / 3;
      double v = std::sqrt(2.0) * m * std::cos(base + shift);
      if(s.HarmonicOrder) v += std::sqrt(2.0) * s.Harmonic * std::cos(s.HarmonicOrder * (base + shift));
      if(NoiseRms > 0) v += NoiseRms * gauss(rng);
      x.Values[c] = v;
    }
    C37118::Phasor p;
    if(!est.Push(x, p)) continue;
    // Report time in s since Epoch, subtracting in integers first (doubles would lose ~0.2 us here)
    Report r{double(int64_t(p.Timestamp) - int64_t(Epoch / 1000)) / 1e6, {}, p.Frequency, p.ROCOF};
    for(unsigned c = 0; c < 3; ++c) r.V[c] = p.Voltage[c];
    r.V[3] = (p.Voltage[0] + A3 * p.Voltage[1] + A3 * A3 * p.Voltage[2]) / 3.0;
    out.push_back(r);
  }
  return out;
}

// Phase k lags phase A by 2 pi k / 3; the positive sequence of the balanced set equals phase A.
double Shift(unsigned k) { return k < 3 ? -2 * Pi * k / 3 : 0; }
std::complex<double> Reference(double mag, double phase, unsigned k) { return std::polar(mag, phase + Shift(k)); }

struct Errors { double Tve[Phasors] = {}; double Fe = 0, Rfe = 0; };
void Merge(Errors &into, const Errors &e) {
  for(unsigned k = 0; k < Phasors; ++k) into.Tve[k] = std::max(into.Tve[k], e.Tve[k]);
  into.Fe = std::max(into.Fe, e.Fe); into.Rfe = std::max(into.Rfe, e.Rfe);
}
Errors Worst(const std::vector<Report> &r, const Signal &s, double from, double to) {
  Errors e;
  for(const auto &x : r) {
    if(x.T < from || x.T > to) continue;
    for(unsigned k = 0; k < Phasors; ++k) {
      const std::complex<double> ref = Reference(s.Mag(x.T), s.Phase(x.T), k);
      e.Tve[k] = std::max(e.Tve[k], std::abs(x.V[k] - ref) / std::abs(ref));
    }
    e.Fe = std::max(e.Fe, std::abs(x.F - (F0 + s.Freq(x.T))));
    e.Rfe = std::max(e.Rfe, std::abs(x.R - s.Rocof(x.T)));
  }
  return e;
}

int failures = 0;
void Line(const char *test, const Errors &e, double tve, double fe, double rfe) {
  const double phase = std::max({e.Tve[0], e.Tve[1], e.Tve[2]}), worst = std::max(phase, e.Tve[3]);
  const bool ok = worst <= tve && e.Fe <= fe && e.Rfe <= rfe;
  if(!ok) ++failures;
  std::printf("%-40s TVE %8.2e phase, %8.2e PS (%5.1f%% of %g%%)  FE %8.2e Hz (%5.1f%% of %g)  RFE %8.2e Hz/s (%5.1f%% of %g)  %s\n",
              test, phase, e.Tve[3], 100 * worst / tve, tve * 100, e.Fe, 100 * e.Fe / fe, fe, e.Rfe, 100 * e.Rfe / rfe, rfe,
              ok ? "PASS" : "FAIL");
}

Signal Steady(double df, double mag = 1) {
  return {[mag](double) { return mag; }, [df](double t) { return 2 * Pi * df * t; },
          [df](double) { return df; }, [](double) { return 0.0; }};
}

// Steps (Tables 8-9). One point of the interleaved waveform: its offset from the step and the report.
struct StepPoint { double Tau; Report R; };

// The first and last time a series exceeds a limit, with linear interpolation between neighbours
// (Annex C.4); 0 when it never does.
double ExceedSpan(const std::vector<StepPoint> &w, const std::function<double(const StepPoint &)> &value, double limit) {
  size_t first = w.size(), last = 0;
  for(size_t i = 0; i < w.size(); ++i)
    if(value(w[i]) > limit) { first = std::min(first, i); last = i; }
  if(first == w.size()) return 0;
  auto cross = [&](size_t a, size_t b) {
    const double va = value(w[a]), vb = value(w[b]);
    return w[a].Tau + (limit - va) / (vb - va) * (w[b].Tau - w[a].Tau);
  };
  const double t0 = first > 0 ? cross(first - 1, first) : w[first].Tau;
  const double t1 = last + 1 < w.size() ? cross(last, last + 1) : w[last].Tau;
  return t1 - t0;
}

struct StepResult { double Response = 0, Delay = 0, PreOver = 0, PreUnder = 0, PostOver = 0, PostUnder = 0; };

// 5.2.3-5.2.4 on one phasor's stepped parameter (magnitude or phase) of the interleaved waveform.
StepResult Evaluate(const std::vector<StepPoint> &w, unsigned k, bool magnitude, double kx, double ka, double epoch) {
  auto param = [&](const StepPoint &p) {
    return magnitude ? std::abs(p.R.V[k]) : std::arg(p.R.V[k] * std::polar(1.0, -Shift(k)));
  };
  double s1 = 0, s2 = 0; size_t n1 = 0, n2 = 0;
  for(const auto &p : w) {
    if(p.Tau < -epoch / 2 + 1) { s1 += param(p); ++n1; }
    if(p.Tau > epoch / 2 - 1) { s2 += param(p); ++n2; }
  }
  s1 /= n1; s2 /= n2;
  // Normalised to a positive-going step from 0 to 1: the state boundaries are +-0.005
  const double amp = s2 - s1, sign = amp > 0 ? 1 : -1;
  std::vector<double> y(w.size());
  for(size_t i = 0; i < w.size(); ++i) y[i] = sign * (param(w[i]) - s1) / std::abs(amp);
  StepResult r;
  size_t mid = 0;
  while(mid < y.size() && y[mid] < 0.5) ++mid;
  if(mid == 0 || mid == y.size()) { r.Delay = 1e9; return r; }
  r.Delay = std::abs(w[mid - 1].Tau + (0.5 - y[mid - 1]) / (y[mid] - y[mid - 1]) * (w[mid].Tau - w[mid - 1].Tau));
  size_t pre = mid;  // t_pre: the last crossing of the initial upper boundary before the transition
  while(pre > 0 && y[pre - 1] >= 0.005) --pre;
  size_t post = mid; // t_post: the first crossing of the final lower boundary after it
  while(post < y.size() && y[post] < 0.995) ++post;
  double preMax = 0, preMin = 0, postMax = 1, postMin = 1;
  for(size_t i = 0; i < pre; ++i) { preMax = std::max(preMax, y[i]); preMin = std::min(preMin, y[i]); }
  for(size_t i = post; i < y.size(); ++i) { postMax = std::max(postMax, y[i]); postMin = std::min(postMin, y[i]); }
  r.PreOver = preMax > 0.005 ? preMax : 0;
  r.PreUnder = preMin < -0.005 ? -preMin : 0;
  r.PostOver = postMax > 1.005 ? postMax - 1 : 0;
  r.PostUnder = postMin < 0.995 ? 1 - postMin : 0;
  r.Response = ExceedSpan(w, [&](const StepPoint &p) {
    const std::complex<double> ref = Reference(1 + (p.Tau >= 0 ? kx : 0), p.Tau >= 0 ? ka : 0, k);
    return std::abs(p.R.V[k] - ref) / std::abs(ref);
  }, 0.01);
  return r;
}
}

int main(int argc, char **argv) {
  if(argc > 1) F0 = std::atof(argv[1]);
  if(argc > 2 && std::string(argv[2]) != "none") NoiseRms = std::pow(10.0, -std::atof(argv[2]) / 20.0); // dB below RMS
  if(argc > 3) JitterNs = std::atof(argv[3]);
  const std::string csv = argc > 4 ? argv[4] : "";
  const double Fs = F0;
  std::printf("P class at f0 = %g Hz, Fs = %g frames/s; noise %s dB below RMS; time jitter %g ns RMS\n\n", F0, Fs,
              argc > 2 ? argv[2] : "none", JitterNs);
  const double settle = 0.5; // Skip the start-up (filter and window fill)

  // Steady state, Tables 2-3: f0 +-2 Hz; magnitude 80-120 %; one harmonic at 1 %.
  Errors f;
  for(double df = -2.0; df <= 2.0001; df += 0.25) {
    const Signal s = Steady(df);
    Merge(f, Worst(Run(s, 3), s, settle, 3));
  }
  Line("Frequency f0 +-2 Hz (0.25 Hz steps)", f, 0.01, 0.005, 0.4);
  Errors m;
  for(double mag : {0.8, 1.0, 1.2}) {
    const Signal s = Steady(0, mag);
    Errors e = Worst(Run(s, 2), s, settle, 2);
    e.Fe = e.Rfe = 0; // Table 2 limits TVE only
    Merge(m, e);
  }
  Line("Magnitude 80-120 %", m, 0.01, 1, 1e9);
  Errors h;
  for(unsigned k = 2; k <= 50; ++k) {
    Signal s = Steady(0); s.Harmonic = 0.01; s.HarmonicOrder = k;
    Merge(h, Worst(Run(s, 2), s, settle, 2));
  }
  Line("Harmonics 1 %, 2nd-50th", h, 0.01, 0.005, 0.4);

  // Modulation, Tables 4-5: kx = 0.1 or ka = 0.1 rad, fm 0.1 to min(Fs/10, 2) Hz.
  const double fr = std::min(Fs / 10, 2.0);
  Errors am, pm;
  for(double fm : {0.1, 0.2, 0.5, 1.0, 1.5, 2.0}) {
    if(fm > fr + 1e-9) continue;
    const double w = 2 * Pi * fm, dur = std::max(5.0, 2.0 / fm + 1);
    Signal a{[w](double t) { return 1 + 0.1 * std::cos(w * t); }, [](double) { return 0.0; },
             [](double) { return 0.0; }, [](double) { return 0.0; }};
    Signal p{[](double) { return 1.0; }, [w](double t) { return 0.1 * std::cos(w * t - Pi); },
             [w](double t) { return -0.1 * w / (2 * Pi) * std::sin(w * t - Pi); },
             [w](double t) { return -0.1 * w * w / (2 * Pi) * std::cos(w * t - Pi); }};
    Merge(am, Worst(Run(a, dur), a, settle, dur));
    Merge(pm, Worst(Run(p, dur), p, settle, dur));
  }
  Line("Amplitude modulation kx 0.1, 0.1-2 Hz", am, 0.03, 0.03 * fr, 0.18 * Pi * fr * fr);
  Line("Phase modulation ka 0.1 rad, 0.1-2 Hz", pm, 0.03, 0.03 * fr, 0.18 * Pi * fr * fr);

  // Frequency ramp, Tables 6-7: +-1 Hz/s over +-2 Hz; exclusion max(2/Fs, 2/f0) after the start.
  for(double rf : {1.0, -1.0}) {
    const double start = -2.0 * (rf > 0 ? 1 : -1), dur = 4.0 + 1.0;
    Signal r{[](double) { return 1.0; },
             [rf, start](double t) { return 2 * Pi * (start * t + 0.5 * rf * t * t); },
             [rf, start](double t) { return start + rf * t; }, [rf](double) { return rf; }};
    const double excl = std::max(2 / Fs, 2 / F0);
    Line(rf > 0 ? "Frequency ramp +1 Hz/s" : "Frequency ramp -1 Hz/s", Worst(Run(r, dur), r, settle + excl, 4.0 - excl),
         0.01, 0.01, 0.4);
  }

  // Steps, Tables 8-9: magnitude +-10 %, phase +-10 deg.
  const unsigned n = 20;                       // Annex C.4: n = 20 for low reporting rates
  const double responseLimit = 2 / F0, epoch = 2 + 4 * responseLimit;
  for(int kind = 0; kind < 4; ++kind) {
    const double kx = kind == 0 ? 0.1 : kind == 1 ? -0.1 : 0, ka = kind == 2 ? Pi / 18 : kind == 3 ? -Pi / 18 : 0;
    std::vector<StepPoint> w;
    for(unsigned off = 0; off < n; ++off) {
      const double ts = settle + epoch / 2 + 0.1 + off / (n * Fs); // The first at a reporting time
      Signal s{[kx, ts](double t) { return 1 + (t >= ts ? kx : 0); }, [ka, ts](double t) { return t >= ts ? ka : 0.0; },
               [](double) { return 0.0; }, [](double) { return 0.0; }};
      for(const auto &x : Run(s, ts + epoch / 2 + 2 / F0 + 0.05))
        if(std::abs(x.T - ts) <= epoch / 2) w.push_back({x.T - ts, x});
    }
    std::sort(w.begin(), w.end(), [](const StepPoint &a, const StepPoint &b) { return a.Tau < b.Tau; });
    StepResult worst;
    unsigned worstOver = 0;
    double psOver = 0;
    for(unsigned k = 0; k < Phasors; ++k) {
      const StepResult r = Evaluate(w, k, kx != 0, kx, ka, epoch);
      const double over = std::max({r.PreOver, r.PreUnder, r.PostOver, r.PostUnder});
      if(k == 3) psOver = over;
      if(over > std::max({worst.PreOver, worst.PreUnder, worst.PostOver, worst.PostUnder})) worstOver = k;
      worst.Response = std::max(worst.Response, r.Response); worst.Delay = std::max(worst.Delay, r.Delay);
      worst.PreOver = std::max(worst.PreOver, r.PreOver); worst.PreUnder = std::max(worst.PreUnder, r.PreUnder);
      worst.PostOver = std::max(worst.PostOver, r.PostOver); worst.PostUnder = std::max(worst.PostUnder, r.PostUnder);
    }
    const double feResp = ExceedSpan(w, [](const StepPoint &p) { return std::abs(p.R.F - F0); }, 0.005);
    const double rfeResp = ExceedSpan(w, [](const StepPoint &p) { return std::abs(p.R.R); }, 0.4);
    const double over = std::max({worst.PreOver, worst.PreUnder, worst.PostOver, worst.PostUnder});
    const bool ok = worst.Response <= responseLimit && worst.Delay <= 1 / (4 * Fs) && over <= 0.05 &&
                    feResp <= 4.5 / F0 && rfeResp <= 6 / F0;
    if(!ok) ++failures;
    const char *name[] = {"Step magnitude +10 %", "Step magnitude -10 %", "Step phase +10 deg", "Step phase -10 deg"};
    std::printf("%-40s response %.1f ms (<= %.1f), delay %.2f ms (<= %.1f), over/undershoot %.1f %% (%s; PS %.1f %%) "
                "[pre +%.1f/-%.1f, post +%.1f/-%.1f] (<= 5), FE resp %.1f ms (<= %.1f), RFE resp %.1f ms (<= %.1f)  %s\n",
                name[kind], worst.Response * 1e3, responseLimit * 1e3, worst.Delay * 1e3, 1 / (4 * Fs) * 1e3, over * 100,
                PhasorName[worstOver], psOver * 100, worst.PreOver * 100, worst.PreUnder * 100, worst.PostOver * 100,
                worst.PostUnder * 100, feResp * 1e3, 4.5 / F0 * 1e3, rfeResp * 1e3, 6 / F0 * 1e3, ok ? "PASS" : "FAIL");
    if(!csv.empty()) {
      std::FILE *out = std::fopen((csv + "/e5-step-" + std::to_string(kind) + ".csv").c_str(), "w");
      std::fprintf(out, "tau_ms,mag_a,phase_a_rad,mag_ps,phase_ps_rad,fe_hz,rocof_hzs\n");
      for(const auto &p : w)
        if(std::abs(p.Tau) <= 0.1)
          std::fprintf(out, "%.4f,%.8f,%.8f,%.8f,%.8f,%.6e,%.6e\n", p.Tau * 1e3, std::abs(p.R.V[0]), std::arg(p.R.V[0]),
                       std::abs(p.R.V[3]), std::arg(p.R.V[3]), p.R.F - F0, p.R.R);
      std::fclose(out);
    }
  }
  std::printf("\n%s: %d test groups outside the P-class limits\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
