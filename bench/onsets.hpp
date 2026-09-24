// SPDX-License-Identifier: MIT
// Note onsets in a recording, for the real-audio benches.
#pragma once
#include <vector>

namespace bench {

inline std::vector<double> hopEnergies(const std::vector<float>& x, size_t hop) {
  std::vector<double> e;
  for (size_t i = 0; i + hop <= x.size(); i += hop) {
    double s = 0;
    for (size_t j = 0; j < hop; ++j) s += double(x[i + j]) * x[i + j];
    e.push_back(s / static_cast<double>(hop));
  }
  return e;
}

/// Onsets of notes that ring at least `minGap` s before the next: hop energy (256 samples) 8x
/// the energy four hops earlier, above -50 dBFS.
inline std::vector<double> findOnsets(const std::vector<float>& x, float fs, double minGap = 1.5) {
  const size_t hop = 256;
  const std::vector<double> e = hopEnergies(x, hop);
  std::vector<double> all;
  for (size_t h = 4; h < e.size(); ++h) {
    const double t = double(h * hop) / fs;
    if (e[h] > 8.0 * e[h - 4] && e[h] > 1e-5 && (all.empty() || t > all.back() + 0.25)) all.push_back(t);
  }
  std::vector<double> keep;
  const double end = double(x.size()) / fs;
  for (size_t i = 0; i < all.size(); ++i)
    if ((i + 1 < all.size() ? all[i + 1] : end) - all[i] >= minGap) keep.push_back(all[i]);
  return keep;
}

}  // namespace bench
