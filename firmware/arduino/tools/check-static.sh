#!/bin/sh
# Static analysis gate (`make check-cpp` in VS Code terms): clang-tidy (.clang-tidy) and cppcheck on this
# project's sources; exits non-zero on any finding. A finding is fixed, or suppressed where it is, with the
# reason:   clang-tidy: // NOLINT(<check>) <reason>       cppcheck: // cppcheck-suppress <id> <reason>
#
# clang-tidy runs on the host build's compile commands (`pio run -e check -t compiledb`: the controller, the
# protocol, the show engine and script checker, HTTP, the simulator); the board-only files (arduino/*.cpp) are
# compiled out there. It needs clang-tidy 18 or newer (PlatformIO's own is 15 and cannot read this computer's
# C library headers): `pip install clang-tidy` into PlatformIO's Python (~/.platformio/penv/bin/pip), or set
# CLANG_TIDY. cppcheck runs through `pio check -e check` (PlatformIO installs it).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
# PlatformIO's Python first (tools/setup.sh puts clang-tidy 21 there; a distribution's may be older), then PATH
penv_tidy="$HOME/.platformio/penv/bin/clang-tidy"
if [ -z "$CLANG_TIDY" ]; then
  if [ -x "$penv_tidy" ]; then CLANG_TIDY=$penv_tidy; else CLANG_TIDY=$(command -v clang-tidy || echo "$penv_tidy"); fi
fi
if ! "$CLANG_TIDY" --version >/dev/null 2>&1; then
  echo "clang-tidy not found: ~/.platformio/penv/bin/pip install clang-tidy (or set CLANG_TIDY)" >&2
  exit 2
fi
pio run -e check -t compiledb >/dev/null
files=$(python3 -c "
import json
for e in json.load(open('compile_commands.json')):
    f = e['file']
    if f.startswith(('lib/proptroller/src/', 'src/')) and '/arduino/' not in f:
        print(f)")
# the host compiler's own include directories (clang-tidy does not ask g++ for them)
CXX=${CXX:-g++}
incs=""
for d in $(echo | "$CXX" -xc++ -E -v - 2>&1 | sed -n '/#include <...> search starts/,/End of search/p' | grep '^ /'); do
  incs="$incs --extra-arg=-isystem$d"
done
status=0
echo "== clang-tidy ($("$CLANG_TIDY" --version | grep -o 'version [0-9.]*'))"
# the GCC-only warning flags in the compile commands mean nothing to clang: its own warnings off (-w), the
# checks of .clang-tidy on
echo "$files" | xargs -P "$(nproc)" -n 1 "$CLANG_TIDY" -p . --quiet --extra-arg=-w $incs > .pio/clang-tidy.log 2>&1 || status=1
grep -E "warning:|error:" .pio/clang-tidy.log | sort -u || true
echo "== cppcheck"
pio check -e check --fail-on-defect low --skip-packages -s || status=1
[ $status -eq 0 ] && echo "static analysis: no findings"
exit $status
