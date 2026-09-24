// SPDX-License-Identifier: MIT
// Combine per-partial frequency measurements into one fundamental.
//
// A real string is stiff, so its partials are stretched: f_k = k f0 sqrt(1 + B k^2)
// (Fletcher & Rossing). With B = 1e-4, partial 4 is already 1.4 cents sharp of 4 * f0, so a
// tuner that averages harmonics without a stiffness term reads sharp. Using
// f_k / k ~= f0 + c k^2 (c = f0 B / 2), a two-parameter weighted least-squares fit returns the
// fundamental the string actually has, plus B. Weights are k^2 * information: dividing a partial
// by k divides its error by k, which is why upper partials are worth tracking at all.
#pragma once
#include <algorithm>
#include <cmath>

#include "strobe.hpp"

namespace squatch::tuner {

struct PartialFit {
  float f0 = 0.0f;
  float sigmaHz = 0.0f;        // standard error of f0 (optimistic: ignores correlated errors)
  float inharmonicity = 0.0f;  // B
  int used = 0;                // partials that passed the power gate
  bool valid = false;
};

struct FitOptions {
  bool fitInharmonicity = true;
  float minRelativePower = 1e-5f;  // ignore partials 50 dB below the strongest (weights do the rest)
  int minPoints = 8;               // phase samples (4 per period) a partial needs before it counts
  float maxInharmonicity = 2e-3f;
  bool lagCompensation = true;     // read each partial at the newest point, not the window centre
  float fixedInharmonicity = -1.0f;  // >= 0: use this B and fit f0 alone
};

namespace detail {
struct Sums {
  double w = 0, wk2 = 0, wk4 = 0, wy = 0, wyk2 = 0;
  void add(double weight, double k2, double y) {
    w += weight;
    wk2 += weight * k2;
    wk4 += weight * k2 * k2;
    wy += weight * y;
    wyk2 += weight * y * k2;
  }
};

inline float strongestPower(const PartialMeasure* m, int n) {
  float p = 0.0f;
  for (int k = 0; k < n; ++k) p = m[k].power > p ? m[k].power : p;
  return p;
}

/// Weighted least squares for y_k = f0 + c k^2 over the partials in `use`.
inline PartialFit solve(const PartialMeasure* ms, int n, double refHz, unsigned use, const FitOptions& o) {
  Sums s;
  PartialFit r;
  for (int k = 1; k <= n; ++k) {
    if (!(use & (1u << k))) continue;
    const double k2 = static_cast<double>(k) * k;
    s.add(k2 * ms[k - 1].information, k2, refHz + ms[k - 1].offsetHz / static_cast<double>(k));
    ++r.used;
  }
  if (r.used == 0 || s.w <= 0.0) return r;
  double f0 = s.wy / s.w, c = 0.0, var = 1.0 / s.w;
  const double det = s.w * s.wk4 - s.wk2 * s.wk2;
  const bool freeB = o.fitInharmonicity && r.used >= 3 && det > 1e-12 * s.w * s.wk4;
  if (o.fixedInharmonicity >= 0.0f) {
    c = 0.5 * o.fixedInharmonicity * f0;
  } else if (freeB) {
    var = s.wk4 / det;
    c = std::clamp((s.w * s.wyk2 - s.wk2 * s.wy) / det, 0.0, 0.5 * o.maxInharmonicity * f0);
  }
  f0 = (s.wy - c * s.wk2) / s.w;  // best f0 for that stiffness
  r.f0 = static_cast<float>(f0);
  r.sigmaHz = static_cast<float>(std::sqrt(var));
  r.inharmonicity = static_cast<float>(2.0 * c / f0);
  r.valid = true;
  return r;
}

/// The partial whose residual is most improbable given its own variance, if chi^2 > 16 and it
/// is also more than 1 cent off the model (the variances ignore correlated errors, so chi^2
/// alone would throw away good partials).
inline int worstOutlier(const PartialMeasure* ms, int n, double refHz, unsigned use, const PartialFit& f) {
  int worst = 0;
  double worstChi2 = 16.0;
  for (int k = 1; k <= n; ++k) {
    if (!(use & (1u << k))) continue;
    const double model = k * f.f0 * (1.0 + 0.5 * f.inharmonicity * k * k);
    const double resid = refHz * k + ms[k - 1].offsetHz - model;  // Hz, partial k
    const double chi2 = resid * resid * ms[k - 1].information;
    const bool farOff = std::fabs(resid) > 5.8e-4 * model;  // 1 cent = 0.058%
    if (farOff && chi2 > worstChi2) {
      worstChi2 = chi2;
      worst = k;
    }
  }
  return worst;
}
}  // namespace detail

/// m[k-1] describes partial k measured against a reference at k * refHz. Partials that are too
/// weak or too new are skipped; one partial that disagrees wildly with the rest (hum, a
/// neighbouring string) is dropped and the fit is redone.
inline PartialFit fitPartials(const PartialMeasure* ms, int n, double refHz, const FitOptions& o) {
  const float gate = detail::strongestPower(ms, n) * o.minRelativePower;
  unsigned use = 0;
  for (int k = 1; k <= n; ++k) {
    const PartialMeasure& m = ms[k - 1];
    if (m.points >= o.minPoints && m.power >= gate && m.information > 0.0f) use |= 1u << k;
  }
  PartialFit r = detail::solve(ms, n, refHz, use, o);
  if (!r.valid || r.used < 3) return r;
  const int worst = detail::worstOutlier(ms, n, refHz, use, r);
  return worst ? detail::solve(ms, n, refHz, use & ~(1u << worst), o) : r;
}

inline PartialFit fitPartials(const StrobeBank& bank, const FitOptions& o) {
  std::array<PartialMeasure, Limits::kMaxPartials> m{};
  for (int k = 0; k < bank.partials(); ++k) m[k] = bank.measure(k, o.lagCompensation);
  return fitPartials(m.data(), bank.partials(), bank.refHz(), o);
}

}  // namespace squatch::tuner
