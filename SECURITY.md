# Security Policy

## Supported versions

glyphcade is pre-1.0 and moves fast. Only the latest tagged release is
supported; fixes land on `main` and go out in the next tag.

## Reporting a vulnerability

Please report privately via
[GitHub Security Advisories](https://github.com/gobha-me/glyphcade/security/advisories/new)
rather than opening a public issue.

Expect an acknowledgement within a few days. There is no bounty.

## Realistic scope

glyphcade is a local terminal application. It opens no sockets, runs no server,
and takes no network input. The interesting surface is small and mostly about
what it *reads and writes*:

- **The score file** — `$XDG_DATA_HOME/glyphcade/scores`, or
  `~/.local/share/glyphcade/scores`. It is parsed at startup, so a malformed or
  hostile file is a real input. It is plain text with a versioned header and
  unknown versions are refused.
- **`GLYPHCADE_SCORES`** overrides that path verbatim, and
  **`GLYPHCADE_AUDIO_WAV`** names a file the audio sink writes to. Both are
  environment-controlled paths.
- **Terminal escape sequences** written to a tty, and terminal responses parsed
  back during capability detection.
- **Level data** compiled into the binary.

Memory-safety bugs in parsing or rendering are in scope. CI runs ASan, UBSan and
TSan on every push, so please mention it if you have a reproducer that these do
not catch.

Out of scope: anything requiring an attacker who can already run code as your
user, and the behaviour of your terminal emulator.
