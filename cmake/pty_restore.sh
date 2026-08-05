#!/bin/bash
#
# Does the terminal come back when a frame throws?
#
# This is the assertion term-game#16 exists for, and the only one in the suite that
# looks at real escape bytes. test/21exception pins the contract headlessly —
# teardown() ran, the exception was rethrown, our boundary turned it into exit 1
# — but nothing there enters the alternate screen, so none of it can see the
# sequences a user's terminal actually receives. script(1) allocates a pty, so
# setup() takes the tty path and everything gets written down.
#
# Usage: pty_restore.sh <path-to-pty_restore_probe>
# Wired up in test/CMakeLists.txt as the `pty-restore` test.

set -euo pipefail

probe="${1:?usage: pty_restore.sh <probe-binary>}"
capture="$(mktemp)"
trap 'rm -f "$capture"' EXIT

# No stdin choreography: the probe exits by throwing on its second frame, so
# there is no keystroke to time against the capability probe (the hazard
# AGENTS.md warns about for the interactive recipes). timeout is the backstop
# for a regression that hangs instead of throwing.
timeout 20 script -q -c "$probe" "$capture" >/dev/null 2>&1 || true

# ⚠ Deliberately NOT asserting the exit status. Under the sanitizer toolchains
# a LeakSanitizer report at exit would change it, and `script` reports the
# child's status through its own anyway. The return-value contract is already
# pinned by test/21exception's "an escaping exception becomes exit 1"; what only
# a pty can show is the bytes, so bytes are all this script judges.

enter=$(grep -c $'\033\[?1049h' "$capture" || true)
leave=$(grep -c $'\033\[?1049l' "$capture" || true)

if [ "$enter" != "1" ]; then
  echo "FAIL: expected 1 alternate-screen enter (?1049h), got $enter" >&2
  echo "      the probe never got into the alt screen; nothing below is meaningful" >&2
  exit 1
fi

if [ "$leave" != "1" ]; then
  echo "FAIL: expected 1 alternate-screen leave (?1049l), got $leave" >&2
  echo "      a thrown frame left the terminal on the alternate screen with" >&2
  echo "      nothing restoring it at all — not run_loop(), not ~App()." >&2
  exit 1
fi

# The ordering claim — and the assertion that actually discriminates. The probe
# constructs its App outside the boundary precisely so that ~App() cannot be
# what restores the terminal; see test/pty_restore_probe.cpp. If termforge#71
# regresses, ~App() still leaves the alt screen at the end of main(), so the
# count above stays 1 and only the ORDER changes: the diagnostic gets written
# first, into a screen about to vanish. A count-only check passes right through
# that, which is why this is here.
#
# ⚠ The `|| true` on both is load-bearing, not defensive noise. Under
# `set -euo pipefail` a grep that matches nothing fails the pipeline, fails the
# assignment, and kills the script — with exit 1 and not one word printed. The
# "no fatal message" branch below would be unreachable, and the most likely
# real failure would report itself as silence. Found by mutation-testing this
# script, not by reading it.
leave_at=$(grep -abo $'\033\[?1049l' "$capture" | head -1 | cut -d: -f1 || true)
fatal_at=$(grep -abo 'glyphcade: fatal:' "$capture" | head -1 | cut -d: -f1 || true)

if [ -z "$fatal_at" ]; then
  echo "FAIL: no 'glyphcade: fatal:' in the capture" >&2
  echo "      the boundary in src/lib/arcade/exception_boundary.cpp did not run," >&2
  echo "      or the probe stopped throwing." >&2
  exit 1
fi

if [ "$leave_at" -ge "$fatal_at" ]; then
  echo "FAIL: fatal message at byte $fatal_at precedes the alt-screen leave at $leave_at" >&2
  echo "      the diagnostic was written into a screen that then vanished." >&2
  exit 1
fi

echo "PASS: ?1049h=1 ?1049l=1, leave at byte $leave_at precedes fatal at $fatal_at"
