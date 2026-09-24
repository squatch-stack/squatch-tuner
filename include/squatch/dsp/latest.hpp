// SPDX-License-Identifier: MIT
// Latest<T>: hands the newest value from one thread to another without locks or allocation.
//
// A triple buffer. The producer (the audio callback) fills back() and calls publish(); the
// consumer (a display thread) calls update() and reads front(). Neither side ever waits, the
// consumer never sees a half-written value, and values the consumer was too slow to see are
// simply replaced: a display only wants the newest reading, not a queue of old ones.
//
// Exactly one producer thread and one consumer thread. T must be trivially copyable in spirit
// (it is copied by the caller, never by this class). Blocks with no audio output, such as the
// tuner, publish their readings through one of these.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace squatch::dsp {

template <typename T>
class Latest {
 public:
  /// Producer: the slot to fill. It belongs to the producer until publish().
  T& back() { return slots_[back_]; }

  /// Producer: make back() the newest value and take a free slot for the next one.
  void publish() {
    const uint8_t old = middle_.exchange(static_cast<uint8_t>(back_ | kFresh), std::memory_order_acq_rel);
    back_ = old & kIndex;
  }

  /// Consumer: true if a value newer than front() was published; front() is then that value.
  bool update() {
    if ((middle_.load(std::memory_order_acquire) & kFresh) == 0) return false;
    const uint8_t old = middle_.exchange(front_, std::memory_order_acq_rel);
    front_ = old & kIndex;
    return true;
  }

  /// Consumer: the newest value taken by update(). Stable until the next update().
  const T& front() const { return slots_[front_]; }

 private:
  static constexpr uint8_t kIndex = 3, kFresh = 4;
  static_assert(std::atomic<uint8_t>::is_always_lock_free, "the handoff must be lock-free");

  std::array<T, 3> slots_{};
  uint8_t back_ = 0;   // producer only
  uint8_t front_ = 1;  // consumer only
  std::atomic<uint8_t> middle_{2};
};

}  // namespace squatch::dsp
