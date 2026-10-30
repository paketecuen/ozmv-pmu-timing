// E3: the same 40 s of real mains samples (WMU recording of release 17, channel 0) timed six ways,
// each run through the production synchrophasor estimator (decimated Taylor-Fourier, 50 frames/s).
// Reference: our per-sample GPS time (the WMU time track, 80-81 ns k = 2). For every other dating
// strategy: its time error per sample, and the error of each report against the reference report
// carrying the same UTC label (TVE, FE, RFE), plus the reports it loses.
//
// Build (from the paper folder):
//   c++ -std=c++17 -O2 -I algorithms/estimator -o analysis/e3 analysis/e3_dating_strategies.cpp
// Run: analysis/e3 <wmu.raw: s32le, 7 interleaved channels> <r17-wmu-time.csv> <output folder>
#include "C37118Estimator.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr uint32_t Numerator = 1000000000U, Denominator = 31744U; // Declared PB2 rate: 31,502.016 S/s
constexpr double NominalPeriodNs = 31744.0;
constexpr unsigned Channels = 7;

struct Frame { std::complex<double> V; double F = 0, R = 0; };

std::vector<double> ReadSamples(const std::string &path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  const std::streamsize bytes = in.tellg();
  in.seekg(0);
  std::vector<int32_t> raw(size_t(bytes) / 4);
  in.read(reinterpret_cast<char *>(raw.data()), bytes);
  std::vector<double> v(raw.size() / Channels);
  for(size_t n = 0; n < v.size(); ++n) v[n] = double(raw[n * Channels]) / 2147483648.0; // Full scale = 1
  return v;
}

// The WMU time track: one record per 256-sample frame, first and last sample's UTC; linear inside.
std::vector<uint64_t> ReadTimes(const std::string &path, size_t count) {
  std::ifstream in(path);
  std::string line;
  std::getline(in, line); // header
  std::vector<uint64_t> t(count, 0);
  while(std::getline(in, line)) {
    std::stringstream ss(line);
    std::string f[7];
    for(auto &x : f) std::getline(ss, x, ',');
    const uint64_t first = std::stoull(f[0]), n = std::stoull(f[1]), t0 = std::stoull(f[3]), t1 = std::stoull(f[4]);
    for(uint64_t i = 0; i < n && first + i < count; ++i)
      t[first + i] = t0 + uint64_t(std::llround(double(t1 - t0) * double(i) / double(n - 1)));
  }
  return t;
}

std::map<uint64_t, Frame> Estimate(const std::vector<double> &v, const std::vector<uint64_t> &t, uint64_t &restarts) {
  C37118::Estimator estimator(50, 50, C37118::Estimator::Decimation::Automatic);
  std::map<uint64_t, Frame> frames;
  for(size_t n = 0; n < v.size(); ++n) {
    C37118::EstimatorSample s;
    s.TimestampNs = t[n];
    s.Sequence = n;
    s.Generation = 1;
    s.SampleRate = Numerator;
    s.SampleRateDenominator = Denominator;
    s.VoltageChannels = 3;
    s.CurrentChannels = 0;
    s.TimeSource = 1;
    s.Values[0] = s.Values[1] = s.Values[2] = v[n];
    C37118::Phasor p;
    if(estimator.Push(s, p)) frames[p.Timestamp] = Frame{p.Voltage[0], p.Frequency, p.ROCOF};
  }
  restarts = estimator.GetStats().Resets;
  return frames;
}

struct Strategy { std::string Name; std::vector<uint64_t> T; };
}

