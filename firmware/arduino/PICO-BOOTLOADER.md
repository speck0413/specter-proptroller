# The Pico W / Pico 2 W bootloader

The Pico W and Pico 2 W run behind a small bootloader of our own (about 3 KB), so a bad update rolls back as it
does on the ESP32. Source: `src/picoboot` (start-up, linker scripts), `lib/picoboot` (the boot decision,
host-tested), `tools/picoboot_*.py` (build, linker script, first-flash image). Envs: `boot_rp2040`,
`boot_rp2350` (the bootloaders), `picow`, `pico2w` (the firmware behind them). Nothing here has run on a board.

## The map

`lib/proptroller/src/proptroller/picoboot_map.h` holds every number; `test/test_picoboot_map` checks it against
`platformio.ini` and every built image. In order: the bootloader (24 KB; on the RP2040 boot2 comes first), the
state sector (4 KB at 0x10006000), the active slot (0x10007000; 912 KB Pico W, 1,936 KB Pico 2 W), the dfu slot
(one 4 KB page larger: the swap's spare page), LittleFS (188 KB) and arduino-pico's EEPROM sector.

## How an update goes in

`arduino/ota.cpp` with `PT_PICOBOOT`:

- **header**: the image must be linked for the active slot (stack pointer in RAM, reset vector inside the
  slot). A plain arduino-pico image is refused before anything is written.
- **begin**: refused while the running image is still pending, or while an update is already marked and waits
  for the restart.
- **write**: the body goes straight into dfu, one 4 KB sector at a time (erase, program).
- **finish**: check the board and implementation markers, compare the SHA-256 of dfu read back with the sent
  hash, then mark the update with the number of pages to swap (the larger of the two images).
- **markValid** after the 30 s self-check marks the image good; **markInvalid** just restarts, and the
  bootloader swaps the previous image back.
- Flash writes idle the other core with interrupts off (about 45 ms per sector, as LittleFS does).
- **Watchdog**: a static constructor re-arms the bootloader's 8 s watchdog before `setup()`; the step feeds it
  (only while core 1's step ran less than 100 ms ago). A new image that hangs before its self-check is restarted
  and reverted; a stalled loop() or network never restarts a good image.

## The boot decision

The state sector uses embassy-boot's layout: byte 0 the magic (0xF0 swap, 0xC0 reverted, anything else boot),
byte 1 progress valid, then one byte per completed step; the page count N (and its complement) at the end,
written before the "swap" byte. A missing or damaged count swaps nothing: a power cut while "boot" or "reverted"
is programmed can leave bits that read as "swap", and that sector has no count.

At power-on (`picoboot_prepare`):

- swap, steps < 2N: **swap**. For page p from N-1 down to 0: active[p] to dfu[p+1], then dfu[p] to active[p].
- swap, steps = 2N (swapped last time, never marked good): **revert**. For p from 0 up: active[p] to dfu[p],
  then dfu[p+1] to active[p]; then write "reverted".
- otherwise: boot.

Then it checks the active slot's vector table: no firmware means USB boot mode (BOOTSEL). It switches clk_ref
to the crystal (an exact watchdog tick) and jumps. Every mark erases the sector, writes the page count, then the
magic, so a cut leaves the old magic or plain boot.

`test/test_picoboot` simulates NOR flash and cuts the power at every flash call of a swap, of the revert that
follows, and of both marks, with half-programmed bytes, and two cuts in a row over the first 40 calls: the board
always ends on a whole, correct image. Two cuts lose only the report: a
cut in the final erase after a revert loses `rolled_back`; a cut inside `mark_booted` keeps the new image but
reports `rolled_back` (embassy-boot does the same).

**Time.** Two page copies per image page: about 16 s typical for a Pico W image, about 2.5 min with worst-case
flash timings. The watchdog is fed at every erase and program. The phone page waits up to 5 minutes.

## Build and first flash

```sh
pio run -e boot_rp2040 -e boot_rp2350        # build a Pico firmware env first: they use its packages
pio run -e picow -e pico2w
python3 tools/picoboot_image.py merge pico-w # .pio/build/picoboot-pico-w/flash.uf2 (pico2-w for the Pico 2 W)
pio test -e native -f test_picoboot -f test_picoboot_map
```

The bootloader builds without a framework from the installed arduino-pico package (pico-sdk headers,
`libpico.a` for the flash and watchdog calls, boot2 on the RP2040) with arduino-pico's GCC. On `pico2w` the
platform's own UF2 conversion prints "Memory segment ... outside of valid address range"; the build still
succeeds, and that UF2 is not used.

First flash: hold BOOTSEL, plug in, copy `flash.uf2` (FLASH-MAP.md). Never `pio run -t upload` on these envs.

## Risks

1. **Hand-over to arduino-pico's start-up**: the firmware starts with clk_sys on the crystal, not a cold-boot
   state; on the RP2350 the boot ROM never saw its IMAGE_DEF.
2. **Watchdog take-over**: from the jump to the first step must take less than 8 s (USB, the CYW43
   start, the sketch's `setup()`). A `setup()` that blocks longer restarts the board, and a new image is reverted.
3. **Flash writes with Wi-Fi active**: interrupts are off about 45 ms per sector during an upload.
4. **Swap time**: about 16 s with the pins in their reset state.
5. **Recovery**: a damaged bootloader region needs a BOOTSEL flash. RP2350 drag-and-drop UF2 errata (E10) are
   untested.
6. **Package versions**: pin `framework-arduinopico` and the toolchain in a release and record the bootloader's
   sha256.

## Bench steps (with a Pico W and a Pico 2 W)

1. **First flash**: copy `flash.uf2`. Expect "ready after … ms" on USB serial, `info.update.slot = active`,
   `state = valid`.
2. **Bootloader only** (after a full erase): the board comes back as the BOOTSEL drive.
3. **Update**: `POST /api/v1/update` with another build's `firmware.bin`; time the restart (the swap). Expect
   `pending`, then `valid` after 30 s; it stays on the new image after another restart.
4. **Rollback on a crash**: an image that hangs in `setup()` is restarted by the watchdog and reverted; the old
   image reports `rolled_back = true` until the next update.
5. **Rollback on a failed self-check**: an image whose records cannot be read reverts after the self-check.
6. **Power cuts** 2, 5 and 10 s into a swap and into a revert: every power-on ends on a whole image.
7. **Wrong images**: a plain `picow` image and the other board's image are refused before anything is written.
8. **Records**: settings, shows and the script survive an update, a revert and a re-flash.
9. **Watchdog**: a step blocked for 10 s restarts the board; a `loop()` blocked for 10 s does not.
