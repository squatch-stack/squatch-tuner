// SPDX-License-Identifier: MIT
// Unit tests for the host layer (hosts/common): the lock-free handoff and the display naming.
// Built with ThreadSanitizer by `make test`, so a data race in Latest<T> fails the build's test.
#include <cmath>
#include <cstdio>
#include <string_view>
#include <thread>

#include "../common/display.hpp"
#include "../common/latest.hpp"

using squatch::host::Latest;
using squatch::host::noteName;
using squatch::host::toDisplay;
using squatch::tuner::TunerReading;

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

// Every field of a value is derived from its sequence number, so a torn read shows up.
struct Probe {
  long seq = 0;
  long twice = 0;
  double root = 0.0;
};

static void testLatestHandsOverWholeValues() {
  static Latest<Probe> box;
  constexpr long kCount = 200000;
  std::thread producer([] {
    for (long i = 1; i <= kCount; ++i) {
      Probe& p = box.back();
      p.seq = i;
      p.twice = 2 * i;
      p.root = std::sqrt(static_cast<double>(i));
      box.publish();
    }
  });
  long last = 0, torn = 0, backwards = 0, seen = 0;
  while (last < kCount) {
    if (!box.update()) continue;
    const Probe& p = box.front();
    ++seen;
    torn += (p.twice != 2 * p.seq || p.root != std::sqrt(static_cast<double>(p.seq)));
    backwards += p.seq <= last;
    last = p.seq;
  }
  producer.join();
  CHECK(torn == 0, "%ld torn values", torn);
  CHECK(backwards == 0, "%ld values older than one already seen", backwards);
  CHECK(seen > 0 && last == kCount, "consumer saw %ld values, last %ld", seen, last);
  CHECK(!box.update(), "no new value after the last one was taken");
}

static void testNoteNames() {
  const struct { int midi; const char* name; int octave; } cases[] = {
      {69, "A", 4}, {60, "C", 4}, {59, "B", 3}, {40, "E", 2}, {45, "A", 2}, {0, "C", -1}, {61, "C#", 4}};
  for (const auto& c : cases) {
    const auto n = noteName(c.midi);
    CHECK(std::string_view(n.name) == c.name && n.octave == c.octave, "midi %d -> %s%d", c.midi, n.name, n.octave);
  }
}

static TunerReading readingAt(double steadyHz, double hz) {
  TunerReading r;
  r.valid = true;
  r.steadyHz = static_cast<float>(steadyHz);
  r.hz = static_cast<float>(hz);
  return r;
}

static void testDisplayNaming() {
  const double plus7 = 110.0 * std::exp2(7.0 / 1200.0);
  auto d = toDisplay(readingAt(plus7, plus7), 440.0f);
  CHECK(d.valid && d.midi == 45 && std::fabs(d.cents - 7.0f) < 1e-3f, "A2 +7: midi %d, %+.4f c", d.midi, d.cents);
  d = toDisplay(readingAt(plus7, plus7), 442.0f);
  const double at442 = 7.0 - 1200.0 * std::log2(442.0 / 440.0);
  CHECK(d.midi == 45 && std::fabs(d.cents - at442) < 1e-3, "A4 = 442: %+.4f c, expected %+.4f", d.cents, at442);
  // Near the half-way point the strobe must be measured against the note the number names.
  const double a = 110.0 * std::exp2(49.9 / 1200.0), b = 110.0 * std::exp2(50.2 / 1200.0);
  d = toDisplay(readingAt(a, b), 440.0f);
  CHECK(d.midi == 45 && d.fastCents > 50.0f, "fast cents %+.2f against midi %d", d.fastCents, d.midi);
  TunerReading off = readingAt(plus7, plus7);
  off.valid = false;
  CHECK(!toDisplay(off, 440.0f).valid, "an invalid reading stays invalid");
}

int main() {
  testLatestHandsOverWholeValues();
  testNoteNames();
  testDisplayNaming();
  std::printf(gFailures ? "hosts: %d failure(s)\n" : "hosts: all tests passed\n", gFailures);
  return gFailures ? 1 : 0;
}
