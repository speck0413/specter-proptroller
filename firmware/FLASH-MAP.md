# Flash maps (every board)

Each board has one map, so it can be updated over the air and what it stores (settings, shows, the show script,
the profile) survives every update. The numbers live in one place per board: `firmware/partitions/esp32-4mb.csv`
(ESP32), `firmware/arduino/lib/proptroller/src/proptroller/picoboot_map.h` (Pico W, Pico 2 W) and
`platformio.ini` (the plain Pico). `proptroller/flash_map.h` names them in C++; `test/test_flash_map` and
`test/test_picoboot_map` check them, and every built image against its slot. Target: an image uses at most
80 % of its slot.

Rules:

* Stored data never sits in an app slot. An update writes only the slot it is not running from.
* A store keeps its offset and size from release to release. A board in the field cannot be re-partitioned
  over the air.

## ESP32 DevKit (4 MB)

Flash at QIO 80 MHz (the esp32dev env; the merged first-flash image keeps the bootloader's own DIO 80 MHz header,
and the bootloader switches to quad mode: `docs/FIRMWARE-BUILD.md`).

`esp32-4mb.csv`: two 1,966,080 B app slots (ota_0, ota_1), LittleFS on the partition labelled `spiffs` (128 KB:
the settings and script records), and two reserved 32 KB partitions after it (`store`, `script`, unused).

Updates (`arduino/ota.cpp`): the Update library writes the other app slot and reads it back before selecting
it. The new image is pending until the controller's self-check marks it valid; a failed self-check, or a crash
before it, boots the previous slot.

