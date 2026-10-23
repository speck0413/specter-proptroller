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

* `/cfg-a.bin` / `/cfg-b.bin`: settings, shows and the profile (JSON, `STORED_VERSION`).
* `/runs-a.bin` / `/runs-b.bin`: the lifetime run count (`runs.h`, `{"lifetime":n}`, 64 B at most); kept by a
  factory reset and a backup restore.
* `/script-a.bin` / `/script-b.bin`: the show script (`SCRIPT_RECORD_VERSION`, `script_record.h`): a 24-byte
  header ("PTSR", the format, the step table's format and key, the lengths), the text and its sound list
  compressed together (`lz.h`; `get_script` and backups get the exact text back), then the checked step table
  (at most about 2.7 KB). Power-on runs the stored table while its key matches (table format, firmware version
  and build, the settings the checker reads, the sound list); otherwise the text is checked again and the record
  rewritten. The worst case (8 KB of incompressible text, the largest table, 254 random 32-character sound
  names) takes 21.9 KB of the 24 KB store; a longer sound list does not fit and `set_script` says so, keeping
  the stored script. Fixture: `firmware/arduino/test/fixtures/records/script-record.bin` (`test_script_record`).

## Changing this page

Nothing has shipped, so the map may still change. After the first board ships, moving a store or a slot needs a
USB re-flash of every board and a release note.
