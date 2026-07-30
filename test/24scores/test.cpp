// The score store: its format, its merge, its degraded modes — and the one
// Shell-level claim that only a file can make.
//
// ⚠ Most of this file needs no terminal at all, by construction. scores.hpp names
// no termforge type (its error channel is std::string precisely so it does not),
// so the unit cases here structurally cannot build a Screen. Same discipline as
// test/14minesweeper and test/17audio-sink.
//
// Files go to std::filesystem::temp_directory_path() under a per-case name and
// are removed by a scope guard, so a failing case leaves nothing behind and two
// cases cannot collide. The unwritable case does NOT use chmod: it puts a regular
// FILE where the store needs a directory, which fails identically for every user
// including root, so the case does not quietly pass in a container.
//
// Two things worth knowing before editing:
//
//   * The direction is NOT in the file format. It is re-stated by the recording
//     game on every record(), which is why a value loaded from disk can be merged
//     correctly without disk having to remember how. "the direction survives a
//     reload" below is what holds that.
//   * The last case is the only one that constructs a Shell, and it exists
//     because the ordering of the three startup notices had NO automated check
//     before this PR — a bad-version scores file is the first deterministic way
//     to make a second notice fire.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <termgame/arcade/registry.hpp>
#include <termgame/arcade/scores.hpp>
#include <termgame/arcade/shell.hpp>

namespace {

using termgame::Shell;
using termgame::scores::Better;
using termgame::scores::Store;

// Removes its file — and anything the store wrote beside it — on the way out,
// however the case ends. The same shape as test/17audio-sink's TempWav.
class TempScores {
 public:
  explicit TempScores(const std::string& name)
      : m_path(std::filesystem::temp_directory_path() /
               ("termgame-test-" + name + ".scores")) {
    wipe();
  }
  ~TempScores() { wipe(); }
  TempScores(const TempScores&) = delete;
  auto operator=(const TempScores&) -> TempScores& = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path& {
    return m_path;
  }

  [[nodiscard]] auto exists() const -> bool {
    std::error_code ec;
    return std::filesystem::exists(m_path, ec);
  }

  [[nodiscard]] auto text() const -> std::string {
    std::ifstream in{m_path};
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
  }

  auto write(const std::string& contents) const -> void {
    std::ofstream out{m_path, std::ios::trunc};
    out << contents;
  }

 private:
  auto wipe() const -> void {
    std::error_code ec;  // noexcept: a destructor must not throw
    std::filesystem::remove(m_path, ec);
    // A failed flush can leave a temp sibling behind, and a stray one would make
    // the next run of this suite look like it had leaked.
    for (const auto& entry : std::filesystem::directory_iterator{
             m_path.parent_path(), ec}) {
      const std::string name = entry.path().filename().string();
      if (name.starts_with(m_path.filename().string() + ".tmp.")) {
        std::error_code drop;
        std::filesystem::remove(entry.path(), drop);
      }
    }
  }

  std::filesystem::path m_path;
};

// A Shell pointed at a chosen scores file. The only place in this file that
// needs a terminal.
class Probe final : public Shell {
 public:
  using Shell::screen;

  explicit Probe(std::filesystem::path scores)
      : Shell(std::make_unique<termgame::audio::NullSink>(), std::move(scores)) {
    set_frame_ms(0);  // see the comment in test/10render
  }

  auto step(int frames = 1) -> void { test_run_frames(frames, 60, 20, &m_sink); }

 private:
  std::string m_sink;
};

[[nodiscard]] auto row_text(Probe& app, int y) -> std::string {
  std::string out;
  for (int x = 0; x < app.screen().cols(); ++x) out += app.screen().at(x, y).text;
  return out;
}

// Every byte of every cell must be 7-bit. Same helper, same reason, as
// test/11selector and test/15minesweeper-ui.
[[nodiscard]] auto all_seven_bit(Probe& app) -> bool {
  auto& s = app.screen();
  for (int y = 0; y < s.rows(); ++y) {
    for (int x = 0; x < s.cols(); ++x) {
      for (const char c : s.at(x, y).text) {
        if (static_cast<unsigned char>(c) >= 0x80) return false;
      }
    }
  }
  return true;
}

[[nodiscard]] auto lines_of(const std::string& text) -> std::vector<std::string> {
  std::vector<std::string> out;
  std::string line;
  for (const char c : text) {
    if (c == '\n') {
      out.push_back(line);
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty()) out.push_back(line);
  return out;
}

}  // namespace

TEST_CASE("a record survives a write and a fresh read", "[scores]") {
  const TempScores file{"roundtrip"};

  {
    Store store{file.path()};
    REQUIRE(store.record("2048", "best_score", 20488, Better::Higher));
    REQUIRE(store.dirty());
    REQUIRE(store.flush());
    REQUIRE_FALSE(store.dirty());
  }

  // The format is a contract, not merely a detail: it is documented in
  // scores.hpp as hand-editable, so a player may reasonably open it.
  const auto lines = lines_of(file.text());
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0] == "# term-game scores v1");
  REQUIRE(lines[1] == "2048 best_score 20488");

  const Store reopened{file.path()};
  REQUIRE(reopened.get("2048", "best_score") == 20488);
  REQUIRE_FALSE(reopened.get("2048", "best_tile").has_value());
  REQUIRE_FALSE(reopened.get("minesweeper", "best_score").has_value());
}

