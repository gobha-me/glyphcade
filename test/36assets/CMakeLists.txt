# This directory owns its wiring because the proof PNG is embedded into the
# test target. The production helper is thereby exercised without making a
# diagnostic asset part of the arcade binary or inventing a GameMeta field.

message(STATUS "Configuring test setup ${TEST_NAME}")

add_executable(${TEST_NAME}
  ${PROJECT_SOURCE_DIR}/test/main.cpp
  ${SRCS}
)

target_include_directories(${TEST_NAME}
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(${TEST_NAME}
  PRIVATE Catch2::Catch2
)

if (TARGET ${PROJECT_NAME}::lib)
  target_link_libraries(${TEST_NAME} PRIVATE ${PROJECT_NAME}::lib)
endif ()

glyphcade_embed_asset(
  ${TEST_NAME}
  proof_card_back
  ${PROJECT_SOURCE_DIR}/assets/proof/card-back.png
)
