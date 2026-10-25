# The firmware in an emulator (QEMU, Renode)

What runs where before a board is on the bench, and what that does and does not prove. Measured 2026-10-21 on
1ed6c66 with Espressif QEMU `esp-develop-9.2.2-20260417` and Renode 1.17.0.

## How to run

    tools/qemu/run-esp32.sh              # build, merge, boot, smoke test: about 2 minutes, exit code 1 on failure
    tools/qemu/run-esp32.sh --soak 300   # and 300 s of emulated time with shows and requests (23 minutes here)
    tools/qemu/run-esp32.sh --s3         # the bare ESP32-S3 image on QEMU's esp32s3 machine (section below)

VS Code: Terminal > Run Task > "ESP32 in QEMU". The first run downloads Espressif's QEMU release (sha256 pinned in
the script) into `$HOME/.cache/proptroller-qemu`; nothing fetched goes into the repo. Serial logs:
`.pio/build/esp32dev_qemu_wifi/qemu-serial.log` (stage 1) and `.pio/build/esp32dev_qemu/qemu-serial.log` (stage 2).

- **Stage 1, the board's firmware with Wi-Fi (env `esp32dev_qemu_wifi`).** The `esp32dev` firmware built for
  DIO 40 MHz flash instead of the board's QIO 80 MHz: QEMU's flash model cannot run a QIO app (the app's flash
  init asserts in `do_core_init`, whatever the merged header says), so the release image itself is checked on the
  bench, not here. The 4 MB image a first USB flash writes (FLASH-MAP.md: bootloader 0x1000, partition table
  0x8000, `boot_app0` otadata 0xE000 as `pio run -t upload` writes it, app 0x10000, the rest erased), merged with
  the bootloader's header kept as `release.py` does; the partition table matches FLASH-MAP's sha256, the DIO
  bootloader is not the one listed there. Booted
  with `qemu-system-xtensa -machine esp32 -icount shift=2`. It must reach the banner (bootloader, app, LittleFS
  format and mount, settings, controller, step task). It then stops at the Wi-Fi start: QEMU has no radio, and the
  closed PHY code (`register_chipv7_phy`) faults on a register block QEMU does not map. The check passes only when
  that is the only fault (the backtrace is decoded with the ELF).
- **Stage 2, the same firmware without Wi-Fi (env `esp32dev_qemu`, `-DPROPTROLLER_NO_WIFI`; never flashed).**
  The JSON-lines protocol on UART0, driven by `tools/qemu/drive.py` (standard library only): info, state,
  set_params, put_show, run, check_script (the largest script the limits allow), set_script, get_backup and
  put_backup, two lines over the serial limit (44 and 50 KB: `bad_request`, the heap back after), the console,
  `mem`, `reboot`, and that the name, the show and the script survive it. Any panic,
  watchdog or unexpected reset fails the run.

### The clock: `-icount shift=2`

With `-icount` QEMU's virtual clock follows the instruction count: shift 2 is 2^2 = 4 ns per instruction,
250 MIPS, the nearest power of two to the ESP32's 240 MHz at one instruction per cycle. Without it the guest's
timers run on the host clock, and slow emulated flash writes trip the interrupt and RTC watchdogs (seen:
TG1WDT during the first-boot format, TG0WDT in the bootloader): artifacts, gone with icount. What the numbers
mean:

- Guest milliseconds of CPU work are instruction counts times 4 ns. A real LX6 executing from flash through the
  cache (QIO 80 MHz on the board) does less than one instruction per cycle, so **CPU-bound work takes longer on
  the board**
  (the bench gives the factor: time the same request there; `--icount 3`, 8 ns, is a pessimistic bracket).
  Waits (`delay()`, timers, task delays) are exact in virtual time.
- No cache and no flash wait states; flash erase and program times are whatever QEMU's flash model does, not
  the chip's. A long flash operation that would trip a watchdog on the board need not show here.