TEST_CASE("Higher and Lower disagree about what an improvement is", "[scores]") {
  Store store;  // memory-only: the comparison needs no file

  REQUIRE(store.record("2048", "best_score", 100, Better::Higher));
  REQUIRE_FALSE(store.record("2048", "best_score", 50, Better::Higher));
  REQUIRE(store.get("2048", "best_score") == 100);
  REQUIRE(store.record("2048", "best_score", 150, Better::Higher));
  REQUIRE(store.get("2048", "best_score") == 150);

  REQUIRE(store.record("minesweeper", "best_time_easy", 100, Better::Lower));
  REQUIRE_FALSE(store.record("minesweeper", "best_time_easy", 150, Better::Lower));
  REQUIRE(store.get("minesweeper", "best_time_easy") == 100);
  REQUIRE(store.record("minesweeper", "best_time_easy", 50, Better::Lower));
  REQUIRE(store.get("minesweeper", "best_time_easy") == 50);

  // Equality is not an improvement, which is what keeps a re-record of the same
  // number from marking the store dirty and provoking a pointless write.
  REQUIRE_FALSE(store.record("2048", "best_score", 150, Better::Higher));
}

TEST_CASE("a record that only ever improves cannot be walked backwards",
          "[scores]") {
  // The property 2048 leans on: it records after EVERY move, including the ones
  // that follow an undo, and undo genuinely lowers Board::score(). If record()
  // were not monotone the game would need an end-of-run hook, and every game
  // after it would need one too.
  Store store;
  REQUIRE(store.record("2048", "best_score", 1024, Better::Higher));
  for (const int worse : {512, 0, 1023, 8}) {
    REQUIRE_FALSE(store.record("2048", "best_score", worse, Better::Higher));
    REQUIRE(store.get("2048", "best_score") == 1024);
  }
}

TEST_CASE("an unrecognised version is refused rather than clobbered",
          "[scores]") {
  const TempScores file{"badversion"};
  const std::string original =
      "# term-game scores v9\n2048 best_score 999999\nsomething else\n";
  file.write(original);

  Store store{file.path()};
  // Degradation is an event, not a silence.
  REQUIRE_FALSE(store.load_error().empty());
  // Nothing was read out of a file we do not understand.
  REQUIRE_FALSE(store.get("2048", "best_score").has_value());

  // The session still plays and still keeps records in memory...
  REQUIRE(store.record("2048", "best_score", 10, Better::Higher));
  REQUIRE(store.get("2048", "best_score") == 10);
  // ...but flush REPORTS rather than either writing or silently dropping. The
  // one thing worse than not saving a player's records is overwriting a newer
  // format's file with our narrower understanding of it.
  const auto written = store.flush();
  REQUIRE_FALSE(written);
  REQUIRE(written.error() == store.load_error());
  REQUIRE(file.text() == original);
}

TEST_CASE("an unwritable target is an error, not an abort", "[scores]") {
  const TempScores blocker{"unwritable"};
  // A regular file where the store needs a DIRECTORY. This fails for every user
  // including root, unlike a chmod, so the case cannot quietly pass in a
  // container that runs as uid 0.
  blocker.write("not a directory\n");

  Store store{blocker.path() / "term-game" / "scores"};
  REQUIRE(store.record("2048", "best_score", 1, Better::Higher));

  const auto written = store.flush();
  REQUIRE_FALSE(written);
  REQUIRE_FALSE(written.error().empty());
  // The arcade must still be playable: the record is held in memory and the
  // process is not on fire.
  REQUIRE(store.get("2048", "best_score") == 1);
}

