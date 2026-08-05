// The audio command ring: the one place in this repo where a bug is a
// heisenbug, so it gets the most adversarial treatment (AGENTS.md).
//
// ⚠ Note what is NOT included below: no termforge header, no arcade header, no
// Screen. ring.hpp names no termforge type, so this file structurally cannot
// construct one — same discipline as test/14minesweeper.
//
// The file is split deliberately, and the split is the whole design:
//
//  1. SINGLE-THREADED cases assert FACTS. Capacity, wraparound, FIFO order and
//     the overflow policy are deterministic when one thread does everything, so
//     they are asserted as exact equalities and can never flake.
//  2. The THREADED case asserts only SCHEDULE-INDEPENDENT INVARIANTS — order,
//     integrity, conservation. It must never assert anything about how far
//     ahead the producer got, because that is the OS's decision and an
//     assertion about it is a test that fails on a busy machine.
//
// ⚠ In particular it does NOT assert dropped() > 0. Whether the consumer keeps
// up is scheduling; overflow is asserted above, where it is a fact.
//
// ⚠ No sleeps anywhere. std::this_thread::yield() only. A test that sleeps to
// make a race likely is a test that takes a second and still misses it.
//
// The threaded case exists for TSan more than for its assertions: under
// cmake/toolchain/thread.cmake it is what proves the acquire/release pairing in
// try_push/try_pop is real. Run it there before believing it —
// `ctest --test-dir build-tsan -R 16audio-ring`.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <glyphcade/audio/ring.hpp>

namespace {

using glyphcade::audio::SpscRing;

// A payload with a redundant field, so a torn read is detectable.
//
// This is the thing TSan cannot see and a plain "did it race" test would miss:
// if a slot were read while being written, seq and check would disagree. A
// single-field payload would let a torn read look like a valid item.
struct Probe {
  std::uint32_t seq{0};
  std::uint32_t check{0};
};

[[nodiscard]] constexpr auto checksum(std::uint32_t seq) noexcept
    -> std::uint32_t {
  return seq * 2654435761U;  // Knuth's multiplicative hash; any bijection does
}

[[nodiscard]] constexpr auto probe(std::uint32_t seq) noexcept -> Probe {
  return Probe{.seq = seq, .check = checksum(seq)};
}

constexpr std::size_t kCap = 8;  // small, so overflow is reachable by hand
using Ring = SpscRing<Probe, kCap>;

}  // namespace

TEST_CASE("a fresh ring is empty and counts nothing", "[audio][ring]") {
  Ring r;
  Probe out{};

  REQUIRE(Ring::kCapacity == kCap);
  REQUIRE(r.empty());
  REQUIRE(r.size() == 0);
  REQUIRE(r.pushed() == 0);
  REQUIRE(r.popped() == 0);
  REQUIRE(r.dropped() == 0);
  REQUIRE_FALSE(r.try_pop(out));
}

TEST_CASE("push then pop returns items in order", "[audio][ring]") {
  Ring r;

  for (std::uint32_t i = 0; i < kCap; ++i) REQUIRE(r.try_push(probe(i)));

  REQUIRE(r.size() == kCap);
  REQUIRE_FALSE(r.empty());

  for (std::uint32_t i = 0; i < kCap; ++i) {
    Probe out{};
    REQUIRE(r.try_pop(out));
    REQUIRE(out.seq == i);
    REQUIRE(out.check == checksum(i));
  }

  REQUIRE(r.empty());
  REQUIRE(r.dropped() == 0);
}

TEST_CASE("the ring holds exactly kCapacity items", "[audio][ring]") {
  // Not kCapacity - 1. The monotonic-counter construction needs no sacrificial
  // empty slot, and this is the case that would catch a "reserve one to tell
  // full from empty" regression.
  Ring r;

  for (std::uint32_t i = 0; i < kCap; ++i) REQUIRE(r.try_push(probe(i)));

  REQUIRE(r.size() == kCap);
  REQUIRE_FALSE(r.try_push(probe(999)));
}

TEST_CASE("overflow drops the NEWEST and counts it", "[audio][ring]") {
  // ⚠ THIS CASE PINS A DELIBERATE DIVERGENCE FROM gitea #3, which specifies
  // "drop the oldest command and count it". Drop-oldest cannot be done inside
  // the SPSC contract — it means the producer advancing the consumer's index —
  // so the ring drops the newest instead. ring.hpp carries the full argument.
  //
  // If someone later builds an overwriting ring, this case goes red, and that
  // is the point: the policy changes deliberately or not at all.
  Ring r;

  for (std::uint32_t i = 0; i < kCap; ++i) REQUIRE(r.try_push(probe(i)));

  REQUIRE_FALSE(r.try_push(probe(100)));
  REQUIRE_FALSE(r.try_push(probe(101)));
  REQUIRE(r.dropped() == 2);

  // The survivors are the FIRST kCap. Nothing the rejected pushes carried
  // reached a slot, and nothing already queued was evicted to make room.
  for (std::uint32_t i = 0; i < kCap; ++i) {
    Probe out{};
    REQUIRE(r.try_pop(out));
    REQUIRE(out.seq == i);
  }
  REQUIRE(r.empty());
}

