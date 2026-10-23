// The flash map behind the Pico bootloader (PICO-BOOTLOADER.md): picoboot_map.h against platformio.ini, the regions in order without overlap, and every
// image built so far (bootloaders, firmware, the merged first-flash file) where the map puts it. An env not
// built yet is skipped; `pio run -e boot_rp2040 -e boot_rp2350 -e picow -e pico2w` and
// `tools/picoboot_image.py merge pico-w` (and pico2-w) first check them all.
#include <unity.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "picoboot.h"
#include "picoboot_map.h"

void setUp() {}
void tearDown() {}

static std::string readFile(const char* p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
static bool exists(const std::string& p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0;
}
static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

struct Region {
  const char* name;
  uint32_t off, len;  // from the start of flash
};

static std::vector<Region> regions(uint32_t flash, uint32_t active_len) {
  return {
      {"bootloader", PICOBOOT_BOOT_OFFSET, PICOBOOT_BOOT_LEN},
      {"bootloader state", PICOBOOT_STATE_OFFSET, PICOBOOT_STATE_LEN},
      {"active", PICOBOOT_ACTIVE_OFFSET, active_len},
      {"dfu", PICOBOOT_DFU_OFFSET(active_len), PICOBOOT_DFU_LEN(active_len)},
      {"LittleFS", PICOBOOT_FS_OFFSET(flash), PICOBOOT_FS_LEN},
      {"EEPROM sector", PICOBOOT_EEPROM_OFFSET(flash), PICOBOOT_EEPROM_LEN},
  };
}

void the_regions_tile_the_flash() {
  struct { uint32_t flash, active; } chips[] = {{PICOBOOT_RP2040_FLASH, PICOBOOT_RP2040_ACTIVE_LEN}, {PICOBOOT_RP2350_FLASH, PICOBOOT_RP2350_ACTIVE_LEN}};
  for (auto& c : chips) {
    uint32_t end = 0;
    for (const Region& r : regions(c.flash, c.active)) {
      TEST_ASSERT_EQUAL_HEX32_MESSAGE(end, r.off, r.name);  // in order, no gap, no overlap
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.off % PICOBOOT_SECTOR, r.name);
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, r.len % PICOBOOT_SECTOR, r.name);
      end = r.off + r.len;
    }
    TEST_ASSERT_EQUAL_HEX32(c.flash, end);
    // the swap's progress fits the state sector
    TEST_ASSERT_TRUE(2 + 4 * (c.active / PICOBOOT_SECTOR) <= PICOBOOT_PAGES_AT);
  }
}

// board_build.filesystem_size of [env:<env>] (k / m suffix), in bytes
static uint32_t fsSize(const std::string& env) {
  std::istringstream in(readFile("platformio.ini"));
  std::string line;
  bool inside = false;
  while (std::getline(in, line)) {
    line = trim(line);
    if (!line.empty() && line[0] == '[') inside = line == "[env:" + env + "]";
    if (!inside || line.rfind("board_build.filesystem_size", 0) != 0) continue;
    std::string v = trim(line.substr(line.find('=') + 1));
    uint32_t n = (uint32_t)strtoul(v.c_str(), nullptr, 10);
    char u = v.empty() ? 0 : v.back();
    return u == 'k' ? n * 1024 : u == 'm' ? n * 1024 * 1024 : n;
  }
  return 0;
}

void platformio_ini_matches_the_header() {
  TEST_ASSERT_EQUAL_UINT32(PICOBOOT_FS_LEN, fsSize("picow"));
  TEST_ASSERT_EQUAL_UINT32(PICOBOOT_FS_LEN, fsSize("pico2w"));
}

static uint32_t u32(const std::string& b, size_t at) {
  return (uint32_t)(uint8_t)b[at] | (uint32_t)(uint8_t)b[at + 1] << 8 | (uint32_t)(uint8_t)b[at + 2] << 16 | (uint32_t)(uint8_t)b[at + 3] << 24;
}

