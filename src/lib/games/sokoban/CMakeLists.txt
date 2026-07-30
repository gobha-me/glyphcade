# ── Sokoban: one game, one static library ───────────────────────────────────
# Entered from src/lib/CMakeLists.txt, inside its ${PROJECT_NAME}_BUILD_LIB guard
# and after ${PROJECT_NAME}_core exists.
#
# Directory, namespace, class and slug all agree here (`sokoban`), unlike 2048 —
# see games/twenty48/CMakeLists.txt for the case where they cannot.
if (NOT TARGET ${PROJECT_NAME}_core)
  message(FATAL_ERROR
    "src/lib/games/sokoban is entered from src/lib/CMakeLists.txt only, "
    "after ${PROJECT_NAME}_core is defined. It is not independently buildable.")
endif ()

# THREE TUs, where every other game has two. level.cpp is the parser, and it is
# separate from board.cpp because the format is separable from the rules: a
# level file is validated once, at load, and the rules never look at a character
# again. It is also the only part of this game a future level-file loader would
# reuse unchanged. Headers stay in include/termgame/games/sokoban/ —
# cmake/install.cmake ships public headers with one install(DIRECTORY include/
# ...), so a header that leaves include/ leaves the package.
add_library(${PROJECT_NAME}_game_sokoban STATIC
  board.cpp
  level.cpp
  sokoban.cpp
)

# PUBLIC core: sokoban.cpp calls audio::Engine::play and scores::Recorder::record
# through GameContext, and core carries include/, cxx_std_23 and termforge::lib
# PUBLIC so board.cpp and level.cpp find their own headers. PUBLIC rather than
# PRIVATE because ${PROJECT_NAME}_roster compiles arcade/all_games.cpp and needs
# core's include directory transitively — PRIVATE archives fine and then fails to
# compile the roster.
target_link_libraries(${PROJECT_NAME}_game_sokoban
  PUBLIC ${PROJECT_NAME}_core
)

# ⚠ No TERMGAME_WITH_AUDIO, no rtaudio, no link to ${PROJECT_NAME}_lib. A game
# plays a sound by asking GameContext for the Player; it never learns whether the
# sink is real. And core sits BELOW the games, so termgame::Shell is not on this
# target's link line — a game cannot reach the Shell even by accident.
#
# ⚠ termforge::lib arrives through core, PUBLIC. This is the first game to use a
# termforge WIDGET (MapWidget) rather than only Screen and Frame, and it needs no
# extra link line for it: MapWidget is part of termforge::lib like everything
# else.
