#!/bin/sh
# Fuzz every target for SECONDS each (default 180): tools/fuzz.sh [seconds] [target ...]
# Targets: request script records script_record http backup image http_head ws_frame mqtt profile settings lz
# picoboot console locked http_small (test/fuzz_harnesses.h); a dictionary in test/fuzz-dict/<target>.dict is used when there is one. Needs clang with
# libFuzzer. Corpora grow in .pio/fuzz/<target>/; an input that stops a sanitizer is copied to
# test/fuzz-regressions/<target>/ (commit it with the fix: test_fuzz_corpus replays it in the host tests).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
secs=${1:-180}
[ $# -gt 0 ] && shift
targets=${*:-request script records script_record http backup image http_head ws_frame mqtt profile settings lz picoboot console locked http_small}

# can clang build a libFuzzer program? If not, say which package is missing.
probe=$(mktemp -d)
printf 'int LLVMFuzzerTestOneInput(const unsigned char* d, unsigned long n) { (void)d; return (int)(n & 0); }\n' >"$probe/f.c"
printf '#include <string>\nextern "C" int LLVMFuzzerTestOneInput(const unsigned char*, unsigned long) { return (int)std::string().size(); }\n' >"$probe/f.cpp"
# shellcheck disable=SC2086
c_err=$(clang -fsanitize=fuzzer,address,undefined $PT_FUZZ_FLAGS "$probe/f.c" -o "$probe/f" 2>&1) || true
# shellcheck disable=SC2086
cxx_err=$(clang++ -fsanitize=fuzzer,address,undefined $PT_FUZZ_FLAGS "$probe/f.cpp" -o "$probe/f" 2>&1) || true
rm -rf "$probe"
if [ -n "$c_err$cxx_err" ]; then
  major=$(clang --version | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -1)
  gcc=$(clang++ -v -xc++ /dev/null -fsyntax-only 2>&1 | sed -n 's/.*Selected GCC installation: .*\/\([0-9][0-9]*\)$/\1/p')
  echo "clang cannot build a libFuzzer program. Install:" >&2
  echo "$c_err$cxx_err" | grep -q "libclang_rt" && echo "  libclang-rt-$major-dev (clang's libFuzzer and sanitizer runtime)" >&2
  echo "$cxx_err" | grep -q "lstdc++\|file not found" && echo "  libstdc++-$gcc-dev (the C++ library of GCC $gcc, which clang uses)" >&2
  echo "(or point PT_FUZZ_FLAGS at a runtime elsewhere: tools/fuzz_env.py)" >&2
  exit 2
fi

python3 tools/fuzz_seeds.py
pio run -e fuzz >/dev/null
status=0
for t in $targets; do
  dir=.pio/fuzz/$t
  mkdir -p "$dir/corpus"
  echo "== $t ($secs s)"
  dict=
  [ -f "test/fuzz-dict/$t.dict" ] && dict="-dict=test/fuzz-dict/$t.dict"
  # shellcheck disable=SC2086
  if ! PT_FUZZ_TARGET=$t .pio/build/fuzz/program "$dir/corpus" -max_total_time="$secs" -artifact_prefix="$dir/" $dict >"$dir/log.txt" 2>&1; then
    mkdir -p "test/fuzz-regressions/$t"
    crash=$( (ls "$dir"/crash-* "$dir"/leak-* "$dir"/timeout-* "$dir"/oom-* 2>/dev/null || true) | head -1)
    if [ -n "$crash" ]; then
      name="crash-$(sha1sum "$crash" | cut -c1-12)"
      cp "$crash" "test/fuzz-regressions/$t/$name"
      echo "   FOUND: test/fuzz-regressions/$t/$name (log: $dir/log.txt)"
    fi
    grep -m3 -E "ERROR|runtime error|SUMMARY" "$dir/log.txt" || true
    status=1
  else
    tail -1 "$dir/log.txt"
  fi
done
exit $status
