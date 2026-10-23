/* The flash map of the C++ firmware on the Raspberry Pi Pico family behind its bootloader (firmware/FLASH-MAP.md,
 * firmware/arduino/PICO-BOOTLOADER.md). One header for the bootloader, the firmware's update target, the linker
 * script generator (tools/picoboot_ld.py reads the #defines below) and the host tests (test/test_picoboot_map
 * checks them against FLASH-MAP.md and platformio.ini).
 *
 * Offsets are from the start of flash (XIP 0x10000000). Every region starts and ends on a 4 KB sector.
 *
 *   0x000000  boot2 (RP2040 only, 256 B) + bootloader            24 KB
 *   0x006000  bootloader state                                    4 KB
 *   0x007000  active slot: the firmware runs from here            ACTIVE_LEN
 *   ........  dfu slot: an update is written here                 ACTIVE_LEN + 4 KB (the swap's spare page)
 *   ........  LittleFS: settings, shows, script (A/B records)     188 KB
 *   end-4 KB  arduino-pico's EEPROM sector                        4 KB
 */
#ifndef PICOBOOT_MAP_H
#define PICOBOOT_MAP_H

#define PICOBOOT_XIP_BASE 0x10000000u
#define PICOBOOT_SECTOR 0x1000u

#define PICOBOOT_BOOT_OFFSET 0x0u
#define PICOBOOT_BOOT_LEN 0x6000u
#define PICOBOOT_STATE_OFFSET 0x6000u
#define PICOBOOT_STATE_LEN 0x1000u
#define PICOBOOT_ACTIVE_OFFSET 0x7000u

/* 188 KB (47 blocks): board_build.filesystem_size = 188k. The records at their largest are 2 x 12 KB (settings,
 * shows, profile) and 2 x 24 KB (the show script): 18 blocks, about 26 while one is rewritten (LittleFS writes
 * the new copy before it frees the old), with room left for metadata and wear levelling. */
#define PICOBOOT_FS_LEN 0x2F000u
#define PICOBOOT_EEPROM_LEN 0x1000u  /* arduino-pico keeps it in the last sector */

/* Pico W (RP2040, 2 MB) */
#define PICOBOOT_RP2040_FLASH 0x200000u
#define PICOBOOT_RP2040_ACTIVE_LEN 0xE4000u /* 933,888 B */
/* Pico 2 W (RP2350, 4 MB) */
#define PICOBOOT_RP2350_FLASH 0x400000u
#define PICOBOOT_RP2350_ACTIVE_LEN 0x1E4000u /* 1,982,464 B */

#define PICOBOOT_DFU_OFFSET(active_len) (PICOBOOT_ACTIVE_OFFSET + (active_len))
#define PICOBOOT_DFU_LEN(active_len) ((active_len) + PICOBOOT_SECTOR)
#define PICOBOOT_FS_OFFSET(flash) ((flash) - PICOBOOT_EEPROM_LEN - PICOBOOT_FS_LEN)
#define PICOBOOT_EEPROM_OFFSET(flash) ((flash) - PICOBOOT_EEPROM_LEN)

#if defined(PICO_RP2350)
#define PICOBOOT_FLASH PICOBOOT_RP2350_FLASH
#define PICOBOOT_ACTIVE_LEN PICOBOOT_RP2350_ACTIVE_LEN
#elif defined(PICO_RP2040)
#define PICOBOOT_FLASH PICOBOOT_RP2040_FLASH
#define PICOBOOT_ACTIVE_LEN PICOBOOT_RP2040_ACTIVE_LEN
#endif

#endif