TEST_CASE("two stores over one file merge rather than clobber", "[scores]") {
  const TempScores file{"merge"};

  Store first{file.path()};
  Store second{file.path()};  // opened before either has written: neither sees
                              // the other's records in memory

  REQUIRE(first.record("2048", "best_score", 100, Better::Higher));
  REQUIRE(first.flush());

  REQUIRE(second.record("minesweeper", "best_time_easy", 42, Better::Lower));
  REQUIRE(second.flush());

  // ⚠ The re-read inside flush() is the single line this pins. Without it the
  // second flush publishes only what the second store knows, and 2048's record
  // — written by a process that is still running — is gone.
  const Store reopened{file.path()};
  REQUIRE(reopened.get("2048", "best_score") == 100);
  REQUIRE(reopened.get("minesweeper", "best_time_easy") == 42);
}

TEST_CASE("a same-key race resolves to the better value, in each direction",
          "[scores]") {
  const TempScores file{"race"};

  {
    Store first{file.path()};
    Store second{file.path()};
    REQUIRE(first.record("2048", "best_score", 500, Better::Higher));
    REQUIRE(first.flush());
    // The loser writes second and writes a WORSE value. Last-writer-wins would
    // publish 100; merging on the stored direction keeps 500.
    REQUIRE(second.record("2048", "best_score", 100, Better::Higher));
    REQUIRE(second.flush());

    const Store reopened{file.path()};
    REQUIRE(reopened.get("2048", "best_score") == 500);
  }

  const TempScores lower{"race-lower"};
  {
    Store first{lower.path()};
    Store second{lower.path()};
    REQUIRE(first.record("minesweeper", "best_time_hard", 30, Better::Lower));
    REQUIRE(first.flush());
    // ⚠ Here the LOSING value is the larger one, and this is the case that makes
    // storing the direction load-bearing rather than decorative: a merge that
    // assumed Higher would publish 90 and call it a record.
    REQUIRE(second.record("minesweeper", "best_time_hard", 90, Better::Lower));
    REQUIRE(second.flush());

    const Store reopened{lower.path()};
    REQUIRE(reopened.get("minesweeper", "best_time_hard") == 30);
  }
}

TEST_CASE("a direction survives a reload, because the game restates it",
          "[scores]") {
  // The file format deliberately does NOT store the direction. That is safe only
  // because a value read back from disk is never compared until the recording
  // game supplies the direction again — this is what pins that.
  const TempScores file{"direction"};

  {
    Store store{file.path()};
    REQUIRE(store.record("minesweeper", "best_time_easy", 30, Better::Lower));
    REQUIRE(store.flush());
  }

  Store reopened{file.path()};
  REQUIRE(reopened.get("minesweeper", "best_time_easy") == 30);
  // A worse (slower) time must lose even though the loaded entry arrived with no
  // direction attached to it.
  REQUIRE_FALSE(reopened.record("minesweeper", "best_time_easy", 90,
                                Better::Lower));
  REQUIRE(reopened.get("minesweeper", "best_time_easy") == 30);
  REQUIRE(reopened.record("minesweeper", "best_time_easy", 5, Better::Lower));
  REQUIRE(reopened.get("minesweeper", "best_time_easy") == 5);
}

TEST_CASE("a memory-only store writes nothing at all", "[scores]") {
  const TempScores file{"memory-only"};

  Store store;  // no path: the state every test and every default Shell is in
  REQUIRE(store.record("2048", "best_score", 7, Better::Higher));
  REQUIRE(store.get("2048", "best_score") == 7);
  REQUIRE(store.flush());  // a success, not a degraded mode
  REQUIRE(store.path().empty());
  REQUIRE_FALSE(file.exists());
}

TEST_CASE("a malformed line is skipped and the rest of the file survives",
          "[scores]") {
  const TempScores file{"malformed"};
  file.write(
      "# term-game scores v1\n"
      "2048 best_score 100\n"
      "minesweeper\n"                      // too few fields
      "minesweeper best_time_easy\n"       // no value
      "minesweeper best_time_easy abc\n"   // not a number
      "minesweeper best_time_easy 5 6\n"   // a field we do not understand
      "\n"                                 // blank
      "minesweeper best_time_hard 61\n");  // the survivor after the wreckage

  const Store store{file.path()};
  // ⚠ Skipped, NOT fatal — unlike a bad header. A crash mid-write can truncate
  // the last line, and that must not cost a player every record above it.
  REQUIRE(store.load_error().empty());
  REQUIRE(store.get("2048", "best_score") == 100);
  REQUIRE(store.get("minesweeper", "best_time_hard") == 61);
  REQUIRE_FALSE(store.get("minesweeper", "best_time_easy").has_value());
}

