// The flash map (firmware/FLASH-MAP.md): the ESP32 partition table the build uses
// (firmware/partitions/esp32-4mb.csv) and the Pico filesystem sizes in platformio.ini against the named
// constants of proptroller/flash_map.h; every image built so far (.pio/build/<env>/firmware.bin) against its
// slot.
#include <unity.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "proptroller/flash_map.h"
#include "proptroller/types.h"

using namespace pt;
void setUp() {}
void tearDown() {}

struct Part {
  std::string name, type, subtype;
  uint32_t offset, size;
};

static std::string readFile(const char* p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::vector<Part> table() {
  std::vector<Part> out;
  std::istringstream in(readFile("../partitions/esp32-4mb.csv"));
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    std::vector<std::string> f;
    std::stringstream ls(line);
    std::string x;
    while (std::getline(ls, x, ',')) f.push_back(trim(x));
    TEST_ASSERT_TRUE_MESSAGE(f.size() >= 5, line.c_str());
    out.push_back({f[0], f[1], f[2], (uint32_t)strtoul(f[3].c_str(), nullptr, 0), (uint32_t)strtoul(f[4].c_str(), nullptr, 0)});
  }
  return out;
}

static const Part* find(const std::vector<Part>& t, const char* name) {
  for (const Part& p : t)
    if (p.name == name) return &p;
  return nullptr;
}

void esp32_table_matches_the_constants() {
  std::vector<Part> t = table();
  TEST_ASSERT_TRUE(t.size() >= 6);
  const Part* a0 = find(t, "app0");
  const Part* a1 = find(t, "app1");
  const Part* fs = find(t, ESP32_LITTLEFS_LABEL);
  const Part* ota = find(t, "otadata");
  TEST_ASSERT_NOT_NULL(a0);
  TEST_ASSERT_NOT_NULL(a1);
  TEST_ASSERT_NOT_NULL_MESSAGE(fs, "LittleFS needs the partition labelled spiffs");
  TEST_ASSERT_NOT_NULL_MESSAGE(ota, "two OTA slots need otadata");
  TEST_ASSERT_EQUAL_STRING("app", a0->type.c_str());
  TEST_ASSERT_EQUAL_STRING("ota_0", a0->subtype.c_str());
  TEST_ASSERT_EQUAL_STRING("ota_1", a1->subtype.c_str());
  TEST_ASSERT_EQUAL_HEX32(ESP32_APP0_OFFSET, a0->offset);
  TEST_ASSERT_EQUAL_HEX32(ESP32_APP1_OFFSET, a1->offset);
  TEST_ASSERT_EQUAL_HEX32(ESP32_APP_SLOT_BYTES, a0->size);
  TEST_ASSERT_EQUAL_HEX32(ESP32_APP_SLOT_BYTES, a1->size);
  TEST_ASSERT_EQUAL_HEX32(ESP32_OTADATA_OFFSET, ota->offset);
  TEST_ASSERT_EQUAL_STRING("data", fs->type.c_str());
  TEST_ASSERT_EQUAL_STRING("spiffs", fs->subtype.c_str());
  TEST_ASSERT_EQUAL_HEX32(ESP32_LITTLEFS_OFFSET, fs->offset);
  TEST_ASSERT_EQUAL_HEX32(ESP32_LITTLEFS_BYTES, fs->size);
  // the reserved store and script partitions (FLASH-MAP.md): after the app slots, never in one
  const Part* store = find(t, "store");
  const Part* script = find(t, "script");
  TEST_ASSERT_NOT_NULL(store);
  TEST_ASSERT_NOT_NULL(script);
  TEST_ASSERT_EQUAL_HEX32(0x3F0000, store->offset);
  TEST_ASSERT_EQUAL_HEX32(0x3F8000, script->offset);
}

void esp32_table_is_valid() {
  std::vector<Part> t = table();
  uint32_t end = 0x9000;  // after the bootloader and the partition table
  for (const Part& p : t) {
    char m[96];
    snprintf(m, sizeof m, "%s at 0x%X starts before 0x%X (overlap)", p.name.c_str(), (unsigned)p.offset, (unsigned)end);
    TEST_ASSERT_TRUE_MESSAGE(p.offset >= end, m);
    TEST_ASSERT_EQUAL_MESSAGE(0u, p.offset % 0x1000, p.name.c_str());
    if (p.type == "app") TEST_ASSERT_EQUAL_MESSAGE(0u, p.offset % 0x10000, p.name.c_str());
    end = p.offset + p.size;
  }
  TEST_ASSERT_TRUE(end <= ESP32_FLASH_BYTES);
}

// board_build.filesystem_size in the [env:<env>] section of platformio.ini, in bytes
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

