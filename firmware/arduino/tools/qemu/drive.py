#!/usr/bin/env python3
"""Boot an ESP32 flash image in Espressif's QEMU and test the firmware over the emulated UART (standard library
only). tools/qemu/run-esp32.sh builds the images and calls this; QEMU.md says what it proves and what it cannot.

    drive.py full  --qemu Q --flash flash.bin --elf firmware.elf     the board's own image, up to the Wi-Fi start
    drive.py smoke --qemu Q --flash flash.bin --elf firmware.elf     the no-Wi-Fi image: protocol, reboot, memory
    drive.py smoke ... --soak 300                                    then 300 s of emulated time with shows running
    drive.py net   ... --image firmware.bin                          the Ethernet build (tools/qemu/eth): network checks
    drive.py serve ... --http-port 39417                             the Ethernet build, kept up for other tools

The flash file is changed in place (LittleFS, the stores): pass a copy. Exit code 1 when a check fails.
"""
import argparse, json, os, queue, re, shutil, socket, subprocess, sys, threading, time

FATAL = re.compile(r"Guru Meditation|abort\(\) was called|Stack canary|stack overflow|Task watchdog|"
                   r"task_wdt|rst:0x[0-9a-f]+ \((?!POWERON|SW_CPU|RTC_SW_CPU)|CORRUPT HEAP|assert failed")
SHOW = {"id": 9, "name": "QEMU loop", "enabled": True, "looping": False, "cooldown_ms": 0,
        "triggers": [{"on": "every", "period_ms": 1500}],
        "tracks": [{"target": "OUT1", "cues": [{"at": 0, "do": "on"}, {"at": 300, "do": "off"}]},
                   {"target": "OUT2", "cues": [{"at": 0, "do": "ramp", "to": 1000, "ms": 400}, {"at": 900, "do": "off"}]},
                   {"target": "RLY1", "cues": [{"at": 100, "do": "pulse", "ms": 200}]},
                   {"target": "PIXEL", "cues": [{"at": 0, "do": "flicker", "color": "#ff6000"}, {"at": 1000, "do": "off"}]},
                   {"target": "SOUND", "cues": [{"at": 0, "do": "play", "folder": 1, "track": 1}]}]}


def largest_script():
    """The largest script the limits allow (test/test_script_fit: 8 effects, 224 lines, 8168 bytes)."""
    t = ""
    for e in range(8):
        t += f"effect effect number {e} with a long name:\n  wait for trig1\n"
        t += f"  fade dmx{e + 1} to 37.5% over 1.25 s # c\n" * 26
    return t


def busy_script(effects=8):
    """A script near the size limit (8 effects, 208 lines, about 7 KB) on the outputs only: QEMU's UART has no
    break, so a stored DMX script would stall the DMX driver (QEMU.md). Fewer effects: a smaller one."""
    t = ""
    for e, out in enumerate(["out1", "out2", "out3", "out4", "rly1", "rly2", "cc1", "cc2"][:effects]):
        t += f"effect effect number {e} with a long name:\n  wait for trig1\n"
        for i in range(12):
            if e < 4:
                t += f"  fade {out} to 37.5% over 0.25 s # c\n  fade {out} to 0% over 0.25 s # cc\n"
            else:
                t += f"  {out} = {'on ' if i % 2 == 0 else 'off'} # a comment to fill\n  wait 0.25  # a comment to fill\n"
    return t


