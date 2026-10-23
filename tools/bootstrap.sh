#!/usr/bin/env bash
# Fetch what git does not carry: the aieda submodule, its Node deps and the Freerouting jar. Safe to rerun; offline once populated.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
say() { echo "bootstrap: $*"; }
[ -e aieda/.git ] || { say "cloning aieda"; git submodule update --init aieda; }
[ -x aieda/node_modules/.bin/tsx ] || { say "npm ci in aieda/"; (cd aieda && npm ci --no-audit --no-fund >/dev/null); }
. tools/freerouting.pin
JAR="aieda/tools/freerouting-$VERSION.jar"
if [ ! -f "$JAR" ] || { [ -n "$SHA256" ] && [ "$(sha256sum "$JAR" | cut -d' ' -f1)" != "$SHA256" ]; }; then
  say "downloading Freerouting $VERSION"; mkdir -p aieda/tools
  curl -fL --retry 3 -# -o "$JAR.part" "https://github.com/freerouting/freerouting/releases/download/v$VERSION/freerouting-$VERSION.jar"
  got="$(sha256sum "$JAR.part" | cut -d' ' -f1)"
  if [ -n "$SHA256" ] && [ "$got" != "$SHA256" ]; then rm -f "$JAR.part"; echo "bootstrap: checksum mismatch (got $got)" >&2; exit 1; fi
  mv "$JAR.part" "$JAR"
fi
# aieda v0.1.0 expects tools/freerouting-2.5.0.jar; alias another pinned version to that name.
[ "$VERSION" = "2.5.0" ] || ln -sfn "freerouting-$VERSION.jar" aieda/tools/freerouting-2.5.0.jar
say "ready"
