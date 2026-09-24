// SPDX-License-Identifier: MIT
// What a tuner display shows, from a TunerReading and the reference pitch the player chose.
//
// The core names the note from `hz` at its configured A4. A display needs more than that:
//  - the reference pitch changes at run time (A4 = 430..450 Hz), and the core does not depend on
//    it anywhere but the naming, so naming is redone here without touching the audio thread;
//  - the number and needle use `steadyHz`, the strobe uses `hz`, and both must be measured
//    against the SAME note, or the two could disagree about the note near +-50 cents.
// Any host (JACK, LV2, a WebAssembly worklet, a pedal's screen) can use this as is.
#pragma once
#include <cmath>

#include <squatch/tuner/tuner.hpp>

namespace squatch::host {

struct NoteName {
  const char* name = "";  // "C", "C#", ... "B"
  int octave = 0;         // scientific pitch notation: A4 = 440 Hz, middle C = C4
};

inline NoteName noteName(int midi) {
  static constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  const int pc = ((midi % 12) + 12) % 12;
  NoteName n;
  n.name = kNames[pc];
  n.octave = (midi - pc) / 12 - 1;
  return n;
}

struct DisplayReading {
  bool valid = false;
  int midi = 0;
  NoteName note{};
  float cents = 0.0f;      // steadyHz against the note: the number and the needle
  float fastCents = 0.0f;  // hz against the same note: the strobe
  float hz = 0.0f;
  float steadyHz = 0.0f;
};

/// Name the reading against `a4`. Invalid readings (nothing ringing, or withheld) stay invalid.
inline DisplayReading toDisplay(const tuner::TunerReading& r, float a4) {
  DisplayReading d;
  const float steady = r.steadyHz > 0.0f ? r.steadyHz : r.hz;
  if (!r.valid || steady <= 0.0f || r.hz <= 0.0f || a4 <= 0.0f) return d;
  d.valid = true;
  d.hz = r.hz;
  d.steadyHz = steady;
  d.midi = static_cast<int>(std::lround(tuner::hzToMidi(steady, a4)));
  d.note = noteName(d.midi);
  const float ref = tuner::midiToHz(static_cast<float>(d.midi), a4);
  d.cents = tuner::centsBetween(steady, ref);
  d.fastCents = tuner::centsBetween(r.hz, ref);
  return d;
}

}  // namespace squatch::host
