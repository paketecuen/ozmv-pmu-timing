// E7: the acquisition's noise floor on real data (the release 17 WMU recording, 40 s, 7 channels).
// Channel 0 carries the mains; the others are unconnected inputs. For channel 0: the fundamental's
// RMS and the power left after removing DC and every harmonic of the measured fundamental (+-1.5 Hz,
// up to Nyquist), from Blackman-Harris spectra of 2^18 samples (8.3 s). That residual bounds the
// acquisition noise from above: it also holds the grid's own interharmonics and supraharmonics.
// It is reported over the full band and inside the PMU's band (decimation filter: < 1 kHz), the
// latter as the equivalent full-band white noise the E5 simulation uses. For the other channels: RMS.
//
// Build: c++ -std=c++17 -O2 -o analysis/e7 analysis/e7_channel_snr.cpp
// Run:   analysis/e7 <wmu.raw: s32le, 7 interleaved channels>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <vector>

namespace {
constexpr double Pi = 3.14159265358979323846, Rate = 1e9 / 31744.0;
constexpr unsigned Channels = 7, N = 1u << 18;

void Fft(std::vector<std::complex<double>> &a) {
  const size_t n = a.size();
  for(size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for(; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if(i < j) std::swap(a[i], a[j]);
  }
  for(size_t len = 2; len <= n; len <<= 1) {
    const std::complex<double> w = std::polar(1.0, -2 * Pi / double(len));
    for(size_t i = 0; i < n; i += len) {
      std::complex<double> v(1, 0);
      for(size_t k = 0; k < len / 2; ++k, v *= w) {
        const std::complex<double> x = a[i + k], y = a[i + k + len / 2] * v;
        a[i + k] = x + y; a[i + k + len / 2] = x - y;
      }
    }
  }
}
}

int main(int argc, char **argv) {
  if(argc < 2) { std::fprintf(stderr, "usage: e7 wmu.raw\n"); return 2; }
  std::ifstream in(argv[1], std::ios::binary | std::ios::ate);
  const std::streamsize bytes = in.tellg();
  in.seekg(0);
  std::vector<int32_t> raw(size_t(bytes) / 4);
  in.read(reinterpret_cast<char *>(raw.data()), bytes);
  const size_t samples = raw.size() / Channels;
  std::printf("%zu samples per channel (%.2f s)\n", samples, samples / Rate);
  for(unsigned c = 1; c < Channels; ++c) {
    double mean = 0, var = 0;
    for(size_t n = 0; n < samples; ++n) mean += raw[n * Channels + c];
    mean /= samples;
    for(size_t n = 0; n < samples; ++n) { const double d = raw[n * Channels + c] - mean; var += d * d; }
    std::printf("channel %u (unconnected): RMS %.3e of full scale (%.1f dBFS)\n", c, std::sqrt(var / samples) / 2147483648.0,
                20 * std::log10(std::sqrt(var / samples) / 2147483648.0 * std::sqrt(2.0)));
  }
  // Blackman-Harris (4 terms, -92 dB sidelobes)
  std::vector<double> w(N);
  for(unsigned n = 0; n < N; ++n) {
    const double x = 2 * Pi * n / N;
    w[n] = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
  }
  const double df = Rate / N;
  auto spectrum = [&](size_t start, unsigned channel) {
    std::vector<std::complex<double>> a(N);
    for(unsigned n = 0; n < N; ++n) a[n] = w[n] * double(raw[(start + n) * Channels + channel]) / 2147483648.0;
    Fft(a);
    std::vector<double> p(N / 2 + 1);
    for(unsigned k = 0; k <= N / 2; ++k) p[k] = std::norm(a[k]);
    return p;
  };
  for(size_t start = 0; start + N <= samples; start += N) {
    const std::vector<double> p = spectrum(start, 0);
    // The fundamental: the peak between 45 and 55 Hz, refined by the centroid of its main lobe
    unsigned peak = unsigned(45 / df);
    for(unsigned k = peak; k < unsigned(55 / df); ++k) if(p[k] > p[peak]) peak = k;
    double s = 0, sk = 0;
    for(unsigned k = peak - 6; k <= peak + 6; ++k) { s += p[k]; sk += p[k] * k; }
    const double f1 = sk / s * df, fund = s;
    if(start == 0) {
      double wsum = 0; for(unsigned n = 0; n < N; ++n) wsum += w[n] * w[n];
      std::printf("channel 0 fundamental: %.1f dBFS (RMS against a full-scale sine)\n", 10 * std::log10(2 * fund / (wsum * N) / 0.5));
    }
    auto harmonic = [&](unsigned k) {
      const double f = k * df, h = std::round(f / f1);
      return f < 1.5 || std::abs(f - h * f1) < 1.5;
    };
    double floorAll = 0, floorBand = 0; unsigned binsAll = 0, binsBand = 0, total = 0, totalBand = 0;
    for(unsigned k = 1; k <= N / 2; ++k) {
      const double f = k * df;
      ++total; if(f < 1000) ++totalBand;
      if(harmonic(k)) continue;
      floorAll += p[k]; ++binsAll;
      if(f < 1000) { floorBand += p[k]; ++binsBand; }
    }
    // Scale the kept bins to the whole band (the excluded ~6 % is assumed to hold the same floor)
    const double noiseAll = floorAll * total / binsAll, noiseBandAsWhite = floorBand / binsBand * total;
    double harm = 0;
    for(unsigned h = 2; h * f1 < Rate / 2 - 2; ++h) {
      const unsigned c = unsigned(std::round(h * f1 / df));
      for(unsigned k = c - 6; k <= c + 6; ++k) harm += p[k];
    }
    std::printf("segment at %5.1f s: f1 %.4f Hz, THD %.2f %%, residual (noise + interharmonics) %.1f dB below the fundamental"
                " over 0-%.0f Hz; inside 0-1 kHz it equals white noise %.1f dB below\n", start / Rate, f1,
                100 * std::sqrt(harm / fund), 10 * std::log10(fund / noiseAll), Rate / 2, 10 * std::log10(fund / noiseBandAsWhite));
    // The unconnected channels' floors, against channel 0's fundamental (the same ADC and range)
    for(unsigned c = 1; c <= 2; ++c) {
      const std::vector<double> q = spectrum(start, c);
      double all = 0, band = 0; unsigned nAll = 0, nBand = 0;
      for(unsigned k = 1; k <= N / 2; ++k) {
        if(harmonic(k)) continue;
        all += q[k]; ++nAll;
        if(k * df < 1000) { band += q[k]; ++nBand; }
      }
      std::printf("  channel %u floor: %.1f dB below channel 0's fundamental over the full band; inside 0-1 kHz, white noise %.1f dB below\n", c,
                  10 * std::log10(fund / (all * total / nAll)), 10 * std::log10(fund / (band / nBand * total)));
    }
  }
  return 0;
}
