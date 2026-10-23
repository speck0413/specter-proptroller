#!/usr/bin/env python3
"""MQTT end to end on the simulated board, with a tiny broker of our own (standard library only; no mosquitto).

    python3 tools/mqtt_check.py [path/to/sim/program]      (default .pio/build/sim/program; pio run -e sim first)

Starts the broker on a free port and the simulator pointed at it (--mqtt 127.0.0.1:PORT), then checks:
  1. connect: <p>/online is true (retained), <p>/state is retained and starts with boot_id and mqtt_seq, every
     setting is published, an rpc over MQTT is answered;
  2. broker down, then up: the board reconnects (5 s backoff), publishes online and the state again with the
     same boot_id and a higher mqtt_seq; meanwhile a show runs on time (HTTP);
  3. a broker that accepts the connection but never answers: the board gives up after 10 s and tries again;
     HTTP stays answering and a show runs on time;
  4. the board dies (SIGKILL): the broker publishes its will, online = false (retained).
The broker: MQTT 3.1.1, QoS 0, retained messages, + and # wildcards, wills, keep-alive; `--broker PORT` runs
just the broker (point a simulator at it with --mqtt 127.0.0.1:PORT and watch with --verbose).
"""
import json, os, signal, socket, struct, subprocess, sys, threading, time, urllib.request


# ---------------------------------------------------------------- the broker

def _read_exact(s, n):
    b = b""
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise ConnectionError("closed")
        b += c
    return b


def _read_packet(s):
    h = _read_exact(s, 1)[0]
    mult, n = 1, 0
    while True:
        d = _read_exact(s, 1)[0]
        n += (d & 0x7F) * mult
        mult *= 128
        if not d & 0x80:
            break
    return h, _read_exact(s, n) if n else b""


def _packet(h, body):
    n, enc = len(body), b""
    while True:
        d = n % 128
        n //= 128
        enc += bytes([d | (0x80 if n else 0)])
        if not n:
            break
    return bytes([h]) + enc + body


def _str(b, i):
    n = struct.unpack("!H", b[i:i + 2])[0]
    return b[i + 2:i + 2 + n], i + 2 + n


def _enc(s):
    return struct.pack("!H", len(s)) + s


def topic_matches(flt, topic):
    f, t = flt.split("/"), topic.split("/")
    for i, p in enumerate(f):
        if p == "#":
            return True
        if i >= len(t) or (p != "+" and p != t[i]):
            return False
    return len(f) == len(t)


class Broker:
    def __init__(self, port=0, silent=False, verbose=False):
        self.silent, self.verbose = silent, verbose  # silent: accept TCP, never answer
        self.retained, self.clients, self.log = {}, {}, []
        self.lock = threading.Lock()
        self.ls = socket.socket()
        self.ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.ls.bind(("127.0.0.1", port))
        self.ls.listen(8)
        self.port = self.ls.getsockname()[1]
        self.running = True
        threading.Thread(target=self._accept, daemon=True).start()

    def stop(self):
        self.running = False
        try:
            self.ls.shutdown(socket.SHUT_RDWR)  # wakes accept()
        except OSError:
            pass
        try:
            self.ls.close()
        except OSError:
            pass
        with self.lock:
            for s in list(self.clients):
                try:
                    s.shutdown(socket.SHUT_RDWR)
                    s.close()
                except OSError:
                    pass
            self.clients.clear()

    def _accept(self):
        while self.running:
            try:
                s, _ = self.ls.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(s,), daemon=True).start()

    def publish(self, topic, payload, retain):
        with self.lock:
            self.log.append((time.monotonic(), topic, payload, retain))
            if retain:
                if payload:
                    self.retained[topic] = payload
                else:
                    self.retained.pop(topic, None)
            targets = [(s, c) for s, c in self.clients.items() if any(topic_matches(f, topic) for f in c["subs"])]
        if self.verbose:
            print("  %s%s %s" % (topic, " (retained)" if retain else "", payload[:120]))
        pkt = _packet(0x30, _enc(topic.encode()) + payload)
        for s, _ in targets:
            try:
                s.sendall(pkt)
            except OSError:
                pass

    def _serve(self, s):
        will, clean = None, False
        try:
            s.settimeout(90)
            h, b = _read_packet(s)
            if h >> 4 != 1:
                return
            if self.silent:
                while self.running:  # read and drop, never answer
                    if not s.recv(4096):
                        return
            flags, i = b[7], 10
            cid, i = _str(b, i)
            if flags & 0x04:
                wt, i = _str(b, i)
                wp, i = _str(b, i)
                will = (wt.decode(), wp, bool(flags & 0x20))
            with self.lock:
                self.clients[s] = {"id": cid.decode(), "subs": []}
            s.sendall(_packet(0x20, b"\x00\x00"))
            while self.running:
                h, b = _read_packet(s)
                t = h >> 4
                if t == 3:  # PUBLISH (QoS 0)
                    topic, i = _str(b, 0)
                    if (h >> 1) & 3:
                        i += 2
                    self.publish(topic.decode(), b[i:], bool(h & 1))
                elif t == 8:  # SUBSCRIBE
                    pid, i, codes, new = b[:2], 2, b"", []
                    while i < len(b):
                        f, i = _str(b, i)
                        i += 1
                        new.append(f.decode())
                        codes += b"\x00"
                    with self.lock:
                        self.clients[s]["subs"] += new
                        ret = [(t2, p) for t2, p in self.retained.items() if any(topic_matches(f, t2) for f in new)]
                    s.sendall(_packet(0x90, pid + codes))
                    for t2, p in ret:
                        s.sendall(_packet(0x31, _enc(t2.encode()) + p))
                elif t == 12:  # PINGREQ
                    s.sendall(_packet(0xD0, b""))
                elif t == 14:  # DISCONNECT: no will
                    clean = True
                    return
        except (OSError, ConnectionError, IndexError, struct.error):
            pass
        finally:
            with self.lock:
                self.clients.pop(s, None)
            try:
                s.close()
            except OSError:
                pass
            if will and not clean and self.running:
                self.publish(*will)

    def last(self, topic, after=0.0):
        with self.lock:
            for t, tp, p, _ in reversed(self.log):
                if tp == topic and t >= after:
                    return p
        return None

    def wait(self, topic, after=0.0, timeout=15.0, pred=lambda p: True):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            with self.lock:
                for t, tp, p, _ in self.log:
                    if tp == topic and t >= after and pred(p):
                        return p
            time.sleep(0.05)
        raise AssertionError("no %s within %.0f s" % (topic, timeout))


