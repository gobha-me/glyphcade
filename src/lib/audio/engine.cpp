#include <termgame/audio/engine.hpp>

#include <algorithm>
#include <utility>

namespace termgame::audio {

// ── Mixer ───────────────────────────────────────────────────────────────────

auto Mixer::trigger(const SfxSpec& spec, SfxId id, int sample_rate) noexcept
    -> void {
  const std::uint64_t age = m_next_age++;

  // A free slot first — stealing is the exception, not the policy.
  for (auto& v : m_voices) {
    if (!v.active()) {
      v.trigger(spec, sample_rate, id, age);
      return;
    }
  }

  // All busy: take the oldest. Scanning eight voices is cheaper than any
  // bookkeeping that would avoid the scan, and this runs at most once per
  // command rather than once per sample.
  Voice* oldest = &m_voices[0];
  for (auto& v : m_voices) {
    if (v.age() < oldest->age()) oldest = &v;
  }

  ++m_stolen;
  oldest->trigger(spec, sample_rate, id, age);
}

auto Mixer::render(float* out, int frames) noexcept -> void {
  for (auto& v : m_voices) {
    if (v.active()) v.render_add(out, frames);
  }
}

auto Mixer::active_count() const noexcept -> int {
  int n = 0;
  for (const auto& v : m_voices) {
    if (v.active()) ++n;
  }
  return n;
}

auto Mixer::voice_ids() const noexcept -> std::array<int, kMaxVoices> {
  std::array<int, kMaxVoices> ids{};
  for (std::size_t i = 0; i < m_voices.size(); ++i) ids[i] = m_voices[i].id();
  return ids;
}

// ── Engine ──────────────────────────────────────────────────────────────────

Engine::~Engine() { close(); }

auto Engine::open(std::unique_ptr<AudioSink> sink, const SinkFormat& want)
    -> std::expected<void, std::string> {
  if (sink == nullptr) return std::unexpected("no sink");
  if (m_sink != nullptr) return std::unexpected("engine is already open");

  // ⚠ Everything the callback can touch is settled BEFORE open() is called.
  // A device sink may invoke the trampoline before open() returns, so any
  // initialisation left until afterwards is a race that reproduces only on real
  // hardware — which is to say, never in this container.
  m_rate = want.sample_rate;
  m_pump_carry = 0.0;

  auto opened = sink->open(want, &Engine::trampoline, this);
  if (!opened) return opened;

  // The granted format, which a device is free to have changed under us.
  m_rate = sink->format().sample_rate;

  // Resolved once, here, rather than downcast at every pump(). See the member's
  // comment for why this cast is safe and what would make it unsafe.
  m_offline = sink->kind() == SinkKind::Offline
                  ? static_cast<WavFileSink*>(sink.get())
                  : nullptr;

  m_sink = std::move(sink);
  return {};
}

auto Engine::close() noexcept -> void {
  // Stop the stream FIRST. Releasing the sink while its thread is still calling
  // the trampoline would be a use-after-free of this very object.
  if (m_sink != nullptr) {
    m_sink->close();
    m_sink.reset();
  }
  m_offline = nullptr;
}

auto Engine::play(SfxId id) noexcept -> bool {
  const auto i = static_cast<std::size_t>(id);
  if (i < m_play_count.size()) ++m_play_count[i];
  m_last = static_cast<int>(id);

  // ⚠ Short-circuit when nothing will ever drain the ring.
  //
  // With a NullSink — which is what every CI job and this container run — no
  // consumer exists. Posting anyway would fill 64 slots once and then climb
  // dropped() on every subsequent sound, forever, turning the subsystem's most
  // useful counter into noise on the configuration the repo promises always
  // works. The intent is still recorded above, which is what binding tests
  // assert on.
  if (m_sink == nullptr || m_sink->kind() == SinkKind::Discard) {
    ++m_silenced;
    return false;
  }

  return m_ring.try_push(Command{.id = id, .reserved = 0});
}

auto Engine::pump(std::chrono::duration<double> dt) -> void {
  if (m_offline == nullptr) return;

  // A carried remainder rather than rounding each tick independently. At 60 Hz
  // and 48 kHz the ideal is exactly 800 frames, but 1.0/60.0 is not exact in
  // binary, so a per-tick round would shed a fraction of a frame every tick and
  // the file would drift out of sync with the game clock over a long session.
  // Carrying the remainder makes the error bounded rather than cumulative.
  const double want = m_pump_carry + (dt.count() * static_cast<double>(m_rate));
  if (want <= 0.0) return;

  const auto frames = static_cast<int>(want);
  m_pump_carry = want - static_cast<double>(frames);

  if (frames > 0) m_offline->render(frames);
}

auto Engine::render(float* out, int frames, int channels) noexcept -> void {
  if (out == nullptr || frames <= 0 || channels <= 0) return;

  // ⚠ OVERWRITES. This is the top of the render stack: Voice and Mixer add into
  // a buffer, and this is what guarantees the buffer they add into is clean.
  // A device hands us whatever was last in its buffer, so without this the
  // previous block is heard again — as a rising echo.
  std::fill_n(out, static_cast<std::size_t>(frames) *
                       static_cast<std::size_t>(channels),
              0.0F);

  // Drain the ring first, so a sound posted just before this callback is heard
  // in it rather than one block later.
  Command cmd{};
  while (m_ring.try_pop(cmd)) {
    m_mixer.trigger(spec_for(cmd.id), cmd.id, m_rate);
  }

  const auto cap = static_cast<int>(m_scratch.size());
  int done = 0;

  while (done < frames) {
    const int chunk = std::min(cap, frames - done);

    // Chunked rather than allocated: this runs on the audio thread, where a
    // heap allocation is forbidden however unlikely a >2048-frame buffer is.
    std::fill_n(m_scratch.begin(), static_cast<std::size_t>(chunk), 0.0F);
    m_mixer.render(m_scratch.data(), chunk);

    // The mix is mono; fan it out to however many channels the device wants.
    // Same sample in every channel — these are point-source arcade blips, and
    // there is nothing to pan.
    for (int f = 0; f < chunk; ++f) {
      const float s = m_scratch[static_cast<std::size_t>(f)];
      const auto base = static_cast<std::size_t>(done + f) *
                        static_cast<std::size_t>(channels);
      for (int c = 0; c < channels; ++c) {
        out[base + static_cast<std::size_t>(c)] = s;
      }
    }

    done += chunk;
  }
}

auto Engine::trampoline(float* out, int frames, int channels,
                        void* user) noexcept -> void {
  static_cast<Engine*>(user)->render(out, frames, channels);
}

auto Engine::stats() const noexcept -> EngineStats {
  return EngineStats{
      .pushed = m_ring.pushed(),
      .popped = m_ring.popped(),
      .dropped = m_ring.dropped(),
      .stolen = m_mixer.stolen(),
      .silenced = m_silenced,
      .active_voices = m_mixer.active_count(),
  };
}

auto Engine::play_count(SfxId id) const noexcept -> std::uint32_t {
  const auto i = static_cast<std::size_t>(id);
  return i < m_play_count.size() ? m_play_count[i] : 0;
}

auto Engine::reset_counts() noexcept -> void {
  m_play_count.fill(0);
  m_last = -1;
}

auto Engine::kind() const noexcept -> SinkKind {
  return m_sink != nullptr ? m_sink->kind() : SinkKind::Discard;
}

auto Engine::sink_name() const noexcept -> std::string_view {
  return m_sink != nullptr ? m_sink->name() : "none";
}

}  // namespace termgame::audio
