#pragma once

// term-game — the high-score store: the last of the four services DESIGN.md
// named for GameContext, and the only one that touches a file.
//
// It waited deliberately. gitea #14 says "pick this up when the SECOND scoring
// game lands, not the first", because a format chosen before there is anything
// to persist is a format that gets migrated — and one game's scores can be
// modelled wrong in a way only a second game reveals. That prediction paid off:
// 2048 wants a best SCORE (higher wins) while Minesweeper wants a best TIME per
// difficulty (lower wins), so a record here is a keyed value with a DIRECTION,
// and it would have been a single integer if only Minesweeper had existed.
//
// ⚠ NO TERMFORGE HEADER, and none may be added. Errors are std::string, and the
// Shell translates one into an ErrorEvent, because the Shell is the layer that
// owns a terminal. Same discipline as audio/sink.hpp, board.hpp and ring.hpp.
//
// ⚠ NO getenv, HERE OR IN scores.cpp. resolve_path() takes the two directories
// as strings and stays pure; only src/bin/main.cpp reads the environment. That
// is not tidiness — it means the LIBRARY CANNOT NAME A REAL FILE, so a
// default-constructed Shell is memory-only and no future edit can make the test
// suite start writing into a developer's $HOME.
//
// ⚠ EVERY DIAGNOSTIC STRING BELOW IS A FIXED 7-BIT LITERAL, and that is a hard
// constraint rather than a style note. A load failure becomes an ErrorEvent, an
// ErrorEvent becomes m_notice, and m_notice is painted on the selector's footer
// row — which test/11selector sweeps CELL BY CELL for any byte >= 0x80
// ("the selector screen is 7-bit at the ASCII tier"). So a message must never
// carry a filesystem path (arbitrary bytes from the environment), a
// std::error_code::message() (locale-translated, non-ASCII on a de_DE box), or
// any file content. The only dynamic values that ever reach the screen from
// here are integers. path() exists for a future tool, never for an error.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace termgame::scores {

// Which way a record improves. Stored per entry, not merely passed per call —
// flush() re-reads the file and merges, so it must know the direction for a key
// it did not itself record this session.
enum class Better : std::uint8_t { Higher, Lower };

// The first line of the file, and the whole of the versioning scheme. Refusing
// an unknown version is what makes a v2 free to change everything below it.
inline constexpr std::string_view kHeader = "# term-game scores v1";

// The file format, in full:
//
//     # term-game scores v1
//     2048 best_score 20488
//     2048 best_tile 2048
//     minesweeper best_time_easy 42
//
// `slug key value`, whitespace-separated, one record per line, sorted. Chosen
// greppable and hand-editable over compact because at this scale — tens of
// lines, ever — being able to read it with cat and fix it with a text editor is
// worth more than bytes. The slug is GameMeta::slug, which all_games.cpp
// already static_asserts unique across the registry, so the key space is
// trustworthy without this file having to check.
class Store {
 public:
  // Memory-only: records are kept and compared, and nothing is ever written.
  // This is the state every test and every default-constructed Shell is in.
  Store() = default;

  // Reads `file` if it exists. NEVER THROWS and never fails to construct: an
  // absent file is a new store, and an unreadable or wrong-version one is a
  // degraded store that still answers get() and record(). Interrogate
  // load_error() afterwards — degradation is an event, not a silence.
  //
  // ⚠ Creates nothing. No directory, no file, no writability probe, so a
  // session that never records leaves no trace on disk and the constructor
  // makes at most one read syscall. Write failures surface at flush(), which is
  // why the Shell reports from two places rather than one.
  explicit Store(std::filesystem::path file);

  // Best-effort flush, errors discarded — the only teardown hook there is.
  // Ctrl+C and an unwinding exception both reach here via ~Shell, and neither
  // has anywhere to put an error message by then.
  ~Store();

  // The Shell owns exactly one by value and GameContext hands out a Recorder
  // pointing AT that one. Copying would silently give a game its own store,
  // whose records would then be written by whichever destructor ran last.
  Store(const Store&) = delete;
  auto operator=(const Store&) -> Store& = delete;

