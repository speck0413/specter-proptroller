#!/usr/bin/env python3
"""Worst stack paths of the firmware, from GCC's call graph (-fcallgraph-info=su: each function's frame and its
direct calls), checked against each task's stack with a margin. Run after `pio run` (the board environments keep
the .ci / .su files next to the objects):

    python3 tools/stack_check.py [--margin 50] [--report] [--env picow] [--build-dir DIR]

The builds are read from --build-dir, else PLATFORMIO_BUILD_DIR (as `pio run` uses it), else .pio/build.

Every board build runs it for itself (tools/stack_post.py, a PlatformIO post-build step) and fails when a path
does not fit; the ESP32 figures need a Pico W build next to them (its call graph) and are checked with it.

The call graph is the RP2040 build's (`pico`, `picow`, `pico2w`; the ESP32's GCC 8 has no -fcallgraph-info): the
ESP32 estimate walks the same graph with the Xtensa frames from its .su files wherever a function of this
library matches by file:line:col (the rest keep their RP2040 frames). Indirect calls (virtual functions,
function pointers) are not in the graph: each path below names its hops through them, and a path is the sum of
its hops (the longest direct path from one hop to the caller of the next) plus the worst path from the last.
Recursion is cut at the first repeat, except an LED pattern's (pattern_vm.cpp: exec calls itself through a
pattern's own calls and its list built-ins, at most PATTERN_MAX_DEPTH deep), which is counted that many times
on the stack that renders the strip (the Pico 2 W's core 0, the ESP32's pixel task). Figures are lower bounds of what a board uses; the margin covers the rest
(interrupt frames, the C library's printf, LittleFS's internals behind its function pointers).

Exit 1 when a path does not fit its stack with the margin. Stacks (bytes):
  ESP32 loop task (requests, the network, the serial port)   PROPTROLLER_LOOP_STACK (Proptroller.cpp)
  ESP32 step task (the 10 ms step and 1 ms samples)          TICK_TASK_STACK (Proptroller.cpp)
  Pico core 0 (requests, the network, the serial port)       8 KB (arduino-pico: scratch X + Y), less 1 KB for
                                                             lwIP's background interrupt on the Pico W / 2 W
  Pico core 1 (the step)                                     8 KB (core1_separate_stack)
  ESP32 pixel task (the strip's frames, an LED pattern)      PIXEL_TASK_STACK (Proptroller.cpp)
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def build_root():
    """The folder holding <env>/: --build-dir, else PLATFORMIO_BUILD_DIR (relative to the project), else .pio/build."""
    if "--build-dir" in sys.argv:
        d = sys.argv[sys.argv.index("--build-dir") + 1]
    else:
        d = os.environ.get("PLATFORMIO_BUILD_DIR") or os.path.join(".pio", "build")
    return os.path.join(HERE, os.path.expanduser(d))


BUILD = build_root()

NODE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')


def linked_symbols(env):
    """The symbols in the image (a compiled function that is not linked never runs)."""
    import subprocess
    nm = glob.glob(os.path.expanduser("~/.platformio/packages/toolchain-rp2040-earlephilhower/bin/*-nm"))
    elf = os.path.join(BUILD, env, "firmware.elf")
    if not nm or not os.path.exists(elf):
        return None
    out = subprocess.run([nm[0], elf], capture_output=True, text=True).stdout
    return {line.split()[-1] for line in out.splitlines() if line.strip()}


def load_graph(env):
    nodes, edges, where = {}, {}, {}
    linked = linked_symbols(env)
    files = glob.glob(os.path.join(BUILD, env, "**", "*.ci"), recursive=True)
    if not files:
        return None
    for f in files:
        for line in open(f, errors="replace"):
            m = NODE.search(line)
            if m:
                title, label = m.group(1), m.group(2).split("\\n")
                if len(label) >= 3:
                    if linked is not None and title.rsplit(":", 1)[-1] not in linked:
                        continue  # compiled, inlined everywhere or not linked
                    sz = re.match(r"(\d+) bytes", label[2])
                    nodes[title] = (label[0], int(sz.group(1)) if sz else 0)
                    where[title] = label[1]
                elif title not in nodes:
                    nodes.setdefault(title, (label[0], None))
                continue
            m = EDGE.search(line)
            if m:
                edges.setdefault(m.group(1), set()).add(m.group(2))
    return nodes, edges, where


def load_su(env):
    """base name:line:col -> frame bytes, from the .su files."""
    out = {}
    for f in glob.glob(os.path.join(BUILD, env, "**", "*.su"), recursive=True):
        for line in open(f, errors="replace"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 2:
                continue
            m = re.match(r"(.+?:\d+:\d+):", parts[0])
            if m and parts[1].isdigit():
                out[os.path.basename(m.group(1))] = int(parts[1])  # GCC 8 writes the file's base name
    return out


class Walker:
    def __init__(self, graph, override=None):
        self.nodes, self.edges, self.where = graph
        self.override = override or {}
        self.memo = {}

    def frame(self, t):
        name, sz = self.nodes.get(t, (t, None))
        w = self.where.get(t, "")
        if w.startswith("lib/proptroller/") and os.path.basename(w) in self.override:
            return self.override[os.path.basename(w)]
        return sz or 0

    def find(self, text):
        hits = [t for t, (name, sz) in self.nodes.items() if sz is not None and text in name]
        if not hits:
            raise SystemExit("stack_check: no function matches %r" % text)
        return max(hits, key=lambda t: self.worst(t)[0])

    def worst(self, t, stack=()):
        if t in self.memo:
            return self.memo[t]
        if t in stack:
            return (0, [])
        best = (0, [])
        for c in self.edges.get(t, ()):
            if c in self.nodes and self.nodes[c][1] is not None:
                r = self.worst(c, stack + (t,))
                if r[0] > best[0]:
                    best = r
        res = (self.frame(t) + best[0], [t] + best[1])
        if not stack:
            self.memo[t] = res
        return res

    def to(self, a, b, stack=()):
        """Longest direct path from a down to (not including) b, or None when b is not reached directly."""
        if a == b:
            return (0, [])
        if a in stack:
            return None
        best = None
        for c in self.edges.get(a, ()):
            if c in self.nodes:
                r = self.to(c, b, stack + (a,))
                if r is not None and (best is None or r[0] > best[0]):
                    best = r
        return None if best is None else (self.frame(a) + best[0], [a] + best[1])

    def chain(self, hops):
        ts = [self.find(h) for h in hops]
        total, path = 0, []
        for a, b in zip(ts, ts[1:]):
            r = self.to(a, b)
            if r is None:
                r = (self.frame(a), [a])  # an indirect call from a itself
            total += r[0]
            path += r[1]
        w = self.worst(ts[-1])
        return total + w[0], path + w[1]

    def name(self, t):
        return self.nodes.get(t, (t, 0))[0]


# Each stack and the paths that run on it: lists of hops (substrings of the function names).
REQUEST_HOPS = [
    ["HttpServer::poll(", "HttpServer::serveHttp(", "LockedApp::call(", "PropApp::call(", "pt::Controller::call("],
    ["HttpServer::poll(", "HttpServer::serveWs(", "LockedApp::callFrom(", "PropApp::callFrom(", "pt::Controller::callFrom("],
    ["HttpServer::poll(", "HttpServer::serveUpload(", "LockedApp::updateWrite(", "PropApp::updateWrite(", "pt::Controller::updateWrite("],
    ["HttpServer::poll(", "HttpServer::serveUpload(", "LockedApp::updateFinish(", "PropApp::updateFinish(", "pt::Controller::updateFinish("],
    ["Proptroller::serialByte(", "PropApp::callSerial(", "pt::Controller::callSerial("],
    ["Proptroller::serialByte(", "PropApp::consoleLine(", "pt::Controller::consoleLine("],
    ["ArduinoNet::poll(", "PropApp::mqttOnMessage(", "pt::Controller::mqttOnMessage("],
    # a request that saves: the record goes through Storage (virtual) into LittleFS
    ["pt::Controller::call(", "pt::Controller::scriptWrite(", "pt::ABStorage::save(", "pt::LittleFsIo::write("],
]
USB_REQUEST_HOPS = [h for h in REQUEST_HOPS if not h[0].startswith(("HttpServer", "ArduinoNet"))]
# everything loop() reaches by direct calls (Wi-Fi, mDNS, MQTT, the serial port)
LOOP_ALL = [[]]
# Pico W / Pico 2 W: lwIP's input runs in arduino-pico's background interrupt on core 0, on top of whatever core 0
# runs; lwIP is a prebuilt library (no frames in the graph): this allowance covers ip4_input / tcp_input / the
# receive callback
LWIP_IRQ_ALLOWANCE = 1024
STEP_HOPS = [
    ["Proptroller::tick(", "pt::Controller::tick("],
    ["Proptroller::tick(", "pt::Controller::tick(", "pt::Controller::saveNow(", "pt::ABStorage::save(", "pt::LittleFsIo::write("],
    ["Proptroller::sample(", "pt::Controller::sample("],
]
BOOT_HOPS = [["Proptroller::start(", "pt::Controller::Controller("], ["Proptroller::start(", "pt::Controller::setScriptStore("]]

# the shell frames above the hops (setup / loop / the tasks), from the same graph
LOOP_ROOT = "Proptroller::update("


def stack_size(name, default):
    src = open(os.path.join(HERE, "lib/proptroller/src/Proptroller.cpp")).read()
    m = re.search(r"static const uint32_t %s = ([0-9* ]+);" % name, src)
    return eval(m.group(1)) if m else default


def pattern_depth():
    src = open(os.path.join(HERE, "lib/proptroller/src/proptroller/pattern_vm.h")).read()
    m = re.search(r"PATTERN_MAX_DEPTH = (\d+);", src)
    return int(m.group(1)) if m else 0


class PatternWalker(Walker):
    """Counts an LED pattern's interpreter PATTERN_MAX_DEPTH times: exec and the built-in that calls it again."""

    def chain(self, hops):
        total, path = Walker.chain(self, hops)
        ex, bi = self.find("pt::PatternVM::exec("), self.find("pt::PatternVM::builtin(")
        n = pattern_depth()
        return total + n * (self.frame(ex) + self.frame(bi)), path + [ex, bi] * n


