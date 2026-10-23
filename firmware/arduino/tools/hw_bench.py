#!/usr/bin/env python3
"""Measure a Proptroller over the network (Python 3 standard library only).

    python3 tools/hw_bench.py http://192.168.4.1/ [--n 200] [--latency 30]

Works against any firmware for the round trips; the jitter, boot and trigger-to-output numbers need the
bench firmware (bench/hw_bench, which answers the bench_* ops). It also works against the host
simulator (pio run -e sim), which is how it is tested: those numbers are host numbers, not the board's.
"""
import argparse, base64, http.client, json, os, socket, statistics, struct, sys, time, urllib.parse


def pct(v, p):
    s = sorted(v)
    return s[min(len(s) - 1, int(round(p / 100.0 * (len(s) - 1))))]


def summary(ms):
    return {"n": len(ms), "p50_ms": round(pct(ms, 50), 2), "p95_ms": round(pct(ms, 95), 2), "max_ms": round(max(ms), 2),
            "mean_ms": round(statistics.mean(ms), 2)}


class WS:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(("GET /api/v1/ws HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (host, key)).encode())
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            self.buf += self.s.recv(4096)
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise RuntimeError("no WebSocket upgrade")

    def send(self, text):
        p = text.encode()
        mask = os.urandom(4)
        h = bytes([0x81])
        h += bytes([0x80 | len(p)]) if len(p) < 126 else bytes([0x80 | 126]) + struct.pack(">H", len(p))
        self.s.sendall(h + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(p)))

    def recv(self):
        while True:
            if len(self.buf) >= 2:
                n = self.buf[1] & 0x7F
                i = 2
                if n == 126:
                    if len(self.buf) < 4:
                        n = None
                    else:
                        n = struct.unpack(">H", self.buf[2:4])[0]; i = 4
                elif n == 127:
                    if len(self.buf) < 10:
                        n = None
                    else:
                        n = struct.unpack(">Q", self.buf[2:10])[0]; i = 10
                if n is not None and len(self.buf) >= i + n:
                    op, p = self.buf[0] & 0x0F, self.buf[i:i + n]
                    self.buf = self.buf[i + n:]
                    if op == 1:
                        return p.decode()
                    if op == 9:  # ping: answer, or the board closes the session after 5 s
                        mask = os.urandom(4)
                        self.s.sendall(bytes([0x8A, 0x80 | len(p)]) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(p)))
                    continue
            self.buf += self.s.recv(65536)

    def call(self, req):
        self.send(json.dumps(req))
        while True:
            m = json.loads(self.recv())
            if m.get("id") == req.get("id") and "ok" in m:
                return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url")
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--latency", type=int, default=0, help="trigger-to-output trials (bench firmware + jumper)")
    a = ap.parse_args()
    u = urllib.parse.urlparse(a.url)
    host, port = u.hostname, u.port or 80
    out = {"target": "%s:%d" % (host, port)}

    def rpc(conn, req):
        conn.request("POST", "/api/v1/rpc", json.dumps(req), {"Content-Type": "application/json"})
        return json.loads(conn.getresponse().read())

    # HTTP, a new connection per request (what a browser's first load sees)
    t = []
    for _ in range(a.n):
        t0 = time.perf_counter()
        c = http.client.HTTPConnection(host, port, timeout=5)
        c.request("GET", "/api/v1/info")
        c.getresponse().read()
        c.close()
        t.append((time.perf_counter() - t0) * 1000)
    out["http_info_new_connection"] = summary(t)
    # HTTP keep-alive RPC
    c = http.client.HTTPConnection(host, port, timeout=5)
    t = []
    for i in range(a.n):
        t0 = time.perf_counter()
        rpc(c, {"v": 1, "id": i, "op": "state"})
        t.append((time.perf_counter() - t0) * 1000)
    out["http_rpc_state_keepalive"] = summary(t)
    # the web UI file (gzipped)
    t0 = time.perf_counter()
    c.request("GET", "/app.js", headers={"Accept-Encoding": "gzip"})
    body = c.getresponse().read()
    out["get_app_js"] = {"ms": round((time.perf_counter() - t0) * 1000, 2), "bytes": len(body)}
    # WebSocket round trips
    ws = WS(host, port)
    t = []
    for i in range(a.n):
        t0 = time.perf_counter()
        ws.call({"v": 1, "id": 10000 + i, "op": "info"})
        t.append((time.perf_counter() - t0) * 1000)
    out["ws_rpc_info"] = summary(t)
    # bench firmware extras
    r = rpc(c, {"op": "bench_jitter"})
    if r.get("ok"):
        out["jitter_since_boot"] = r["result"]
        # load: WebSocket requests as fast as possible for 5 s, then the jitter during it
        rpc(c, {"op": "bench_jitter"})
        end = time.time() + 5
        k = 0
        while time.time() < end:
            ws.call({"v": 1, "id": 50000 + k, "op": "state"}); k += 1
        out["jitter_under_ws_load"] = dict(rpc(c, {"op": "bench_jitter"})["result"], requests=k)
        out["boot"] = rpc(c, {"op": "bench_boot"})["result"]
        if a.latency:
            rpc(c, {"op": "bench_latency", "n": a.latency})
            while True:
                time.sleep(2)
                r = rpc(c, {"op": "bench_latency_result"})["result"]
                if r["done"]:
                    break
            us = [v for v in r["latency_us"] if v]
            out["trigger_to_output_us"] = {"n": len(us), "misses": len(r["latency_us"]) - len(us),
                                           "p50": pct(us, 50) if us else None, "max": max(us) if us else None}
    else:
        out["bench_ops"] = "not available (generic firmware): flash bench/hw_bench for jitter, boot and latency"
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    sys.exit(main())
