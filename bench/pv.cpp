// SPDX-License-Identifier: MIT
// Phase-vocoder refinement: MPM finds the note, then each partial's frequency comes from the
// phase advance between two Hann-windowed frames one period apart (Brown & Puckette 1993 style
// phase-derivative estimate), combined with the same stiffness-aware fit as the strobe bank.
// Frame-based and memoryless apart from a median-of-5: the "classic" high-resolution tuner.
#include <complex>

#include "algos.hpp"

namespace bench {
namespace {

using cd = std::complex<double>;

/// Hann-windowed DFT of x[0..n) at frequency f (cycles per sample).
cd dftAt(const float* x, int n, double f) {
  const double pi = 3.141592653589793;
  const cd step = std::polar(1.0, -2.0 * pi * f), wstep = std::polar(1.0, 2.0 * pi / (n - 1));
  cd rot(1.0, 0.0), win(1.0, 0.0), acc(0.0, 0.0);
  for (int i = 0; i < n; ++i) {
    acc += static_cast<double>(x[i]) * (0.5 - 0.5 * win.real()) * rot;
    rot *= step;
    win *= wstep;
  }
  return acc;
}

class PhaseVocoder : public Algo {
 public:
  static constexpr int kCap = 2048;
  PhaseVocoder(std::string name, int partials, bool fitB) : name_(std::move(name)), partials_(partials) {
    opts_.fitInharmonicity = fitB;
    opts_.minPoints = 0;
  }
  void configure(float fs, float minHz) override {
    front_.configure(fs, 12000.0f, 64);
    range_ = rangeFor(front_.rate, minHz, 1400.0f, Limits::kMaxWindow);
    mpm_.configure(range_);
    med_.reset();
    est_ = {};
  }
  void process(const float* x, int n) override {
    for (int i = 0; i < n; ++i) front_.push(x[i], [this](bool loud) { hop(loud); });
  }
  Est estimate() const override { return est_; }
  std::string name() const override { return name_; }

 private:
  void hop(bool loud) {
    est_.valid = false;
    if (!loud || front_.hist.count() < range_.window) return med_.reset();
    const PeriodEstimate p = mpm_.analyze(front_.hist.latest(range_.window));
    if (!p.valid || p.clarity < 0.8f) return;
    const PartialFit f = refine(front_.rate / p.period);
    if (!f.valid) return;
    est_.valid = true;
    est_.hz = med_.push(f.f0);
  }

  PartialFit refine(double hz) const {
    const double rate = front_.rate;
    const int hopLen = std::max(1, static_cast<int>(std::lround(rate / hz)));
    const int n = std::min(4 * hopLen, std::min(front_.hist.count(), kCap) - hopLen);
    const float* newest = front_.hist.latest(n);
    const float* older = front_.hist.latest(n + hopLen);
    std::array<PartialMeasure, Limits::kMaxPartials> m{};
    const int kmax = std::min(partials_, std::min(Limits::kMaxPartials, static_cast<int>(0.45 * rate / hz)));
    for (int k = 1; k <= kmax; ++k) {
      const double f = k * hz / rate;
      const cd a = dftAt(older, n, f), b = dftAt(newest, n, f);
      const double expected = 2.0 * 3.141592653589793 * f * hopLen;
      const double dphi = std::remainder(std::arg(b) - std::arg(a) - expected, 2.0 * 3.141592653589793);
      m[k - 1].offsetHz = static_cast<float>(dphi * rate / (2.0 * 3.141592653589793 * hopLen));
      m[k - 1].power = static_cast<float>(std::norm(b));
      m[k - 1].information = m[k - 1].power;
      m[k - 1].points = 1;
    }
    return fitPartials(m.data(), kmax, hz, opts_);
  }

  std::string name_;
  int partials_;
  FitOptions opts_{};
  FrameFront<kCap> front_;
  PeriodRange range_{};
  Mpm mpm_;
  Median5 med_;
  Est est_{};
};

}  // namespace

std::unique_ptr<Algo> makePhaseVocoder(const std::string& name, int partials, bool fitB) {
  return std::make_unique<PhaseVocoder>(name, partials, fitB);
}

}  // namespace bench