TEST_CASE("resolve_path prefers XDG_DATA_HOME and falls back to HOME",
          "[scores]") {
  using termgame::scores::resolve_path;
  namespace fs = std::filesystem;

  REQUIRE(resolve_path("/xdg", "/home/p") == fs::path{"/xdg/term-game/scores"});
  // XDG wins even when HOME is set, which is the case on every desktop.
  REQUIRE(resolve_path("/xdg", "") == fs::path{"/xdg/term-game/scores"});
  REQUIRE(resolve_path("", "/home/p") ==
          fs::path{"/home/p/.local/share/term-game/scores"});
  // Neither set: empty, which the Store reads as memory-only. Guessing from the
  // cwd would scatter score files wherever the binary was run.
  REQUIRE(resolve_path("", "").empty());
}

TEST_CASE("the scores notice does not displace the colour notice",
          "[scores][selector]") {
  // ⚠ This is the first automated check on the ORDER of the startup notices.
  // shell.cpp used to claim test/11selector asserted it; that was never true —
  // that test sweeps for non-ASCII bytes and has no notice-text assertion at all.
  // The order matters because m_notice keeps only the most recent message, and a
  // bad-version scores file is the first deterministic way to make two fire.
  const TempScores file{"notice-order"};
  file.write("# term-game scores v9\n");

  Probe app{file.path()};
  app.step();

  // test_run_frames installs a FallbackDriver reporting no colour, so the
  // ASCII-tier notice always fires here — and it must be the one that survives,
  // because it describes what the whole screen will look like for the session.
  const std::string footer = row_text(app, 18);  // rows - 2
  REQUIRE(footer.find("no colour capability") != std::string::npos);
  REQUIRE(footer.find("scores") == std::string::npos);

  // The Shell did read the file, whatever the footer ended up showing — this is
  // what stops the case above from passing on a Shell that never opened it.
  REQUIRE_FALSE(app.scores().load_error().empty());

  // And the scores message is held to the 7-bit rule like everything else that
  // can reach this row — which is why it may never name the file it failed on.
  REQUIRE(all_seven_bit(app));
}

TEST_CASE("a store that cannot save says so on the way back to the menu",
          "[scores][selector]") {
  // ⚠ The case the one above cannot make, and the reason the Shell reports from
  // TWO places. On the ASCII tier the colour notice outranks the startup scores
  // notice by design, so a player on a bare terminal would otherwise never learn
  // their score file is unusable. They learn it on the first game exit instead,
  // because m_notice is cleared on game ENTRY and the flush failure is the next
  // thing to write it.
  //
  // A load failure is knowable at startup; a WRITE failure is not, without a
  // probe write the constructor deliberately does not make. Delete the flush
  // report in apply_transitions() and this goes red.
  const TempScores file{"flush-notice"};
  file.write("# term-game scores v9\n");

  Probe app{file.path()};
  app.step();

  const auto games = termgame::all_games();
  int index = -1;
  for (std::size_t i = 0; i < games.size(); ++i) {
    if (games[i].meta.slug == "2048") index = static_cast<int>(i);
  }
  REQUIRE(index >= 0);
  while (app.selector_index() < index) {
    app.dispatch_event(
        termforge::Event{termforge::KeyEvent{.key = termforge::Key::Down}});
  }
  app.dispatch_event(
      termforge::Event{termforge::KeyEvent{.key = termforge::Key::Enter}});
  REQUIRE(app.state() == Shell::State::InGame);

  // 2048 records after every applied move, so one keypress is enough to make the
  // store dirty — and a dirty read-only store is what makes flush() report.
  app.dispatch_event(
      termforge::Event{termforge::KeyEvent{.key = termforge::Key::Left}});
  app.dispatch_event(
      termforge::Event{termforge::KeyEvent{.key = termforge::Key::Right}});

  app.dispatch_event(
      termforge::Event{termforge::KeyEvent{.key = termforge::Key::Escape}});
  app.step();
  REQUIRE(app.state() == Shell::State::Selector);

  const std::string footer = row_text(app, 18);
  REQUIRE(footer.find("scores") != std::string::npos);
  REQUIRE(all_seven_bit(app));
  // And it really was refused rather than written.
  REQUIRE(file.text() == "# term-game scores v9\n");
}
