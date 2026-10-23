#!/usr/bin/env python3
"""Bench check for a board with nothing connected to its outputs: flash an image, time the boot, read the heap and
stack high-water marks, and run a serial smoke test. Standard library plus pyserial (PlatformIO's Python has it):

    ~/.platformio/penv/bin/python tools/hw_check.py --port /dev/ttyUSB0 --env esp32dev
    ~/.platformio/penv/bin/python tools/hw_check.py --port /dev/ttyACM0 --env picow --no-flash

Every firmware built on the Proptroller library prints "ready after N ms; free heap ..." at the end of begin()
and answers "mem" on the USB serial port. --console sends plain console lines instead of the JSON smoke test (for
a firmware with its own console). Prints a summary; exit code 1 when a check fails.
"""
import argparse, json, os, re, subprocess, sys, time

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing: run this with PlatformIO's Python (~/.platformio/penv/bin/python)")

HERE = os.path.dirname(os.path.abspath(__file__))
BOOT_TIMEOUT_S = 20
IDLE_S = 60


def flash(project, env, port):
    print(f"== flashing {env} from {project}")
    r = subprocess.run(["pio", "run", "-d", project, "-e", env, "-t", "upload", "--upload-port", port])
    if r.returncode != 0:
        sys.exit(f"flashing {env} failed")


def reset(ser, esp32):
    if esp32:  # the DevKit's auto-reset circuit: RTS low pulses EN
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
    else:  # RP2040 over USB: reopening the port after a 1200 baud touch would enter the bootloader; just reconnect
        ser.dtr = False
        time.sleep(0.1)
        ser.dtr = True


def read_until(ser, pattern, timeout):
    end = time.time() + timeout
    seen = []
    while time.time() < end:
        line = ser.readline().decode("utf-8", "replace").strip()
        if not line:
            continue
        seen.append(line)
        print("   <", line[:160])
        if re.search(pattern, line):
            return line, seen
    return None, seen


def ask(ser, text, pattern=r".", timeout=3):
    print("   >", text)
    ser.reset_input_buffer()
    ser.write((text + "\n").encode())
    line, _ = read_until(ser, pattern, timeout)
    return line


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--env", required=True, help="PlatformIO environment (esp32dev, pico, picow, pico2w, ...)")
    ap.add_argument("--project", default=os.path.dirname(HERE), help="PlatformIO project directory (default: firmware/arduino)")
    ap.add_argument("--no-flash", action="store_true")
    ap.add_argument("--console", nargs="*", help="console lines to send instead of the JSON smoke test")
    ap.add_argument("--idle", type=int, default=IDLE_S, help="seconds to idle before the second memory reading")
    a = ap.parse_args()
    esp32 = a.env.endswith("esp32dev")
    if not a.no_flash:
        flash(a.project, a.env, a.port)
        time.sleep(2)
    fails = []
    with serial.Serial(a.port, 115200, timeout=0.5) as ser:
        t0 = time.time()
        reset(ser, esp32)
        line, _ = read_until(ser, r"ready after \d+ ms", BOOT_TIMEOUT_S if esp32 else 5)
        host_boot = (time.time() - t0) * 1000
        mem1 = ask(ser, "mem", r"free heap")
        # an RP2040 over USB does not reset from the host side: the boot line went by before the port opened, so the
        # boot time comes from the "mem" answer ("... ready after N ms")
        boot = re.search(r"ready after (\d+) ms", line or mem1 or "")
        if not boot:
            fails.append("no boot time ('ready after' line or in the 'mem' answer)")
        if not line:
            host_boot = float("nan")
        smoke = []
        if a.console is not None:
            for c in a.console:
                ans = ask(ser, c)
                ok = ans is not None and not ans.startswith("error")
                smoke.append((c, ok))
        else:
            for req in ['{"op":"info"}', '{"op":"state"}', '{"op":"get_params"}', '{"op":"list_shows"}',
                        '{"op":"set_params","values":[{"key":"ambient_volume","value":14}]}',
                        '{"op":"set_params","values":[{"key":"ambient_volume","value":15}]}', '{"op":"stop_all"}']:
                ans = ask(ser, req, r'"ok"')
                ok = False
                try:
                    ok = bool(ans) and json.loads(ans).get("ok") is True
                except ValueError:
                    pass
                smoke.append((req, ok))
            ans = ask(ser, "status")
            smoke.append(("status", ans is not None))
        print(f"== idling {a.idle} s")
        time.sleep(a.idle)
        mem2 = ask(ser, "mem", r"free heap")
    fails += [f"smoke: {c}" for c, ok in smoke if not ok]
    print("\n== summary", a.env)
    print(f"boot: firmware ready after {boot.group(1) if boot else '?'} ms (host saw {host_boot:.0f} ms from reset)")
    print(f"memory after boot: {mem1}")
    print(f"memory after {a.idle} s idle: {mem2}")
    for c, ok in smoke:
        print(f"{'ok  ' if ok else 'FAIL'} {c}")
    if fails:
        print("FAILED:", "; ".join(fails))
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
