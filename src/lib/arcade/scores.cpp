#include <glyphcade/arcade/scores.hpp>

#include <charconv>
#include <fstream>
#include <ios>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

namespace glyphcade::scores {

namespace {

// The three diagnostics this file can produce, spelled out in one place so the
// 7-bit rule at the top of scores.hpp is checkable by reading four lines rather
// than auditing every return.
//
// ⚠ Fixed literals. No path, no error_code::message(), no file content — see
// that header comment for what breaks otherwise (test/11selector's cell sweep).
constexpr std::string_view kBadVersion =
    "scores: unrecognised file version - not saving";
constexpr std::string_view kUnreadable =
    "scores: file not readable - this session only";
constexpr std::string_view kUnwritable = "scores: could not save";

// True iff `value` beats `held` in the given direction. Equality is NOT an
// improvement, which is what keeps flush() from marking a file dirty on a
// re-record of the same number.
[[nodiscard]] auto beats(long long value, long long held, Better better)
    -> bool {
  return better == Better::Lower ? value < held : value > held;
}

// A distinct temp name per writer.
//
// ⚠ NOT decoration, and not interchangeable with a fixed "scores.tmp". Two
// arcades flushing at once would otherwise open the SAME temp file, interleave
// their lines into it, and rename the mixture over the target — the one way this
// design can produce a file that was never any process's view of the records.
//
// random_device rather than getpid(): there is no POSIX header anywhere under
// src/ or include/ today, and a unique filename is not the reason to add one.
[[nodiscard]] auto temp_beside(const std::filesystem::path& file)
    -> std::filesystem::path {
  std::random_device dev;
  std::ostringstream name;
  name << file.filename().string() << ".tmp." << std::hex << dev() << dev();
  return std::filesystem::path{file}.replace_filename(name.str());
}

// `slug key value` — all three or the line is not a record. Returns false for a
// blank line, a comment, a missing field, a non-numeric value, or trailing
// garbage after the number.
[[nodiscard]] auto parse_line(const std::string& line, std::string& slug,
                             std::string& key, long long& value) -> bool {
  std::istringstream in{line};
  std::string extra;
  if (!(in >> slug >> key)) return false;
  std::string number;
  if (!(in >> number)) return false;
  if (in >> extra) return false;  // a fourth field means we do not understand it

  const char* first = number.data();
  const char* last = first + number.size();
  const auto [end, ec] = std::from_chars(first, last, value);
  return ec == std::errc{} && end == last;
}

}  // namespace

Store::Store(std::filesystem::path file) : m_file(std::move(file)) { load(); }

// Errors are dropped on purpose: by the time a destructor runs there is no
// terminal left to report to. The reportable flush is the Shell's, on game exit.
Store::~Store() { (void)write_out(); }

auto Store::load() -> void {
  if (m_file.empty()) return;

  std::error_code ec;
  if (!std::filesystem::exists(m_file, ec) || ec) {
    // An absent file is a NEW store, not a failure. This is the first-run path
    // and it must be silent — a notice here would greet every new player.
    return;
  }

  std::ifstream in{m_file};
  if (!in) {
    m_load_error = std::string{kUnreadable};
    m_readonly = true;
    return;
  }

  std::string line;
  if (!std::getline(in, line) || line != kHeader) {
    // ⚠ Refuse the whole file rather than reading what we can. An unknown
    // version is a file some FUTURE glyphcade wrote, and the one thing worse
    // than not reading a player's records is overwriting them with our
    // narrower understanding of them. Read-only, records still kept in memory,
    // and flush() will say so out loud rather than silently discarding.
    m_load_error = std::string{kBadVersion};
    m_readonly = true;
    return;
  }

  while (std::getline(in, line)) {
    std::string slug;
    std::string key;
    long long value = 0;
    // A malformed line is SKIPPED, not fatal — unlike a bad header. A crash
    // during a write can leave a truncated last line, and that must not cost a
    // player every record above it.
    if (!parse_line(line, slug, key, value)) continue;
    // Direction is not in the file: nothing on disk needs it, because the games
    // supply it on every record() and flush() merges with what it was told.
    // Higher is the placeholder until then, and it is never used for a
    // comparison that a game has not already re-stated.
    m_mine[slug][key] = Entry{.value = value, .better = Better::Higher};
  }
}

auto Store::get(std::string_view slug, std::string_view key) const
    -> std::optional<long long> {
  const auto game = m_mine.find(slug);
  if (game == m_mine.end()) return std::nullopt;
  const auto entry = game->second.find(key);
  if (entry == game->second.end()) return std::nullopt;
  return entry->second.value;
}

auto Store::record(std::string_view slug, std::string_view key, long long value,
                   Better better) -> bool {
  auto& keys = m_mine[std::string{slug}];
  const auto held = keys.find(key);

  if (held != keys.end()) {
    // The direction is re-stated even when the value loses, because a value
    // loaded from the file arrived without one and this is where it learns it.
    held->second.better = better;
    if (!beats(value, held->second.value, better)) return false;
    held->second.value = value;
  } else {
    keys.emplace(std::string{key}, Entry{.value = value, .better = better});
  }

  m_dirty = true;
  return true;
}

auto Store::flush() -> std::expected<void, std::string> {
  return write_out();
}

auto Store::write_out() -> std::expected<void, std::string> {
  // Memory-only, or nothing new to say. Both are successes: "no file
  // configured" is a valid state, not a degraded one.
  if (m_file.empty() || !m_dirty) return {};
  // We read a file we do not understand. Refusing to write is the whole point
  // of m_readonly, and saying so is the difference between a degraded mode and
  // a silent one.
  if (m_readonly) return std::unexpected(std::string{kBadVersion});

  // ⚠ RE-READ, then merge. This is what makes two arcades open at once
  // additive rather than last-writer-wins: keys this process never touched come
  // back off disk and are written out again, and a key both touched resolves to
  // the better value in the direction the recording game named. Drop this and
  // every flush publishes only what this process happens to know.
  Table merged;
  {
    std::ifstream in{m_file};
    std::string line;
    if (in && std::getline(in, line) && line == kHeader) {
      while (std::getline(in, line)) {
        std::string slug;
        std::string key;
        long long value = 0;
        if (!parse_line(line, slug, key, value)) continue;
        merged[slug][key] = Entry{.value = value, .better = Better::Higher};
      }
    }
  }
  for (const auto& [slug, keys] : m_mine) {
    for (const auto& [key, entry] : keys) {
      auto& slot = merged[slug];
      const auto held = slot.find(key);
      if (held == slot.end() ||
          beats(entry.value, held->second.value, entry.better)) {
        slot[key] = entry;
      }
    }
  }

  std::error_code ec;
  const std::filesystem::path parent = m_file.parent_path();
  if (!parent.empty()) {
    // Created here rather than in the constructor, so a session that never
    // records leaves nothing behind.
    std::filesystem::create_directories(parent, ec);
    if (ec) return std::unexpected(std::string{kUnwritable});
  }

  const std::filesystem::path temp = temp_beside(m_file);
  {
    std::ofstream out{temp, std::ios::trunc};
    if (!out) return std::unexpected(std::string{kUnwritable});
    out << kHeader << '\n';
    for (const auto& [slug, keys] : merged) {
      for (const auto& [key, entry] : keys) {
        out << slug << ' ' << key << ' ' << entry.value << '\n';
      }
    }
    out.flush();
    if (!out) {
      out.close();
      std::filesystem::remove(temp, ec);
      return std::unexpected(std::string{kUnwritable});
    }
  }

  // ⚠ rename, not a write in place. A reader either sees the old file whole or
  // the new file whole; there is no instant at which scores is half a file.
  std::filesystem::rename(temp, m_file, ec);
  if (ec) {
    std::error_code drop;
    std::filesystem::remove(temp, drop);
    return std::unexpected(std::string{kUnwritable});
  }

  m_dirty = false;
  return {};
}

auto resolve_path(std::string_view xdg_data_home, std::string_view home)
    -> std::filesystem::path {
  // XDG_DATA_HOME wins outright when set, per the basedir spec — including when
  // HOME is also set, which is the case on every desktop.
  if (!xdg_data_home.empty()) {
    return std::filesystem::path{xdg_data_home} / "glyphcade" / "scores";
  }
  if (!home.empty()) {
    return std::filesystem::path{home} / ".local" / "share" / "glyphcade" /
           "scores";
  }
  // Neither set: memory-only. Guessing a path from the cwd would scatter score
  // files wherever the binary happened to be run.
  return {};
}

}  // namespace glyphcade::scores
