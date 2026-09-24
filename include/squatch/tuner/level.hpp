// SPDX-License-Identifier: MIT
// Level bookkeeping for the tuner, once per 64-sample hop (5.3 ms at 12 kHz): the hop level,
// a 43 ms level, a rising-energy test for plucks, and the room's noise floor.
#pragma once
#include <array>
#include <cmath>

namespace squatch::tuner {

class LevelTracker {
 public:
  void reset() {
    energy_.fill(0.0f);
    index_ = 0;
    sinceRise_ = 0;
    rising_ = false;
    level_ = level43_ = 0.0f;
    floor_ = -1.0f;
  }

  /// One hop: its energy (sum of squares over `hopLen` samples) and whether a note is ringing.
  void hop(float energy, int hopLen, bool tracking) {
    level_ = std::sqrt(energy / static_cast<float>(hopLen));
    energy_[index_++ & 7] = energy;
    rising_ = detectRise();
    float sum8 = 0.0f;
    for (float e : energy_) sum8 += e;
    level43_ = std::sqrt(sum8 / (8.0f * static_cast<float>(hopLen)));
    updateFloor(tracking, hopLen);
  }

  float level() const { return level_; }      // this hop's RMS
  float level43() const { return level43_; }  // RMS over the last 8 hops (43 ms)
  float floor() const { return floor_; }      // hum + noise RMS, once known
  bool floorKnown() const { return floor_ > 0.0f; }
  bool rising() const { return rising_; }     // a pluck-like jump in energy on this hop

 private:
  // The energy of the last 4 hops (21 ms) is 8x (+9 dB) that of the 4 before, and no rise in
  // the last 8 hops. Shorter windows flicker with the waveform of a low note (E1: 24 ms period).
  bool detectRise() {
    float recent = 0.0f, before = 0.0f;
    for (unsigned i = 0; i < 4; ++i) {
      recent += energy_[(index_ - 1 - i) & 7];
      before += energy_[(index_ - 5 - i) & 7];
    }
    const bool rise = recent > 8.0f * before && sinceRise_ > 8;
    sinceRise_ = rise ? 0 : sinceRise_ + 1;
    return rise;
  }

  // Minimum statistics over the 43 ms level (a single hop dips inside one period of a low
  // note). The floor drops at once to anything quieter and creeps up otherwise: +0.17 dB per
  // hop while idle (so it finds the hum in about a second, yet moves under 2 dB during the ~10
  // hops an attack takes to be recognised) and 3 dB per 10 s while a note rings (a decaying note
  // must never become its own floor). It is unknown until 8 hops have been idle and steady
  // (hum and noise are; a pluck, or a recording that starts mid-note, is not).
  void updateFloor(bool tracking, int hopLen) {
    if (index_ < 8) return;
    if (floor_ < 0.0f) {
      if (!tracking && steady()) floor_ = std::fmax(std::sqrt(minEnergy() / static_cast<float>(hopLen)), 1e-7f);
      return;
    }
    const float creep = tracking ? 1.00018f : 1.02f;
    floor_ = std::fmax(level43_ < floor_ ? level43_ : floor_ * creep, 1e-7f);
  }

  float minEnergy() const {
    float e = energy_[0];
    for (float v : energy_) e = std::fmin(e, v);
    return e;
  }

  // The last 8 hops' energies lie within 6 dB of each other.
  bool steady() const {
    float hi = 0.0f;
    for (float v : energy_) hi = std::fmax(hi, v);
    return hi <= 4.0f * minEnergy();
  }

  std::array<float, 8> energy_{};
  unsigned index_ = 0;
  int sinceRise_ = 0;
  bool rising_ = false;
  float level_ = 0.0f, level43_ = 0.0f, floor_ = -1.0f;
};

}  // namespace squatch::tuner
