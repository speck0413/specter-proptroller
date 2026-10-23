// The flash map of every board (firmware/FLASH-MAP.md). The ESP32 table is firmware/partitions/esp32-4mb.csv
// (platformio.ini points at it). The Pico W / Pico 2 W sit behind the Pico bootloader: their map is
// picoboot_map.h (plain C macros, also read by the bootloader and the build scripts), named here in C++. The plain
// Pico keeps arduino-pico's map: sketch, then LittleFS (board_build.filesystem_size), then the 4 KB EEPROM sector.
// test/test_flash_map checks these numbers against the CSV and platformio.ini, test/test_picoboot_map the
// bootloader map against FLASH-MAP.md.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "picoboot_map.h"

namespace pt {

// ESP32 DevKit, 4 MB
static const uint32_t ESP32_FLASH_BYTES = 4UL * 1024 * 1024;
static const uint32_t ESP32_APP0_OFFSET = 0x10000;
static const uint32_t ESP32_APP1_OFFSET = 0x1F0000;
static const uint32_t ESP32_APP_SLOT_BYTES = 0x1E0000;    // 1,966,080 B: the largest image an update takes
static const uint32_t ESP32_OTADATA_OFFSET = 0xE000;      // where the Arduino build writes boot_app0.bin
static const char* const ESP32_LITTLEFS_LABEL = "spiffs";  // LittleFS.begin()'s default partition label
static const uint32_t ESP32_LITTLEFS_OFFSET = 0x3D0000;
static const uint32_t ESP32_LITTLEFS_BYTES = 0x20000;     // 128 KB: settings, shows and script records

// Raspberry Pi Pico (RP2040, 2 MB, USB only): arduino-pico's map
static const uint32_t PICO_EEPROM_BYTES = PICOBOOT_EEPROM_LEN;
static const uint32_t PICO_FLASH_BYTES = PICOBOOT_RP2040_FLASH;
static const uint32_t PICO_FS_BYTES = 256UL * 1024;
static const uint32_t PICO_SKETCH_BYTES = PICO_FLASH_BYTES - PICO_FS_BYTES - PICO_EEPROM_BYTES;  // 1,788 KB

// Pico W (RP2040, 2 MB) and Pico 2 W (RP2350, 4 MB) behind the Pico bootloader (picoboot_map.h): the firmware
// runs from the active slot, an update is written to the dfu slot (one 4 KB page larger), LittleFS holds only
// the records.
static const uint32_t PICOW_FLASH_BYTES = PICOBOOT_RP2040_FLASH;
static const uint32_t PICO2W_FLASH_BYTES = PICOBOOT_RP2350_FLASH;
static const uint32_t PICOW_SLOT_BYTES = PICOBOOT_RP2040_ACTIVE_LEN;   // 933,888 B: the largest image
static const uint32_t PICO2W_SLOT_BYTES = PICOBOOT_RP2350_ACTIVE_LEN;  // 1,982,464 B
static const uint32_t PICOW_FS_BYTES = PICOBOOT_FS_LEN;                // 188 KB, both boards
static const uint32_t PICO2W_FS_BYTES = PICOBOOT_FS_LEN;
static const uint32_t PICOBOOT_ACTIVE_ADDRESS = PICOBOOT_XIP_BASE + PICOBOOT_ACTIVE_OFFSET;  // 0x10007000

// Target (FLASH-MAP.md): an image uses at most 80 % of its slot, so later features fit.
static const uint32_t IMAGE_BUDGET_PERCENT = 80;

// The shared ESP32 table's arithmetic (test_flash_map checks the CSV against these numbers).
static_assert(ESP32_APP0_OFFSET % 0x10000 == 0 && ESP32_APP1_OFFSET % 0x10000 == 0, "app slots on 64 KB boundaries");
static_assert(ESP32_APP1_OFFSET == ESP32_APP0_OFFSET + ESP32_APP_SLOT_BYTES, "app1 right after app0");
static_assert(ESP32_LITTLEFS_OFFSET == ESP32_APP1_OFFSET + ESP32_APP_SLOT_BYTES, "LittleFS right after app1");
static_assert(ESP32_LITTLEFS_OFFSET + ESP32_LITTLEFS_BYTES <= ESP32_FLASH_BYTES, "LittleFS within the flash");
static_assert(ESP32_OTADATA_OFFSET + 0x2000 <= ESP32_APP0_OFFSET, "otadata below the app slots");
// The plain Pico's sketch limit: flash less LittleFS less the EEPROM sector.
static_assert(PICO_SKETCH_BYTES == 1788u * 1024, "sketch limit as FLASH-MAP.md");
// Behind the bootloader: bootloader, state, active, dfu, LittleFS and the EEPROM sector fill the flash exactly.
static_assert(PICOBOOT_ACTIVE_OFFSET == PICOBOOT_STATE_OFFSET + PICOBOOT_STATE_LEN &&
                  PICOBOOT_STATE_OFFSET == PICOBOOT_BOOT_OFFSET + PICOBOOT_BOOT_LEN,
              "bootloader, state, active in a row");
static_assert(PICOBOOT_DFU_OFFSET(PICOW_SLOT_BYTES) + PICOBOOT_DFU_LEN(PICOW_SLOT_BYTES) == PICOBOOT_FS_OFFSET(PICOW_FLASH_BYTES),
              "Pico W: dfu ends where LittleFS starts");
static_assert(PICOBOOT_DFU_OFFSET(PICO2W_SLOT_BYTES) + PICOBOOT_DFU_LEN(PICO2W_SLOT_BYTES) == PICOBOOT_FS_OFFSET(PICO2W_FLASH_BYTES),
              "Pico 2 W: dfu ends where LittleFS starts");
static_assert(PICOW_SLOT_BYTES % PICOBOOT_SECTOR == 0 && PICO2W_SLOT_BYTES % PICOBOOT_SECTOR == 0 && PICOBOOT_FS_LEN % PICOBOOT_SECTOR == 0,
              "regions on 4 KB sectors");

}  // namespace pt
