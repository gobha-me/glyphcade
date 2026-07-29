#pragma once

// term-game — the Engine: ring + voices + sink, and the seam games reach.
//
//   UI thread                lock-free SPSC ring              audio thread
//     play(Sfx::Explode) ─────► [command queue] ─────► drain, trigger voice, mix
//                                                              │
//                                                        ┌─────▼──────┐
//                                                        │  AudioSink │
//                                                        └────────────┘
//
// ⚠ The line between the two threads runs through the ring and nowhere else.
// Everything on the play() side may allocate and block; nothing on the render()
// side may do either (AGENTS.md). If a field is touched by both, it is atomic
// or it is a bug.
//
// ⚠ NO TERMFORGE HEADER. The Shell translates an open() failure into an
// ErrorEvent, because the Shell is the layer that owns a terminal. Same
// discipline as ring.hpp, sink.hpp and synth.cpp.

#include <array>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <termgame/audio/ring.hpp>
#include <termgame/audio/sink.hpp>
#include <termgame/audio/synth.hpp>

namespace termgame::audio {

// What crosses the ring. Deliberately tiny and deliberately dumb: two bytes,
// trivially copyable, no pointers.
//
// There is no sequence number and no detune field. A sequence number is a test
// concern and the ring is a template, so test/16audio-ring instantiates its own
// payload; detune would need 2^(cents/1200), i.e. exp, i.e. exactly the
// portability trap synth.cpp was written to avoid. Both are additive later, and
// this repo's rule about not pinning API before something consumes it applies.
struct Command {
  SfxId id{SfxId::Click};
  std::uint8_t reserved{0};
};

// Diagnostics, gathered in one place so a caller cannot accidentally read half
// of them from the wrong thread.
struct EngineStats {
  std::uint64_t pushed{};    // commands that reached the ring
  std::uint64_t popped{};    // commands the audio side consumed
  std::uint64_t dropped{};   // pushes the ring rejected — see ring.hpp
  std::uint64_t stolen{};    // voices cut short because the pool was full
  std::uint64_t silenced{};  // play() calls with no consumer to post to
  int active_voices{};
};

// The voice pool.
//
// ⚠ AUDIO THREAD ONLY, both methods. It is not atomic and does not need to be:
// the Engine only ever touches it from inside render().
class Mixer {
 public:
  // Steals the oldest voice when all are busy. Oldest rather than quietest:
  // quietest needs a per-voice amplitude scan every trigger, and in an arcade
  // the sound that started longest ago is the one nobody is still listening to.
  auto trigger(const SfxSpec& spec, SfxId id, int sample_rate) noexcept -> void;

  // ⚠ ADDS into out, like Voice::render_add. Engine::render owns the clearing.
  auto render(float* out, int frames) noexcept -> void;

  [[nodiscard]] auto active_count() const noexcept -> int;
  [[nodiscard]] auto stolen() const noexcept -> std::uint64_t {
    return m_stolen;
  }
  // Which SfxId each slot is playing, -1 for idle. Exists so the stealing test
  // can name which voice survived rather than only counting them.
  [[nodiscard]] auto voice_ids() const noexcept -> std::array<int, kMaxVoices>;

 private:
  std::array<Voice, kMaxVoices> m_voices{};
  std::uint64_t m_next_age{0};
  std::uint64_t m_stolen{0};
};

class Engine {
 public:
  Engine() = default;
  ~Engine();

  Engine(const Engine&) = delete;
  auto operator=(const Engine&) -> Engine& = delete;

  // ⚠ ORDER IS LOAD-BEARING. The ring and mixer are fully constructed before
  // sink->open() is called, because a device sink's callback can fire before
  // open() even returns. Wiring up afterwards would be a race that only
  // reproduces on real hardware — i.e. never here.
  auto open(std::unique_ptr<AudioSink> sink, const SinkFormat& want = {})
      -> std::expected<void, std::string>;

  auto close() noexcept -> void;

