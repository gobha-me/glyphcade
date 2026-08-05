#pragma once

// glyphcade — the lock-free SPSC command ring, and nothing else.
//
// This is the only channel between the UI thread and the audio thread, and it
// exists because of one absolute rule (AGENTS.md): the audio callback is a
// realtime thread — no locks, no allocation, no syscalls, no I/O, ever. A queue
// that can block, grow, or take a mutex is a queue that can make the callback
// miss its deadline, and a missed deadline is an audible click.
//
// ⚠ THIS HEADER DELIBERATELY INCLUDES NO TERMFORGE HEADER, and must not start.
// Same discipline as games/minesweeper/board.hpp, for the same reason: it makes
// "testable with no Screen and no TTY" a fact rather than an intention.
//
// SINGLE producer, SINGLE consumer. Not a convention — the whole reason this
// needs no atomic read-modify-write anywhere is that exactly one thread writes
// m_tail and exactly one writes m_head. Calling try_push from two threads, or
// try_pop from two, is a data race that TSan will find and that this class does
// nothing to prevent.

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace glyphcade::audio {

// A fixed-capacity single-producer/single-consumer queue.
//
// N must be a power of two so the index wrap is a mask rather than a modulo.
// Indices are monotonic 64-bit counters masked at the point of use, which is
// what lets the buffer hold exactly N items — the usual "sacrifice one slot to
// tell full from empty" trick is not needed when the raw counters are visible.
// At one push per SFX they cannot realistically wrap: 2^64 pushes at 60 Hz is
// longer than the universe has been around.
template <class T, std::size_t N>
class SpscRing {
  static_assert(std::has_single_bit(N),
                "N must be a power of two: the wrap is a mask, not a modulo");
  static_assert(std::is_trivially_copyable_v<T>,
                "a slot is copied on the audio thread; it must not run a "
                "user-defined copy constructor there");

 public:
  static constexpr std::size_t kCapacity = N;

  // ⚠ PRODUCER (UI) THREAD ONLY. Never blocks, never allocates.
  //
  // Returns false when the ring is full, having discarded `v` and counted it.
  //
  // ⚠ DIVERGENCE from gitea #3, which specifies "drop the oldest command and
  // count it". Drop-oldest is not implementable inside the SPSC contract: it
  // means the *producer* advancing m_head, which the consumer also writes, so
  // m_head becomes a CAS target and the consumer has to detect that the slot it
  // is mid-read of was reclaimed underneath it. The sound constructions for
  // that (Vyukov per-slot sequence numbers, or CAS on both sides) put a retry
  // loop on the audio thread — importing unbounded work into the callback to
  // improve behaviour in a state that is already pathological.
  //
  // And the state IS pathological. 64 slots; the UI thread emits a handful of
  // SFX per 16.7 ms frame; a 256-frame buffer at 48 kHz drains the ring every
  // 5.3 ms. Overflowing means >64 commands between two callbacks, i.e. the
  // audio thread is starved or dead — at which point "which 64 of the 300
  // queued sounds survive" has no right answer, because the player hears
  // neither set. Correctness of the ring is worth more than the tie-break.
  //
  // test/16audio-ring pins the policy by asserting the SURVIVORS ARE THE FIRST
  // N, so changing to an overwriting ring is a deliberate, test-visible
  // decision rather than a silent one.
  auto try_push(const T& v) noexcept -> bool {
    // Relaxed on our own index: this thread is the only writer, so it cannot
    // read a stale value of it. Acquire on the consumer's, to see the slots it
    // has finished reading before we overwrite them.
    const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
    const std::uint64_t head = m_head.load(std::memory_order_acquire);

    if (tail - head >= N) {
      m_dropped.store(m_dropped.load(std::memory_order_relaxed) + 1,
                      std::memory_order_relaxed);
      return false;
    }

    m_slots[tail & (N - 1)] = v;
    // Release: the slot write above must be visible to any consumer that sees
    // this new tail. This pairing is the entire correctness argument.
    m_tail.store(tail + 1, std::memory_order_release);
    return true;
  }

  // ⚠ CONSUMER (AUDIO) THREAD ONLY. Never blocks, never allocates.
  auto try_pop(T& out) noexcept -> bool {
    const std::uint64_t head = m_head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);

    if (head == tail) return false;

    out = m_slots[head & (N - 1)];
    m_head.store(head + 1, std::memory_order_release);
    return true;
  }

  // Diagnostics. Each is exact when read by the thread that owns the counter
  // and a snapshot when read by the other one — which is all any caller wants,
  // since nothing branches on them.
  [[nodiscard]] auto pushed() const noexcept -> std::uint64_t {
    return m_tail.load(std::memory_order_acquire);
  }
  [[nodiscard]] auto popped() const noexcept -> std::uint64_t {
    return m_head.load(std::memory_order_acquire);
  }
  // ⚠ Counts REJECTED PUSHES, not lost messages. The two are the same number
  // only because the sole production caller — Engine::play() — posts once and
  // moves on, which is the behaviour the realtime rule demands of the UI
  // thread. A caller that retried on false would inflate this without losing
  // anything, and test/16audio-ring's threaded case does exactly that, which is
  // why it asserts conservation rather than a value here.
  [[nodiscard]] auto dropped() const noexcept -> std::uint64_t {
    return m_dropped.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_head.load(std::memory_order_acquire) ==
           m_tail.load(std::memory_order_acquire);
  }
  [[nodiscard]] auto size() const noexcept -> std::uint64_t {
    return m_tail.load(std::memory_order_acquire) -
           m_head.load(std::memory_order_acquire);
  }

 private:
  // ⚠ 64, spelled as a literal, NOT
  // std::hardware_destructive_interference_size. GCC warns
  // (-Winterference-size) that the value is an ABI-stability hazard, that
  // warning is on by default, and every build arm passes -Werror — so the
  // "correct" spelling does not compile here. 64 is the cache line on every
  // machine this targets.
  //
  // The padding is the point: m_tail and m_head are written by different
  // threads, and sharing a cache line would make every push invalidate the
  // consumer's line and vice versa. That is false sharing — correct, and slow
  // in exactly the place that must not be slow.
  alignas(64) std::atomic<std::uint64_t> m_tail{0};  // producer writes
  alignas(64) std::atomic<std::uint64_t> m_head{0};  // consumer writes

  // Producer-owned. Atomic only so that a reader on another thread is not
  // racing; nothing synchronises through it, hence relaxed everywhere.
  alignas(64) std::atomic<std::uint64_t> m_dropped{0};

  alignas(64) std::array<T, N> m_slots{};
};

}  // namespace glyphcade::audio