First flash (USB): `pio run -e esp32dev -t upload` writes the arduino-esp32 core's bootloader (built with
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`), so the board rolls back. Over-the-air updates never replace it.

| Offset | Bytes | What |
|---|---|---|
| 0x1000 | 17,536 | `bootloader.bin`, sha256 `3d234a7471f67b013686dabd4dee7c1fa915c9928463616a94bc9297acf1abf8` |
| 0x8000 | 3,072 | `partitions.bin`, sha256 `62537f42e22501cc4bcc398098c7c65a15e16a508087a3cad7ed1cf7fe2656d8` |
| 0xE000 | 8 KB | otadata (`boot_app0.bin`): boot app0 |
| 0x10000 | the image | `firmware.bin` |

nvs and LittleFS keep what they hold. The checksums are those of the packages pinned in `platformio.ini`; a
platform update that changes them updates this table.

## Raspberry Pi Pico W and Pico 2 W: behind the Pico bootloader

Our bootloader (`firmware/arduino/PICO-BOOTLOADER.md`) keeps the previous image: an update goes to the dfu slot,
is swapped into the active slot at the restart, and is swapped back if it never marks itself good. The firmware
is linked for the active slot at 0x10007000 (912 KB on the Pico W, 1,936 KB on the Pico 2 W). There is no radio
firmware region: arduino-pico links the CYW43 firmware into the image. LittleFS (188 KB) holds only the records.

First flash (USB):

```sh
pio run -e boot_rp2040 -e picow && python3 tools/picoboot_image.py merge pico-w     # Pico W
pio run -e boot_rp2350 -e pico2w && python3 tools/picoboot_image.py merge pico2-w   # Pico 2 W
```

Hold BOOTSEL, plug the board in and copy `.pio/build/picoboot-<board>/flash.uf2` to the drive. It holds the
bootloader, the state sector erased (a USB flash never finishes an old swap over the new image) and the
firmware; dfu and LittleFS are not written, so the records survive. `pio run -t upload` is refused on `picow`,
`pico2w` and the bootloader envs: it would write one image alone.

## Raspberry Pi Pico (RP2040, 2 MB, USB only)

arduino-pico's own layout: the sketch, then LittleFS (256 KB), then the 4 KB EEPROM sector. No radio and no
update target (updated over USB), so it is not behind the bootloader.

## Stored records (what survives an update)

Every stored record carries a format number. Today every record is format 1.

* A new field with a default keeps the format number: an older firmware ignores fields it does not know.
* A change an older firmware would misread (a renamed or re-meant field) raises the number and adds a
  migration step (`readRecord` with `STORED_MIGRATIONS`; none yet). A record of another number is never
  silently replaced by the defaults.
* A record from a newer firmware (after a rollback, or a USB downgrade) is not loaded and never overwritten;
  the board runs on its defaults until it is updated again or factory reset.
* A rewritten record is written back only after a new image has passed its self-check, so a rollback finds the
  record as it was; `set_script` and `put_backup` are refused until then. A record that is there but cannot be read fails the self-check.

LittleFS is formatted only when its partition was never written (every byte 0xFF, a new board). Any other mount
failure (an older firmware's file system after a rollback, damaged flash) leaves the flash as it is: the records
count as unreadable, nothing is written, a new image fails its self-check, and only a factory reset formats it.

Records (LittleFS, each an A/B file pair with a sequence number and CRC):

* `/cfg-a.bin` / `/cfg-b.bin`: settings, shows and the profile (JSON, `STORED_VERSION`). After
  `factory_reset` with `keep_wifi` the record holds the firmware's defaults with the name and the Wi-Fi kept and
  `"reset": true`: the next power-on treats the board as fresh (its defaults apply), puts the kept values back and
  writes the record again without the mark.
* `/runs-a.bin` / `/runs-b.bin`: the lifetime run count (`runs.h`, `{"lifetime":n}`, 64 B at most); kept by a
  factory reset and a backup restore.
* `/script-a.bin` / `/script-b.bin`: the show script (`SCRIPT_RECORD_VERSION`, `script_record.h`): a 24-byte
  header ("PTSR", the format, the step table's format and key, the lengths), the text and its sound list
  compressed together (`lz.h`; `get_script` and backups get the exact text back), then the checked step table
  (table format 2, at most about 3 KB). Power-on runs the stored table while its key matches (table format, firmware version
  and build, the settings the checker reads, the sound list); otherwise the text is checked again and the record
  rewritten. The worst case (8 KB of incompressible text, the largest table, 254 random 32-character sound
  names) takes 22.2 KB of the 24 KB store; a longer sound list does not fit and `set_script` says so, keeping
  the stored script. Fixtures: `firmware/arduino/test/fixtures/records/script-record.bin` and
  `script-record-table1.bin` (a table of format 1: its text reads back and is checked again) (`test_script_record`).
* `/board-a.bin` / `/board-b.bin`: the board package ([BOARD-PACKAGES.md](BOARD-PACKAGES.md), `board_package.h`,
  `BOARD_RECORD_VERSION`): a 12-byte header ("PTBP", the format, flags: bit 0 = apply the package's defaults once at
  the next power-on, the JSON's length), then the package's JSON packed (`lz.h`). At most 4,048 bytes
  (`BOARD_STORE_CAPACITY`): each file stays within one 4 KB block. Read at power-on before any pin is touched; kept
  by a factory reset unless `forget_board`, and by every update. A record that cannot be used (damaged, from a newer
  firmware, not for this chip) is kept, the board drives no pins, and a new image fails its self-check. Fixture:
  `firmware/arduino/test/fixtures/records/board-record.bin` (the whole file, A/B frame included) with its package
  `board-record.json` (`test_board_package`).

* `/patterns-a.bin` / `/patterns-b.bin` (boards with the LED pattern player: the ESP32 and the Pico 2 W): the stored
  LED patterns (`pattern_store.h`, `PATTERN_RECORD_VERSION`): a 12-byte header ("PTPN", the format, the body's
  unpacked length), then each pattern's name, program bytes and control values packed together (`lz.h`). At most
  8 patterns, 24 KB of programs unpacked and 8,192 bytes packed (`PATTERN_STORE_CAPACITY`: two 4 KB blocks a
  file); `put_pattern` refuses what would not fit. Written when nothing runs (or a minute after the change), never
  while a new image waits for its self-check; a newer or unreadable record is kept (and fails a new image's
  self-check) until a factory reset, which removes it. A stored program a newer firmware cannot run is dropped
  from the list at power-on and said in the event log. Not in backups.

  ESP32 LittleFS budget (128 KB, 32 blocks of 4 KB), every record at its largest: settings 2 x 3 blocks,
  script 2 x 6, board package 2 x 1, patterns 2 x 2, the run count and the directory 2 to 4: 26 to 28 blocks,
  leaving 4 to 6 for LittleFS's copy-on-write while a file is replaced. The Pico 2 W's 188 KB has room to spare.

A factory reset of a file system that could not be opened formats it: the board package goes with everything else,
and the board starts on the image's own board until a package is sent again.

## A board package in the first flash

A first install over USB may write a file system image holding only `/board-a.bin` at the file system's offset,
in the same write as the firmware, so the board starts on its own pins from the first second (and, with the
record's "apply the defaults" flag, with the package's defaults). Writing the file system replaces whatever it held:
settings, the Wi-Fi, shows and the script. The file is the A/B frame around the board record, little endian: "PTC1",
the sequence number (1), the payload's length, its CRC-32 (zlib's: reflected, polynomial 0xEDB88320), then the
payload (the board record above; any valid `lz.h` stream decodes, all literals included).

The image must be made with the same LittleFS geometry as the firmware's (littlefs 2.6 or later, on-disk version
2.1; the name limit at most the reader's):

| Board | Offset | Size | Block | Blocks | Read / prog | Cache / lookahead | Name max |
|---|---|---|---|---|---|---|---|
| ESP32 DevKit (esp_littlefs, arduino-esp32 2.0.17) | 0x3D0000 (partition `spiffs`) | 128 KB | 4096 | 32 | 128 / 128 | 512 / 128 | 64 |
| Pico W (2 MB), Pico 2 W (4 MB) (arduino-pico LittleFS) | flash size - 4 KB - 188 KB (`PICOBOOT_FS_OFFSET`) | 188 KB | 4096 | 47 | 256 / 256 | 256 / 256 | 255 (64 also mounts) |
| Pico (2 MB) | flash size - 4 KB - 256 KB | 256 KB | 4096 | 64 | 256 / 256 | 256 / 256 | 255 (64 also mounts) |

A file system the firmware cannot open is never formatted at power-on (only a blank one is): its records count as
unreadable and, with no usable board package, no pins are driven until a factory reset.

## Changing this page

Nothing has shipped, so the map may still change. After the first board ships, moving a store or a slot needs a
USB re-flash of every board and a release note.
