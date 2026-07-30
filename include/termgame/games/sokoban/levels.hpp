#pragma once

// term-game — Sokoban: the twenty bundled levels.
//
// ⚠ NO TERMFORGE HEADER, like level.hpp and board.hpp.
//
// Its own header for the same reason tetris/pieces.hpp is its own header: this
// is the part of the reference taken verbatim, and that boundary is worth
// seeing in a file list. The maps below are byte-for-byte the twenty in
// HTML-Games' sokoban/js/levels.js, in its order, under its names.
//
// ── The pars are OURS, and that is not a preference ─────────────────────────
//
// The reference ships a `par` on every level and its README states they are
// "derived from the optimal solution length". Neither half survives contact:
//
//   * `par` is never read. It appears in levels.js and nowhere in game.js, so
//     twenty numbers ship as dead data.
//   * They are not optimal. Every one of the twenty was BFS-solved for the true
//     minimum player-move count before this file was written, and only THREE of
//     the reference's numbers are right (The Vault, Four Corners, Master
//     Catalog). Nine are loose. And EIGHT are below the mathematical optimum —
//     levels 7-13 and 15 set targets that cannot be reached by any sequence of
//     moves, in a game where the par is the only thing telling you how you did.
//
//   lvl:      1   2   3   4   5   6   7   8   9  10
//   ref:      5   8   8   9  12  12  16  14  14  18
//   true:     3   5   5   6   8  11  17  16  15  19
//                             ^^  ^^  ^^  ^^   impossible
//   lvl:     11  12  13  14  15  16  17  18  19  20
//   ref:     14  26  26  26  28  30  36  36  36  40
//   true:    16  41  34  26  37  28  35  36  36  38
//            ^^  ^^  ^^      ^^   impossible
//
// The reference cites its verifier as /tmp/sokoban_design.py, a path outside
// its own repository, so the claim was never reproducible either. Ours is: the
// solver is a plain breadth-first search over (player, sorted boxes) and
// test/31sokoban re-derives the same numbers for the small levels at test time,
// where a BFS is cheap enough to run in a test.
//
// ⚠ Same family as Tetris' reference, whose README speed table was off by one
// level from its own code. GENERATE FROM CODE, NOT FROM PROSE — this is the
// second epic in a row where the reference's documentation was the wrong half.

#include <span>
#include <string_view>

