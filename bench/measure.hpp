// SPDX-License-Identifier: MIT
// Running an algorithm over a signal, and the numbers we report.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "algos.hpp"

namespace bench {

/// One estimate per block of input (5.3 ms at 48 kHz), time measured from the pluck.
struct Trace {
  std::vector<double> t;
  std::vector<Est> e;
};

inline Trace runTrace(Algo& a, const std::vector<float>& x, float fs, double onset) {
  const int block = static_cast<int>(std::lround(fs * 64.0 / 12000.0));
  Trace tr;
  for (size_t i = 0; i + static_cast<size_t>(block) <= x.size(); i += static_cast<size_t>(block)) {
    a.process(&x[i], block);
    tr.t.push_back(static_cast<double>(i + static_cast<size_t>(block)) / fs - onset);
    tr.e.push_back(a.estimate());
  }
  return tr;
}

/// The part of a whole-recording trace from `onset - 0.1` to `onset + length`, re-timed from onset.
inline Trace slice(const Trace& whole, double onset, double length) {
  Trace t;
  for (size_t i = 0; i < whole.t.size(); ++i) {
    if (whole.t[i] < onset - 0.1 || whole.t[i] > onset + length) continue;
    t.t.push_back(whole.t[i] - onset);
    t.e.push_back(whole.e[i]);
  }
  return t;
}

inline double centsErr(const Est& e, double truth) { return 1200.0 * std::log2(e.hz / truth); }

/// Truth as a function of time after the pluck (a constant for real recordings).
using Truth = std::function<double(double)>;

/// First time with a valid estimate within `tol` cents after which no valid estimate up to
/// `until` leaves that band (gaps are allowed; coverage is reported separately). A tuner that
/// shows a wrong reading after "locking" has not locked. Negative if it never happens.
inline double lockTime(const Trace& tr, double truth, double tol, double until) {
  double lock = -1.0;
  for (size_t i = 0; i < tr.t.size() && tr.t[i] <= until; ++i) {
    if (tr.t[i] < 0.0 || !tr.e[i].valid) continue;
    if (std::fabs(centsErr(tr.e[i], truth)) > tol) lock = -1.0;
    else if (lock < 0.0) lock = tr.t[i];
  }
  return lock;
}

struct WindowStats {
  double mean = 0.0, sd = 0.0, coverage = 0.0;
  int n = 0;
};

/// Error statistics of the valid estimates in [a, b] seconds after the pluck.
inline WindowStats windowStats(const Trace& tr, const Truth& truth, double a, double b) {
  WindowStats s;
  int slots = 0;
  double sum = 0.0, sum2 = 0.0;
  for (size_t i = 0; i < tr.t.size(); ++i) {
    if (tr.t[i] < a || tr.t[i] > b) continue;
    ++slots;
    if (!tr.e[i].valid) continue;
    const double c = centsErr(tr.e[i], truth(tr.t[i]));
    sum += c;
    sum2 += c * c;
    ++s.n;
  }
  if (s.n == 0) return s;
  s.mean = sum / s.n;
  s.sd = std::sqrt(std::max(0.0, sum2 / s.n - s.mean * s.mean));
  s.coverage = static_cast<double>(s.n) / std::max(1, slots);
  return s;
}

/// Fraction of valid estimates after `from` that are more than 50 cents out (octave slips etc).
inline double grossRate(const Trace& tr, double truth, double from) {
  int n = 0, bad = 0;
  for (size_t i = 0; i < tr.t.size(); ++i) {
    if (tr.t[i] < from || !tr.e[i].valid) continue;
    ++n;
    bad += std::fabs(centsErr(tr.e[i], truth)) > 50.0 ? 1 : 0;
  }
  return n ? static_cast<double>(bad) / n : 0.0;
}

inline double quantile(std::vector<double> v, double q) {
  if (v.empty()) return NAN;
  std::sort(v.begin(), v.end());
  const double pos = q * static_cast<double>(v.size() - 1);
  const size_t i = static_cast<size_t>(pos);
  const double f = pos - static_cast<double>(i);
  return i + 1 < v.size() ? v[i] * (1 - f) + v[i + 1] * f : v[i];
}

/// Aggregate over trials: steady-state error of the per-trial means, jitter, lock times.
struct Summary {
  std::vector<double> steadyMean, jitter, lock1, lock01, gross, coverage;
  int never1 = 0, never01 = 0;

  /// Accuracy is scored against the instantaneous truth; lock time against the settled pitch.
  void add(const Trace& tr, const Truth& truth, double settled, double steadyFrom, double until) {
    const WindowStats w = windowStats(tr, truth, steadyFrom, until);
    if (w.n > 0) {
      steadyMean.push_back(w.mean);
      jitter.push_back(w.sd);
    }
    coverage.push_back(w.coverage);
    gross.push_back(grossRate(tr, settled, 0.1));
    const double l1 = lockTime(tr, settled, 1.0, until), l01 = lockTime(tr, settled, 0.1, until);
    if (l1 < 0) ++never1; else lock1.push_back(l1);
    if (l01 < 0) ++never01; else lock01.push_back(l01);
  }

  int trials() const { return static_cast<int>(coverage.size()); }

  double bias() const {
    double s = 0;
    for (double v : steadyMean) s += v;
    return steadyMean.empty() ? NAN : s / static_cast<double>(steadyMean.size());
  }

  double rms() const {
    double s = 0;
    for (double v : steadyMean) s += v * v;
    return steadyMean.empty() ? NAN : std::sqrt(s / static_cast<double>(steadyMean.size()));
  }

  double p95abs() const {
    std::vector<double> a;
    for (double v : steadyMean) a.push_back(std::fabs(v));
    return quantile(a, 0.95);
  }

  double mean(const std::vector<double>& v) const {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? NAN : s / static_cast<double>(v.size());
  }
};

}  // namespace bench