  // ⚠ UI THREAD ONLY. Never blocks, never allocates. The bool is a diagnostic
  // — every production call site ignores it.
  //
  // Returns false when nothing was posted, which happens for two very different
  // reasons: there is no consumer (a Discard sink, or no sink at all), or the
  // ring was full. stats() tells them apart.
  auto play(SfxId id) noexcept -> bool;

  // Drive an OFFLINE sink by wall-clock time. No-op for anything else.
  //
  // ⚠ UI THREAD, and it does file I/O — which does not violate the realtime
  // rule, because an offline sink has no audio thread at all. The UI thread is
  // both producer and consumer there: still one of each, still legal SPSC.
  auto pump(std::chrono::duration<double> dt) -> void;

  // ⚠ AUDIO THREAD. Public only so a sink's callback can reach it, and so
  // tests can drive it directly. OVERWRITES out — it is the top of the render
  // stack, where Voice and Mixer add into a buffer someone else cleared.
  auto render(float* out, int frames, int channels) noexcept -> void;

  [[nodiscard]] auto stats() const noexcept -> EngineStats;

  // ⚠ UI-THREAD DIAGNOSTICS. The audio thread never reads or writes these, so
  // they are plain members rather than atomics.
  //
  // play_count is INTENT — it counts calls, including ones that posted nothing
  // because the build is silent. That is exactly what makes it the right thing
  // for a binding test to assert on: "pressing f asks for the Flag sound" is
  // true whether or not this machine can make one, so the same assertions pass
  // on the TERMGAME_WITH_AUDIO=OFF arm CI runs.
  [[nodiscard]] auto play_count(SfxId id) const noexcept -> std::uint32_t;
  [[nodiscard]] auto last_played() const noexcept -> int { return m_last; }
  auto reset_counts() noexcept -> void;

  [[nodiscard]] auto is_open() const noexcept -> bool {
    return m_sink != nullptr;
  }
  [[nodiscard]] auto kind() const noexcept -> SinkKind;
  [[nodiscard]] auto sink_name() const noexcept -> std::string_view;
  [[nodiscard]] auto sink() noexcept -> AudioSink* { return m_sink.get(); }

 private:
  static auto trampoline(float* out, int frames, int channels,
                         void* user) noexcept -> void;

  SpscRing<Command, 64> m_ring;
  Mixer m_mixer;
  std::unique_ptr<AudioSink> m_sink;

  // ⚠ Non-owning, and resolved ONCE in open() rather than by downcasting at
  // every pump(). It is null unless m_sink is the Offline kind.
  //
  // WavFileSink is currently the only Offline sink and it is `final`, so the
  // cast in open() is safe — but that is a fact about today's code, not an
  // invariant the type system enforces. A second offline sink means giving them
  // a common base with the pull on it, not adding a second cast here.
  WavFileSink* m_offline{nullptr};

  int m_rate{48000};

  // Fixed and generous: 2048 frames is eight times the default device block.
  // render() chunks anything larger rather than allocating, because allocating
  // is forbidden on the thread that calls it.
  std::array<float, 2048> m_scratch{};

  std::array<std::uint32_t, kSfxIds.size()> m_play_count{};
  int m_last{-1};
  std::uint64_t m_silenced{0};
  double m_pump_carry{0.0};
};

// What a game is handed. Copyable, pointer-sized, and safe when empty.
//
// ⚠ The empty case is the design, not an oversight. A default-constructed
// GameContext hands out an empty Player, so a game writes
//
//     ctx.audio().play(SfxId::Reveal);
//
// with no null check, no has_audio(), and no #ifdef — and "this build makes no
// sound" stays a property of the engine rather than a shape every one of the
// dozen call sites has to carry. Same choice GameContext::border_style()
// already makes, where the default is the floor rather than an optional.
//
// It also deliberately exposes only play(). A game must not be able to open,
// close or pump the engine; those belong to the Shell that owns it.
class Player {
 public:
  Player() = default;
  explicit Player(Engine* engine) noexcept : m_engine(engine) {}

  auto play(SfxId id) const noexcept -> void {
    if (m_engine != nullptr) m_engine->play(id);
  }

  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_engine == nullptr;
  }

 private:
  Engine* m_engine{nullptr};
};

}  // namespace termgame::audio
