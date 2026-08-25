if (NOT TARGET ${PROJECT_NAME}_core)
  message(FATAL_ERROR
    "src/lib/games/solitaire is entered from src/lib/CMakeLists.txt only, "
    "after ${PROJECT_NAME}_core is defined. It is not independently buildable.")
endif ()

add_library(${PROJECT_NAME}_game_solitaire STATIC
  board.cpp
  solitaire.cpp
)

target_link_libraries(${PROJECT_NAME}_game_solitaire
  PUBLIC ${PROJECT_NAME}_core
)

glyphcade_embed_asset(
  ${PROJECT_NAME}_game_solitaire
  solitaire_neon_atlas
  ${PROJECT_SOURCE_DIR}/assets/solitaire/neon-atlas.png
)
glyphcade_embed_asset(
  ${PROJECT_NAME}_game_solitaire
  solitaire_classic_atlas
  ${PROJECT_SOURCE_DIR}/assets/solitaire/classic-atlas.png
)
