// SPDX-License-Identifier: MIT
// Squatch Tuner: monophonic tuner = coarse MPM note finder + strobe bank + stiffness-aware fit.
//
//   input (any rate) -> decimate to ~12 kHz -> history -> MPM every hop (5.3 ms): which note?
//                                        \-> strobe bank (every sample): how far off (0.01 cent on a steady tone)
//
// Usage: configure() once (not real-time safe: it builds tables), then process() from the audio
// callback with any block size, and read reading() after a block. No allocation, no locks, no
// system calls after configure(). The object is ~128 kB: make it static or a member, not a local.
#pragma once
#include <algorithm>

#include "decimator.hpp"
#include "history.hpp"
#include "level.hpp"
#include "partial_fit.hpp"
#include "period.hpp"
#include "strobe.hpp"

namespace squatch::tuner {

struct TunerConfig {
  float sampleRate = 48000.0f;
  float a4 = 440.0f;
  float minHz = 38.0f;            // 38 covers bass E1 (41.2) and 8-string F#1 (46.2); 27 for B0
  float maxHz = 1400.0f;          // E6, 24th fret of the high E, is 1318.5
  float workRate = 12000.0f;      // decimate towards this rate
  float gate = 3e-4f;             // RMS level below which nothing is reported (-70 dBFS)
  float clarity = 0.8f;           // MPM peak needed to start a note
  float memorySeconds = 0.5f;     // phase-fit memory: longer = steadier, slower to follow
  float minBoxcarSeconds = 0.0f;  // demodulator lowpass length floor (see StrobeBank::start)
  float maxUncertainty = 1.0f;    // cents; a noisier estimate is withheld
  int partials = 6;
  FitOptions fit{};
};

struct TunerReading {
  bool valid = false;
  float hz = 0.0f;             // fundamental (partial 1) now: follows glides (drives the strobe)
  float steadyHz = 0.0f;       // fundamental averaged over the fit window (drives the number)
  int midi = 0;                // nearest note to hz
  float cents = 0.0f;          // hz's offset from that note
  float inharmonicity = 0.0f;  // B of this string, as measured on this note
  float uncertainty = 0.0f;    // standard error of `cents`, from the fit's own residuals
  float clarity = 0.0f;
  float level = 0.0f;          // RMS at the work rate
  float noteSeconds = 0.0f;    // time since this note was picked up
  int partials = 0;            // partials the fit used
  std::array<float, Limits::kMaxPartials> strobeTurns{};  // per-partial phase, for the display
};

class Tuner {
 public:
  static constexpr int kHop = 64;

  void configure(const TunerConfig& c) {
    cfg_ = c;
    dec_.configure(static_cast<int>(c.sampleRate / c.workRate + 0.5f));
    rate_ = c.sampleRate / static_cast<float>(dec_.factor());
    PeriodRange r;
    r.maxLag = std::min(static_cast<int>(rate_ / c.minHz) + 1, Limits::kMaxLag - 2);
    r.minLag = std::max(static_cast<int>(rate_ / c.maxHz), 2);
    r.window = 2 * r.maxLag + 4;
    window_ = r.window;
    mpm_.configure(r);
    (void)CosTable::instance();
    reset();
  }

  void reset() {
    dec_.reset();
    hist_.clear();
    levels_.reset();
    reading_ = TunerReading{};
    tracking_ = loudNoteSeen_ = false;
    hopFill_ = hopsInNote_ = quietHops_ = disagree_ = confirmed_ = lowOctave_ = pendingOnset_ = octaveUp_ = 0;
    energy_ = notePeak_ = 0.0f;
  }

  void process(const float* in, int n) {
    for (int i = 0; i < n; ++i) {
      float y;
      if (dec_.push(in[i], y)) pushWork(y);
    }
  }

  const TunerReading& reading() const { return reading_; }
  float workRate() const { return rate_; }

 private:
  void pushWork(float y) {
    hist_.push(y);
    energy_ += y * y;
    if (tracking_) bank_.push(y);
    if (++hopFill_ < kHop) return;
    hopFill_ = 0;
    onHop();
  }

  void onHop() {
    levels_.hop(energy_, kHop, tracking_);
    energy_ = 0.0f;
    const float level = levels_.level();
    reading_.level = level;
    // While a note rings, only a jump to within 8 dB of its peak counts as a new pluck; smaller
    // jumps are beating between the dying note and hum or noise.
    const bool onset = levels_.rising() && level > cfg_.gate && (!tracking_ || level > 0.4f * notePeak_);
    if (belowGate(level) || hist_.count() < window_) return;
    followNote(onset, level);
    if (!tracking_) return;
    checkOctave();
    updateReading();
  }

  bool belowGate(float level) {
    if (level >= cfg_.gate) {
      quietHops_ = 0;
      return false;
    }
    if (tracking_ && ++quietHops_ > 10) stopNote();
    return true;
  }