class Board:
    def __init__(self, a, log):
        self.a, self.log, self.lines, self.id = a, log, queue.Queue(), 0
        self.all = []  # every line, for the fatal check and the backtraces
        cmd = [a.qemu, "-machine", a.machine, "-display", "none", "-monitor", "none", "-serial", "stdio",
               "-drive", f"file={a.flash},if=mtd,format=raw"]
        if a.icount:
            cmd[1:1] = ["-icount", f"shift={a.icount}"]
        if getattr(a, "http_port", 0):  # QEMU's OpenCores Ethernet, user networking: the host is 10.0.2.2
            cmd += ["-nic", f"user,model=open_eth,hostfwd=tcp:127.0.0.1:{a.http_port}-:80,"
                            f"hostfwd=udp:127.0.0.1:{a.udp_port}-:47800"]
        self.cmd, self.t0 = cmd, time.time()
        self._spawn()

    def _spawn(self):
        self.p = subprocess.Popen(self.cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        threading.Thread(target=self._read, args=(self.p,), daemon=True).start()

    def power_cycle(self):
        """Off and on again on the same flash (QEMU's own reset path is slow or hangs after a software reset)."""
        self.stop()
        self.log.write(f"{time.time() - self.t0:8.2f} ==== power cycle (a new QEMU on the same flash)\n")
        self.lines = queue.Queue()
        self._spawn()

    def _read(self, p):
        for raw in p.stdout:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            self.log.write(f"{time.time() - self.t0:8.2f} {line}\n")
            self.log.flush()
            self.all.append(line)
            if p is self.p:
                self.lines.put(line)

    def stop(self):
        if self.p.poll() is None:
            self.p.kill()  # only the QEMU this script started, by its own handle
        self.p.wait()

    def wait_for(self, pattern, timeout, fatal_ok=False):
        end = time.time() + timeout
        while time.time() < end:
            try:
                line = self.lines.get(timeout=0.2)
            except queue.Empty:
                continue
            if line is None:
                return None
            if re.search(pattern, line):
                return line
            if FATAL.search(line):  # a panic, watchdog or reset: the answer is not coming
                self._fatal(line)
                if not fatal_ok:
                    return None
        return None

    def _fatal(self, line):
        time.sleep(1.0)  # let the backtrace arrive
        print("   ! " + line)

    def send(self, text):
        self.p.stdin.write((text + "\n").encode())
        self.p.stdin.flush()

    def ask_line(self, text, pattern, timeout):
        self.send(text)
        return self.wait_for(pattern, timeout)

    def call(self, op, timeout=30, **fields):
        self.id += 1
        req = dict(v=1, id=self.id, op=op, **fields)
        self.send(json.dumps(req, separators=(",", ":")))
        end = time.time() + timeout
        while time.time() < end:
            line = self.wait_for(r"^\{", end - time.time())
            if line is None:
                return None
            try:
                m = json.loads(line)
            except ValueError:
                continue
            if "ok" in m and m.get("id") == self.id:
                return m
        return None

    def uptime(self):
        r = self.call("state")
        return r["result"]["uptime_ms"] if r and r.get("ok") else None

    def fatal_lines(self):
        return [l for l in self.all if FATAL.search(l)]


def decode(elf, addr2line, lines):
    """Backtrace lines decoded with the ELF (addresses only; the source paths are cut to the file name)."""
    out = []
    for l in lines:
        if not l.startswith("Backtrace:"):
            continue
        addrs = re.findall(r"(0x4[0-9a-f]{7}):0x", l)
        if not addrs or not addr2line:
            continue
        r = subprocess.run([addr2line, "-pfiaC", "-e", elf] + addrs, capture_output=True, text=True)
        out += [re.sub(r" at \S*/", " at ", s) for s in r.stdout.splitlines()]
    return out


def check(results, name, ok, detail=""):
    results.append((name, bool(ok)))
    print(f"{'ok  ' if ok else 'FAIL'} {name}{': ' + detail if detail else ''}")
    return ok


def boot(b, results, label, timeout, fatal_ok=False):
    t = time.time()
    line = b.wait_for(r"ready after \d+ ms" if fatal_ok else r"ready after \d+ ms|Guru Meditation", timeout, fatal_ok)
    ok = line is not None and "ready after" in line
    check(results, f"{label}: ready", ok, (line or "no 'ready after' line") + f" (wall {time.time() - t:.1f} s)")
    return ok


def pixel_failure(a, b, results, ready):
    """QEMU's RMT makes the pixel driver's install fail (esp32 machine; the S3 env has no pixel driver). The failure
    message once overflowed the IPC task's 1 KB stack into the heap (hal.cpp BoardPixels::begin), and the ready
    line's heap walk (memoryReport) faulted. With ready: the failure line, then a ready line (which walks the heap)
    and no fatal line between them; without (stage 1 stops at the Wi-Fi fault first): only that the install ran."""
    if a.machine != "esp32":
        return
    lines = b.all
    i = next((k for k, l in enumerate(lines) if "pixel driver not available" in l), None)
    if not ready:
        check(results, "the pixel driver's failed install ran (QEMU's RMT)", i is not None)
        return
    j = next((k for k in range(i + 1, len(lines)) if re.search(r"ready after \d+ ms", lines[k])), None) if i is not None else None
    clean = j is not None and not any(FATAL.search(l) for l in lines[i:j])
    check(results, "after the pixel driver's failed install, the ready line's heap walk passes", clean,
          "" if clean else "no failure line, or no ready line after it without a fault")


def run_full(a, b, results):
    """The board's own image: QEMU has no radio, so the Wi-Fi driver faults in the closed PHY code
    (register_chipv7_phy reads a register block QEMU does not map). Everything before it must have worked."""
    banner = b.wait_for(r"Proptroller \S+ on " + re.escape(a.mcu), a.boot_timeout)
    check(results, "banner (bootloader, app, LittleFS, settings, controller)", banner, banner or "not seen")
    fault = b.wait_for(r"ready after \d+ ms|Guru Meditation", 30)
    time.sleep(1.5)
    bt = decode(a.elf, a.addr2line, b.all)
    for s in bt:
        print("     " + s)
    fs_ok = not any("could not open the flash file system" in l for l in b.all)
    check(results, "LittleFS mounted (formatted on the first boot)", fs_ok)
    pixel_failure(a, b, results, ready=False)
    if fault and "ready after" in fault:
        check(results, "ready (this QEMU emulates the radio?)", True, fault)
    else:
        check(results, "the only fault is the Wi-Fi PHY init (QEMU has no radio)",
              fault and bt and "register_chipv7_phy" in bt[0], fault or "no fault and no ready line")


def run_smoke(a, b, results):
    if not boot(b, results, "first boot", a.boot_timeout):
        return
    pixel_failure(a, b, results, ready=True)
    mem0 = b.ask_line("mem", r"free heap", 10)
    check(results, "mem after boot", mem0, mem0 or "")
    r = b.call("info")
    check(results, "info", r and r["ok"] and r["result"].get("mcu") == a.mcu,
          r and json.dumps(r["result"])[:200])
    check(results, "state", b.uptime() is not None)
    r = b.call("set_params", values=[{"key": "name", "value": "QEMU test"}])
    check(results, "set_params name", r and r["ok"], r and json.dumps(r)[:200])
    r = b.call("put_show", data=SHOW)
    check(results, "put_show", r and r["ok"], r and json.dumps(r)[:200])
    r = b.call("run", show=9)
    check(results, "run", r and r["ok"], r and json.dumps(r)[:200])
    # the run starts at the next 10 ms tick: a state request can be answered just before it (seen on the S3)
    running = False
    for _ in range(5):
        r = b.call("state")
        running = r and any(s.get("id") == 9 and s.get("state") in ("running", "cooldown") for s in r["result"].get("shows", []))
        if running:
            break
        time.sleep(0.2)
    check(results, "the show runs", running)
    r = b.call("set_params", values=[{"key": "dmx_port", "value": "s4"}])  # the largest script uses DMX
    check(results, "set_params dmx_port=s4", r and r["ok"], r and json.dumps(r)[:200])
    text = largest_script()
    r, ms = timed(b, "check_script", text=text)
    ok = r and r["ok"] and r["result"].get("ok") is True
    check(results, f"check_script, the largest script ({len(text)} B)", ok, f"{ms} ms guest; {low(b)}" if ok else json.dumps(r)[:300])
    r = b.call("set_params", values=[{"key": "dmx_port", "value": "off"}, {"key": "cc1_role", "value": "shows"},
                                     {"key": "cc2_role", "value": "shows"}])
    check(results, "set_params dmx_port=off, cc1_role=cc2_role=shows", r and r["ok"], r and json.dumps(r)[:200])
    text = busy_script()
    r, ms = timed(b, "set_script", text=text)
    ok = r and r["ok"] and r["result"].get("ok") is True
    check(results, f"set_script, a script on every output ({len(text)} B)", ok, f"{ms} ms guest; {low(b)}" if ok else json.dumps(r)[:300])
    r, ms = timed(b, "get_backup")
    ok = r and r["ok"]
    check(results, "get_backup", ok, f"{ms} ms guest; {low(b)}")
    if ok:
        data = r["result"]["data"]
        line = len(json.dumps(dict(v=1, id=0, op="put_backup", data=data), separators=(",", ":")))
        r, ms = timed(b, "put_backup", data=data)
        check(results, f"put_backup of it ({line} B line; the serial line limit is 42952)", r and r["ok"],
              f"{ms} ms guest; {low(b)}" if r and r["ok"] else json.dumps(r)[:200])
    # lines over the serial limit (42952 B): dropped as they arrive, answered bad_request once, nothing held after
    mem0 = b.ask_line("mem", r"free heap", 10)
    for size in (44000, 50000):
        b.send(json.dumps(dict(v=1, id=0, op="check_script", text="#" * size), separators=(",", ":")))
        ans = b.wait_for(r'"code":"bad_request"', 60)
        check(results, f"a {size} B serial line is answered bad_request", ans and "longer than 42952 bytes" in ans, ans or "")
    r = b.call("info")
    check(results, "info after the over-long lines", r and r["ok"])
    mem_after = b.ask_line("mem", r"free heap", 10)
    free = lambda m: int(re.search(r"free heap (\d+)", m).group(1)) if m else 0
    # within 8 KB (the event log and the running show move a few KB); a held line buffer would be 32 KB or more
    check(results, "the heap is back after the over-long lines", mem0 and mem_after and free(mem_after) >= free(mem0) - 8192,
          f"{mem0} -> {mem_after}")
    ans = b.ask_line("name", r"^name=", 10)
    check(results, "console: name", ans == "name=QEMU test", ans or "")
    mem1 = b.ask_line("mem", r"free heap", 10)
    check(results, "mem after the requests", mem1, mem1 or "")
    # the reboot keeps what was saved (settings, the show, the script)
    r = b.call("reboot")
    check(results, "reboot", r and r["ok"])
    if not boot(b, results, "after the reboot", a.boot_timeout):
        return
    r = b.call("info")
    check(results, "the name was kept", r and r["ok"] and r["result"].get("name") == "QEMU test", r and r["result"].get("name"))
    r = b.call("list_shows")
    check(results, "the show was kept", r and r["ok"] and any(s.get("id") == 9 for s in r["result"].get("shows", [])))
    r = b.call("get_script")
    check(results, "the script was kept", r and r["ok"] and r["result"].get("text") == text)
    if a.soak:
        soak(a, b, results)
    mem2 = b.ask_line("mem", r"free heap", 10)
    check(results, "mem at the end", mem2, mem2 or "")


def timed(b, op, **kw):
    """One request and the guest time it took (two state requests around it, less one state request alone)."""
    t0 = b.uptime()
    t1 = b.uptime()
    r = b.call(op, timeout=90, **kw)
    t2 = b.uptime()
    ms = (t2 - t1) - (t1 - t0) if None not in (t0, t1, t2) else None
    return r, ms


def low(b):
    """The heap low-water mark so far, from the mem line."""
    h = heap(b.ask_line("mem", r"free heap", 10))
    return f"free heap {h[0]} B, min {h[1]} B" if h else "no mem answer"


def heap(line):
    m = re.search(r"free heap (\d+) B \(min (\d+), largest block (\d+)\); stack free: loop (\d+) B, step (\d+) B", line or "")
    return tuple(int(x) for x in m.groups()) if m else None


def soak(a, b, results):
    """Shows running for a.soak seconds of emulated time, with requests and a stress loop in between."""
    r = b.call("run", show=9)
    start, w0 = b.uptime(), time.time()
    first = None  # the heap after a warm-up of 40 rounds (the event ring and the buffers reach their size)
    n, fails, now, rtt = 0, 0, start, []
    text = busy_script()
    while now is not None and now - start < a.soak * 1000:
        n += 1
        t0, t1 = b.uptime(), b.uptime()  # one state request's round trip (line in, about 2 KB out), guest ms
        if None not in (t0, t1):
            rtt.append(t1 - t0)
        for op, kw in (("state", {}), ("list_shows", {}), ("get_params", {}), ("put_show", {"data": SHOW}),
                       ("run", {"show": 9}), ("check_script", {"text": text}), ("get_backup", {})):
            r = b.call(op, **kw)
            if not r:
                fails += 1
        if n % 20 == 0:
            m = b.ask_line("mem", r"free heap", 10)
            if n == 40:
                first = heap(m)
            print(f"     soak: {(now - start) / 1000:.0f} s guest, {time.time() - w0:.0f} s wall, " + (m or "no mem answer"))
        time.sleep(0.2)
        now = b.uptime()
    last = heap(b.ask_line("mem", r"free heap", 10))
    guest = (now - start) / 1000 if now else 0
    check(results, f"soak: {n} rounds of 7 requests, {guest:.0f} s guest / {time.time() - w0:.0f} s wall", fails == 0 and now,
          f"{fails} unanswered")
    if rtt:
        rtt.sort()
        print(f"     state request round trip, guest ms: median {rtt[len(rtt) // 2]}, max {rtt[-1]}")
    if first and last:
        check(results, "soak: free heap after the warm-up did not shrink by more than 2 KB", first[0] - last[0] < 2048,
              f"free {first[0]} -> {last[0]} B, min {last[1]}, largest block {first[2]} -> {last[2]}, stack free loop {last[3]} step {last[4]}")


# ---------------------------------------------------------------- the network (env esp32dev_qemu_eth)

class Broker(threading.Thread):
    """A minimal MQTT 3.1.1 broker for one client (QoS 0): enough to see the board connect, subscribe and publish,
    and to send it a message. silent=True accepts the TCP connection and never answers (a broken broker)."""

    def __init__(self, port, silent=False):
        super().__init__(daemon=True)
        self.srv = socket.create_server(("127.0.0.1", port))
        self.port, self.silent, self.conn, self.conns = self.srv.getsockname()[1], silent, None, []
        self.connects, self.subs, self.pubs = 0, [], []

    def run(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            self.connects += 1
            threading.Thread(target=self.client, args=(c,), daemon=True).start()

    def client(self, c):
        if self.silent:
            c.recv(4096)  # read the CONNECT, then nothing: the board waits for a CONNACK that never comes
            time.sleep(3600)
            return
        self.conn = c
        self.conns.append(c)
        try:
            while True:
                h = c.recv(1)
                if not h:
                    return
                n, mult = 0, 1
                while True:
                    d = c.recv(1)[0]
                    n += (d & 127) * mult
                    mult *= 128
                    if d < 128:
                        break
                body = b""
                while len(body) < n:
                    body += c.recv(n - len(body))
                t = h[0] >> 4
                if t == 1:
                    c.sendall(b"\x20\x02\x00\x00")
                elif t == 8:  # SUBSCRIBE: packet id, then (topic, qos) pairs
                    pid, i, k = body[:2], 2, 0
                    while i < len(body):
                        ln = int.from_bytes(body[i:i + 2], "big")
                        self.subs.append(body[i + 2:i + 2 + ln].decode())
                        i += 3 + ln
                        k += 1
                    c.sendall(bytes([0x90, 2 + k]) + pid + b"\x00" * k)
                elif t == 3:
                    ln = int.from_bytes(body[:2], "big")
                    self.pubs.append((body[2:2 + ln].decode(), body[2 + ln:].decode("utf-8", "replace")))
                elif t == 12:
                    c.sendall(b"\xd0\x00")
        except (OSError, IndexError):
            return

    def publish(self, topic, payload):
        t, p = topic.encode(), payload.encode()
        body = len(t).to_bytes(2, "big") + t + p
        n, rl = len(body), b""
        while True:
            d, n = n % 128, n // 128
            rl += bytes([d | (128 if n else 0)])
            if not n:
                break
        for c in self.conns:  # every session the board opened (it may reconnect once at the start)
            try:
                c.sendall(b"\x30" + rl + body)
            except OSError:
                pass


def run_net(a, b, results):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import ptclient
    addr = f"127.0.0.1:{a.http_port}"
    line = b.wait_for(r"QEMU Ethernet at|did not come up", a.boot_timeout)
    check(results, "Ethernet up (DHCP from QEMU)", line and "at http" in line, line or "")
    if not boot(b, results, "first boot", a.boot_timeout):
        return
    # discovery through the forwarded UDP port; the announce names port 80 (the board's), forwarded to http_port
    found = ptclient.discover(a.udp_port, ("127.0.0.1",), wait=5)
    check(results, "UDP discovery answered", found, json.dumps(found)[:200])
    # not differences (PROTOCOL.md "state"): `pattern` may be {"count":n}; sound.track / source only while a
    # sound plays (no DFPlayer in QEMU)
    known = re.compile(r"^\$\.pattern: dict, expected str$|^\$\.sound\.(source|track): missing$")
    st = json.loads(ptclient.http_call(addr, "GET", "/api/v1/state", timeout=60)[2])
    errs = ptclient.shape_errors(st, [ptclient._golden(n) for n in ("06-state-idle.json", "07-state-running.json", "08-state-cooldown.json")])
    errs = [e for e in errs if not known.match(e)]
    check(results, "state has the golden shape", not errs, "; ".join(errs[:6]))
    shape_errors = ptclient.shape_errors
    ptclient.shape_errors = lambda got, refs, path="$": [e for e in shape_errors(got, refs, path) if not known.match(e)]
    orig = ptclient.discover
    ptclient.discover = lambda p=0, to=(), wait=0.8: [dict(f, addr=addr) for f in orig(a.udp_port, ("127.0.0.1",), 5)]
    try:
        ptclient.conformance(addr, a.udp_port)
        check(results, "ptclient.py conformance (HTTP, RPC, WebSocket session with events)", True)
    except SystemExit:
        check(results, "ptclient.py conformance (HTTP, RPC, WebSocket session with events)", False, "see the FAIL line above")
    except Exception as e:
        check(results, "ptclient.py conformance (HTTP, RPC, WebSocket session with events)", False, repr(e))
    def rpc(req, timeout=10):
        try:
            return ptclient.rpc(addr, req, timeout)
        except Exception as e:  # a board that went away: the checks report it
            return {"ok": False, "error": repr(e)}
    # an update over HTTP: the same image into the other slot, restart, pending, then valid after the self-check
    rpc({"op": "stop_all"})
    img = open(a.image, "rb").read()
    t, box, n0 = time.time(), {}, len(b.all)

    def up():
        try:
            box["r"] = ptclient.update(addr, img, timeout=600)
        except Exception as e:
            box["r"] = {"error": repr(e)}
    th = threading.Thread(target=up, daemon=True)
    th.start()
    while th.is_alive() and time.time() - t < 600 and not any(FATAL.search(l) for l in b.all[n0:]):
        time.sleep(1)  # the answer, or a panic / watchdog (then the answer never comes)
    r = box.get("r") or {"error": "no answer: " + "; ".join(l for l in b.all[n0:] if FATAL.search(l))[:200]}
    check(results, f"POST /api/v1/update ({len(img)} B)", r.get("ok") is True, f"{time.time() - t:.0f} s wall; {json.dumps(r)[:200]}")
    for s_ in decode(a.elf, a.addr2line, b.all):
        print("     " + s_)
    # the board says "restarting" after the answer; QEMU's own software reset hangs in the second-stage bootloader
    # after it has marked the new image pending, so the next boot would roll it back: power-cycle, as restart() does
    b.wait_for(r"\[proptroller\] restarting", 60, fatal_ok=True)
    b.power_cycle()
    up = boot(b, results, "after the update, power cycled", a.boot_timeout)
    if up:
        u = json.loads(ptclient.http_call(addr, "GET", "/api/v1/info", timeout=60)[2])["update"]
        check(results, "the new image runs from app1, pending", (u["slot"], u["state"]) == ("app1", "pending"), json.dumps(u))
        end = time.time() + 300
        while time.time() < end and u["state"] != "valid":
            time.sleep(5)
            u = json.loads(ptclient.http_call(addr, "GET", "/api/v1/info", timeout=60)[2])["update"]
        check(results, "it marks itself valid after its self-check", u["state"] == "valid", json.dumps(u))
    # MQTT against a broker on the host (10.0.2.2 from inside QEMU)
    b.mark = len(b.all)  # the checks below count panics from here on
    br = Broker(a.broker_port)
    br.start()
    r = rpc({"op": "set_params", "values": [{"key": "mqtt_broker", "value": "10.0.2.2"}, {"key": "mqtt_port", "value": br.port}]})
    check(results, "set_params mqtt_broker=10.0.2.2", r.get("ok"), json.dumps(r)[:200])
    restart(b, rpc)
    if not boot(b, results, "after the MQTT setting (power cycle)", a.boot_timeout):
        return
    end = time.time() + 60
    while time.time() < end and not any(t.endswith("/online") for t, _ in br.pubs):
        time.sleep(1)
    check(results, "MQTT: connected, subscribed, online published", br.subs and any(t.endswith("/online") for t, _ in br.pubs),
          f"{br.connects} connects, subs {br.subs[:4]}, {len(br.pubs)} publishes")
    p = next((t[:-len("/online")] for t, _ in br.pubs if t.endswith("/online")), "proptroller/proptroller")
    if br.conn:
        time.sleep(10)  # let the board finish its hello (every setting, the state)
        for _ in range(2):
            br.publish(p + "/rpc", json.dumps({"v": 1, "id": 77, "op": "info"}))
            end = time.time() + 30
            while time.time() < end and not any(t == p + "/rpc/reply" for t, _ in br.pubs):
                time.sleep(0.5)
        check(results, "MQTT: rpc in, rpc/reply out", any(t == p + "/rpc/reply" for t, _ in br.pubs))
    show_first(a, b, results, addr, rpc, ptclient)


def restart(b, rpc):
    """`reboot` (the board saves what changed and restarts), then a power cycle once it says so: QEMU's own reset
    path is slow or hangs after a software reset (QEMU.md)."""
    rpc({"op": "reboot"})
    b.wait_for(r"\[proptroller\] restarting", 60)
    b.power_cycle()


def gaps(b):
    """The step gaps since the last `mem` (QEMU Ethernet build only): (max ms, count over 15 ms), the mem line."""
    m = b.ask_line("mem", r"free heap", 30)
    g = re.search(r"step gap max (\d+) ms, (\d+) over 15 ms", m or "")
    return (int(g.group(1)), int(g.group(2)), m) if g else (None, None, m)


def show_first(a, b, results, addr, rpc, ptclient):
    """A show runs while a WebSocket client stops reading, then while the broker accepts TCP and never answers."""
    rpc({"op": "put_show", "data": SHOW})
    rpc({"op": "run", "show": 9})
    gaps(b)
    host, port = addr.split(":")
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
    s.connect((host, int(port)))
    s.sendall(b"GET /api/v1/ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n")
    lat, errs, end = [], 0, time.time() + a.stall
    while time.time() < end:  # the stalled client never reads; another client keeps asking
        t = time.time()
        try:
            ptclient.http_call(addr, "GET", "/api/v1/state", timeout=30)
            lat.append(time.time() - t)
        except Exception:
            errs += 1
        time.sleep(1)
    mx, over, m = gaps(b)
    s.close()
    check(results, f"show-first, a WebSocket client that stops reading ({a.stall} s): no step gap over 15 ms",
          mx is not None and over == 0, f"max gap {mx} ms, {over} over 15 ms; other client: {len(lat)} answers, "
          f"slowest {max(lat) if lat else 0:.1f} s wall, {errs} failed")
    silent = Broker(0, silent=True)
    silent.start()
    rpc({"op": "set_params", "values": [{"key": "mqtt_port", "value": silent.port}]})
    restart(b, rpc)
    if not boot(b, results, "with a broker that never answers (power cycle)", a.boot_timeout):
        return
    rpc({"op": "run", "show": 9})
    gaps(b)
    lat, errs, end = [], 0, time.time() + a.stall
    while time.time() < end:
        t = time.time()
        try:
            ptclient.http_call(addr, "GET", "/api/v1/state", timeout=30)
            lat.append(time.time() - t)
        except Exception:
            errs += 1
        time.sleep(1)
    mx, over, m = gaps(b)
    check(results, f"show-first, a broker that accepts TCP and never answers ({a.stall} s): no step gap over 15 ms",
          mx is not None and over == 0, f"{silent.connects} connects; max gap {mx} ms, {over} over 15 ms; HTTP: {len(lat)} answers, "
          f"slowest {max(lat) if lat else 0:.1f} s wall, {errs} failed")
    # the script record written while the show runs: set_script of the largest script (about 7 KB; its CC1 / CC2
    # need the role 'shows'), then put_backup of the board's own backup, with the heap around it (the request heap
    # rule: requestHeapNeed, 4 x body + 48 KB). The backup is parsed and checked in seconds of emulated time: a
    # long timeout
    rpc({"op": "set_params", "values": [{"key": "cc1_role", "value": "shows"}, {"key": "cc2_role", "value": "shows"}]})
    text = busy_script()
    gaps(b)
    r1 = rpc({"op": "set_script", "text": text}, 120)
    mx1, over1, _ = gaps(b)
    bk = rpc({"op": "get_backup"}, 60)
    h0 = heap(b.ask_line("mem", r"free heap", 10))
    r2 = rpc({"op": "put_backup", "data": bk["result"]["data"]}, 180) if bk.get("ok") else bk
    m1 = b.ask_line("mem", r"free heap", 10)  # also the step gaps since h0's mem: across put_backup alone
    h1 = heap(m1)
    g = re.search(r"step gap max (\d+) ms, (\d+) over 15 ms", m1 or "")
    mx2, over2 = (int(g.group(1)), int(g.group(2))) if g else (None, None)
    blen = len(json.dumps(dict(v=1, op="put_backup", data=(bk.get("result") or {}).get("data")), separators=(",", ":")))
    print(f"     put_backup: body {blen} B; heap (free, min, largest block) before {h0[:3] if h0 else None}, after "
          f"{h1[:3] if h1 else None}; answer {json.dumps(r2)[:160]}", flush=True)
    good = r1.get("ok") and (r1.get("result") or {}).get("ok") is True and r2.get("ok")
    check(results, f"show-first, set_script ({len(text)} B) and put_backup of it: no step gap over 15 ms",
          good and mx1 is not None and mx2 is not None and over1 == 0 and over2 == 0,
          f"set_script: max gap {mx1} ms, {over1} over 15 ms; put_backup: max gap {mx2} ms, {over2} over 15 ms"
          if good else json.dumps(r1)[:150] + " " + json.dumps(r2)[:150])
    # a backup of about 20 KB (the script and 14 shows: the shape of finding 1), admitted by the request heap rule
    # with the network up (the whole need against the free heap, one piece against the largest block)
    for i in range(14):
        rpc({"op": "put_show", "data": dict(SHOW, id=20 + i, name=f"QEMU show number {i} with a longer name", enabled=False)})
    bk = rpc({"op": "get_backup"}, 60)
    blen = len(json.dumps(dict(v=1, op="put_backup", data=(bk.get("result") or {}).get("data")), separators=(",", ":")))
    h0 = heap(b.ask_line("mem", r"free heap", 10))
    r3 = rpc({"op": "put_backup", "data": bk["result"]["data"]}, 300) if bk.get("ok") else bk
    m3 = b.ask_line("mem", r"free heap", 10)
    h3 = heap(m3)
    g = re.search(r"step gap max (\d+) ms, (\d+) over 15 ms", m3 or "")
    print(f"     put_backup: body {blen} B; heap (free, min, largest block) before {h0[:3] if h0 else None}, after "
          f"{h3[:3] if h3 else None}; {g.group(0) if g else 'no gap line'}; answer {json.dumps(r3)[:160]}", flush=True)
    check(results, f"show-first, put_backup of a {blen} B body (14 shows and the script): restored",
          bool(r3.get("ok")) and h3 is not None, json.dumps(r3)[:200])
    fatal = [l for l in b.all[getattr(b, "mark", 0):] if FATAL.search(l)]
    check(results, "no panic, watchdog or unexpected reset after the update test", not fatal, "; ".join(fatal[:3]))


def serve(a, b, results):
    """Boot with the network and keep it up for a.serve seconds (for loadtest.py / pageload.py / a browser)."""
    boot(b, results, "boot", a.boot_timeout)
    print(f"     serving: http://127.0.0.1:{a.http_port}/  discovery udp 127.0.0.1:{a.udp_port}  qemu pid {b.p.pid}", flush=True)
    end = time.time() + a.serve
    while time.time() < end and b.p.poll() is None:
        time.sleep(1)
    print("     " + (gaps(b)[2] or "no mem answer"))
    check(results, "no panic, watchdog or unexpected reset while serving", not b.fatal_lines(), "; ".join(b.fatal_lines()[:3]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["full", "smoke", "net", "serve"])
    ap.add_argument("--qemu", required=True)
    ap.add_argument("--flash", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--addr2line", default=shutil.which("xtensa-esp32-elf-addr2line"))
    ap.add_argument("--machine", default="esp32", help="QEMU machine: esp32, or esp32s3 (the bare S3 image)")
    ap.add_argument("--mcu", default="esp32-devkit", help="the info.mcu the image reports (esp32-s3 for the S3)")
    ap.add_argument("--icount", default="2", help="instruction count shift (2 = 4 ns per instruction); 0 = off")
    ap.add_argument("--boot-timeout", type=float, default=60, help="wall seconds")
    ap.add_argument("--soak", type=int, default=0, help="emulated seconds of shows and requests after the smoke test")
    ap.add_argument("--log", default="qemu-serial.log")
    ap.add_argument("--http-port", type=int, default=0, help="net/serve: the host port forwarded to the board's port 80")
    ap.add_argument("--udp-port", type=int, default=47801, help="net/serve: the host port forwarded to UDP 47800")
    ap.add_argument("--broker-port", type=int, default=0, help="net: the MQTT broker's port (0: any free one)")
    ap.add_argument("--image", help="net: the app image for the update test (firmware.bin)")
    ap.add_argument("--stall", type=int, default=60, help="net: wall seconds of each show-first test")
    ap.add_argument("--serve", type=int, default=600, help="serve: wall seconds to stay up")
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    if a.icount == "0":
        a.icount = None
    if a.mode in ("net", "serve") and not a.http_port:
        a.http_port = 18080
    results = []
    with open(a.log, "w") as log:
        b = Board(a, log)
        try:
            {"full": run_full, "smoke": run_smoke, "net": run_net, "serve": serve}[a.mode](a, b, results)
            time.sleep(0.5)
            fatal = b.fatal_lines()
            if a.mode == "smoke":
                check(results, "no panic, watchdog or unexpected reset", not fatal, "; ".join(fatal[:3]))
                for s in decode(a.elf, a.addr2line, b.all):
                    print("     " + s)
        finally:
            b.stop()
    failed = [n for n, ok in results if not ok]
    print(f"== {a.mode}: {len(results) - len(failed)} of {len(results)} checks passed (serial log: {a.log})")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
