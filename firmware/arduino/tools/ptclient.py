#!/usr/bin/env python3
"""A small Proptroller protocol client (firmware/PROTOCOL.md), Python 3 standard library only.

    ptclient.py discover [--port 47800] [--to HOST] [--wait-ms 800]
    ptclient.py info HOST:PORT
    ptclient.py rpc HOST:PORT '{"op":"list_shows"}'
    ptclient.py update HOST:PORT IMAGE [--pin PIN]
    ptclient.py image OUT.bin [--version 0.2.0] [--build simnew] [--fail-self-check] [--size BYTES]
    ptclient.py conformance HOST:PORT [DISCOVERY_PORT] [--golden DIR]

`conformance` makes the calls the desktop app makes (UDP discovery, GET /api/v1/info and /state, the web UI,
HTTP RPC) and a WebSocket session (info, get_params, list_shows, stop, run, a notice, a state event, pixel),
and checks every answer's shape against the committed protocol golden files (test/golden): a field that is
missing, unknown or of another type fails, as parsing into typed structures would.

`image` writes a simulator firmware image for POST /api/v1/update (the format the simulated board takes:
`PROPTROLLER-SIM-IMAGE\\n`, a one-line JSON header, `\\n`, then filler bytes).
"""
import base64
import gzip
import hashlib
import http.client
import json
import os
import socket
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GOLDEN = os.path.join(HERE, "..", "test", "golden")
VERSION_FILE = os.path.join(HERE, "..", "..", "VERSION")
DISCOVERY_PORT = 47800
SIM_IMAGE_MAGIC = b"PROPTROLLER-SIM-IMAGE\n"


def split_addr(addr):
    host, _, port = addr.rpartition(":")
    return host, int(port)


# ---------------------------------------------------------------- discovery, HTTP

