#include <glyphcade/audio/sink.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace glyphcade::audio {

namespace {

// Little-endian emitters. Spelled out byte by byte rather than memcpy'ing an
// integer, because RIFF is defined as little-endian regardless of the host and
// a memcpy would silently produce a big-endian file on a big-endian box. Nobody
// is going to run this on one — but a format written wrong is a bug you find by
// ear, months later, on someone else's machine.
auto put_u32(std::array<char, 44>& buf, std::size_t at, std::uint32_t v)
    -> void {
  buf[at + 0] = static_cast<char>((v >> 0) & 0xFFU);
  buf[at + 1] = static_cast<char>((v >> 8) & 0xFFU);
  buf[at + 2] = static_cast<char>((v >> 16) & 0xFFU);
  buf[at + 3] = static_cast<char>((v >> 24) & 0xFFU);
}

auto put_u16(std::array<char, 44>& buf, std::size_t at, std::uint16_t v)
    -> void {
  buf[at + 0] = static_cast<char>((v >> 0) & 0xFFU);
  buf[at + 1] = static_cast<char>((v >> 8) & 0xFFU);
}

auto put_tag(std::array<char, 44>& buf, std::size_t at, std::string_view tag)
    -> void {
  for (std::size_t i = 0; i < 4; ++i) buf[at + i] = tag[i];
}

}  // namespace

// Out of line, and not `= default` in the header, so that this file is the only
// place the vtable is emitted. Otherwise every translation unit including
// sink.hpp gets its own copy for the linker to fold.
AudioSink::~AudioSink() = default;

// ── NullSink ────────────────────────────────────────────────────────────────

auto NullSink::open(const SinkFormat& want, RenderFn fn, void* user)
    -> std::expected<void, std::string> {
  // Both are accepted and both are deliberately ignored: a Discard sink never
  // pulls, so there is nothing to call and nothing to call it with. Named
  // rather than left anonymous so the signature still reads as the interface's.
  (void)fn;
  (void)user;

  m_format = want;  // reported back verbatim; nothing negotiated it away
  return {};
}

auto NullSink::close() noexcept -> void {}

// ── WavFileSink ─────────────────────────────────────────────────────────────

WavFileSink::WavFileSink(std::filesystem::path path)
    : m_path(std::move(path)) {}

WavFileSink::~WavFileSink() { close(); }

auto WavFileSink::open(const SinkFormat& want, RenderFn fn, void* user)
    -> std::expected<void, std::string> {
  if (m_open) return std::unexpected("wav sink is already open");
  if (fn == nullptr) return std::unexpected("wav sink needs a render function");
  if (want.sample_rate <= 0 || want.channels <= 0 ||
      want.frames_per_buffer <= 0) {
    return std::unexpected("wav sink: nonsensical format");
  }

  m_out.open(m_path, std::ios::binary | std::ios::trunc);
  if (!m_out) {
    return std::unexpected("wav sink: cannot open " + m_path.string());
  }

  m_format = want;  // an offline sink grants exactly what was asked for
  m_render = fn;
  m_user = user;
  m_frames = 0;
  m_mirror.clear();
  m_scratch.assign(
      static_cast<std::size_t>(want.frames_per_buffer) *
          static_cast<std::size_t>(want.channels),
      0.0F);

  // A placeholder, back-patched by close() once the size is known. Writing it
  // now rather than buffering the whole file means a crash leaves a file whose
  // header is wrong but whose samples are all there — recoverable, and it keeps
  // memory flat.
  write_header(0);
  m_open = true;
  return {};
}

auto WavFileSink::render(int frames) -> int {
  if (!m_open || frames <= 0) return 0;

  const int block = m_format.frames_per_buffer;
  const int channels = m_format.channels;
  int done = 0;

  while (done < frames) {
    const int chunk = std::min(block, frames - done);
    const auto samples = static_cast<std::size_t>(chunk) *
                         static_cast<std::size_t>(channels);

    // ⚠ Cleared every chunk, not once. The render function ADDS into the
    // buffer (that is what lets voices mix by summation), so a stale chunk left
    // in here would be heard again on the next one — as an echo that gets
    // louder, which is a memorable way to discover this line is missing.
    std::fill_n(m_scratch.begin(), samples, 0.0F);

    m_render(m_scratch.data(), chunk, channels, m_user);

    for (std::size_t i = 0; i < samples; ++i) {
      const std::int16_t pcm = to_pcm16(m_scratch[i]);
      m_mirror.push_back(pcm);
      // Same little-endian discipline as the header.
      const auto raw = static_cast<std::uint16_t>(pcm);
      const char bytes[2] = {static_cast<char>(raw & 0xFFU),
                             static_cast<char>((raw >> 8) & 0xFFU)};
      m_out.write(bytes, 2);
    }

    m_frames += chunk;
    done += chunk;
  }

  return done;
}

auto WavFileSink::close() noexcept -> void {
  if (!m_open) return;
  m_open = false;

  // noexcept, and file I/O throws — so everything here is guarded. A sink that
  // threw from close() would do it from ~WavFileSink, i.e. during stack
  // unwinding, i.e. std::terminate. A truncated wav is a worse test artifact
  // than a wrong one, but neither is worth aborting the process over.
  try {
    const auto data_bytes = static_cast<std::uint32_t>(
        m_frames * m_format.channels * 2);
    m_out.seekp(0, std::ios::beg);
    write_header(data_bytes);
    m_out.close();
  } catch (...) {  // NOLINT(bugprone-empty-catch) — see above
  }

  m_render = nullptr;
  m_user = nullptr;
}

auto WavFileSink::write_header(std::uint32_t data_bytes) -> void {
  // The canonical 44-byte PCM header. Offsets are spelled as literals because
  // that is how every reference and every hex dump reads them, and because
  // test/17audio-sink asserts against exactly these numbers.
  const auto channels = static_cast<std::uint16_t>(m_format.channels);
  const auto rate = static_cast<std::uint32_t>(m_format.sample_rate);
  const std::uint16_t bits = 16;
  const auto block_align = static_cast<std::uint16_t>(channels * (bits / 8));
  const std::uint32_t byte_rate = rate * block_align;

  std::array<char, 44> h{};
  put_tag(h, 0, "RIFF");
  put_u32(h, 4, 36 + data_bytes);  // everything after this field
  put_tag(h, 8, "WAVE");

  put_tag(h, 12, "fmt ");
  put_u32(h, 16, 16);        // fmt chunk size for PCM
  put_u16(h, 20, 1);         // format tag: 1 == PCM, uncompressed
  put_u16(h, 22, channels);
  put_u32(h, 24, rate);
  put_u32(h, 28, byte_rate);
  put_u16(h, 32, block_align);
  put_u16(h, 34, bits);

  put_tag(h, 36, "data");
  put_u32(h, 40, data_bytes);

  m_out.write(h.data(), static_cast<std::streamsize>(h.size()));
}

}  // namespace glyphcade::audio
