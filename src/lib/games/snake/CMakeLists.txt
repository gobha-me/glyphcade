# ── Snake: one game, one static library ─────────────────────────────────────
# Entered from src/lib/CMakeLists.txt, inside its ${PROJECT_NAME}_BUILD_LIB guard
# and after ${PROJECT_NAME}_core exists.
#
# Directory, namespace, class and slug all agree here (`snake`), unlike 2048 —
# see games/twenty48/CMakeLists.txt for the case where they cannot.
if (NOT TARGET ${PROJECT_NAME}_core)
  message(FATAL_ERROR
    "src/lib/games/snake is entered from src/lib/CMakeLists.txt only, "
    "after ${PROJECT_NAME}_core is defined. It is not independently buildable.")
endif ()

# Two TUs, minesweeper's shape. There is no anim.cpp: Snake occupies whole cells,
# and a sub-cell tween in a character grid is a feel decision with no reference
# behind it. Headers stay in include/glyphcade/games/snake/ — cmake/install.cmake
# ships public headers with one install(DIRECTORY include/ ...), so a header that
# leaves include/ leaves the package.
add_library(${PROJECT_NAME}_game_snake STATIC
  board.cpp
  snake.cpp
)

# PUBLIC core: snake.cpp calls audio::Engine::play and scores::Recorder::record
# through GameContext, and core carries include/, cxx_std_23 and termforge::lib
# PUBLIC so board.cpp finds its own headers. PUBLIC rather than PRIVATE because
# ${PROJECT_NAME}_roster compiles arcade/all_games.cpp and needs core's include
# directory transitively — PRIVATE archives fine and then fails to compile the
# roster.
target_link_libraries(${PROJECT_NAME}_game_snake
  PUBLIC ${PROJECT_NAME}_core
)

# ⚠ No GLYPHCADE_WITH_AUDIO, no rtaudio, no link to ${PROJECT_NAME}_lib. A game
# plays a sound by asking GameContext for the Player; it never learns whether the
# sink is real. And core sits BELOW the games, so glyphcade::Shell is not on this
# target's link line — a game cannot reach the Shell even by accident.