# ---------------------------------------------------------------- the checks

def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def http(port, method, path, body=None, timeout=2.0):
    req = urllib.request.Request("http://127.0.0.1:%d%s" % (port, path), method=method,
                                 data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def show_on_time(port):
    """Run the example show over HTTP: OUT1 comes on at once and stays on 2 s (wall time, polled every 20 ms)."""
    r = http(port, "POST", "/api/v1/rpc", {"v": 1, "id": 1, "op": "run", "show": 1})
    assert r.get("ok"), r
    out1 = lambda: next(o["level"] for o in http(port, "GET", "/api/v1/state")["outputs"] if o["channel"] == "OUT1")
    t0 = time.monotonic()
    while not out1():
        assert time.monotonic() - t0 < 0.2, "OUT1 did not come on"
        time.sleep(0.02)
    on = time.monotonic()
    while out1() and time.monotonic() - on < 3.0:
        time.sleep(0.02)
    took = time.monotonic() - on
    assert 1.85 <= took <= 2.15, "OUT1 on for %.2f s, not 2 s" % took
    return took


def check(program):
    broker = Broker()
    port, disc = free_port(), 0
    sim = subprocess.Popen([program, "--port", str(port), "--discovery-port", str(disc), "--name", "Mqtt Check",
                            "--mqtt", "127.0.0.1:%d" % broker.port], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p = "proptroller/mqtt-check"
    try:
        # the example show (a fresh board has none: add_example) without its cooldown, so the checks can run it again
        time.sleep(0.5)
        show = http(port, "POST", "/api/v1/rpc", {"v": 1, "id": 1, "op": "add_example"})["result"]["data"]
        show["cooldown_ms"] = 0
        assert http(port, "POST", "/api/v1/rpc", {"v": 1, "id": 2, "op": "put_show", "data": show})["ok"]
        # 1. connect
        assert broker.wait(p + "/online", timeout=10) == b"true"
        st = json.loads(broker.wait(p + "/state"))
        assert list(st)[:2] == ["boot_id", "mqtt_seq"] and len(st["boot_id"]) == 8, list(st)[:3]
        boot, seq = st["boot_id"], st["mqtt_seq"]
        assert broker.retained[p + "/online"] == b"true" and p + "/state" in broker.retained
        assert broker.wait(p + "/config/name") == b"Mqtt Check"
        t = time.monotonic()
        broker.publish(p + "/rpc", json.dumps({"v": 1, "id": 7, "op": "info"}).encode(), False)
        rep = json.loads(broker.wait(p + "/rpc/reply", after=t))
        assert rep["id"] == 7 and rep["ok"], rep
        print("ok: connected; online retained, state with boot_id %s mqtt_seq %d, rpc answered" % (boot, seq))

        # 2. broker down, then up
        bport = broker.port
        broker.stop()
        took = show_on_time(port)
        time.sleep(1)
        broker = Broker(bport)
        t = time.monotonic()
        assert broker.wait(p + "/online", after=t, timeout=20) == b"true"
        st = json.loads(broker.wait(p + "/state", after=t))
        assert st["boot_id"] == boot and st["mqtt_seq"] > seq, (st["boot_id"], st["mqtt_seq"])
        print("ok: broker down: the show ran on time (%.2f s); back up: reconnected in %.1f s, state again (mqtt_seq %d -> %d)"
              % (took, time.monotonic() - t, seq, st["mqtt_seq"]))

        # 3. a broker that accepts the connection and never answers
        broker.stop()
        silent = Broker(bport, silent=True)
        t = time.monotonic()
        worst = 0.0
        while time.monotonic() - t < 12:
            a = time.monotonic()
            http(port, "GET", "/api/v1/info")
            worst = max(worst, time.monotonic() - a)
            time.sleep(0.1)
        took = show_on_time(port)
        silent.stop()
        assert worst < 0.5, "HTTP took %.2f s" % worst
        print("ok: a broker that never answers: HTTP answered within %.0f ms throughout, the show ran on time (%.2f s)" % (worst * 1000, took))

        # 4. the board dies: its will
        broker = Broker(bport)
        t = time.monotonic()
        broker.wait(p + "/online", after=t, timeout=70, pred=lambda v: v == b"true")
        sim.send_signal(signal.SIGKILL)
        sim.wait()
        assert broker.wait(p + "/online", after=time.monotonic() - 0.01, timeout=5, pred=lambda v: v == b"false") == b"false"
        assert broker.retained[p + "/online"] == b"false"
        print("ok: the board died: its will set online false (retained)")
        broker.stop()
    finally:
        if sim.poll() is None:
            sim.kill()
            sim.wait()


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--broker":
        b = Broker(int(sys.argv[2]), verbose="--verbose" in sys.argv)
        print("broker on 127.0.0.1:%d" % b.port)
        while True:
            time.sleep(3600)
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    check(sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, ".pio/build/sim/program"))
    print("mqtt check passed")