  // The coarse stage runs every hop until a note is found and for the first 16 hops of a note,
  // then every 4th hop to check the note has not changed.
  void followNote(bool onset, float level) {
    ++hopsInNote_;
    noteBookkeeping(onset);
    const bool due = !tracking_ || onset || pendingOnset_ > 0 || hopsInNote_ < 16 || (hopsInNote_ & 3) == 0;
    if (!due) return;
    coarse_ = mpm_.analyze(hist_.latest(window_));
    if (!coarseIsGood()) return;
    if (!tracking_ || pendingOnset_ > 0) {
      startNote(rate_ / coarse_.period);
      pendingOnset_ = 0;
    } else {
      checkAgreement(level);
    }
  }

  // A pluck stays pending for 8 hops (43 ms): on the onset hop itself the coarse window is still
  // mostly what came before (hum, the last note's tail).
  void noteBookkeeping(bool onset) {
    notePeak_ = std::max(notePeak_, levels_.level43());
    pendingOnset_ = onset ? 8 : std::max(pendingOnset_ - 1, 0);
    loudNoteSeen_ = loudNoteSeen_ || (tracking_ && levels_.floorKnown() && notePeak_ > 10.0f * levels_.floor());
  }

  // Once a real note (20 dB over the floor) has been heard, the floor is known to be the room,
  // not a signal, and nothing may start at it (hum is periodic too). Before that, a steady tone
  // from power-on (a test oscillator) must still read.
  bool coarseIsGood() const {
    const bool clear = coarse_.valid && coarse_.clarity >= cfg_.clarity;
    return clear && (!loudNoteSeen_ || levels_.level43() > 3.16f * levels_.floor());
  }

  // Sub-octave lock: if the odd partials of the reference stay 20 dB below the even ones for
  // three hops, the string is an octave higher than the coarse stage said.
  void checkOctave() {
    if (bank_.partials() < 4 || hopsInNote_ < 4) return;
    lowOctave_ = bank_.oddEvenRatio() < 0.01f ? lowOctave_ + 1 : 0;
    if (lowOctave_ >= 3 && bank_.refHz() * 2.0f <= cfg_.maxHz) startNote(bank_.refHz() * 2.0f);
  }

  // The coarse stage re-checks the note. Agreement confirms it; an octave away is ignored (MPM
  // slips an octave on decaying notes) unless it keeps saying "octave up" while our odd partials
  // are weak (a sympathetic string an octave down can hide an empty odd series from
  // checkOctave); a persistent, strong disagreement means the player moved on without a
  // detectable onset; a weak one means the note has sunk into noise or hum.
  void checkAgreement(float level) {
    const float hz = rate_ / coarse_.period;
    const float c = centsBetween(hz, reading_.hz > 0.0f ? reading_.hz : bank_.refHz());
    octaveUp_ = std::fabs(c - 1200.0f) <= 60.0f ? octaveUp_ + 1 : 0;
    if (std::fabs(c) <= 60.0f) return confirm();
    if (octaveUp_ >= 3 && oddPartialsWeak()) return startNote(hz);
    if (std::fabs(std::fabs(c) - 1200.0f) <= 60.0f || ++disagree_ < 2) return;
    if (strongCoarse(level)) startNote(hz);
    else if (disagree_ >= 4) stopNote();
  }

  bool strongCoarse(float level) const { return coarse_.clarity >= 0.9f && level >= 0.1f * notePeak_; }

  void confirm() {
    ++confirmed_;
    disagree_ = 0;
  }

  // Weak odd partials that also disagree with the even ones (> 1 c) come from another source,
  // such as a sympathetic string an octave down. A weak fundamental on the same string does not
  // disagree: its odd and even partials share one f0.
  bool oddPartialsWeak() const {
    return bank_.partials() >= 4 && bank_.oddEvenRatio() < 0.1f && std::fabs(oddEvenCents()) > 1.0f;
  }

  // Fundamental implied by the odd partials minus that implied by the even ones, in cents.
  float oddEvenCents() const {
    double w[2] = {0.0, 0.0}, wy[2] = {0.0, 0.0};
    const double c = 0.5 * noteB_ * bank_.refHz();
    for (int k = 1; k <= bank_.partials(); ++k) {
      const PartialMeasure m = bank_.measure(k - 1, false);
      const double y = bank_.refHz() + m.offsetHz / k - c * k * k, wk = static_cast<double>(m.information) * k * k;
      w[k & 1] += wk;
      wy[k & 1] += wk * y;
    }
    if (w[0] <= 0.0 || w[1] <= 0.0) return 0.0f;
    return static_cast<float>(1200.0 * std::log2((wy[1] / w[1]) / (wy[0] / w[0])));
  }

