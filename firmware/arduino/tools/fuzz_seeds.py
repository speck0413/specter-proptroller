#!/usr/bin/env python3
"""Seed corpora for the fuzz targets, from what the repository already has: the golden session's requests and
answers, the show-script golden set and example scripts, the record fixtures, HTTP samples, the golden backup,
simulator images. Writes .pio/fuzz/<target>/corpus/ (generated, not committed)."""
import glob
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def put(target, data):
    d = os.path.join(HERE, ".pio", "fuzz", target, "corpus")
    os.makedirs(d, exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    with open(os.path.join(d, hashlib.sha1(data).hexdigest()[:16]), "wb") as f:
        f.write(data)


def read(p):
    with open(p, "rb") as f:
        return f.read()


def ws_frame(payload, op, mask):
    n = len(payload)
    head = bytes([0x80 | op])
    m = 0x80 if mask else 0
    if n < 126:
        head += bytes([m | n])
    elif n <= 0xFFFF:
        head += bytes([m | 126]) + n.to_bytes(2, "big")
    else:
        head += bytes([m | 127]) + n.to_bytes(8, "big")
    if not mask:
        return head + payload
    key = b"\x12\x34\x56\x78"
    return head + key + bytes(c ^ key[i % 4] for i, c in enumerate(payload))


def lz_literals(text):
    """An lz.h stream of literals only (every flag bit 1)."""
    out = b""
    for i in range(0, len(text), 8):
        chunk = text[i:i + 8]
        out += bytes([(1 << len(chunk)) - 1]) + chunk
    return out


def main():
    t = os.path.join(HERE, "test")
    session = (read(os.path.join(t, "test_golden/session.inc")) + read(os.path.join(t, "test_golden/test_main.cpp"))).decode()
    for m in re.finditer(r'R"__\((.*?)\)__"', session, re.S):
        put("request", m.group(1))
    for p in glob.glob(os.path.join(t, "golden", "*.json")):
        put("request", read(p))
    scripts = glob.glob(os.path.join(HERE, "..", "show-script-golden", "cases", "*", "script.txt"))
    scripts += glob.glob(os.path.join(HERE, "..", "show-script-examples", "*.txt"))
    for p in scripts:
        put("script", read(p))
    put("records", '{"version":1,"settings":{"name":"Door"},"shows":[]}')
    for p in glob.glob(os.path.join(t, "fixtures", "records", "script*")):
        put("script_record", read(p))
    for p in glob.glob(os.path.join(t, "fuzz-seeds", "http", "*")):
        put("http", read(p))
    b = json.loads(read(os.path.join(t, "golden", "94-fl-get-backup.json")))
    put("backup", json.dumps(b["result"]["data"], separators=(",", ":")))
    hdr = '{"mcu":"sim","fw_version":"0.2.0","fw_build":"seed","self_check":"pass"}'
    put("image", "PROPTROLLER-SIM-IMAGE\n" + hdr + "\n" + "x" * 200)
    put("image", bytes([0xE9, 5, 2, 0x20] + [0] * 28 + [0x32, 0x54, 0xCD, 0xAB] + [0] * 64))
    # the newer targets: heads, frames, MQTT messages, profiles, settings records, packed streams, boot states
    for p in glob.glob(os.path.join(t, "fuzz-seeds", "http", "*")):
        put("http_head", read(p).split(b"\r\n\r\n")[0] + b"\r\n\r\n")
    for payload, op, mask in ((b"", 9, False), (b'{"op":"info"}', 1, True), (b"x" * 300, 2, True), (b"\x03\xe8", 8, False)):
        put("ws_frame", bytes([3 if mask else 0]) + ws_frame(payload, op, mask) + ws_frame(b"ok", 10, mask))
    for m in re.finditer(r'R"__\((\{"op":.*?)\)__"', session, re.S):
        put("mqtt", "rpc\n" + m.group(1))
        if '"op":"set_profile"' in m.group(1):
            put("profile", json.dumps(json.loads(m.group(1))["data"], separators=(",", ":")))
    for line in ("config/get/name", "config/set/name\nDoor", "config/set/out1_on_ms\n250", "config/set/effect\npulse_and_recover",
                 "control/trigger", "control/restart", "status/get/run_count", "cue\n  door  ", "/proptroller/broadcast/getName",
                 'script/check\n{"text":"on TRIG1:\\n  OUT1 on 1s\\n","sounds":{}}', 'script/set\n{"text":"","sounds":{}}'):
        put("mqtt", line)
    put("settings", json.dumps(b["result"]["data"].get("settings", {}), separators=(",", ":")))
    put("settings", '{"name":"Door","soft_start_ms":[0,10,20,30],"roles":["shows","not_used"],"sketch":{"a":"1"},'
                    '"profile":{"topic_prefix":"p","aliases":[{"alias":"x","key":"out1_on_ms","scale":10}]}}')
    for text in (b"", b"a", b"abcabcabcabcabcabcabc", read(scripts[0]) if scripts else b"on TRIG1:\n"):
        put("lz", bytes([len(text) & 0xFF, len(text) >> 8 & 0xFF]) + lz_literals(text))
    for magic in (0xF0, 0xC0, 0xD0, 0xE0, 0xFF):
        put("picoboot", bytes([magic, 0xFF] + [0xFF] * 62) + (2).to_bytes(4, "little") + (0xFFFFFFFD).to_bytes(4, "little"))
    put("picoboot", bytes([0xF0, 0xFF, 0, 0, 0] + [0xFF] * 59) + (4).to_bytes(4, "little") + (0xFFFFFFFB).to_bytes(4, "little") + b"\x00" * 64)
    console = re.search(r'for \(const char\* l : \{(.*?)\}\)', read(os.path.join(t, "test_console_golden/test_main.cpp")).decode(), re.S)
    lines = re.findall(r'"((?:[^"\\]|\\.)*)"', console.group(1)) if console else ["help", "status", "settings"]
    put("console", "\n".join(lines))
    for line in lines:
        put("console", line)
    put("console", '{"proptroller":"discover","v":1}')
    for line in lines:
        put("locked", line)
    put("locked", '{"op":"set_params","values":[{"key":"name","value":"x"}]}\n{"op":"set_params","pin":"door-42","values":[{"key":"name","value":"y"}]}')
    put("locked", 'control/trigger\ncue fog\nconfig/set/name Door\n{"op":"put_backup","data":{}}\n{"op":"factory_reset"}')
    for p in glob.glob(os.path.join(t, "fuzz-seeds", "http", "*")):
        put("http_small", read(p) * 2)
    for target in ("request", "script", "records", "script_record", "http", "backup", "image", "http_head", "ws_frame", "mqtt",
                   "profile", "settings", "lz", "picoboot", "console", "locked", "http_small"):
        for p in glob.glob(os.path.join(t, "fuzz-regressions", target, "*")):
            put(target, read(p))
    return 0


if __name__ == "__main__":
    sys.exit(main())