namespace termgame::sokoban {

struct PackEntry {
  std::string_view name;
  std::span<const std::string_view> rows;
  int par;  // measured optimum, in player moves. See the header note.
};

namespace detail {

//  1. First Push -- reference par 5, measured optimum 3
inline constexpr std::string_view kMap01[] = {
    "########",
    "#      #",
    "#@$  . #",
    "#      #",
    "########",
};

//  2. Two Steps -- reference par 8, measured optimum 5
inline constexpr std::string_view kMap02[] = {
    "########",
    "# @    #",
    "# $    #",
    "#      #",
    "#  .   #",
    "#      #",
    "########",
};

//  3. Two Texts -- reference par 8, measured optimum 5
inline constexpr std::string_view kMap03[] = {
    "########",
    "#      #",
    "# .    #",
    "#  $   #",
    "#  $@  #",
    "#  .   #",
    "#      #",
    "########",
};

//  4. The Antechamber -- reference par 9, measured optimum 6
inline constexpr std::string_view kMap04[] = {
    "########",
    "#   .  #",
    "#  $   #",
    "#@ $.  #",
    "#      #",
    "########",
};

//  5. Around the Pillar -- reference par 12, measured optimum 8
inline constexpr std::string_view kMap05[] = {
    "########",
    "#  .   #",
    "# ##   #",
    "# @$   #",
    "#    # #",
    "#      #",
    "########",
};

//  6. Inner Chamber -- reference par 12, measured optimum 11
inline constexpr std::string_view kMap06[] = {
    "##########",
    "#        #",
    "# ###### #",
    "# # .  # #",
    "# # $  # #",
    "# # $@ # #",
    "# # .  # #",
    "# ###### #",
    "#        #",
    "##########",
};

//  7. Crossroads -- reference par 16, measured optimum 17
inline constexpr std::string_view kMap07[] = {
    "###########",
    "#####.#####",
    "#####$#####",
    "#         #",
    "#.$  @  $.#",
    "#         #",
    "#####$#####",
    "#####.#####",
    "###########",
};

//  8. Stacked Storage -- reference par 14, measured optimum 16
inline constexpr std::string_view kMap08[] = {
    "########",
    "#      #",
    "# $@   #",
    "# $    #",
    "#  .   #",
    "#  .   #",
    "#      #",
    "########",
};

//  9. The Reading Room -- reference par 14, measured optimum 15
inline constexpr std::string_view kMap09[] = {
    "#########",
    "#       #",
    "# .   . #",
    "#   $   #",
    "# $ @ $ #",
    "#   .   #",
    "#       #",
    "#########",
};

// 10. Twin Pillars -- reference par 18, measured optimum 19
inline constexpr std::string_view kMap10[] = {
    "##########",
    "#        #",
    "# .  ##  #",
    "#  $ ## $#",
    "#@   ##  #",
    "# $  ## .#",
    "#  .     #",
    "##########",
};

// 11. The Stacks -- reference par 14, measured optimum 16
inline constexpr std::string_view kMap11[] = {
    "##########",
    "#        #",
    "# . . .  #",
    "#        #",
    "# $ $ $@ #",
    "#        #",
    "##########",
};

// 12. The Courtyard -- reference par 26, measured optimum 41
inline constexpr std::string_view kMap12[] = {
    "############",
    "#          #",
    "#  . ## .  #",
    "#          #",
    "# $  ##  $ #",
    "#    ##    #",
    "# $  ##  $ #",
    "#          #",
    "#  . ## .  #",
    "#     @    #",
    "############",
};

// 13. Library Floor -- reference par 26, measured optimum 34
inline constexpr std::string_view kMap13[] = {
    "############",
    "#          #",
    "# .  ..  . #",
    "#          #",
    "# $$ ## $$ #",
    "#    ##    #",
    "#     @    #",
    "############",
};

// 14. The Vault -- reference par 26, measured optimum 26
inline constexpr std::string_view kMap14[] = {
    "##########",
    "#   ..   #",
    "#        #",
    "#  $  $  #",
    "#   $$   #",
    "#   @    #",
    "#        #",
    "#   ..   #",
    "##########",
};

// 15. Concentric -- reference par 28, measured optimum 37
inline constexpr std::string_view kMap15[] = {
    "##########",
    "#  .  .  #",
    "#        #",
    "# $    $ #",
    "#   ##   #",
    "#  @##   #",
    "# $    $ #",
    "#        #",
    "#  .  .  #",
    "##########",
};

// 16. Quartet -- reference par 30, measured optimum 28
inline constexpr std::string_view kMap16[] = {
    "##########",
    "#  .  .  #",
    "#        #",
    "# $ $$ $ #",
    "#   @    #",
    "#        #",
    "#  .  .  #",
    "##########",
};

// 17. Shelves -- reference par 36, measured optimum 35
inline constexpr std::string_view kMap17[] = {
    "###########",
    "#         #",
    "# . . . . #",
    "#         #",
    "## # # # ##",
    "#         #",
    "# $ $ $ $ #",
    "#@        #",
    "###########",
};

// 18. Four Corners -- reference par 36, measured optimum 36
inline constexpr std::string_view kMap18[] = {
    "##########",
    "#.######.#",
    "#        #",
    "#  $  $  #",
    "#   @    #",
    "#  $  $  #",
    "#        #",
    "#.######.#",
    "##########",
};

// 19. Master Catalog -- reference par 36, measured optimum 36
inline constexpr std::string_view kMap19[] = {
    "############",
    "#  .    .  #",
    "#          #",
    "# $$ ## $$ #",
    "#    ##    #",
    "## #    # ##",
    "#    @     #",
    "#          #",
    "#  .    .  #",
    "############",
};

// 20. The Final Archive -- reference par 40, measured optimum 38
inline constexpr std::string_view kMap20[] = {
    "###########",
    "#         #",
    "# . . . . #",
    "#         #",
    "## # # # ##",
    "#         #",
    "# $ $ $ $ #",
    "#         #",
    "#    @    #",
    "###########",
};

inline constexpr PackEntry kPack[] = {
    {"First Push", kMap01, 3},
    {"Two Steps", kMap02, 5},
    {"Two Texts", kMap03, 5},
    {"The Antechamber", kMap04, 6},
    {"Around the Pillar", kMap05, 8},
    {"Inner Chamber", kMap06, 11},
    {"Crossroads", kMap07, 17},
    {"Stacked Storage", kMap08, 16},
    {"The Reading Room", kMap09, 15},
    {"Twin Pillars", kMap10, 19},
    {"The Stacks", kMap11, 16},
    {"The Courtyard", kMap12, 41},
    {"Library Floor", kMap13, 34},
    {"The Vault", kMap14, 26},
    {"Concentric", kMap15, 37},
    {"Quartet", kMap16, 28},
    {"Shelves", kMap17, 35},
    {"Four Corners", kMap18, 36},
    {"Master Catalog", kMap19, 36},
    {"The Final Archive", kMap20, 38},
};

}  // namespace detail

// The pack, in menu order.
[[nodiscard]] constexpr auto pack() noexcept -> std::span<const PackEntry> {
  return detail::kPack;
}

[[nodiscard]] constexpr auto level_count() noexcept -> int {
  return static_cast<int>(pack().size());
}

}  // namespace termgame::sokoban
