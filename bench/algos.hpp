// SPDX-License-Identifier: MIT
// The algorithms under test, behind one interface. All share the same front end (decimate to
// ~12 kHz, 64-sample hop = 5.3 ms) unless a full-rate variant is asked for, so differences are
// the estimators, not the plumbing.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>

#include <squatch/tuner/tuner.hpp>

namespace bench {
using namespace squatch::tuner;

struct Est {
  bool valid = false;
  float hz = 0.0f;
};

class Algo {
 public:
  virtual ~Algo() = default;
  virtual void configure(float fs, float minHz) = 0;
  virtual void process(const float* x, int n) = 0;
  virtual Est estimate() const = 0;
  virtual std::string name() const = 0;
};

/// Median of the last five valid values (what most tuners do to hide outliers).
class Median5 {
 public:
  void reset() { n_ = 0; }
  float push(float v) {
    buf_[static_cast<size_t>(n_ % 5)] = v;
    ++n_;
    std::array<float, 5> s = buf_;
    const int m = n_ < 5 ? n_ : 5;
    std::sort(s.begin(), s.begin() + m);
    return s[static_cast<size_t>(m / 2)];
  }

 private:
  std::array<float, 5> buf_{};
  int n_ = 0;
};

/// Front end shared by the frame-based algorithms: decimator, history, level gate, hop clock.
template <int Cap>
class FrameFront {
 public:
  void configure(float fs, float workRate, int hop) {
    dec.configure(static_cast<int>(fs / workRate + 0.5f));
    rate = fs / static_cast<float>(dec.factor());
    hop_ = hop;
    hist.clear();
    fill_ = 0;
    energy_ = 0.0f;
  }
  /// Push one input sample; true when a hop completed and the level is above the gate.
  template <typename F>
  void push(float x, F&& onHop) {
    float y;
    if (!dec.push(x, y)) return;
    hist.push(y);
    energy_ += y * y;
    if (++fill_ < hop_) return;
    const float level = std::sqrt(energy_ / static_cast<float>(hop_));
    fill_ = 0;
    energy_ = 0.0f;
    onHop(level >= TunerConfig{}.gate);  // same gate as the Squatch tuner
  }
  Decimator dec;
  History<Cap> hist;
  float rate = 12000.0f;

 private:
  int hop_ = 64, fill_ = 0;
  float energy_ = 0.0f;
};

inline PeriodRange rangeFor(float rate, float minHz, float maxHz, int cap) {
  PeriodRange r;
  r.maxLag = std::min(static_cast<int>(rate / minHz) + 1, cap / 2 - 3);
  r.minLag = std::max(2, static_cast<int>(rate / maxHz));
  r.window = 2 * r.maxLag + 4;
  return r;
}

/// YIN or MPM alone: period from the detector, optionally median-of-5 smoothed.
template <typename Detector, int Cap>
class CoarseAlgo : public Algo {
 public:
  CoarseAlgo(std::string name, float workRate, bool smooth, float clarity)
      : name_(std::move(name)), work_(workRate), clarity_(clarity), smooth_(smooth) {}
  void configure(float fs, float minHz) override {
    front_.configure(fs, work_, 64);
    front_.configure(fs, work_, 64 * std::max(1, static_cast<int>(std::lround(front_.rate / 12000.0f))));
    range_ = rangeFor(front_.rate, minHz, 1400.0f, Cap);
    det_.configure(range_);
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
    if (!loud || front_.hist.count() < range_.window) {
      est_.valid = false;
      med_.reset();
      return;
    }
    const PeriodEstimate p = det_.analyze(front_.hist.latest(range_.window));
    est_.valid = p.valid && p.clarity >= clarity_;
    if (!est_.valid) return;
    const float hz = front_.rate / p.period;
    est_.hz = smooth_ ? med_.push(hz) : hz;
  }

  std::string name_;
  float work_, clarity_;
  bool smooth_;
  FrameFront<Cap> front_;
  PeriodRange range_{};
  Detector det_;
  Median5 med_;
  Est est_{};
};

/// The Squatch tuner itself.
class SquatchAlgo : public Algo {
 public:
  SquatchAlgo(std::string name, TunerConfig base, bool steady = false)
      : name_(std::move(name)), cfg_(base), steady_(steady) {}
  void configure(float fs, float minHz) override {
    cfg_.sampleRate = fs;
    cfg_.minHz = minHz;
    tuner_.configure(cfg_);
  }
  void process(const float* x, int n) override { tuner_.process(x, n); }
  Est estimate() const override {
    const TunerReading& r = tuner_.reading();
    return {r.valid, steady_ ? r.steadyHz : r.hz};
  }
  std::string name() const override { return name_; }
  const Tuner& tuner() const { return tuner_; }

 private:
  std::string name_;
  TunerConfig cfg_;
  bool steady_;
  Tuner tuner_;
};

std::unique_ptr<Algo> makePhaseVocoder(const std::string& name, int partials, bool fitB);

}  // namespace bench
