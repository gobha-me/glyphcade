# Vendored dependencies

## stb_image

- Upstream: <https://github.com/nothings/stb>
- File: `stb_image.h` v2.30
- Commit: `2c980bb59875b0d32144a71867fbdebb2f77cd20`
- Source path: `stb_image.h`
- SHA-256: `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3`
- License: `LICENSE.stb.md` (MIT/Public Domain dual choice; glyphcade uses MIT)

Only PNG decode is enabled in `src/lib/assets/png.cpp`; stdio entry points are
disabled. Update the header, digest, commit and notice together.
