# Build-time half of glyphcade_embed_asset(). Writes a generated header and a
# C++ translation unit containing bounded hexadecimal string chunks. String
# literals are materially cheaper for the compiler than 1.5 million
# comma-separated integer initializers, while separate array elements stay
# below the standard's guaranteed literal-length limit.

cmake_minimum_required(VERSION 3.28)

foreach (_required INPUT HEADER SOURCE NAME)
  if (NOT DEFINED ${_required})
    message(FATAL_ERROR "embed_asset.cmake needs -D${_required}=")
  endif ()
endforeach ()
if (NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "embed_asset.cmake: input does not exist: ${INPUT}")
endif ()

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hex_length)
math(EXPR _byte_count "${_hex_length} / 2")

set(_body "")
set(_offset 0)
set(_chunk_count 0)
# 2048 bytes per literal: far below implementation string-literal limits, and
# only ~768 CMake iterations for the 1.5 MiB proof asset.
while (_offset LESS _hex_length)
  math(EXPR _remaining "${_hex_length} - ${_offset}")
  if (_remaining GREATER 4096)
    set(_take 4096)
  else ()
    set(_take ${_remaining})
  endif ()
  string(SUBSTRING "${_hex}" ${_offset} ${_take} _chunk)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "\\\\x\\1" _escaped "${_chunk}")
  math(EXPR _chunk_bytes "${_take} / 2")
  string(APPEND _body
    "    std::string_view{\"${_escaped}\", ${_chunk_bytes}},\n")
  math(EXPR _offset "${_offset} + ${_take}")
  math(EXPR _chunk_count "${_chunk_count} + 1")
endwhile ()

get_filename_component(_header_dir "${HEADER}" DIRECTORY)
get_filename_component(_source_dir "${SOURCE}" DIRECTORY)
file(MAKE_DIRECTORY "${_header_dir}" "${_source_dir}")

file(WRITE "${HEADER}" "#pragma once\n\n#include <cstddef>\n#include <span>\n\nnamespace glyphcade::assets::embedded {\n[[nodiscard]] auto ${NAME}() noexcept -> std::span<const std::byte>;\n}\n")

file(WRITE "${SOURCE}" "#include <glyphcade/generated_assets/${NAME}.hpp>\n\n#include <array>\n#include <string_view>\n\nnamespace glyphcade::assets::embedded {\nnamespace {\nconstexpr std::array<std::string_view, ${_chunk_count}> kChunks{{\n${_body}}};\n}\n\nauto ${NAME}() noexcept -> std::span<const std::byte> {\n  static const std::array<std::byte, ${_byte_count}> data = [] {\n    std::array<std::byte, ${_byte_count}> result{};\n    std::size_t offset = 0;\n    for (const std::string_view chunk : kChunks) {\n      for (const unsigned char value : chunk) {\n        result[offset++] = static_cast<std::byte>(value);\n      }\n    }\n    return result;\n  }();\n  return data;\n}\n\n}\n")