void built_images_are_where_the_map_puts_them() {
  struct { const char* boot; const char* app; const char* board; uint32_t active_len, ram_end, vt; } envs[] = {
      {"boot_rp2040", "picow", "pico-w", PICOBOOT_RP2040_ACTIVE_LEN, 0x20042000, 0x100},
      {"boot_rp2350", "pico2w", "pico2-w", PICOBOOT_RP2350_ACTIVE_LEN, 0x20082000, 0},
  };
  const uint32_t base = PICOBOOT_XIP_BASE + PICOBOOT_ACTIVE_OFFSET;
  char m[200];
  for (auto& e : envs) {
    std::string bp = std::string(".pio/build/") + e.boot + "/firmware.bin", ap = std::string(".pio/build/") + e.app + "/firmware.bin";
    std::string boot = exists(bp) ? readFile(bp.c_str()) : "", app = exists(ap) ? readFile(ap.c_str()) : "";
    if (!boot.empty()) {
      snprintf(m, sizeof m, "%s: %u B of the %u B bootloader region", e.boot, (unsigned)boot.size(), (unsigned)PICOBOOT_BOOT_LEN);
      TEST_MESSAGE(m);
      TEST_ASSERT_TRUE_MESSAGE(boot.size() <= PICOBOOT_BOOT_LEN, m);
      TEST_ASSERT_TRUE_MESSAGE(picoboot_vectors_ok((const uint8_t*)boot.data() + e.vt, PICOBOOT_XIP_BASE + e.vt, PICOBOOT_BOOT_LEN - e.vt, 0x20000000, e.ram_end), e.boot);
    } else {
      snprintf(m, sizeof m, "%s: not built, skipped", e.boot);
      TEST_MESSAGE(m);
    }
    if (!app.empty()) {
      snprintf(m, sizeof m, "%s: %u B, %.1f %% of the %u B active slot (target 80 %%)", e.app, (unsigned)app.size(), 100.0 * (double)app.size() / e.active_len, (unsigned)e.active_len);
      TEST_MESSAGE(m);
      TEST_ASSERT_TRUE_MESSAGE(app.size() <= e.active_len, m);
      TEST_ASSERT_TRUE_MESSAGE(picoboot_vectors_ok((const uint8_t*)app.data(), base, e.active_len, 0x20000000, e.ram_end), "the firmware is linked for the active slot");
      // the image carries its board and implementation markers (the update target checks them)
      TEST_ASSERT_TRUE(app.find(std::string("PROPTROLLER-IMPL:cpp")) != std::string::npos);
    } else {
      snprintf(m, sizeof m, "%s: not built, skipped", e.app);
      TEST_MESSAGE(m);
    }
    std::string mp = std::string(".pio/build/picoboot-") + e.board + "/flash.bin";
    if (exists(mp) && !boot.empty() && !app.empty()) {
      std::string merged = readFile(mp.c_str());
      TEST_ASSERT_EQUAL_UINT32(PICOBOOT_ACTIVE_OFFSET + app.size(), merged.size());
      TEST_ASSERT_TRUE(merged.compare(0, boot.size(), boot) == 0);
      for (size_t i = boot.size(); i < PICOBOOT_ACTIVE_OFFSET; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, (uint8_t)merged[i]);  // the state sector: erased
      TEST_ASSERT_TRUE(merged.compare(PICOBOOT_ACTIVE_OFFSET, app.size(), app) == 0);
      TEST_ASSERT_EQUAL_HEX32(u32(app, 0), u32(merged, PICOBOOT_ACTIVE_OFFSET));
      // the UF2 writes the state sector (erased) so a USB flash always starts from plain boot
      std::string up = std::string(".pio/build/picoboot-") + e.board + "/flash.uf2";
      std::string uf = readFile(up.c_str());
      TEST_ASSERT_TRUE(!uf.empty() && uf.size() % 512 == 0);
      std::vector<bool> seen(PICOBOOT_STATE_LEN / 256, false);
      for (size_t i = 0; i < uf.size(); i += 512) {
        uint32_t addr = u32(uf, i + 12);
        if (addr >= PICOBOOT_XIP_BASE + PICOBOOT_STATE_OFFSET && addr < PICOBOOT_XIP_BASE + PICOBOOT_STATE_OFFSET + PICOBOOT_STATE_LEN)
          seen[(addr - PICOBOOT_XIP_BASE - PICOBOOT_STATE_OFFSET) / 256] = true;
      }
      for (bool b : seen) TEST_ASSERT_TRUE_MESSAGE(b, "flash.uf2 covers the state sector");
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(the_regions_tile_the_flash);
  RUN_TEST(platformio_ini_matches_the_header);
  RUN_TEST(built_images_are_where_the_map_puts_them);
  return UNITY_END();
}