- Boot before the app is not representative: about 1.4 s of guest time on power-on and 8-18 s after a software
  reset (QEMU's reset path); after a panic the emulated board then resets on the interrupt watchdog (TG1WDT)
  before the app starts, again and again, while a fresh QEMU on the same flash boots normally. Treat as an
  emulator artifact; the bench checks `reboot` time and that a board comes back after a panic.
- The two cores run interleaved on one host thread: cross-core races are not exercised.
- When both cores sleep, virtual time runs at wall speed; when they work, the guest ran at about 0.2-0.3 of real
  time on the test host (300 s of guest time took 22 minutes 54 s).

## What it proves

- The real firmware boots from a first-flash image (stage 1 at DIO 40 MHz, above): the bootloader built with
  rollback accepts the merged image,
  the partition table matches the app, the app starts, LittleFS is formatted on a blank board and mounted,
  settings load (defaults on a new board, stored values after a reboot), the controller and its 10 ms step task
  start, all before Wi-Fi.
- Without Wi-Fi: every protocol request above answers correctly over the UART, a stored setting, show and script
  survive `reboot` (the A/B stores read back), no watchdog, stack guard or heap check fires in several minutes of
  shows running and requests looping, and the heap does not leak.
- The pixel driver failing (QEMU's RMT) is reported ("pixel driver not available") and the firmware carries on
  with its heap intact (both stages check that the failed install ran; stage 2 checks that the ready line after
  it, which walks the heap, comes without a fault). The RMT driver is installed on a short task of its own on core 0
  with a 4 KB stack (`hal.cpp` `BoardPixels::begin`), not through `esp_ipc_call_blocking`: the IPC task's stack is
  1 KB, the driver's failure message overflows it into the heap, and the next heap walk (`ESP.getMaxAllocHeap` in
  the ready line's `memoryReport`) faults with LoadStorePIFAddrError. A failed install on a board does the same.

## What it cannot prove (bench items)

- Anything about the radio: joining, the setup access point, roaming, RF timing, ESP-NOW, the Wi-Fi driver's
  own buffers and tasks. The network code above the interface is tested on QEMU's OpenCores Ethernet instead
  (section Network below); mDNS is not tested.
- Heap and stack with Wi-Fi up: the numbers below are **without the Wi-Fi/lwIP stack** and are optimistic.
- Real timing (see the clock above), flash erase times against the watchdog, UART overruns: QEMU's UART applies
  back-pressure and no baud pacing on receive, so a request the board would lose at 115200 baud arrives intact, and transfers take no guest time.
- Outputs, inputs, PWM, the DFPlayer, pixels (RMT), DMX: QEMU models no pin levels; its UART has no TX break,
  so a stored DMX script stalls the DMX driver (`uart_write_bytes_with_break` waits for a break-done interrupt
  that never comes; the test uses a script on the outputs instead).
- The efuse MAC is blank in QEMU (id `000000000000`).

## Measurements (QEMU, esp32dev_qemu, without Wi-Fi, icount shift 2)

| | |
|---|---|
| ready after (app start to end of `begin()`) | 274 ms on a blank flash (LittleFS format), 380-580 ms with stored settings, show and script |
| free heap after boot | 324,204 B (min 318,024, largest block 110,580); 226,888 B with 10 shows of 2 tracks x 20 cues and a 7 KB script stored |
| stack free after boot | loop task 8,924 of 12,288 B, step task 7,664 of 8,192 B |
| `check_script`, largest script (8,160 B, DMX) | 1.5 s guest; heap low-water 213 KB: about 105 KB at its peak |
| `set_script`, 7,112 B on the outputs | 1.35 s guest (1.43 s at shift 3) |
| `put_backup`, 1 show + that script (10 KB line) | 2.0 s guest; heap low-water 183 KB: about 140 KB at its peak |
| `put_backup`, 10 shows + that script (21 KB line) | **aborts: out of memory** (finding 1) |
| `get_backup` | 10-50 ms guest |
| stack free, lowest seen | loop 7,100 B (5.2 KB used; the static walk says 3.9 KB), step 5,440 B (2.7 KB used); the FreeRTOS canary and the end-of-stack watchpoint never fired |
| soak, 300 s guest | 200 rounds of 7 requests (state, list_shows, get_params, put_show, run, check_script 7 KB, get_backup) with a show firing every 1.5 s: 0 unanswered, no panic, watchdog or reset. Free heap after the warm-up 267,288 B (60 s), 266,660-267,304 B after, 266,884 B at 300 s: no leak. Min free heap 211,112 B |
| state request round trip (soak) | median 0 ms, max 11 ms guest over 80 rounds: the loop answers within a tick; QEMU's UART moves the 2 KB answer in no guest time (no baud pacing either way), so on the board add about 0.2 s at 115200 baud |

### LED patterns (QEMU, esp32dev_qemu, icount shift 2, 600 lights)

QEMU has no RMT, and the image as built does not boot there: `rmt_translator_init` fails ("RMT translator buffer
create fail"), and the heap check in the first `mem` report then panics (seen at af9ab35 too, before the pattern
player). These numbers come from the same image with `BoardPixels::begin()` returning false at once (a local
build only, `-DPT_QEMU_NO_RMT`, not in the tree): the strip's frames are rendered and not sent.

| | no pattern (strip off) | the heavy golden pattern (`pattern-golden/cases/heavy-600`) |
|---|---|---|
| step | max 33-39 us (average 16), 0 of about 1,030 over 1 ms | max 40-45 us (average 33), 0 of about 1,020 over 1 ms |
| 1 ms sample | max 7-14 us (average 2) | max 17-24 us (average 15) |
| pattern frame | | average 32.5 ms, max 36.6 ms, 42,021 steps of the 98,304 budget; about 150 frames in 10 s (the rest after a long frame) |
| free heap | 258 KB | 228 KB (the player 23 KB, the stored list and the hand-over copy) |

Per piece, 600 lights, average frame: an empty `render` 1.6 ms, `rgb()` 4.4 ms, `hsv()` 6.2 ms, `sin()` 7.6 ms,
some arithmetic (12,600 steps) 10.7 ms. A runaway pattern (`while (1) {}` in `render`) stops on its step budget
after 153 ms with the event-log line, no watchdog and no panic.

In QEMU both cores run on one host thread, so while core 0 renders, core 1's timer interrupt and wakes come late
(the step's wake gap reaches 6 ms there, and the 1 ms samples are taken once per step): an emulator artifact,
not seen as late steps (0 over 1 ms). Board timing is a bench item: run `mem` with a heavy pattern on 600 lights.

The heap peaks are about 3.5 times (check) and 2.6 times (put_backup) the old host estimates (30 KB and
53 KB, 64-bit host); they include the serial path's line buffer and answer strings. With Wi-Fi up the board
starts with less.

## Findings, most severe first

1. **`put_backup` of a full board's own backup crashes it (out of memory), even without Wi-Fi.** With 10 shows
   of 2 tracks x 20 cues (the store refuses the 11th: "storage full") and a 7 KB script, `get_backup` gives a
   21 KB document; `put_backup` of it aborts in `scriptCompile` (`operator new` fails, no exceptions, so
   `abort()`), from `Controller::restore` > `backupOp`; free heap was 215 KB before the request. Nothing was
   changed (the stores read back intact at the next power-on). With Wi-Fi the board has tens of KB less, so
   smaller backups will fail too, over HTTP as well as serial. Decoded backtrace (top):
   `operator new` < `vector<string>::push_back` < `pt::scriptCompile` (script.cpp:284) < `Controller::restore` <
   `Controller::backupOp` < `Controller::handle` < `Controller::call` < `Proptroller::serialByte`.
   Fix: lower the peak of `restore` (check the script before the shows are copied,
   free the parsed JSON as soon as it is read, check the shows one at a time), and refuse with
   `internal: not enough memory` when `ESP.getMaxAllocHeap()` / free heap is below a measured need, instead of
   aborting. The same heap check belongs before `set_script` and `check_script`.
2. **The serial console does not answer (all boards).** `help`, `name`, `key=value`, `trigger`, `status`,
   `settings` get no reply; only `mem` and JSON lines work. `PropApp` in `lib/proptroller/src/Proptroller.cpp`
   forwards every `NetApp` call to the controller except `consoleLine` (the default returns "") and
   `callSerial` (the default calls `call`, so a serial JSON request is not marked as serial: with the page lock
   on, a change over USB is refused without the password, `device.cpp:750`). The host tests call the controller
   directly, past this wrapper. Fix (tested in QEMU: `help` and `name` then answer):

       void sessionClosed(uint32_t ss) override { c_->sessionClosed(ss); }
     + std::string consoleLine(const std::string& l) override { return c_->consoleLine(l); }
     + std::string callSerial(const char* b, size_t n) override { return c_->callSerial(b, n); }

   Until it is in, the smoke test's "console: name" check fails (`run-esp32.sh` exits 1).
3. **The 10 ms step stops while a long request runs.** Requests run under the controller lock (serial and
   `LockedApp` for HTTP), and the step task takes the same lock every tick. `check_script`, `set_script` and
   `put_backup` hold it 1.4-2.0 s of guest time at one instruction per cycle; the board is slower, and real flash
   erase adds to it. Meanwhile outputs hold their level, a show's timing slips, `*_max_on_ms` is not enforced,
   and the step task's 5 s watchdog comes closer. Bench: time these requests on the board. If they pass 1-2 s,
   consider checking the script outside the lock and taking it only to store.
   - Fixed: the parse and the script check give the lock back (`Unlocked`), and so does the script
     record's write (`scriptWrite`: the old script is parked first, outputs let go and its runner detached, so
     the step runs without a script for the length of the write; the save is power-safe, and a cut between
     put_backup's script and settings saves is caught by the boot re-check). `--net` measures it with a show running: set_script of 3.7 KB and put_backup of it, gaps 80 ms and 130 ms before, 11 ms and 12-13 ms after
     (`test_safety` `the_step_never_waits_on_a_script_write`). What still holds the lock: building the record and
     applying a backup (shows, settings), a few ms.
4. **The heap peaks are 2.6-3.5 times the host estimate** (above); 10 shows of 2 x 20 cues and the script take about 97 KB of
   heap. Bench: `mem` after a `put_backup` and a `set_script` of the largest script, over Wi-Fi.
5. **The step task can block forever on the DMX UART while it holds the controller lock.**
   `BoardDmx::send` calls `uart_write_bytes_with_break`, which waits without a timeout for room in the ring
   buffer. In QEMU (no TX break) that stops the step and the loop, and the task watchdog restarts the board
   after 5 s (seen; gdb on QEMU's gdbstub: the step task holds the lock and waits on UART1's TX ring buffer,
   `tx_waiting_brk = 1`). On a board the break completes, so this is an emulator artifact, but skipping a frame
   when the buffer is full would make the step independent of the UART. By arithmetic (not measured), frames
   20 ms apart against 22.6 ms of transmission (513 bytes at 250 kbaud) make the step wait about 3 ms now and
   then.
6. **Field panics leave no core dump.** The core's sdkconfig writes core dumps to flash
   (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`), the shared partition table has no `coredump` partition, so each
   boot logs "No core dump partition found!" twice and a panic leaves only its serial output. Information only:
   adding the partition would move the stores.
7. Every boot until the first save logs `vfs_api.cpp:105 open(): /littlefs/... does not exist, no permits for
   creation` (an `open` for reading of a missing record): noise, harmless.

### Network findings (QEMU Ethernet, before the network fixes)

N1. **A broker that accepts TCP and never answers restarts the board again and again (high).** PubSubClient's
    `connect()` waits for the CONNACK inside the loop task (its 15 s socket timeout). Our loop task is on the 5 s
    task watchdog, so the board panics, restarts, connects again, and panics again. HTTP never answers in between.
    The show keeps its step (no gap over 15 ms) until each restart. Fixed by the asynchronous MQTT client
    (espMqttClient on AsyncTCP): a silent broker now leaves the step and HTTP alone.
N2. **An update over HTTP gets no answer: the loop task watchdog fires while the image is checked (high, timing to
    confirm on the bench).**
    - `BoardOta::finish` runs inside one `loop()` and under the controller lock (`LockedApp::updateFinish`). It
      calls `Update.end(true)`, then reads the whole image back in 256 B `esp_partition_read` calls (4,930 flash
      operations, each disabling the cache and stalling the other core).
    - In QEMU that passes 5 s. The board restarts, and because `Update.end` already selected app1, it boots the
      new image `pending`, then `valid`. The client sees the connection drop.
    - On silicon it is probably faster, but it freezes the step for its whole length either way.
    - Fix: hash the image through one `esp_partition_mmap` (or 4 KB reads from the
      heap), feed the watchdog between chunks, and do it outside the controller lock.
N3. **Every 10 ms step maps the flash (medium).** `Controller::updateTick` calls `BoardOta::state()`, which, until
    `markValid()` ran in this boot, calls `esp_ota_get_state_partition`. That reads otadata through
    `spi_flash_mmap` / `munmap`, a cache flush and an IPC stall of the network core, 100 times a second, with the
    controller lock held. On a board flashed over USB it never stops (seen in gdb samples).
    - Fix: read the state once at boot and cache it (it changes only through `markValid` / `markInvalid`).
N4. **The step wrote the settings itself: a gap of about 0.1 s after every change (medium).** First seen as "a
    WebSocket client that stops reading delays the step" (111-115 ms once in 60 s), but the reader was not the
    cause: the same gap (108 ms) comes with no reader at all, and a reader that connects after the save gives
    none in 40 s. `put_show` (just before the test) marks the settings dirty; about 2 s later, between two runs,
    `Controller::tick` called `saveNow()`: LittleFS's write in the step task, under the controller lock (a probe
    on the lock and the step: the step body 96-98 ms, the lock held as long). Fixed: the step only says the save
    is due, the loop writes it (`Controller::background()`), with the lock given back during the write; the step
    gap stays at 10-11 ms through the save. `test_safety` `the_step_never_writes_the_flash` checks it.
    The self-check's `markValid` / `markInvalid` (otadata, once per update) are made by
    `background()` too, with the lock given back (`the_step_never_marks_the_new_image`; on the ESP32 a failed
    check now restarts from the loop task), and the script record of `set_script` / `put_backup` is
    written without the lock (finding 3). On a board each flash erase / program still stops both
    cores' cache for its own length whichever task writes, and on the RP2040 arduino-pico pauses core 1 (the step)
    while core 0 writes (bench: the step gap across a save). Measured on a real ESP32 DevKit (2026-10-22, a show
    running): `put_backup` of 10.5 KB 98-104 ms, a 7.6 KB `set_script` 44-51 ms. That is the flash, a known
    limit (README "On hardware so far"); `get_params` (32-41 ms) and `get_backup` (21-24 ms) were the lock held
    while their answers were built, now built and written out with it given back.
N4b. **Each MQTT connect held the step for 8-11 ms (medium).** The hello (every setting and the effect's status,
    about 100 messages) was built under the controller lock: with the broker dropping the session every 15 s, 5 of
    12 reconnects gave step gaps of 15-18 ms. Now built from a copy of the settings with the lock given back (also
    when settings change): the lock is then held 3-5 ms, 0 of 12 reconnects over 15 ms (11-14 ms).
    `test_safety` `the_mqtt_hello_is_built_without_the_lock`.
N5. **Page loads fail while sessions are open (medium).** With 4-5 WebSocket sessions open, a phone's 5 parallel
    GETs get connections closed without an answer, or time out. `loadtest.py` shows 78-95 failed page loads in 120 s.
N6. **The state differs from the golden files (low):** `sound.source` and `sound.track` are missing, and `pattern`
    is sometimes an object (`{"count":n}`, as PROTOCOL.md allows) where the golden files have only strings. Fix
    either the board or the golden files.
N7. Once under load a WebSocket client received a frame whose payload was not JSON (its stream out of step). Not
    reproduced on demand since the non-blocking WebSocket writes; watch for it.

## Network: the firmware on QEMU's Ethernet (`--net`)

QEMU has no Wi-Fi radio, but it has an OpenCores Ethernet MAC (`open_eth`). The Arduino core's prebuilt IDF has it
off, so `tools/qemu/eth` builds the core as an ESP-IDF component (IDF 4.4.7, the version its prebuilt libraries
come from; PlatformIO picks it for `framework = arduino, espidf`):

- `sdkconfig.defaults` is the core's own sdkconfig, plus `CONFIG_ETH_USE_OPENETH` and flash DIO 40 MHz.
- LittleFS links the core's own prebuilt `libesp_littlefs.a`.
- `src/main.cpp` is `main_wifi.cpp`.
- Under `PROPTROLLER_QEMU_ETH` (never set for a board), `lib/.../arduino/qemu_eth.cpp` brings up `open_eth`
  and DHCP instead of Wi-Fi (three hooks in `net.cpp`, ESP-NOW skipped).
- Everything above the interface is the board's own code: lwIP sockets through `WiFiServer` / `WiFiClient` /
  `WiFiUDP`, the HTTP and WebSocket server, discovery, PubSubClient, the update path.
- `mem` also reports the longest gap between two 10 ms steps since the last `mem` (QEMU build only).

QEMU runs with `-nic user,model=open_eth,hostfwd=tcp:127.0.0.1:18080-:80,hostfwd=udp:127.0.0.1:47801-:47800`.
The board gets 10.0.2.15, and the host is 10.0.2.2 from inside.

    tools/qemu/run-esp32.sh --net     # discovery, conformance, update, MQTT, show-first (about 7 minutes)
    tools/qemu/run-esp32.sh --serve   # the board on http://127.0.0.1:18080/ for 10 minutes (a browser, load tests)

The first build downloads ESP-IDF 4.4.7, CMake and Ninja into `$HOME/.platformio`, which takes a few minutes. The
image is 1,261,936 B (64 % of a slot), against 1,119,456 B for the board's prebuilt build.

What the run does, and the results before the network fixes (asynchronous MQTT, non-blocking writes, the update
check outside the lock):

| Check | Result |
|---|---|
| Ethernet + DHCP, boot | ok: ready after about 400 ms. Free heap 254,904 B with lwIP up (324,204 B without a network stack) |
| UDP discovery through the forwarded port | ok. The announce names port 80; the driver maps it to the forwarded port |
| `ptclient.py conformance`: HTTP info / state / web UI, RPC, 415 without JSON, a WebSocket session with run, notice and state events | ok, with the known shape differences below left out |
| state against the golden files | **differs**: `sound.source` and `sound.track` missing; `pattern` sometimes `{"count":n}` where the golden has a string |
| `POST /api/v1/update` (the same image, 1.26 MB) | **no answer: the loop task watchdog fires** in `BoardOta::finish` (finding N2). The board then boots app1 `pending` and marks itself `valid` after the self-check, so the update took effect, but the client never heard so |
| MQTT to a broker on the host (10.0.2.2) | ok: connects, 13 subscriptions, `online`, every setting, the state; `<p>/rpc` in, `<p>/rpc/reply` out |
| show-first, one WebSocket client stops reading for 60 s while a show runs | **a 111-115 ms step gap** (one over 15 ms). The other client's `GET /api/v1/state` stays answered (slowest 1.0-1.4 s wall) |
| show-first, a broker that accepts TCP and never answers | **the loop task watchdog fires** about 5 s after each connect attempt (PubSubClient waits for the CONNACK in the loop task): the board restarts again and again (3 connects in 60 s, HTTP never answers) |
| `pageload.py` (5 parallel GETs = one phone opening the page, with 0-5 WebSocket sessions open), 15 s timeouts | 4 or 5 of 5 succeed with 0-3 sessions open. With 4-5 sessions open, `RemoteDisconnected` (closed without an answer) and timeouts. lwIP: 16 sockets, 5,760 B send buffer and window |
| `loadtest.py` 120 s, 4 WebSocket sessions + HTTP pollers + RPC writes + page bursts + 1 slow reader | HTTP p50 52-58 ms, p95 129-155 ms; page loads 25-42 ok, 78-95 failed; state gap between WebSocket state frames up to 3.9-5.2 s; writes p50 40-49 ms, max 0.26-2.1 s; 2 sessions closed by the board; once a WebSocket frame whose payload was not JSON (the client's stream was out of step) |
| browser tests (`web-test`, Chrome, each on a fresh board) | alias-test 13/13 and settings-test 11/11 pass. ui-test (3 ok) and script-test (8 ok) stop at their 3-5 s wall-clock waits: QEMU runs at about a quarter of real time, so these are not evidence either way |

Loads are against an emulated board at about a quarter of real speed, through QEMU's user-mode network stack
(slirp). Latencies are wall time, so compare runs with each other, not with a board.
The step gaps are guest time and are meaningful.

With the network fixes, `--net` passes 18 of 18: the update is answered, the new image boots from app1 `pending`
and marks itself `valid`; a broker that accepts and never answers leaves the step alone; the step gap stays at
11-13 ms in every show-first window, including `set_script` / `put_backup` of a 3.7 KB script with a show running.
Run the `--net` checks on an otherwise idle host: with every host core busy, a gap of 26-54 ms appeared about once
per run (the step task was not scheduled; not the firmware's locks, saves or requests).
The state's `pattern` / `sound` differences are not
differences: PROTOCOL.md allows `{"count":n}` and sends `sound.track` / `source` only while a sound plays.
`drive.py` power-cycles after the update's "restarting" too: QEMU's software reset hung after the bootloader
had marked the new image pending, so the next boot rolled it back.

To rerun:

    tools/qemu/run-esp32.sh --net
    tools/qemu/run-esp32.sh --serve --http-port 39417 &          # pageload.py has port 39417 built in
    python3 pageload.py loadtest.py                              # in the review's scratch folder
    python3 loadtest.py 39417 <qemu pid> 120 4 1

The three QEMU hooks in `net.cpp` (keep them if the file is rewritten):
1. `qemuEthBegin` instead of the Wi-Fi start.
2. `PT_LINK_UP()` instead of `WiFi.status() == WL_CONNECTED` for MQTT.
3. ESP-NOW skipped.

What this proves:
- The board's TCP/IP code works on real lwIP sockets: discovery, HTTP, WebSocket sessions and events, the
  update path into the other slot, the pending / valid self-check after it, and MQTT both ways.
- How that code behaves under stalled clients and a silent broker (blocking, watchdog, step gaps), with the
  real lwIP limits of the board's sdkconfig.

What it does not prove:
- Nothing about the radio: join, access point, roaming, RF timing, retransmissions over the air.
- The Ethernet driver is not the Wi-Fi driver; Wi-Fi has its own buffers, tasks and memory, so heap with Wi-Fi up
  stays a bench number.
- QEMU timing is approximate (the clock section above), and slirp is not a phone on a 2.4 GHz network.
- QEMU's own restart path is unreliable (a software reset sometimes hangs in the second-stage bootloader). The
  driver therefore power-cycles QEMU after the board says "restarting", and the stores persist as on a board.

## The bare ESP32-S3 image (`--s3`)

    tools/qemu/run-esp32.sh --s3 [--soak 60]

The bare S3 DevKit image ([../BARE-DEVKIT.md](../BARE-DEVKIT.md)) without Wi-Fi (env `esp32s3_qemu`) on the same QEMU
release's `esp32s3` machine: the flash image a first USB flash writes (bootloader at 0x0), booted with
`-icount shift=2`, and the serial smoke test of stage 2 (`drive.py --machine esp32s3 --mcu esp32-s3`). It proves
that the S3 image boots on its dual-core layout (the step and the 1 ms sampling interrupt on core 1), formats and
mounts LittleFS, answers the protocol, keeps its records over a reboot, and runs shows and scripts without a panic
or watchdog. QEMU's esp32s3 machine has no LED PWM controller and no RMT, so this env drives OUT1-4 on / off only
and has no pixel driver (`PROPTROLLER_QEMU_NO_LEDC`), and its GPIO model reads every input LOW (no pull-ups): every
active-low input reads pressed from power-on (held at power-on, so nothing triggers). Every check passes.

Measured 2026-10-25 (`mem`, icount shift 2, with a 7 KB script on every output and requests in a loop): step max
340-575 us (average 106-115 us), none over 1 ms; samples handled at most 1.25 ms after they were latched, none
lost; the step's longest wait for the lock 2.15 ms (the requests building their answers). There is no C3 machine
in this QEMU release: the C3 image is built and host-tested only.

## RP2040 / RP2350: Renode

Mainline QEMU has no RP2040 machine. Renode 1.17.0 has no RP2040 platform either; the community models
(github.com/matgla/Renode_RP2040, marked work in progress and frozen; RP2040 only, no RP2350) run with it
(peripherals compiled from source at load time). Result, after 3 s of emulated time, judged by the program
counter and two globals read from RAM (none of these images prints to a UART):

| Image | Result |
|---|---|
| `pico` | **runs**: bootrom, boot2, arduino-pico start, `begin()` finished (`g_prop` set, ready after 11 ms), core 0 in the USB stack, core 1 stepping. The models have no USB, so the serial protocol cannot be driven |
| `boot_rp2040` alone | **works**: with no firmware in the slot it goes to the bootrom's USB boot mode, as designed |
| `boot_rp2040` + `picow` | **partly**: the bootloader jumps to the app at 0x10007000 and both cores run, but `begin()` does not finish: core 0 waits in `dma_channel_wait_for_finish_blocking` (the CYW43 radio over PIO and DMA, which the models do not support) |
| `pico2w` (RP2350) | **no**: no RP2350 model |

So Renode shows the RP2040 images start and the bootloader hands over, nothing more; it is not wired into a
script. The community models (C# compiled at load time, and a prebuilt `libpiosim.so` for PIO) were run from
`$HOME/.cache` without a full read.
