#!/bin/sh
# End-to-end check of a host build: start the C++ simulated board (pio run -e sim), then
#  1. the protocol client tools/ptclient.py (conformance: UDP discovery, info / state / announce / params
#     checked field by field against the golden files, HTTP RPC, a WebSocket session), the calls the desktop
#     app makes;
#     then MQTT with tools/mqtt_check.py (its own small broker: connect, broker down and up, a broker that never
#     answers, the will); then the board link with tools/link_check.py (two simulators: a tell line peer to peer over
#     UDP on localhost, refused, signed, a setting, then over MQTT);
#  2. a firmware update over HTTP (POST /api/v1/update with a `ptclient.py image` file): the board restarts
#     into that version and build, pending;
#  3. the unit tests of the shared Record effect module (node --test web-test/take-test.mjs: take.js), then the
#     browser tests from firmware/web-test (the phone UI, the Live tab when things go wrong (live-test), the Shows tab (shows-test), Settings, Settings -> Wi-Fi (wifi-test), aliases, the Script tab,
#     Record effect (record-test), the Script tab's colours (highlight-test) and the Firmware tab's update-test and update-swap-test; needs node, npm and a Chromium or Chrome: set CHROMIUM).
# Needs python3 (standard library only). Scratch files go to $WORK (default: a temporary directory);
# PORT / DISC choose the simulator's HTTP and discovery ports.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
PORT=${PORT:-18080}
DISC=${DISC:-47811}
cd "$here"
pio run -e sim >/dev/null
.pio/build/sim/program --port "$PORT" --discovery-port "$DISC" >"$WORK/sim.log" 2>&1 &
pid=$!
trap 'kill $pid 2>/dev/null' EXIT
sleep 1
python3 tools/ptclient.py conformance "127.0.0.1:$PORT" "$DISC"
# MQTT against a broker of our own: connect, broker down / up, a broker that never answers, the will
python3 tools/mqtt_check.py .pio/build/sim/program
# the board link: two simulated boards, one's show script tells the other (UDP on localhost, then MQTT)
python3 tools/link_check.py .pio/build/sim/program
# a firmware update with a simulator image, on a fresh board
kill $pid 2>/dev/null; wait $pid 2>/dev/null || true
.pio/build/sim/program --port "$PORT" --discovery-port "$DISC" >"$WORK/sim-update.log" 2>&1 &
pid=$!
sleep 1
python3 tools/ptclient.py image "$WORK/update.bin" --version 0.9.0 --build chk0001 >/dev/null
python3 - "$WORK/update.bin" "$PORT" <<'PY'
import json, sys, time
sys.path.insert(0, "tools")
import ptclient
addr = "127.0.0.1:%s" % sys.argv[2]
answer = ptclient.update(addr, open(sys.argv[1], "rb").read())
assert answer == {"v": 1, "ok": True, "result": {"kind": "done"}}, answer
time.sleep(2)
info = json.loads(ptclient.http_call(addr, "GET", "/api/v1/info")[2])
assert (info["fw_version"], info["fw_build"], info["fw_impl"], info["update"]["state"]) == ("0.9.0", "chk0001", "cpp", "pending"), info
print("ok: update over HTTP: restarted into 0.9.0 chk0001, pending")
PY
# take.js (Record effect: take -> effect script), shared with Specter Studio: plain node, no packages
node --test ../web-test/take-test.mjs ../web-test/blocks-test.mjs ../web-test/words-test.mjs
# program --check: show-script messages as FILE:LINE:COL: error: message (VS Code's "Check effect scripts" task
# turns them into clickable problems with the pattern in .vscode/tasks.json, checked here against the output)
printf 'effect porch:\n  wait for trig1\n  out5 = on\n\n    rly1 = 50%%\n' >"$WORK/bad script.txt"
printf 'effect porch:\n  wait for trig1\n  out1 = on\n  wait 1\n  out1 = off\n' >"$WORK/good.txt"
set +e
.pio/build/sim/program --check "$WORK/good.txt" "$WORK/bad script.txt" >"$WORK/check.out" 2>&1
rc=$?
set -e
python3 - "$WORK" "$rc" "$here/../../.vscode/tasks.json" <<'PY'
import json, re, sys
work, rc, tasks = sys.argv[1], int(sys.argv[2]), sys.argv[3]
got = open(work + "/check.out").read().splitlines()
want = [work + "/bad script.txt:3:3: error: there is no out5 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.",
        work + "/bad script.txt:5:5: error: rly1 can only be on or off, not 50%. Dimming works on out1 to out4."]
assert got == want and rc == 1, (rc, got)
src = re.sub(r"^\s*//.*$", "", open(tasks).read(), flags=re.M)
for label in ("Check effect scripts", "Check this effect script"):
    t = next(t for t in json.loads(src)["tasks"] if t["label"] == label)
    m = t["problemMatcher"][0]
    p = m["pattern"]
    rx = re.compile(p["regexp"])
    hits = [rx.match(line) for line in got]
    assert all(hits), (label, got)
    assert [(h.group(p["file"]), h.group(p["line"]), h.group(p["column"]), h.group(p["severity"])) for h in hits] == \
        [(work + "/bad script.txt", "3", "3", "error"), (work + "/bad script.txt", "5", "5", "error")], label
print("ok: program --check prints FILE:LINE:COL: error: message (exit 1), and the VS Code tasks' problem pattern reads it")
PY
.pio/build/sim/program --check --dmx "$WORK/good.txt" ../show-script-examples/*.txt workspace-template/effects/*.txt >/dev/null
echo "ok: program --check passes the good script and every example (exit 0)"
if [ -n "$CHROMIUM" ]; then
  # each browser test on a fresh board (the client checks above ran a show)
  mkdir -p "$WORK/webtest"
  cp ../web-test/package.json ../web-test/*.mjs "$WORK/webtest/"
  (cd "$WORK/webtest" && npm install --silent >/dev/null)
  # update-test makes simulator images with ptclient.py and runs the board 10x fast (the 30 s self-check
  # takes 3 s)
  export PTCLIENT="$here/tools/ptclient.py"
  # update-swap-test: a board that installs the update at the restart, as the Pico W bootloader swaps it in
  # (off the network for 20 s of board time, 2 s at 10x)
  for t in ui-test live-test shows-test settings-test wifi-test alias-test script-test remote-script-test record-test views-test highlight-test update-test update-swap-test; do
    kill $pid 2>/dev/null; wait $pid 2>/dev/null || true
    opts="--speed 1"
    [ "$t" = update-test ] && opts="--speed 10"
    [ "$t" = update-swap-test ] && opts="--speed 10 --swap-ms 20000"
    # shellcheck disable=SC2086
    .pio/build/sim/program --port "$PORT" --discovery-port "$DISC" $opts >"$WORK/sim-$t.log" 2>&1 &
    pid=$!
    sleep 1
    echo "== $t"
    (cd "$WORK/webtest" && node $t.mjs "$WORK/webtest" "http://127.0.0.1:$PORT/")
  done
else
  echo "browser tests skipped: set CHROMIUM (e.g. CHROMIUM=/usr/bin/google-chrome)"
fi
