// SPDX-License-Identifier: MIT
// Synthetic plucked-string tones with known truth, for measuring tuners.
//
// Model (all in double precision):
//  - partial k at k * f0 * sqrt(1 + B k^2) (stiff string, Fletcher & Rossing),
//  - amplitude from pluck and pickup position: sin(k pi p) sin(k pi q) / k (a velocity pickup
//    over a plucked string), with an optional extra gain on the fundamental,
//  - per-partial exponential decay, higher partials faster,
//  - pitch glide from tension modulation: +g cents * (envelope of partial 1)^2, so the note
//    starts sharp and settles as it decays (Tolonen, Valimaki & Karjalainen 2000),
//  - a pick-noise burst, white noise at a set SNR, optional 60 Hz mains hum with its 3rd.
// Truth is f0: the fundamental once the glide has died away.
#pragma once
#include <cmath>
#include <random>
#include <vector>

namespace bench {

struct ToneSpec {
  double f0 = 82.4069;
  double inharmonicity = 0.0;  // B
  int partials = 12;
  double pluckPos = 0.2, pickupPos = 0.2;
  double fundamentalGainDb = 0.0;
  double t60 = 4.0;            // seconds, partial 1
  double t60Slope = 0.25;      // partial k decays (1 + slope (k-1)) times faster
  double glideCents = 0.0;
  double snrDb = 60.0;         // against the initial RMS of the tone
  double humDb = -200.0;       // hum level against the initial RMS
  double pickNoiseDb = -200.0; // 5 ms burst at the onset
  double peak = 0.3;
  double preroll = 0.1;        // seconds of noise before the pluck
  double seconds = 2.0;
};

inline double partialFreq(const ToneSpec& s, int k) { return k * s.f0 * std::sqrt(1.0 + s.inharmonicity * k * k); }

inline double partialAmp(const ToneSpec& s, int k) {
  const double pi = 3.141592653589793;
  double a = std::sin(k * pi * s.pluckPos) * std::sin(k * pi * s.pickupPos) / k;
  if (k == 1) a *= std::pow(10.0, s.fundamentalGainDb / 20.0);
  return std::fabs(a);
}

inline std::vector<float> synthesize(const ToneSpec& s, double rate, std::mt19937& rng) {
  const double pi = 3.141592653589793;
  const size_t pre = static_cast<size_t>(s.preroll * rate), n = pre + static_cast<size_t>(s.seconds * rate);
  std::vector<double> y(n, 0.0);
  std::uniform_real_distribution<double> uni(0.0, 2.0 * pi);
  double norm = 0.0;
  for (int k = 1; k <= s.partials; ++k) norm += partialAmp(s, k);
  const double decay1 = std::log(1000.0) / s.t60;  // amplitude rate of partial 1 (1/s)
  for (int k = 1; k <= s.partials && partialFreq(s, k) < 0.45 * rate; ++k) {
    const double amp = s.peak * partialAmp(s, k) / norm, fk = partialFreq(s, k);
    const double dk = decay1 * (1.0 + s.t60Slope * (k - 1));
    double ph = uni(rng);
    for (size_t i = pre; i < n; ++i) {
      const double t = static_cast<double>(i - pre) / rate;
      const double env1 = std::exp(-decay1 * t);
      y[i] += amp * std::exp(-dk * t) * std::sin(ph);
      ph += 2.0 * pi * fk * std::exp2(s.glideCents * env1 * env1 / 1200.0) / rate;
    }
  }
  double rms = 0.0;
  const size_t r1 = std::min(n, pre + static_cast<size_t>(0.1 * rate));
  for (size_t i = pre; i < r1; ++i) rms += y[i] * y[i];
  rms = std::sqrt(rms / static_cast<double>(r1 - pre));
  std::normal_distribution<double> g(0.0, 1.0);
  const double noise = rms * std::pow(10.0, -s.snrDb / 20.0), hum = rms * std::pow(10.0, s.humDb / 20.0);
  const double pick = rms * std::pow(10.0, s.pickNoiseDb / 20.0);
  std::vector<float> out(n);
  for (size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / rate;
    double v = y[i] + noise * g(rng) + hum * (std::sin(2 * pi * 60 * t) + 0.5 * std::sin(2 * pi * 180 * t));
    if (i >= pre && i < pre + static_cast<size_t>(0.005 * rate)) v += pick * g(rng);
    out[i] = static_cast<float>(v);
  }
  return out;
}

/// The true fundamental at time t after the pluck, glide included.
inline double truthAt(const ToneSpec& s, double t) {
  if (t < 0.0) t = 0.0;
  const double env1 = std::exp(-std::log(1000.0) / s.t60 * t);
  return s.f0 * std::exp2(s.glideCents * env1 * env1 / 1200.0);
}

/// Band-limited resampling by `ratio` (> 1 raises pitch): windowed-sinc interpolation.
inline std::vector<float> resample(const std::vector<float>& x, double ratio) {
  const double pi = 3.141592653589793;
  const int half = 48;
  const double cut = ratio > 1.0 ? 0.95 / ratio : 0.95;
  std::vector<float> y(static_cast<size_t>(static_cast<double>(x.size()) / ratio));
  for (size_t i = 0; i < y.size(); ++i) {
    const double pos = static_cast<double>(i) * ratio;
    const long c = static_cast<long>(std::floor(pos));
    double acc = 0.0;
    for (long j = c - half + 1; j <= c + half; ++j) {
      if (j < 0 || j >= static_cast<long>(x.size())) continue;
      const double t = pos - static_cast<double>(j);
      const double sinc = std::fabs(t) < 1e-12 ? cut : std::sin(pi * cut * t) / (pi * t);
      const double w = 0.42 + 0.5 * std::cos(pi * t / half) + 0.08 * std::cos(2 * pi * t / half);
      acc += x[static_cast<size_t>(j)] * sinc * w;
    }
    y[i] = static_cast<float>(acc);
  }
  return y;
}

}  // namespace bench
