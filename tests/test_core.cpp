// SPDX-License-Identifier: MIT
// Unit tests for the tuner core. No framework: each test is a function, CHECK counts failures.
// Built with AddressSanitizer and UBSan by `make test`.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

#include <squatch/tuner/poly.hpp>
#include <squatch/tuner/tuner.hpp>

#include "../bench/synth.hpp"

using namespace squatch::tuner;

// ---- allocation counter: the audio path must never touch the heap ----
static std::atomic<long> gAllocs{0};
void* operator new(std::size_t n) {
  ++gAllocs;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

static int gFailures = 0;
#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      ++gFailures;                                                \
      std::printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
      std::printf(__VA_ARGS__);                                   \
      std::printf("\n");                                          \
    }                                                             \
  } while (0)

static std::vector<float> sine(double hz, double seconds, double rate = 48000.0, double amp = 0.3) {
  std::vector<float> x(static_cast<size_t>(seconds * rate));
  for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(amp * std::sin(2.0 * 3.141592653589793 * hz * i / rate));
  return x;
}

static void testDecimator() {
  Decimator d;
  d.configure(4);
  float y = 0.0f, last = 0.0f;
  for (int i = 0; i < 4000; ++i)
    if (d.push(1.0f, y)) last = y;
  CHECK(std::fabs(last - 1.0f) < 1e-4f, "DC gain %f", last);
  const auto hi = sine(10000.0, 0.1);
  float peak = 0.0f;
  for (size_t i = 0; i < hi.size(); ++i)
    if (d.push(hi[i], y) && i > 1000) peak = std::fmax(peak, std::fabs(y));
  CHECK(peak < 0.3f * 0.01f, "10 kHz must be 40 dB down after decimating to 12 kHz, got %f", peak);
}

static void testNco() {
  Nco n;
  n.setFrequency(82.4069, 12000.0);
  CHECK(std::fabs(n.frequency() - 82.4069) < 1e-5, "NCO frequency %f", n.frequency());
}

static void testPhaseSlope() {
  PhaseSlopeWindow<512> w;
  w.setWindow(200);
  for (int i = 0; i < 300; ++i) w.add(0.01f, 1.0f);  // constant 0.01 rad/step
  CHECK(std::fabs(w.slope() - 0.01f) < 1e-6f, "line slope %g", w.slope());
  PhaseSlopeWindow<512> q;
  q.setWindow(200);
  double prev = 0.0;
  for (int i = 1; i <= 300; ++i) {  // phase = 0.01 t + 1e-5 t^2: slope at t is 0.01 + 2e-5 t
    const double p = 0.01 * i + 1e-5 * i * i;
    q.add(static_cast<float>(p - prev), 1.0f);
    prev = p;
  }
  float var = 0.0f;
  const float end = q.endSlope(var);
  CHECK(std::fabs(end - (0.01f + 2e-5f * 300.0f)) < 2e-4f, "end slope %g", end);
  CHECK(std::fabs(q.slope() - (0.01f + 2e-5f * 200.0f)) < 2e-4f, "centre slope %g", q.slope());
}

static void testFitRecoversStiffness() {
  const double f0 = 82.0, b = 2e-4;
  std::array<PartialMeasure, 6> m{};
  for (int k = 1; k <= 6; ++k) {
    m[k - 1].offsetHz = static_cast<float>(k * f0 * std::sqrt(1.0 + b * k * k) - k * 81.9);
    m[k - 1].information = 1e6f;
    m[k - 1].power = 1.0f;
    m[k - 1].points = 100;
  }
  const PartialFit f = fitPartials(m.data(), 6, 81.9, FitOptions{});
  CHECK(std::fabs(1200.0 * std::log2(f.f0 / f0)) < 0.02, "f0 %f", f.f0);
  CHECK(std::fabs(f.inharmonicity - b) < 1e-5, "B %g", f.inharmonicity);
}

static void testPeriodDetectors() {
  const auto x = sine(200.0, 0.1, 12000.0);
  PeriodRange r{640, 8, 316};
  Mpm mpm;
  mpm.configure(r);
  Yin yin;
  yin.configure(r);
  const PeriodEstimate a = mpm.analyze(x.data()), b = yin.analyze(x.data());
  CHECK(a.valid && std::fabs(a.period - 60.0f) < 0.05f, "MPM period %f", a.period);
  CHECK(b.valid && std::fabs(b.period - 60.0f) < 0.05f, "YIN period %f", b.period);
}

static float tuneCents(const std::vector<float>& x, double truthHz, Tuner& t) {
  for (size_t i = 0; i + 64 <= x.size(); i += 64) t.process(&x[i], 64);
  return t.reading().valid ? 1200.0f * std::log2(t.reading().hz / static_cast<float>(truthHz)) : 1e9f;
}

static void testTunerSine() {
  static Tuner t;  // 128 kB: keep it off the stack, as firmware would
  t.configure(TunerConfig{});
  const float c = tuneCents(sine(110.0 * std::exp2(13.0 / 1200.0), 0.6), 110.0 * std::exp2(13.0 / 1200.0), t);
  CHECK(std::fabs(c) < 0.01f, "sine error %f c", c);
  CHECK(t.reading().midi == 45 && std::fabs(t.reading().cents - 13.0f) < 0.01f, "note %d %+.3f", t.reading().midi,
        t.reading().cents);
}

static void testTunerStiffString() {
  static Tuner t;
  t.configure(TunerConfig{});
  bench::ToneSpec s;
  s.f0 = 82.4069 * std::exp2(-21.0 / 1200.0);
  s.inharmonicity = 2e-4;
  std::mt19937 rng(1);
  const float c = tuneCents(bench::synthesize(s, 48000.0, rng), s.f0, t);
  CHECK(std::fabs(c) < 0.05f, "stiff E2 error %f c (B read %g)", c, t.reading().inharmonicity);
}

static void testNoAllocationInProcess() {
  static Tuner t;
  t.configure(TunerConfig{});
  const auto x = sine(196.0, 1.0);
  const long before = gAllocs.load();
  for (size_t i = 0; i + 128 <= x.size(); i += 128) t.process(&x[i], 128);
  CHECK(gAllocs.load() == before, "process() allocated %ld times", gAllocs.load() - before);
}

static void testPolyStrum() {
  static PolyTuner p;
  p.configure(PolyConfig{});
  const double off[6] = {-12.0, 7.0, 0.0, 15.0, -5.0, 3.0};
  const double hz[6] = {82.4069, 110.0, 146.832, 195.998, 246.942, 329.628};
  std::vector<float> x(48000, 0.0f);
  for (int s = 0; s < 6; ++s) {
    const auto y = sine(hz[s] * std::exp2(off[s] / 1200.0), 1.0, 48000.0, 0.05);
    for (size_t i = 0; i < x.size(); ++i) x[i] += y[i];
  }
  p.process(x.data(), static_cast<int>(x.size()));
  for (int s = 0; s < 6; ++s) {
    const PolyString r = p.reading(s);
    CHECK(r.valid && std::fabs(r.cents - off[s]) < 0.1, "string %d: %+.3f c, expected %+.1f", s, r.cents, off[s]);
  }
}

int main() {
  testDecimator();
  testNco();
  testPhaseSlope();
  testFitRecoversStiffness();
  testPeriodDetectors();
  testTunerSine();
  testTunerStiffString();
  testNoAllocationInProcess();
  testPolyStrum();
  std::printf(gFailures ? "%d failure(s)\n" : "all tests passed\n", gFailures);
  return gFailures ? 1 : 0;
}
