# ── Tetris: one game, one static library ─────────────────────────────────────
# Entered from src/lib/CMakeLists.txt, inside its ${PROJECT_NAME}_BUILD_LIB guard
# and after ${PROJECT_NAME}_core exists.
#
# Directory, namespace, class and slug all agree here (`tetris`), unlike 2048 —
# see games/twenty48/CMakeLists.txt for the case where they cannot.
if (NOT TARGET ${PROJECT_NAME}_core)
  message(FATAL_ERROR
    "src/lib/games/tetris is entered from src/lib/CMakeLists.txt only, "
    "after ${PROJECT_NAME}_core is defined. It is not independently buildable.")
endif ()

# Two TUs, minesweeper's shape. There is no anim.cpp: pieces occupy whole cells,
# and the one animation this game does have — the line-clear flash — is a whole-
# cell effect the model already owns as a state, so there is nothing to tween.
# ⚠ FOUR headers here, not three: pieces.hpp carries the SRS tables, which are
# the one part of the reference taken verbatim, and having that boundary visible
# in a file list is worth a header. Headers stay in include/glyphcade/games/tetris/ — cmake/install.cmake
# ships public headers with one install(DIRECTORY include/ ...), so a header that
# leaves include/ leaves the package.
add_library(${PROJECT_NAME}_game_tetris STATIC
  board.cpp
  tetris.cpp
)

# PUBLIC core: tetris.cpp calls audio::Engine::play and scores::Recorder::record
# through GameContext, and core carries include/, cxx_std_23 and termforge::lib
# PUBLIC so board.cpp finds its own headers. PUBLIC rather than PRIVATE because
# ${PROJECT_NAME}_roster compiles arcade/all_games.cpp and needs core's include
# directory transitively — PRIVATE archives fine and then fails to compile the
# roster.
target_link_libraries(${PROJECT_NAME}_game_tetris
  PUBLIC ${PROJECT_NAME}_core
)

# ⚠ No GLYPHCADE_WITH_AUDIO, no rtaudio, no link to ${PROJECT_NAME}_lib. A game
# plays a sound by asking GameContext for the Player; it never learns whether the
# sink is real. And core sits BELOW the games, so glyphcade::Shell is not on this
# target's link line — a game cannot reach the Shell even by accident.
