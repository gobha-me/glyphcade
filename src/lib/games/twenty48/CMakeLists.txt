# ── 2048: one game, one static library ──────────────────────────────────────
# Entered from src/lib/CMakeLists.txt, inside its ${PROJECT_NAME}_BUILD_LIB guard
# and after ${PROJECT_NAME}_core exists.
#
# ⚠ The directory is twenty48 and the game's slug is "2048". Not a mistake: a slug
# is a user-visible id and may begin with a digit, a C++ namespace may not, and
# the directory follows the namespace so that
# #include <glyphcade/games/twenty48/board.hpp> matches glyphcade::twenty48::Board.
# The CMake target follows the directory. See AGENTS.md.
if (NOT TARGET ${PROJECT_NAME}_core)
  message(FATAL_ERROR
    "src/lib/games/twenty48 is entered from src/lib/CMakeLists.txt only, "
    "after ${PROJECT_NAME}_core is defined. It is not independently buildable.")
endif ()

# Three TUs, not two: anim.cpp is the tween, which minesweeper had no need for.
# Headers stay in include/glyphcade/games/twenty48/ — cmake/install.cmake ships
# public headers with one install(DIRECTORY include/ ...), so a header that leaves
# include/ leaves the package.
add_library(${PROJECT_NAME}_game_twenty48 STATIC
  board.cpp
  anim.cpp
  twenty48.cpp
)

# PUBLIC core: twenty48.cpp calls audio::Engine::play through GameContext, and
# core carries include/, cxx_std_23 and termforge::lib PUBLIC so board.cpp and
# anim.cpp find their own headers. PUBLIC rather than PRIVATE because
# ${PROJECT_NAME}_roster compiles arcade/all_games.cpp and needs core's include
# directory transitively — PRIVATE archives fine and then fails to compile the
# roster.
target_link_libraries(${PROJECT_NAME}_game_twenty48
  PUBLIC ${PROJECT_NAME}_core
)

# ⚠ No GLYPHCADE_WITH_AUDIO, no rtaudio, no link to ${PROJECT_NAME}_lib. A game
# plays a sound by asking GameContext for the Player; it never learns whether the
# sink is real. And core sits BELOW the games, so glyphcade::Shell is not on this
# target's link line — a game cannot reach the Shell even by accident.