def discover(port=DISCOVERY_PORT, to=(), wait=0.8):
    """Broadcast (and unicast to each host in `to`) the discover probe; the announces, one per board id."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.bind(("0.0.0.0", 0))
    probe = json.dumps({"proptroller": "discover", "v": 1}, separators=(",", ":")).encode()
    for host in ("255.255.255.255",) + tuple(to):
        try:
            s.sendto(probe, (host, port))
        except OSError:
            pass  # no broadcast route: the unicast probes still go out
    found = {}
    deadline = time.monotonic() + wait
    while True:
        left = deadline - time.monotonic()
        if left <= 0:
            break
        s.settimeout(left)
        try:
            data, src = s.recvfrom(2048)
        except socket.timeout:
            break
        try:
            a = json.loads(data)
        except ValueError:
            continue
        if isinstance(a, dict) and a.get("proptroller") == "announce" and a.get("id") not in found:
            found[a["id"]] = {"addr": "%s:%d" % (src[0], a.get("port", 80)), "announce": a}
    s.close()
    return list(found.values())


def http_call(addr, method, path, body=None, headers=None, timeout=10):
    """(status, headers, body bytes); a gzipped body is decoded as a browser would."""
    host, port = split_addr(addr)
    c = http.client.HTTPConnection(host, port, timeout=timeout)
    h = {"Accept-Encoding": "gzip"}
    h.update(headers or {})
    c.request(method, path, body=body, headers=h)
    r = c.getresponse()
    data = r.read()
    if (r.getheader("Content-Encoding") or "").lower() == "gzip":
        data = gzip.decompress(data)
    out = (r.status, dict((k.lower(), v) for k, v in r.getheaders()), data)
    c.close()
    return out


def rpc(addr, req, timeout=10):
    env = dict(req)
    env.setdefault("v", 1)
    st, _, body = http_call(addr, "POST", "/api/v1/rpc", json.dumps(env).encode(), {"Content-Type": "application/json"}, timeout)
    if st != 200:
        raise RuntimeError("HTTP %d: %s" % (st, body[:200]))
    return json.loads(body)


def update(addr, image, pin=None, timeout=60):
    h = {"Content-Type": "application/octet-stream", "X-Proptroller-Sha256": hashlib.sha256(image).hexdigest()}
    if pin is not None:
        h["X-Proptroller-Pin"] = pin
    _, _, body = http_call(addr, "POST", "/api/v1/update", image, h, timeout)
    return json.loads(body)


# ---------------------------------------------------------------- WebSocket (RFC 6455, client side)

class Session:
    """A WebSocket session on /api/v1/ws: requests in, responses and events out. Pings are answered."""

    def __init__(self, addr, timeout=5):
        host, port = split_addr(addr)
        self.sock = socket.create_connection((host, port), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(("GET /api/v1/ws HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (addr, key)).encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(1)
            if not chunk:
                raise RuntimeError("closed during the WebSocket handshake")
            head += chunk
        lines = head.decode("latin-1").split("\r\n")
        if " 101 " not in lines[0] + " ":
            raise RuntimeError("no WebSocket upgrade: " + lines[0])
        want = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        accept = [l.split(":", 1)[1].strip() for l in lines if l.lower().startswith("sec-websocket-accept:")]
        if accept != [want]:
            raise RuntimeError("bad Sec-WebSocket-Accept")
        self.next_id = 1
        self.events = []
        self.last_state = None

    def _send(self, opcode, payload):
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            head = struct.pack("!BB", 0x80 | opcode, 0x80 | n)
        elif n < 65536:
            head = struct.pack("!BBH", 0x80 | opcode, 0x80 | 126, n)
        else:
            head = struct.pack("!BBQ", 0x80 | opcode, 0x80 | 127, n)
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def _read(self, n):
        out = b""
        while len(out) < n:
            chunk = self.sock.recv(n - len(out))
            if not chunk:
                raise RuntimeError("the board closed the WebSocket")
            out += chunk
        return out

    def _frame(self):
        b0, b1 = self._read(2)
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack("!H", self._read(2))[0]
        elif n == 127:
            n = struct.unpack("!Q", self._read(8))[0]
        mask = self._read(4) if b1 & 0x80 else None
        data = self._read(n)
        if mask:
            data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        return b0 & 0x80, b0 & 0x0F, data

    def recv(self, wait):
        """The next text message as JSON, or None after `wait` seconds."""
        deadline = time.monotonic() + wait
        parts, kind = [], None
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                fin, op, data = self._frame()
            except socket.timeout:
                return None
            if op == 9:
                self._send(10, data)
                continue
            if op == 10:
                continue
            if op == 8:
                raise RuntimeError("the board closed the WebSocket")
            if op in (1, 2):
                parts, kind = [data], op
            elif op == 0:
                parts.append(data)
            if fin and kind == 1:
                return json.loads(b"".join(parts))

    def _queue(self, m):
        if m.get("event") == "state":
            self.last_state = m.get("data")
        self.events.append(m)
        del self.events[:-256]

    def call(self, req, wait=5):
        rid = self.next_id
        self.next_id += 1
        env = {"v": 1, "id": rid}
        env.update(req)
        self._send(1, json.dumps(env).encode())
        deadline = time.monotonic() + wait
        while True:
            m = self.recv(deadline - time.monotonic())
            if m is None:
                raise RuntimeError("no answer to %s" % req.get("op"))
            if "event" in m:
                self._queue(m)
            elif m.get("id") == rid:
                return m

    def wait_event(self, test, wait):
        for i, m in enumerate(self.events):
            if test(m):
                return self.events.pop(i)
        deadline = time.monotonic() + wait
        while True:
            m = self.recv(deadline - time.monotonic())
            if m is None:
                return None
            if "event" in m:
                if m["event"] == "state":
                    self.last_state = m.get("data")
                if test(m):
                    return m
                self._queue(m)

    def close(self):
        try:
            self._send(8, b"")
        except OSError:
            pass
        self.sock.close()


# ---------------------------------------------------------------- simulator images

def sim_image(fw_version, fw_build, self_check_passes=True, payload_len=64 * 1024):
    head = json.dumps({"mcu": "sim", "fw_version": fw_version, "fw_build": fw_build,
                       "self_check": "pass" if self_check_passes else "fail"}, separators=(",", ":"))
    return SIM_IMAGE_MAGIC + head.encode() + b"\n" + bytes(i % 251 for i in range(payload_len))


# ---------------------------------------------------------------- shape checks against the golden files

def _golden(name):
    """A golden file's value; for a response, its result without the reply's `kind`."""
    with open(os.path.join(GOLDEN, name), "rb") as f:
        v = json.load(f)
    return _result(v) if isinstance(v, dict) and "ok" in v and "result" in v else v


def _result(response):
    r = dict(response.get("result") or {})
    r.pop("kind", None)
    return r


def _kind(v):
    if v is None:
        return "null"
    if isinstance(v, bool):
        return "bool"
    if isinstance(v, (int, float)):
        return "number"
    return type(v).__name__


def shape_errors(got, refs, path="$"):
    """Where `got` differs in shape from every reference value: keys present in every reference are required,
    keys in none are unknown, and a value's type must be one a reference has (null matches anything)."""
    refs = [r for r in refs if r is not None]
    if got is None or not refs:
        return []
    kinds = {_kind(r) for r in refs}
    if _kind(got) not in kinds:
        return ["%s: %s, expected %s" % (path, _kind(got), "/".join(sorted(kinds)))]
    errs = []
    if isinstance(got, dict):
        dicts = [r for r in refs if isinstance(r, dict)]
        required = set.intersection(*(set(r) for r in dicts))
        known = set.union(*(set(r) for r in dicts))
        errs += ["%s.%s: missing" % (path, k) for k in sorted(required - set(got))]
        errs += ["%s.%s: unknown field" % (path, k) for k in sorted(set(got) - known)]
        for k in sorted(set(got) & known):
            errs += shape_errors(got[k], [r[k] for r in dicts if k in r], path + "." + k)
    elif isinstance(got, list):
        items = [x for r in refs if isinstance(r, list) for x in r]
        if items:
            for i, x in enumerate(got):
                errs += shape_errors(x, items, "%s[%d]" % (path, i))
    return errs


# ---------------------------------------------------------------- conformance