void pico_filesystem_sizes_match_the_constants() {
  TEST_ASSERT_EQUAL_UINT32(PICO_FS_BYTES, fsSize("pico"));
  TEST_ASSERT_EQUAL_UINT32(1788u * 1024, PICO_SKETCH_BYTES);
  // behind the Pico bootloader: LittleFS where picoboot_map.h puts it (tools/picoboot_ld.py links it there;
  // board_build.filesystem_size gives arduino-pico's FS_START / FS_END the same numbers)
  TEST_ASSERT_EQUAL_UINT32(PICOW_FS_BYTES, fsSize("picow"));
  TEST_ASSERT_EQUAL_UINT32(PICO2W_FS_BYTES, fsSize("pico2w"));
  TEST_ASSERT_EQUAL_UINT32(188u * 1024, PICOW_FS_BYTES);
  TEST_ASSERT_EQUAL_UINT32(0x10007000u, PICOBOOT_ACTIVE_ADDRESS);
  TEST_ASSERT_EQUAL_UINT32(933888u, PICOW_SLOT_BYTES);
  TEST_ASSERT_EQUAL_UINT32(1982464u, PICO2W_SLOT_BYTES);
}

// Every image built so far fits its slot (and is reported against the 80 % target). An env not built yet
// is skipped; `pio run` before `pio test` checks them all.
void built_images_fit_their_slots() {
  struct { const char* env; uint32_t slot; } envs[] = {
      {"esp32dev", ESP32_APP_SLOT_BYTES}, {"pico", PICO_SKETCH_BYTES},
      {"picow", PICOW_SLOT_BYTES}, {"pico2w", PICO2W_SLOT_BYTES}};
  for (auto& e : envs) {
    std::string p = std::string(".pio/build/") + e.env + "/firmware.bin";
    struct stat st;
    char m[160];
    if (stat(p.c_str(), &st) != 0) {
      snprintf(m, sizeof m, "%s: not built, skipped", e.env);
      TEST_MESSAGE(m);
      continue;
    }
    snprintf(m, sizeof m, "%s: %ld of %u B (%.1f %%, target %u %%)", e.env, (long)st.st_size, (unsigned)e.slot,
             100.0 * (double)st.st_size / e.slot, (unsigned)IMAGE_BUDGET_PERCENT);
    TEST_MESSAGE(m);
    TEST_ASSERT_TRUE_MESSAGE((uint32_t)st.st_size <= e.slot, m);
  }
}

// firmware/VERSION (the release, the same for both firmwares) against the defaults an Arduino IDE build reports
// and the library's own version
void the_version_is_the_same_everywhere() {
  std::string v = readFile("../VERSION");
  while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
  TEST_ASSERT_FALSE(v.empty());
  TEST_ASSERT_EQUAL_STRING(v.c_str(), PROPTROLLER_FW_VERSION);  // from the build (tools/embed_web.py)
  TEST_ASSERT_TRUE_MESSAGE(readFile("lib/proptroller/src/proptroller/types.h").find("#define PROPTROLLER_FW_VERSION \"" + v + "\"") != std::string::npos, "types.h default");
  TEST_ASSERT_TRUE_MESSAGE(readFile("lib/proptroller/library.properties").find("version=" + v) != std::string::npos, "library.properties");
  TEST_ASSERT_TRUE_MESSAGE(readFile("lib/proptroller/library.json").find("\"version\": \"" + v + "\"") != std::string::npos, "library.json");
}

// The owner supports every ESP32 revision (rev 0 to v3 DevKits): the image's header must not ask for a newer
// chip (esp_image_header_t: min_chip_rev at byte 14, min_chip_rev_full at 15-16; ESP-IDF esp_app_format.h).
// A core or IDF update that raised the minimum fails here. Skipped until esp32dev is built.
void esp32_image_runs_on_every_chip_revision() {
  std::string img = readFile(".pio/build/esp32dev/firmware.bin");
  if (img.size() < 24) { TEST_MESSAGE("esp32dev not built: skipped"); return; }
  TEST_ASSERT_EQUAL_HEX8(0xE9, (uint8_t)img[0]);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, (uint8_t)img[14], "min_chip_rev");
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, (uint16_t)((uint8_t)img[15] | (uint8_t)img[16] << 8), "min_chip_rev_full");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(esp32_table_matches_the_constants);
  RUN_TEST(esp32_table_is_valid);
  RUN_TEST(pico_filesystem_sizes_match_the_constants);
  RUN_TEST(built_images_fit_their_slots);
  RUN_TEST(the_version_is_the_same_everywhere);
  RUN_TEST(esp32_image_runs_on_every_chip_revision);
  return UNITY_END();
}