  void startNote(float hz) {
    bank_.start(hz, rate_, cfg_.partials, cfg_.memorySeconds, cfg_.minBoxcarSeconds);
    // Replay the last two periods so the fit starts from what the coarse stage already saw.
    const int replay = std::min(static_cast<int>(2.0f * rate_ / hz), window_);
    const float* p = hist_.latest(replay);
    for (int i = 0; i < replay; ++i) bank_.push(p[i]);
    tracking_ = true;
    hopsInNote_ = disagree_ = confirmed_ = lowOctave_ = octaveUp_ = 0;
    notePeak_ = noteB_ = 0.0f;
    infoNow_.fill(0.0f);
    infoSteady_.fill(0.0f);
    reading_.valid = false;
    reading_.hz = 0.0f;
    reading_.noteSeconds = static_cast<float>(replay) / rate_;
  }

  void stopNote() {
    tracking_ = false;
    reading_.valid = false;
  }

  // Stiffness is a property of the string, so B is estimated per note (from the steady fit) and
  // smoothed over hops; f0 is then fitted with that B held fixed. Partials fading in and out
  // change the weights a little, never the model, so the reading cannot jump when one drops out.
  // Each partial's weight is smoothed too (~25 ms): when two partials disagree (a sympathetic
  // string, a beating pair) the blend must not flip from hop to hop.
  PartialFit fitNote(bool lagCompensation, std::array<float, Limits::kMaxPartials>& info) {
    FitOptions o = cfg_.fit;
    o.lagCompensation = lagCompensation;
    std::array<PartialMeasure, Limits::kMaxPartials> m{};
    const int n = bank_.partials();
    for (int k = 0; k < n; ++k) {
      m[k] = bank_.measure(k, lagCompensation);
      info[k] = info[k] <= 0.0f ? m[k].information : info[k] + 0.2f * (m[k].information - info[k]);
      m[k].information = info[k];
    }
    if (o.fitInharmonicity && !lagCompensation) updateStiffness(fitPartials(m.data(), n, bank_.refHz(), o));
    if (o.fitInharmonicity) o.fixedInharmonicity = noteB_;
    return fitPartials(m.data(), n, bank_.refHz(), o);
  }

  // The attack's first 50 ms give wild stiffness estimates, so B starts at 0 and learns after.
  void updateStiffness(const PartialFit& free) {
    if (!free.valid || free.used < 3 || reading_.noteSeconds < 0.05f) return;
    noteB_ += 0.2f * (free.inharmonicity - noteB_);
  }

  // Withhold the reading once the note has decayed (10 dB below its peak) to within 10 dB of the
  // idle floor: past that point hum and noise, not the string, set the phase. A steady tone (a
  // test oscillator, the tuner switched on mid-note) never decays, so it always reads.
  bool aboveFloor() const {
    if (!levels_.floorKnown()) return true;
    return levels_.level43() > 3.16f * levels_.floor() || levels_.level43() > 0.316f * notePeak_;
  }

  void updateReading() {
    reading_.noteSeconds += static_cast<float>(kHop) / rate_;
    reading_.clarity = coarse_.clarity;
    const PartialFit steady = fitNote(false, infoSteady_);
    const PartialFit f = cfg_.fit.lagCompensation ? fitNote(true, infoNow_) : steady;
    reading_.steadyHz = steady.valid ? steady.f0 : f.f0;
    reading_.uncertainty = f.valid ? 1731.23f * f.sigmaHz / f.f0 : 0.0f;  // 1200 / ln 2
    reading_.valid = f.valid && confirmed_ > 0 && aboveFloor() && reading_.uncertainty <= cfg_.maxUncertainty;
    if (f.valid) publish(f);
  }

  void publish(const PartialFit& f) {
    reading_.hz = f.f0;
    reading_.inharmonicity = f.inharmonicity;
    reading_.partials = f.used;
    const NoteReading n = nearestNote(f.f0, cfg_.a4);
    reading_.midi = n.midi;
    reading_.cents = n.cents;
    for (int k = 0; k < bank_.partials(); ++k) reading_.strobeTurns[k] = bank_.phaseTurns(k);
  }

  TunerConfig cfg_{};
  Decimator dec_;
  History<Limits::kMaxWindow> hist_;
  Mpm mpm_;
  StrobeBank bank_;
  LevelTracker levels_;
  PeriodEstimate coarse_{};
  TunerReading reading_{};
  float rate_ = 12000.0f;
  float energy_ = 0.0f, notePeak_ = 0.0f;
  float noteB_ = 0.0f;  // this note's stiffness estimate
  std::array<float, Limits::kMaxPartials> infoNow_{}, infoSteady_{};  // smoothed partial weights
  int window_ = 0, hopFill_ = 0;
  int hopsInNote_ = 0, quietHops_ = 0, disagree_ = 0, confirmed_ = 0, lowOctave_ = 0, pendingOnset_ = 0;
  int octaveUp_ = 0;
  bool tracking_ = false, loudNoteSeen_ = false;
};

}  // namespace squatch::tuner
