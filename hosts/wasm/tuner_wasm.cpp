// SPDX-License-Identifier: MIT
// The Squatch Tuner as a WebAssembly module for an AudioWorklet (see worklet.js).
//
// A plain C ABI over static storage: no allocation, no locks, no imports the browser has to
// supply. The worklet copies each 128-frame block into tuner_input(), calls tuner_process(), and
// about 30 times a second reads tuner_frame(): the reading named against the player's A4 by the
// same display code the JACK host uses (hosts/common/display.hpp).
#include <algorithm>
#include <cmath>

#include "../common/display.hpp"

namespace {

constexpr int kMaxBlock = 1024;  // the render quantum is 128 frames; room for larger ones

// tuner_frame() layout, one float each. worklet.js and demo.js read it by these indices.
enum Field { kValid, kMidi, kOctave, kCents, kFast, kHz, kSteadyHz, kUnc, kParts, kB, kA4, kLevel, kPeak, kFields };

squatch::tuner::Tuner gTuner;
float gIn[kMaxBlock];
float gFrame[kFields];
float gA4 = 440.0f;
float gPeak = 0.0f;
float gDecayPerFrame = 1.0f;  // the peak falls 20 dB per second

float dbfs(float x) { return 20.0f * std::log10(std::max(x, 1e-6f)); }

}  // namespace

extern "C" {

/// Where the worklet writes each block (kMaxBlock floats).
__attribute__((export_name("tuner_input"))) float* tuner_input() { return gIn; }

/// Largest block tuner_process() takes.
__attribute__((export_name("tuner_max_block"))) int tuner_max_block() { return kMaxBlock; }

/// Not real-time safe (it builds tables): call once, before audio starts. Returns 0 on success.
__attribute__((export_name("tuner_configure"))) int tuner_configure(float sampleRate, float minHz) {
  if (!(sampleRate >= 8000.0f && sampleRate <= 384000.0f) || !(minHz >= 20.0f && minHz <= 200.0f)) return 1;
  squatch::tuner::TunerConfig c;
  c.sampleRate = sampleRate;
  c.minHz = minHz;
  gTuner.configure(c);
  gPeak = 0.0f;
  gDecayPerFrame = std::exp2(-1.0f / (std::log10(2.0f) * sampleRate));
  return 0;
}

/// Real-time safe: tune the first n samples of tuner_input().
__attribute__((export_name("tuner_process"))) void tuner_process(int n) {
  n = std::clamp(n, 0, kMaxBlock);
  gTuner.process(gIn, n);
  float peak = gPeak * std::pow(gDecayPerFrame, static_cast<float>(n));
  for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(gIn[i]));
  gPeak = peak;
}

/// The reference pitch the display names notes against (430..450 Hz; others are ignored).
__attribute__((export_name("tuner_set_a4"))) void tuner_set_a4(float hz) {
  if (hz >= 430.0f && hz <= 450.0f) gA4 = hz;
}

/// Real-time safe: the newest reading, laid out as Field. Stable until the next call.
__attribute__((export_name("tuner_frame"))) const float* tuner_frame() {
  const auto& r = gTuner.reading();
  const auto d = squatch::host::toDisplay(r, gA4);
  gFrame[kValid] = d.valid ? 1.0f : 0.0f;
  gFrame[kMidi] = static_cast<float>(d.midi);
  gFrame[kOctave] = static_cast<float>(d.note.octave);
  gFrame[kCents] = d.cents;
  gFrame[kFast] = d.fastCents;
  gFrame[kHz] = d.hz;
  gFrame[kSteadyHz] = d.steadyHz;
  gFrame[kUnc] = r.uncertainty;
  gFrame[kParts] = static_cast<float>(r.partials);
  gFrame[kB] = r.inharmonicity;
  gFrame[kA4] = gA4;
  gFrame[kLevel] = dbfs(r.level);
  gFrame[kPeak] = dbfs(gPeak);
  return gFrame;
}

/// Length of the frame, so the page can check it matches what it expects.
__attribute__((export_name("tuner_frame_size"))) int tuner_frame_size() { return kFields; }

}  // extern "C"
