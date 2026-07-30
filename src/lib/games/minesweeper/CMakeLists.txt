# ── Minesweeper: one game, one static library ───────────────────────────────
# Entered from src/lib/CMakeLists.txt, inside its ${PROJECT_NAME}_BUILD_LIB guard
# and after ${PROJECT_NAME}_core exists. A game has no standalone configuration,
# and saying so here beats a wall of undefined symbols later.
if (NOT TARGET ${PROJECT_NAME}_core)
  message(FATAL_ERROR
    "src/lib/games/minesweeper is entered from src/lib/CMakeLists.txt only, "
    "after ${PROJECT_NAME}_core is defined. It is not independently buildable.")
endif ()

# Sources explicit, never globbed — same rule, and the same reason, as src/lib.
#
# The headers stay in include/termgame/games/minesweeper/ and deliberately did
# NOT move next to these sources. cmake/install.cmake ships every public header
# with one install(DIRECTORY include/ ...), so a header that leaves include/
# leaves the package; .clangd and every #include <termgame/games/...> in the tree
# assume that root too. The CMake target is what buys the isolation here. The
# directory move would buy nothing and cost a tree-wide rewrite.
add_library(${PROJECT_NAME}_game_minesweeper STATIC
  board.cpp
  minesweeper.cpp
)

# PUBLIC ${PROJECT_NAME}_core, and both halves of that matter.
#
# Symbols: minesweeper.cpp calls termgame::audio::Engine::play, which is defined
# in core's audio/engine.cpp. That one edge is why audio lives BELOW the games
# instead of up with the Shell — see the layering block in src/lib/CMakeLists.txt.
#
# Usage requirements: core carries include/, cxx_std_23 and termforge::lib
# PUBLIC, so this line is also how board.cpp finds its own header and how
# minesweeper.cpp finds <termforge/widgets/theme.hpp>.
#
# PUBLIC and not PRIVATE: ${PROJECT_NAME}_roster links this target and compiles
# arcade/all_games.cpp, which needs core's include directory. PRIVATE would still
# archive correctly — CMake records it as $<LINK_ONLY:...> — and then fail to
# compile the roster, which is a confusing place to land for a one-word mistake.
target_link_libraries(${PROJECT_NAME}_game_minesweeper
  PUBLIC ${PROJECT_NAME}_core
)

# ⚠ NO TERMGAME_WITH_AUDIO here, ever, and no rtaudio. A game plays a sound by
# asking GameContext for the Player; it never learns how the sink is built, or
# whether it is real. Either line would put a fourth name in front of
# cmake/check_export.cmake and undo gitea #13.
#
# ⚠ And NO link to ${PROJECT_NAME}_lib. That is the cycle this split exists to
# remove, and it is also what stops a game reaching the Shell by accident:
# core sits below the games, so termgame::Shell is not on this target's link
# line at all. AGENTS.md's "games never touch App, Terminal, or each other" is a
# link error now rather than a convention.