def conformance(addr, disc_port=DISCOVERY_PORT):
    passed = [0]

    def check(ok, what):
        print("%s: %s" % ("ok" if ok else "FAIL", what))
        if not ok:
            sys.exit(1)
        passed[0] += 1

    def shape(value, golden_names, what):
        errs = shape_errors(value, [_golden(n) for n in golden_names])
        check(not errs, what + ("" if not errs else ": " + "; ".join(errs[:8])))

    host, port = split_addr(addr)
    found = discover(disc_port, (host,))
    me = [f for f in found if split_addr(f["addr"])[1] == port]
    check(bool(me), "UDP discovery found the board (%d answers)" % len(found))
    a = me[0]["announce"]
    shape(a, ["05-announce.json"], "announce: %s %s %s %s" % (a.get("name"), a.get("fw"), a.get("fw_version"), a.get("mcu")))
    st, _, body = http_call(addr, "GET", "/api/v1/info")
    check(st == 200, "GET /api/v1/info 200")
    info = json.loads(body)
    shape(info, ["01-info.json"], "info has the fields and types of the golden info")
    check(info.get("id") == a.get("id"), "info: %s capabilities %s" % (info.get("name"), info.get("capabilities")))
    st, _, body = http_call(addr, "GET", "/api/v1/state")
    check(st == 200, "GET /api/v1/state 200")
    shape(json.loads(body), ["06-state-idle.json", "07-state-running.json", "08-state-cooldown.json"], "state has the fields and types of the golden states")
    st, _, body = http_call(addr, "GET", "/")
    check(st == 200 and b"<html" in body.lower(), "GET / (web UI)")
    r = rpc(addr, {"op": "list_shows"})
    check(r.get("ok") is True, "POST /api/v1/rpc list_shows")
    st, _, _ = http_call(addr, "POST", "/api/v1/rpc")
    check(st == 415, "rpc without the JSON content type is refused (415)")
    s = Session(addr)
    check(s.call({"op": "info"}).get("ok") is True, "ws: info")
    p = s.call({"op": "get_params"})
    shape(_result(p), ["02-params.json"], "ws: get_params has the golden shape")
    shows = s.call({"op": "list_shows"})["result"]["shows"]
    if not shows:
        # a fresh board has no shows: add the example (add_example) so a run can be checked
        r = s.call({"op": "add_example"})
        check(r.get("ok") is True and r["result"]["kind"] == "show", "ws: add_example on a board with no shows")
        shows = s.call({"op": "list_shows"})["result"]["shows"]
    if shows:
        sid = shows[0]["id"]
        s.call({"op": "stop", "show": sid})
        r = s.call({"op": "run", "show": sid})
        check(r.get("ok") is True or (r.get("error") or {}).get("code") == "refused", "ws: run")
        e = s.wait_event(lambda m: m.get("event") == "notice", 3)
        check(e is not None, "ws: a notice event arrived")
    e = s.wait_event(lambda m: m.get("event") == "state", 6)
    check(e is not None, "ws: a state event arrived (pings answered)")
    shape(e["data"], ["06-state-idle.json", "07-state-running.json", "08-state-cooldown.json"], "ws: the state event has the golden shape")
    check(s.call({"op": "pixel"}).get("ok") is True, "ws: pixel release")
    s.close()
    print("%d checks passed" % passed[0])


# ---------------------------------------------------------------- command line

def _opt(args, name, default=None):
    if name in args:
        i = args.index(name)
        v = args[i + 1]
        del args[i:i + 2]
        return v
    return default


def _flag(args, name):
    if name in args:
        args.remove(name)
        return True
    return False


def main(argv):
    args = list(argv)
    if not args:
        print(__doc__)
        return 2
    cmd = args.pop(0)
    if cmd == "discover":
        port = int(_opt(args, "--port", DISCOVERY_PORT))
        to = _opt(args, "--to")
        wait = int(_opt(args, "--wait-ms", 800)) / 1000
        for f in discover(port, (to,) if to else (), wait):
            print(f["addr"], json.dumps(f["announce"]))
    elif cmd == "info":
        print(http_call(args[0], "GET", "/api/v1/info")[2].decode())
    elif cmd == "rpc":
        print(json.dumps(rpc(args[0], json.loads(args[1]))))
    elif cmd == "update":
        pin = _opt(args, "--pin")
        with open(args[1], "rb") as f:
            print(json.dumps(update(args[0], f.read(), pin)))
    elif cmd == "image":
        with open(VERSION_FILE) as f:
            default_version = f.read().strip()
        version = _opt(args, "--version", default_version)
        build = _opt(args, "--build", "simnew")
        size = int(_opt(args, "--size", 64 * 1024))
        passes = not _flag(args, "--fail-self-check")
        img = sim_image(version, build, passes, size)
        with open(args[0], "wb") as f:
            f.write(img)
        print("%s: %d bytes, sha256 %s" % (args[0], len(img), hashlib.sha256(img).hexdigest()))
    elif cmd == "conformance":
        global GOLDEN
        GOLDEN = _opt(args, "--golden", GOLDEN)
        conformance(args[0], int(args[1]) if len(args) > 1 else DISCOVERY_PORT)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
