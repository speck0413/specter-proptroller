#!/usr/bin/env python3
"""The board link end to end: two simulated boards, one's show script tells the other (standard library only).

    python3 tools/link_check.py [path/to/sim/program]      (default .pio/build/sim/program; pio run -e sim first)

Starts mqtt_check.py's small broker and two simulators, "Front Door" and "Porch", each on its own discovery port,
sending their peer-to-peer frames to each other (--link-peers). Both report their events to the broker, which is
how the checks read their logs. Then:
  1. peer to peer (board_link = wifi, UDP on localhost): Front Door's TRIG1 runs `tell porch blackout`; Porch's
     blackout starts (OUT1 on), both boards log it, no "no answer";
  2. with remote_free_effects empty on Porch the start is refused, and Front Door logs why;
  3. with the same link_key on both (signed, challenged): it starts again; with another key on Porch: nothing,
     and Front Door logs "no answer from porch";
  4. a setting (`tell porch pixel_speed = 150`): Porch uses it, says it is not saved;
  5. over MQTT (board_link = mqtt): the same start through the broker, answered.
"""
import json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mqtt_check import Broker, free_port, http  # noqa: E402

DOORBELL = "effect doorbell:\n  wait for trig1\n  tell porch blackout\n  wait 1\n"
SETTING = "effect doorbell:\n  wait for trig1\n  tell porch pixel_speed = 150\n  wait 1\n"
PORCH = "effect blackout:\n  wait for trig1\n  out1 = on\n  wait 2\n  out1 = off\n"


def free_udp():
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def rpc(port, op, **kw):
    r = http(port, "POST", "/api/v1/rpc", dict({"v": 1, "id": 1, "op": op}, **kw))
    assert r.get("ok"), r
    return r.get("result")


def params(port, **kv):
    return rpc(port, "set_params", values=[{"key": k, "value": v} for k, v in kv.items()])


def out1(port):
    return next(o["level"] for o in http(port, "GET", "/api/v1/state")["outputs"] if o["channel"] == "OUT1")


def ring(port):
    rpc(port, "sim_input", input="TRIG1", active=True)
    time.sleep(0.25)
    rpc(port, "sim_input", input="TRIG1", active=False)


def logged(broker, board, text, after, timeout=4.0):
    pred = lambda p: text in json.loads(p).get("data", "") if b'"event":"log"' in p else False
    return broker.wait("proptroller/%s/event" % board, after=after, timeout=timeout, pred=pred)


def never_logged(broker, board, text, after):
    for t, tp, p, _ in broker.log:
        if tp == "proptroller/%s/event" % board and t >= after and text.encode() in p:
            raise AssertionError("%s logged %r" % (board, text))


def reconnected(broker, board, after, timeout=15.0):
    """The board's session ended (its will: online false) and came back (online true) after `after`."""
    topic = "proptroller/%s/online" % board
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        down = [t for t, tp, p, _ in list(broker.log) if tp == topic and t >= after and p == b"false"]
        if down and any(tp == topic and t > down[0] and p == b"true" for t, tp, p, _ in list(broker.log)):
            return
        time.sleep(0.05)
    raise AssertionError("%s did not reconnect" % board)


def wait_for(cond, timeout, what):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return
        time.sleep(0.02)
    raise AssertionError(what)