TEST_CASE("a dropped push does not disturb the ring", "[audio][ring]") {
  // Overflow must be a no-op beyond the counter: after draining one slot the
  // ring accepts again, and the item that follows the drop is the next one
  // pushed — not the one that was rejected.
  Ring r;
  Probe out{};

  for (std::uint32_t i = 0; i < kCap; ++i) REQUIRE(r.try_push(probe(i)));
  REQUIRE_FALSE(r.try_push(probe(100)));

  REQUIRE(r.try_pop(out));
  REQUIRE(out.seq == 0);

  REQUIRE(r.try_push(probe(200)));

  for (std::uint32_t i = 1; i < kCap; ++i) {
    REQUIRE(r.try_pop(out));
    REQUIRE(out.seq == i);
  }
  REQUIRE(r.try_pop(out));
  REQUIRE(out.seq == 200);  // 100 was discarded, not queued behind
  REQUIRE(r.empty());
  REQUIRE(r.dropped() == 1);
}

TEST_CASE("indices wrap without disturbing order", "[audio][ring]") {
  // Ten laps of the buffer, one in flight at a time, so every slot is reused
  // many times and the mask arithmetic is exercised well past the first wrap.
  Ring r;
  Probe out{};

  for (std::uint32_t i = 0; i < kCap * 10; ++i) {
    REQUIRE(r.try_push(probe(i)));
    REQUIRE(r.try_pop(out));
    REQUIRE(out.seq == i);
    REQUIRE(out.check == checksum(i));
    REQUIRE(r.empty());
  }

  REQUIRE(r.pushed() == kCap * 10);
  REQUIRE(r.popped() == kCap * 10);
  REQUIRE(r.dropped() == 0);
}

TEST_CASE("interleaved partial drains keep FIFO order", "[audio][ring]") {
  // The realistic shape: the producer runs ahead, the consumer takes a chunk,
  // and the ring is never empty in between. Straddles the wrap repeatedly.
  Ring r;
  Probe out{};
  std::uint32_t next_push = 0;
  std::uint32_t next_pop = 0;

  for (int round = 0; round < 20; ++round) {
    for (int k = 0; k < 5 && r.size() < kCap; ++k) {
      REQUIRE(r.try_push(probe(next_push++)));
    }
    for (int k = 0; k < 3 && !r.empty(); ++k) {
      REQUIRE(r.try_pop(out));
      REQUIRE(out.seq == next_pop++);
      REQUIRE(out.check == checksum(out.seq));
    }
  }

  while (r.try_pop(out)) {
    REQUIRE(out.seq == next_pop++);
  }
  REQUIRE(next_pop == next_push);
}

TEST_CASE("counters conserve: pushed == popped + dropped + in flight",
          "[audio][ring]") {
  Ring r;
  Probe out{};
  std::uint64_t accepted = 0;
  std::uint64_t taken = 0;

  for (std::uint32_t i = 0; i < 500; ++i) {
    if (r.try_push(probe(i))) ++accepted;
    if ((i % 3) == 0 && r.try_pop(out)) ++taken;
  }

  REQUIRE(r.pushed() == accepted);
  REQUIRE(r.popped() == taken);
  REQUIRE(accepted + r.dropped() == 500);
  REQUIRE(r.size() == accepted - taken);
}

TEST_CASE("one producer and one consumer, concurrently", "[audio][ring]") {
  // ⚠ THE TSAN CASE. Its assertions are deliberately weak — they are the only
  // things true under EVERY schedule. Its real job is to give TSan a real
  // producer and a real consumer hammering the same slots, so that a wrong
  // memory order in try_push/try_pop is reported rather than merely possible.
  //
  // Read it as: "whatever the scheduler did, nothing was reordered, nothing was
  // torn, and nothing went missing."
  constexpr std::uint32_t kMessages = 200000;

  SpscRing<Probe, 64> r;
  std::atomic<bool> producer_done{false};

  std::vector<std::uint32_t> got;
  got.reserve(kMessages);

  std::thread producer([&] {
    for (std::uint32_t i = 0; i < kMessages; ++i) {
      // Spin until accepted: this test is about the ring, not about the drop
      // policy, and retrying keeps the conservation law exact. The real UI
      // thread does NOT do this — play() posts once and moves on.
      while (!r.try_push(probe(i))) std::this_thread::yield();
    }
    producer_done.store(true, std::memory_order_release);
  });

  Probe out{};
  while (!producer_done.load(std::memory_order_acquire) || !r.empty()) {
    if (r.try_pop(out)) {
      // Integrity, checked on every single item: a torn read would show up
      // here as a checksum that does not match the sequence number it arrived
      // with. This is what a single-field payload could not detect.
      REQUIRE(out.check == checksum(out.seq));
      got.push_back(out.seq);
    } else {
      std::this_thread::yield();
    }
  }

  producer.join();

  // Nothing lost, nothing duplicated, nothing reordered.
  REQUIRE(got.size() == kMessages);
  for (std::uint32_t i = 0; i < kMessages; ++i) REQUIRE(got[i] == i);

  REQUIRE(r.empty());
  REQUIRE(r.pushed() == kMessages);
  REQUIRE(r.popped() == kMessages);

  // ⚠ Nothing is asserted about dropped() here, and the reason is worth
  // knowing: it counts REJECTED PUSHES, not lost messages. This producer
  // retries until accepted, so it rejects tens of thousands of times while
  // losing nothing — the count is a function of how the OS interleaved two
  // threads, which is exactly the sort of thing an assertion must not touch.
  //
  // The counter is only equal to "sounds the player did not hear" because the
  // real caller, Engine::play(), posts once and moves on. Overflow is asserted
  // exactly, single-threaded, in the cases above.
}