# the strip's frame with a pattern playing: from where the pixel side is called down to the interpreter
PATTERN_HOPS = [["pt::Controller::pixelService(", "pt::Controller::patternRender(", "pt::PatternVM::frame("]]


def report(env, w, stacks, margin, verbose):
    ok = True
    print("== %s" % env)
    for label, size, groups in stacks:
        worst = (0, [], None)
        for hops in groups:
            try:
                total, path = w.chain(hops)
            except SystemExit:
                continue  # a hop this build does not have (no network on the plain Pico)
            if total > worst[0]:
                worst = (total, path, hops)
        limit = size * 100 // (100 + margin)
        fits = worst[0] <= limit
        ok &= fits
        print("  %-34s worst %6d B of %6d B (limit with %d %% margin %d B)  %s" % (label, worst[0], size, margin, limit, "ok" if fits else "OVER"))
        if verbose and worst[1]:
            for t in worst[1]:
                print("      %6d  %s" % (w.frame(t), w.name(t)[:110]))
    return ok


def main():
    margin = 50
    verbose = "--report" in sys.argv
    if "--margin" in sys.argv:
        margin = int(sys.argv[sys.argv.index("--margin") + 1])
    only = sys.argv[sys.argv.index("--env") + 1] if "--env" in sys.argv else None
    loop_stack = stack_size("PROPTROLLER_LOOP_STACK", 8192)
    tick_stack = stack_size("TICK_TASK_STACK", 8192)
    pico_core0, pico_core1 = 8192, 8192
    shell = ["loop()", LOOP_ROOT]
    ok = True
    ran = False
    for env, usb in (("pico", True), ("picow", False), ("pico2w", False)):
        if only and only != env and not (only == "esp32dev" and env == "picow"):
            continue
        g = load_graph(env)
        if not g:
            continue
        ran = True
        w = Walker(g)
        req = [[LOOP_ROOT] + h if h[0] != LOOP_ROOT else h for h in (USB_REQUEST_HOPS if usb else REQUEST_HOPS)]
        irq = 0 if usb else LWIP_IRQ_ALLOWANCE
        stacks = [
            ("core 0: a request (loop)%s" % ("" if usb else " + lwIP IRQ"), pico_core0 - irq, [[LOOP_ROOT] + h for h in (USB_REQUEST_HOPS if usb else REQUEST_HOPS)] + [[LOOP_ROOT]]),
            ("core 0: boot (setup)", pico_core0, BOOT_HOPS),
            ("core 1: the step", pico_core1, STEP_HOPS),
        ]
        if env == "pico2w":
            # the strip's frames run in loop() on core 0 (with lwIP's interrupt on top)
            stacks.append(("core 0: a pattern frame (loop) + lwIP IRQ", pico_core0 - irq,
                           [[LOOP_ROOT] + h for h in PATTERN_HOPS]))
        if only != "esp32dev":
            ok &= report(env, w, stacks, margin, verbose)
            if env == "pico2w":
                ok &= report(env + " (an LED pattern at its deepest)", PatternWalker(g), stacks[-1:], margin, verbose)
                su = load_su("esp32dev")
                if su:
                    pixel_stack = stack_size("PIXEL_TASK_STACK", 4096)
                    ok &= report("esp32dev (Xtensa frames on the Pico 2 W graph, a pattern at its deepest)", PatternWalker(g, su),
                                 [("ESP32 pixel task", pixel_stack, PATTERN_HOPS)], margin, verbose)
        if env == "picow" and only in (None, "picow", "esp32dev"):
            su = load_su("esp32dev")
            if su:
                we = Walker(g, su)
                stacks = [
                    ("ESP32 loop task: a request", loop_stack, [[LOOP_ROOT] + h for h in REQUEST_HOPS] + [[LOOP_ROOT]]),
                    ("ESP32 loop task: boot (setup)", loop_stack, BOOT_HOPS),
                    ("ESP32 step task", tick_stack, STEP_HOPS),
                ]
                ok &= report("esp32dev (Xtensa frames on the RP2040 graph)", we, stacks, margin, verbose)
    if not ran:
        print("stack_check: no call graph (.ci files) yet: build a Pico env (`pio run -e picow`)")
        return 0 if only == "esp32dev" else 1
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