  [[nodiscard]] auto get(std::string_view slug, std::string_view key) const
      -> std::optional<long long>;

  // Returns true iff this improved on what was held. MONOTONE BY CONSTRUCTION,
  // and that is what makes the call sites trivial: 2048 records after every
  // move without caring that undo lowers Board::score(), because a worse value
  // cannot displace a better one. Callers need no "is this final" flag and no
  // end-of-run hook.
  auto record(std::string_view slug, std::string_view key, long long value,
              Better better) -> bool;

  // Re-read, merge, write a unique temp sibling, rename over the target.
  //
  // ⚠ NOT ON THE FRAME PATH. The Shell calls this once per game exit and once
  // at teardown; a flush per improvement would be a write per 2048 move.
  [[nodiscard]] auto flush() -> std::expected<void, std::string>;

  // Empty unless the constructor found the file unreadable or wrong-version.
  [[nodiscard]] auto load_error() const noexcept -> const std::string& {
    return m_load_error;
  }

  // ⚠ For tooling and tests. NEVER put this in a user-visible message — see the
  // 7-bit rule at the top of this file.
  [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& {
    return m_file;
  }

  // True when a record has been made that flush() has not yet written.
  [[nodiscard]] auto dirty() const noexcept -> bool { return m_dirty; }

 private:
  struct Entry {
    long long value{};
    Better better{Better::Higher};
  };
  // std::less<> on BOTH levels, so get() and record() look up a string_view
  // without materialising a std::string to do it.
  using Keys = std::map<std::string, Entry, std::less<>>;
  using Table = std::map<std::string, Keys, std::less<>>;

  auto load() -> void;
  [[nodiscard]] auto write_out() -> std::expected<void, std::string>;

  // Empty means memory-only, and it is the null value the whole design leans
  // on: no branch anywhere else has to ask whether persistence is configured.
  std::filesystem::path m_file;
  Table m_mine;
  // The file exists but we must not write it (bad version). Records still
  // accumulate in memory so a session is playable; flush() reports rather than
  // clobbering someone's file or dropping their records silently.
  bool m_readonly{false};
  bool m_dirty{false};
  std::string m_load_error;
};

// What a GAME is handed. Copyable, pointer-sized, and safe when empty.
//
// This is audio::Player transposed, for the same reason: the empty case is the
// design. A game writes
//
//     ctx.scores().record(kSlug, "best_score", score, Better::Higher);
//
// with no null check and no has_scores(), and "this session persists nothing"
// stays a property of the store rather than a shape every call site carries.
//
// It also exposes no flush(). WHEN to write is the Shell's I/O policy — it owns
// the frame path and knows which frames can afford a syscall — exactly as
// audio() hands out a Player rather than the Engine.
class Recorder {
 public:
  Recorder() = default;
  explicit Recorder(Store* store) noexcept : m_store(store) {}

  [[nodiscard]] auto get(std::string_view slug, std::string_view key) const
      -> std::optional<long long> {
    return m_store != nullptr ? m_store->get(slug, key) : std::nullopt;
  }

  auto record(std::string_view slug, std::string_view key, long long value,
              Better better) const -> bool {
    return m_store != nullptr && m_store->record(slug, key, value, better);
  }

  [[nodiscard]] auto empty() const noexcept -> bool {
    return m_store == nullptr;
  }

 private:
  Store* m_store{nullptr};
};

// $XDG_DATA_HOME/term-game/scores, else $HOME/.local/share/term-game/scores,
// else empty — and empty means memory-only, which is the honest answer for a
// process with neither variable set.
//
// Takes both directories as arguments and reads no environment, so it is
// unit-testable with strings and so that core physically cannot resolve a real
// path. main.cpp supplies them.
[[nodiscard]] auto resolve_path(std::string_view xdg_data_home,
                                std::string_view home)
    -> std::filesystem::path;

}  // namespace termgame::scores