def check(program):
    broker = Broker()
    pa, pb, da, db = free_port(), free_port(), free_udp(), free_udp()
    procs = []

    def start(name, port, disc, peer):
        procs.append(subprocess.Popen([program, "--port", str(port), "--discovery-port", str(disc), "--name", name,
                                       "--id", name.lower().replace(" ", ""), "--link-peers", "127.0.0.1:%d" % peer,
                                       "--mqtt", "127.0.0.1:%d" % broker.port], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))

    start("Front Door", pa, da, db)
    start("Porch", pb, db, da)
    try:
        time.sleep(0.6)
        broker.wait("proptroller/front-door/online", timeout=10)
        broker.wait("proptroller/porch/online", timeout=10)
        t = time.monotonic()
        for p in (pa, pb):
            params(p, board_link="wifi")
        # a new way out: each session ends and comes back with its topics (5 s backoff)
        reconnected(broker, "front-door", t)
        reconnected(broker, "porch", t)
        assert rpc(pa, "set_script", text=DOORBELL, sounds={})["ok"]
        assert rpc(pb, "set_script", text=PORCH, sounds={})["ok"]
        params(pb, remote_free_effects="blackout")
        time.sleep(2.2)  # the boot lockout

        # 1. peer to peer
        t = time.monotonic()
        ring(pa)
        wait_for(lambda: out1(pb) == 1000, 1.5, "Porch's OUT1 did not come on")
        took = time.monotonic() - t
        logged(broker, "front-door", "tell porch blackout: sent (Wi-Fi)", t)
        logged(broker, "porch", "remote start by front-door: effect \"blackout\"", t)
        time.sleep(2.5)
        never_logged(broker, "front-door", "no answer", t)
        print("ok: peer to peer: Front Door's tell started Porch's blackout, answered (OUT1 on %.0f ms after TRIG1 closed, its 100 ms debounce included)" % (took * 1000))

        # 2. refused
        params(pb, remote_free_effects="")
        time.sleep(1.2)
        t = time.monotonic()
        ring(pa)
        logged(broker, "front-door", "tell porch blackout: porch refused: blackout: ", t)
        assert out1(pb) == 0
        print("ok: refused by Porch (remote trigger off): Front Door logged why")
        params(pb, remote_free_effects="blackout")

        # 3. keys
        params(pa, link_key="shared-key-1")
        params(pb, link_key="shared-key-1")
        time.sleep(1.2)
        t = time.monotonic()
        ring(pa)
        wait_for(lambda: out1(pb) == 1000, 1.5, "signed: Porch's OUT1 did not come on")
        time.sleep(2.5)
        never_logged(broker, "front-door", "no answer", t)
        params(pb, link_key="another-key")
        time.sleep(0.5)
        t = time.monotonic()
        ring(pa)
        logged(broker, "front-door", "tell porch blackout: no answer from porch", t, timeout=5)
        assert out1(pb) == 0
        params(pb, link_key="shared-key-1")
        print("ok: with a shared key it runs (challenged, signed); with another key nothing runs and Front Door says so")

        # 4. a setting
        assert rpc(pa, "set_script", text=SETTING, sounds={})["ok"]
        time.sleep(1.2)
        t = time.monotonic()
        ring(pa)
        logged(broker, "porch", "front-door (board link, Wi-Fi) set pixel_speed = 150 (until a restart or a change here; not saved)", t)
        speed = next(v["value"] for v in rpc(pb, "get_params")["values"] if v["key"] == "pixel_speed")
        assert speed == 150, speed
        print("ok: a setting told peer to peer is used at once and not saved")

        # 5. over MQTT
        assert rpc(pa, "set_script", text=DOORBELL, sounds={})["ok"]
        t = time.monotonic()
        for p in (pa, pb):
            params(p, board_link="mqtt")
        reconnected(broker, "front-door", t)
        reconnected(broker, "porch", t)
        time.sleep(0.5)
        t = time.monotonic()
        ring(pa)
        wait_for(lambda: out1(pb) == 1000, 2.0, "MQTT: Porch's OUT1 did not come on")
        logged(broker, "front-door", "tell porch blackout: sent (MQTT)", t)
        msg = broker.wait("proptroller/front-door/link/answer", after=t, timeout=3)
        assert json.loads(msg)["ok"] is True, msg
        time.sleep(2.5)
        never_logged(broker, "front-door", "no answer", t)
        print("ok: over MQTT: proptroller/porch/control/blackout started it, answered on proptroller/front-door/link/answer")
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        broker.stop()


if __name__ == "__main__":
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, ".pio/build/sim/program"))
    print("link check passed")
