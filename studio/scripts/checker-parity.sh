#!/bin/sh
# SS-C5 against another firmware commit's golden set, to see what a firmware change brings to Studio's checker:
#   scripts/checker-parity.sh [ref] [repo]     (default: HEAD of this repository; pass a branch, tag or commit,
#                                              and optionally another clone, to try a firmware change)
# Copies <ref>:firmware/show-script-golden into a temporary folder (the working tree is not touched) and runs
# Studio's parity tests on it. The board checker is still the simulator built from the working tree, so a
# difference here is what that ref brings; the tests on the working tree's golden set (cargo test, npm test)
# must always pass.
set -e
cd "$(dirname "$0")/.."
REF=${1:-HEAD}
REPO=${2:-$(git rev-parse --show-toplevel)}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git -C "$REPO" archive "$REF" firmware/show-script-golden | tar -x -C "$TMP"
export SHOW_SCRIPT_GOLDEN="$TMP/firmware/show-script-golden"
echo "golden set: $REF of $REPO"
status=0
cargo test -q -p studio-core --test checker_parity every_golden_case || status=1
npx vitest run src/effects/parity.test.ts || status=1
exit $status
