#!/bin/sh
# Regenerate the golden files from the C++ simulator: test/golden (the protocol session of test_golden, defined
# in test/test_golden/session.inc and test_main.cpp) and test/golden-console (the serial console and MQTT topics
# of test_console_golden). The tests write every message instead of comparing it (GOLDEN_WRITE,
# GOLDEN_WRITE_CONSOLE), the new files replace the old ones, and the tests then run again against them.
# The golden files are the protocol contract (firmware/PROTOCOL.md): review `git diff test/golden*` before
# committing; a change there is a change of the wire format.
# A new call in the session also needs the file counts in test_golden's session_matches_the_golden_files (66, 87,
# 123, 134, 138) bumped first: write mode stops at a count that no longer fits, and then nothing is copied.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/golden" "$tmp/golden-console"
GOLDEN_WRITE="$tmp/golden" GOLDEN_WRITE_CONSOLE="$tmp/golden-console" pio test -e native -f test_golden -f test_console_golden >/dev/null
rm -rf test/golden test/golden-console
cp -r "$tmp/golden" test/golden
cp -r "$tmp/golden-console" test/golden-console
pio test -e native -f test_golden -f test_console_golden | tail -4
git status --short test/golden test/golden-console