int main(int argc, char **argv) {
  if(argc < 4) { std::fprintf(stderr, "usage: e3 wmu.raw time.csv outdir\n"); return 2; }
  const std::vector<double> v = ReadSamples(argv[1]);
  const std::vector<uint64_t> truth = ReadTimes(argv[2], v.size());
  const std::string out = argv[3];
  const size_t N = v.size();

  std::vector<Strategy> s;
  s.push_back({"ours: per-sample GPS time", truth});
  { // Singh & Parida 2022: the first sample's GPS time, then the nominal period
    Strategy x{"first sample + nominal period", std::vector<uint64_t>(N)};
    for(size_t n = 0; n < N; ++n) x.T[n] = truth[0] + uint64_t(std::llround(n * NominalPeriodNs));
    s.push_back(x);
  }
  { // SMU (Guarnieri 2022), and Lythgoe 2025 without its correction: re-aligned at each PPS, nominal period within the second
    Strategy x{"PPS re-alignment + nominal period", std::vector<uint64_t>(N)};
    size_t k0 = 0;
    for(size_t n = 0; n < N; ++n) {
      if(n == 0 || truth[n] / 1000000000ULL != truth[n - 1] / 1000000000ULL) k0 = n;
      x.T[n] = truth[k0] + uint64_t(std::llround(double(n - k0) * NominalPeriodNs));
    }
    s.push_back(x);
  }
  { // Lythgoe 2025's correction: re-aligned at each PPS, with the period counted over the previous second
    Strategy x{"PPS re-alignment + period of the previous second", std::vector<uint64_t>(N)};
    size_t k0 = 0, prev = 0; double period = NominalPeriodNs; bool have = false;
    for(size_t n = 0; n < N; ++n) {
      if(n == 0 || truth[n] / 1000000000ULL != truth[n - 1] / 1000000000ULL) {
        if(have) period = double(truth[n] - truth[prev]) / double(n - prev);
        if(n) have = true;
        prev = k0 = n;
      }
      x.T[n] = truth[k0] + uint64_t(std::llround(double(n - k0) * period));
    }
    s.push_back(x);
  }
  { // Our own ablation: the PRU's polled DRDY tick, without the lower-envelope fit (lateness uniform 0-1.52 us)
    Strategy x{"polled DRDY tick, no envelope fit", std::vector<uint64_t>(N)};
    uint64_t state = 0x9E3779B97F4A7C15ULL;
    for(size_t n = 0; n < N; ++n) {
      state ^= state << 13; state ^= state >> 7; state ^= state << 17;
      x.T[n] = truth[n] + (state % 1520ULL);
    }
    s.push_back(x);
  }
  { // Microsecond labels (a us-resolution time base)
    Strategy x{"microsecond labels", std::vector<uint64_t>(N)};
    for(size_t n = 0; n < N; ++n) x.T[n] = truth[n] / 1000ULL * 1000ULL;
    s.push_back(x);
  }

  // Each strategy's signed time error, every 63rd sample (2 ms), for the figures
  std::FILE *errors = std::fopen((out + "/e3-time-errors.csv").c_str(), "w");
  std::fprintf(errors, "t_s");
  for(size_t k = 1; k < s.size(); ++k) std::fprintf(errors, ",err_ns_%zu", k);
  std::fprintf(errors, "\n");
  for(size_t n = 0; n < N; n += 63) {
    std::fprintf(errors, "%.6f", double(truth[n] - truth[0]) / 1e9);
    for(size_t k = 1; k < s.size(); ++k) std::fprintf(errors, ",%lld", (long long)int64_t(s[k].T[n] - truth[n]));
    std::fprintf(errors, "\n");
  }
  std::fclose(errors);

  uint64_t r0 = 0;
  const auto ref = Estimate(v, truth, r0);
  std::FILE *summary = std::fopen((out + "/e3-summary.csv").c_str(), "w");
  std::fprintf(summary, "strategy,time_err_rms_ns,time_err_max_ns,reports,reports_lost,restarts,tve_rms,tve_max,fe_rms_hz,fe_max_hz,rfe_rms_hzs,rfe_max_hzs\n");
  std::printf("%-48s %10s %10s %7s %6s %4s %9s %9s %9s %9s %8s %8s\n", "strategy", "dt RMS ns", "dt max ns", "reports",
              "lost", "rst", "TVE RMS", "TVE max", "FE RMS", "FE max", "RFE RMS", "RFE max");
  for(size_t k = 0; k < s.size(); ++k) {
    double e2 = 0, emax = 0;
    for(size_t n = 0; n < N; ++n) {
      const double e = double(int64_t(s[k].T[n] - truth[n]));
      e2 += e * e; emax = std::max(emax, std::abs(e));
    }
    uint64_t restarts = 0;
    const auto fr = k == 0 ? ref : Estimate(v, s[k].T, restarts);
    if(k == 0) restarts = r0;
    double t2 = 0, tmax = 0, f2 = 0, fmax = 0, q2 = 0, qmax = 0; size_t m = 0, lost = 0;
    std::FILE *series = std::fopen((out + "/e3-frames-" + std::to_string(k) + ".csv").c_str(), "w");
    std::fprintf(series, "utc_us,tve,fe_hz,rfe_hzs\n");
    for(const auto &r : ref) {
      const auto it = fr.find(r.first);
      if(it == fr.end()) { ++lost; continue; }
      const double tve = std::abs(it->second.V - r.second.V) / std::abs(r.second.V);
      const double fe = std::abs(it->second.F - r.second.F), rfe = std::abs(it->second.R - r.second.R);
      t2 += tve * tve; tmax = std::max(tmax, tve); f2 += fe * fe; fmax = std::max(fmax, fe); q2 += rfe * rfe; qmax = std::max(qmax, rfe);
      ++m;
      std::fprintf(series, "%llu,%.6e,%.6e,%.6e\n", (unsigned long long)r.first, tve, fe, rfe);
    }
    std::fclose(series);
    const double rms = m ? 1.0 / m : 0;
    std::printf("%-48s %10.1f %10.1f %7zu %6zu %4llu %9.2e %9.2e %9.2e %9.2e %8.4f %8.4f\n", s[k].Name.c_str(),
                std::sqrt(e2 / N), emax, m, lost, (unsigned long long)restarts, std::sqrt(t2 * rms), tmax,
                std::sqrt(f2 * rms), fmax, std::sqrt(q2 * rms), qmax);
    std::fprintf(summary, "\"%s\",%.3f,%.3f,%zu,%zu,%llu,%.6e,%.6e,%.6e,%.6e,%.6e,%.6e\n", s[k].Name.c_str(),
                 std::sqrt(e2 / N), emax, m, lost, (unsigned long long)restarts, std::sqrt(t2 * rms), tmax,
                 std::sqrt(f2 * rms), fmax, std::sqrt(q2 * rms), qmax);
  }
  std::fclose(summary);
  return 0;
}
