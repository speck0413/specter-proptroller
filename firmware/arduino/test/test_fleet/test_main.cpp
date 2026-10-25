// Backups, over-the-air updates and stored records beyond the golden session: the backup round trip, update mode, rollbacks by a crash and by a
// failed self-check, records from newer and unreadable formats.
#include <unity.h>
#include <string.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include "proptroller/password.h"
#include "proptroller/sim/sim.h"
#include "proptroller/storage_ab.h"
#include "proptroller/flash_map.h"
#include "picoboot.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static std::string shaHex(const std::string& b) {
  uint8_t d[32];
  sha256((const uint8_t*)b.data(), b.size(), d);
  return toHex(d, 32);
}

static std::string sendImage(Sim& sim, const std::string& image) {
  UpdateRequest q;
  q.has_len = true;
  q.len = image.size();
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = shaHex(image);
  std::string answer;
  if (!sim.ctl().updateBegin(q, answer)) return answer;
  for (size_t i = 0; i < image.size(); i += 4096)
    if (!sim.updateWrite((const uint8_t*)image.data() + i, std::min<size_t>(4096, image.size() - i), answer)) return answer;
  return sim.updateFinish();
}

static std::string result(Sim& sim, const char* req, const char* field) {
  Response r = sim.request(req);
  std::string s;
  serializeJson(r.doc()["result"][field], s);
  return s;
}

struct MemFs : FileIo {
  std::map<std::string, std::string> f;
  bool read(const char* p, std::string& o) override { auto it = f.find(p); if (it == f.end()) return false; o = it->second; return true; }
  bool write(const char* p, const std::string& d) override { f[p] = d; return true; }
  bool remove(const char* p) override { return f.erase(p) > 0; }
};

// ---------------------------------------------------------------- backups

void backup_round_trip_is_exact() {
  Sim a("a1");
  TEST_ASSERT_TRUE(a.request(R"({"op":"set_params","values":[{"key":"name","value":"Porch"},{"key":"out2_name","value":"fog"},{"key":"pixel_count","value":30},{"key":"pin","value":"999999"}]})").isOk());
  TEST_ASSERT_TRUE(a.request(R"({"op":"put_show","data":{"id":5,"name":"Prop","triggers":[{"on":"input","input":"TRIG2"}],"tracks":[{"target":"RLY1","cues":[{"at":0,"do":"pulse","ms":3000}]}]}})").isOk());
  TEST_ASSERT_TRUE(a.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"PROP","terminal":"RLY1","label":"Prop button"}]}})").isOk());
  TEST_ASSERT_TRUE(a.request(R"({"op":"set_script","text":"effect door:\n  wait for user1\n  out3 = on\n  sleep 1\n  out3 = off\n"})").isOk());
  std::string backup = result(a, R"({"op":"get_backup"})", "data");
  TEST_ASSERT_TRUE(backup.find("9999") == std::string::npos);
  TEST_ASSERT_TRUE(backup.find("pin_hash") == std::string::npos && backup.find("\"wifi_") == std::string::npos);
  Sim b("b2");
  TEST_ASSERT_TRUE(b.request(R"({"op":"set_params","values":[{"key":"wifi_ssid","value":"other-net"}]})").isOk());
  Response r = b.request("{\"op\":\"put_backup\",\"data\":" + backup + "}");
  TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().c_str());
  std::string again = result(b, R"({"op":"get_backup"})", "data");
  // the same document apart from `from` and the kept name
  JsonDocument x, y;
  deserializeJson(x, backup);
  deserializeJson(y, again);
  x.remove("from"); y.remove("from");
  x.remove("name"); y.remove("name");
  std::string xs, ys;
  serializeJson(x, xs);
  serializeJson(y, ys);
  TEST_ASSERT_EQUAL_STRING(xs.c_str(), ys.c_str());
  // the target keeps its own Wi-Fi and name; the example show was replaced by the backup's shows
  TEST_ASSERT_EQUAL_STRING("\"other-net\"", result(b, R"({"op":"get_params"})", "values").find("other-net") != std::string::npos ? "\"other-net\"" : "missing");
  TEST_ASSERT_TRUE(result(b, R"({"op":"list_shows"})", "shows").find("\"id\":1,") == std::string::npos);
  // it survives a restart, and the restored script runs
  b.powerCycle();
  TEST_ASSERT_EQUAL_STRING(ys.c_str(), [&] { std::string s = result(b, R"({"op":"get_backup"})", "data"); JsonDocument z; deserializeJson(z, s); z.remove("from"); z.remove("name"); std::string o; serializeJson(z, o); return o; }().c_str());
  b.step(3000);
  b.press(Input::User1, 150);
  b.step(200);
  TEST_ASSERT_EQUAL(1000, b.level(Channel::Out3));
}

void a_bad_backup_changes_nothing() {
  Sim a("a1");
  std::string before = result(a, R"({"op":"get_backup"})", "data");
  JsonDocument d;
  deserializeJson(d, before);
  d["settings"][0]["key"] = "pixel_count";
  d["settings"][0]["value"] = 30;
  d["script"]["text"] = "effect a:\n  wait for trig1\n  out9 = on\n";
  std::string bad;
  serializeJson(d, bad);
  Response r = a.request("{\"op\":\"put_backup\",\"data\":" + bad + "}");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("invalid", r.doc()["error"]["code"]);
  std::string after = result(a, R"({"op":"get_backup"})", "data");
  TEST_ASSERT_EQUAL_STRING(before.c_str(), after.c_str());
  // not a document at all
  Response s = a.request(R"({"op":"put_backup","data":{"format":"proptroller-backup"}})");
  TEST_ASSERT_EQUAL_STRING("bad_request", s.doc()["error"]["code"]);
}

// ---------------------------------------------------------------- the script store

struct FailingStore : MemStorage {
  bool fail = false;
  FailingStore() : MemStorage(SCRIPT_STORE_CAPACITY) {}
  bool save(const std::string& d) override { return fail ? false : MemStorage::save(d); }
};

void a_script_that_cannot_be_stored_answers_internal_and_the_old_one_stays() {
  Sim sim("s");
  const char* OLD = "effect a:\n  wait for trig2\n  out2 = on\n  sleep 1\n  out2 = off\n";
  JsonDocument q;
  q["op"] = "set_script";
  q["text"] = OLD;
  std::string j;
  serializeJson(q, j);
  TEST_ASSERT_TRUE(sim.request(j).doc()["result"]["ok"].as<bool>());
  FailingStore st;
  std::string rec;
  sim.scriptFlash().load(rec);
  st.save(rec);
  st.fail = true;
  sim.ctl().setScriptStore(&st);
  Response r = sim.request(R"({"op":"set_script","text":"effect b:\n  wait for trig1\n  out1 = on\n"})");
  TEST_ASSERT_EQUAL_STRING("internal", r.doc()["error"]["code"]);
  TEST_ASSERT_EQUAL_STRING("saving the script to flash failed", r.doc()["error"]["message"]);
  TEST_ASSERT_EQUAL_STRING(OLD, sim.request(R"({"op":"get_script"})").doc()["result"]["text"]);
  sim.step(3000);
  sim.press(Input::Trig2, 150);
  sim.step(200);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out2));  // the old script still runs
  sim.ctl().setScriptStore(&sim.scriptFlash());
}

// ---------------------------------------------------------------- updates

void update_mode_ignores_triggers_and_turns_outputs_off() {
  Sim sim("u");
  sim.addExample();  // a fresh board has no shows
  std::string img = sim_image::build("0.2.0", "x", true, 10000);
  UpdateRequest q;
  q.has_len = true;
  q.len = img.size();
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = shaHex(img);
  std::string answer;
  sim.step(3000);
  TEST_ASSERT_TRUE(sim.ctl().updateBegin(q, answer));
  sim.press(Input::Trig1, 200);  // the example show would pulse OUT1
  sim.step(500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  sim.ctl().updateAbort();
  sim.step(500);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));  // no stale press after the update stopped
  sim.press(Input::Trig1, 200);
  sim.step(100);
  TEST_ASSERT_EQUAL(1000, sim.level(Channel::Out1));
}

void a_crash_while_pending_rolls_back() {
  Sim sim("u");
  TEST_ASSERT_TRUE(sendImage(sim, sim_image::build("0.2.0", "new", true, 5000)).find("\"done\"") != std::string::npos);
  sim.step(1100);
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("fw_build":"new")") != std::string::npos);
  // a refused update while pending
  TEST_ASSERT_TRUE(sendImage(sim, sim_image::build("0.3.0", "n2", true, 5000)).find("self-check") != std::string::npos);
  sim.step(5000);
  sim.powerCycle();  // a reset before the self-check
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("fw_build":"sim","mcu":"sim","board":"proptroller","update":{"slot":"app0","max_bytes":1966080,"state":"valid","rolled_back":true})") != std::string::npos);
}

// A board whose bootloader installs the update at the restart (the Pico W swap, modelled by the simulator):
// info.update.install_max_s says how long that may take; the boots that swap are marked (an update and its
// revert), a plain restart is not. Without the option the field is left out (the ESP32, the golden files).
void a_swapping_bootloader_reports_its_install_time() {
  Sim plain("p");
  TEST_ASSERT_TRUE(plain.infoJson().find("install_max_s") == std::string::npos);
  Sim sim("w");
  sim.setSwapMs(16000);
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("rolled_back":false,"install_max_s":180})") != std::string::npos);
  TEST_ASSERT_TRUE(sendImage(sim, sim_image::build("0.2.0", "new", true, 5000)).find("\"done\"") != std::string::npos);
  sim.step(1100);
  TEST_ASSERT_TRUE(sim.slots().swapped);
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("fw_build":"new")") != std::string::npos);
  sim.powerCycle();  // a reset before the self-check: swapped back
  TEST_ASSERT_TRUE(sim.slots().swapped);
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("state":"valid","rolled_back":true,"install_max_s":180)") != std::string::npos);
  sim.powerCycle();  // a plain restart
  TEST_ASSERT_FALSE(sim.slots().swapped);
}

void a_newer_settings_record_is_kept_and_fails_the_self_check() {
  Sim sim("r");
  std::string newer = R"({"version":7,"settings":{"name":"From the future"},"shows":[]})";
  TEST_ASSERT_TRUE(sim.flash().save(newer));
  // as after a downgrade over the air: the board boots the older image, pending
  sim.slots().slots[1] = sim.slots().slots[0];
  sim.slots().slots[1].fw_build = "old";
  sim.slots().boot_next = 1;
  sim.powerCycle();
  TEST_ASSERT_FALSE(sim.ctl().recordsOk());
  TEST_ASSERT_EQUAL_STRING("Proptroller", sim.ctl().settings().name.c_str());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"name","value":"Changed"}]})").isOk());
  sim.ctl().saveNow();
  std::string rec;
  sim.flash().load(rec);
  TEST_ASSERT_EQUAL_STRING(newer.c_str(), rec.c_str());  // never overwritten
  sim.step(UPDATE_SELF_CHECK_MS + 1000);  // the self-check fails: back to the image that wrote it
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("slot":"app0","max_bytes":1966080,"state":"valid","rolled_back":true)") != std::string::npos);
  sim.flash().load(rec);
  TEST_ASSERT_EQUAL_STRING(newer.c_str(), rec.c_str());  // the image that wrote it finds it as it was
}

static void renameTitle(JsonDocument& d) {
  if (d["settings"]["title"].is<const char*>()) {
    std::string t = d["settings"]["title"].as<std::string>();
    d["settings"].remove("title");
    d["settings"]["name"] = t;
  }
}
static void morePixels(JsonDocument& d) { d["settings"]["pixel_count"] = 32; }

void records_current_older_newer_unreadable() {
  JsonDocument d;
  uint32_t v;
  TEST_ASSERT_EQUAL((int)RecordKind::Current, (int)readRecord(R"({"version":1,"settings":{"name":"Door"}})", 1, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, d, v));
  TEST_ASSERT_EQUAL((int)RecordKind::Newer, (int)readRecord(R"({"version":7,"settings":{}})", 1, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, d, v));
  TEST_ASSERT_EQUAL(7, v);
  TEST_ASSERT_EQUAL((int)RecordKind::Unreadable, (int)readRecord("not json", 1, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, d, v));
  TEST_ASSERT_EQUAL((int)RecordKind::Unreadable, (int)readRecord(R"({"settings":{}})", 1, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, d, v));
  TEST_ASSERT_EQUAL((int)RecordKind::Unreadable, (int)readRecord(R"({"version":0,"settings":{}})", 1, STORED_MIGRATIONS, STORED_MIGRATION_COUNT, d, v));
  // a migration path: a format 1 record in which the name was "title", read by a format 3 firmware
  const RecordMigration steps[2] = {renameTitle, morePixels};
  TEST_ASSERT_EQUAL((int)RecordKind::Migrated, (int)readRecord(R"({"version":1,"settings":{"title":"Porch"}})", 3, steps, 2, d, v));
  TEST_ASSERT_EQUAL(3, d["version"].as<int>());
  TEST_ASSERT_EQUAL_STRING("Porch", d["settings"]["name"]);
  TEST_ASSERT_EQUAL(32, d["settings"]["pixel_count"].as<int>());
  TEST_ASSERT_EQUAL((int)RecordKind::Unreadable, (int)readRecord(R"({"version":1,"settings":{"title":"Porch"}})", 3, steps, 1, d, v));  // a missing step: not guessed
}

static void put32(std::string& b, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (char)(v >> (8 * i)); }

// The board image checks (update.h): ESP-IDF app images, and the Pico W / Pico 2 W images behind the Pico
// bootloader (linked for the active slot at 0x10007000) against plain arduino-pico images (linked for the start
// of flash), which are refused before anything is written. The bootloader's own check agrees on every case.
void image_checks() {
  // the first bytes of an ESP32 app image (as this firmware's firmware.bin starts)
  const uint8_t esp[40] = {0xe9, 0x05, 0x02, 0x20, 0xe8, 0x32, 0x08, 0x40, 0xee, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 1,
                           0x20, 0, 0x40, 0x3f, 0x1c, 0x72, 0x02, 0, 0x32, 0x54, 0xcd, 0xab, 0, 0, 0, 0};
  TEST_ASSERT_TRUE(esp_image::isEsp32App(esp, sizeof esp));
  uint8_t s3[40];
  memcpy(s3, esp, 40);
  s3[12] = 0x09;  // ESP32-S3
  TEST_ASSERT_FALSE(esp_image::isEsp32App(s3, 40));
  TEST_ASSERT_FALSE(esp_image::isEsp32App(esp, 20));
  // each chip of the family takes its own images only (the bare-devkit images, BARE-DEVKIT.md)
  uint8_t c3[40];
  memcpy(c3, esp, 40);
  c3[12] = 0x05;  // ESP32-C3
  TEST_ASSERT_TRUE(esp_image::isEspApp(s3, 40, esp_image::CHIP_ESP32S3));
  TEST_ASSERT_TRUE(esp_image::isEspApp(c3, 40, esp_image::CHIP_ESP32C3));
  TEST_ASSERT_FALSE(esp_image::isEspApp(c3, 40, esp_image::CHIP_ESP32S3));
  TEST_ASSERT_FALSE(esp_image::isEspApp(s3, 40, esp_image::CHIP_ESP32C3));
  TEST_ASSERT_FALSE(esp_image::isEspApp(esp, 40, esp_image::CHIP_ESP32C3));
  TEST_ASSERT_FALSE(esp_image::isEspApp(c3, 20, esp_image::CHIP_ESP32C3));
  auto both = [](const std::string& img, uint32_t slot, uint32_t ram_end) {
    bool ours = pico_image::vectorsOk((const uint8_t*)img.data(), img.size(), slot, ram_end);
    bool boot = picoboot_vectors_ok((const uint8_t*)img.data(), PICOBOOT_ACTIVE_ADDRESS, slot, pico_image::RAM_BASE, ram_end) != 0;
    TEST_ASSERT_EQUAL(boot, ours);
    return ours;
  };
  // a Pico W image behind the bootloader: its vector table at the start of the active slot
  std::string w(512, '\0');
  put32(w, 0, 0x20042000);
  put32(w, 4, 0x100070C1);
  TEST_ASSERT_TRUE(both(w, PICOW_SLOT_BYTES, pico_image::RP2040_RAM_END));
  // the Pico 2 W's stack top is outside the Pico W's RAM
  std::string w2(512, '\0');
  put32(w2, 0, 0x20082000);
  put32(w2, 4, 0x10007147);
  TEST_ASSERT_TRUE(both(w2, PICO2W_SLOT_BYTES, pico_image::RP2350_RAM_END));
  TEST_ASSERT_FALSE(both(w2, PICOW_SLOT_BYTES, pico_image::RP2040_RAM_END));
  put32(w2, 4, 0x10007146);  // not Thumb
  TEST_ASSERT_FALSE(both(w2, PICO2W_SLOT_BYTES, pico_image::RP2350_RAM_END));
  put32(w2, 4, 0x10007003);  // inside the vector table itself
  TEST_ASSERT_FALSE(both(w2, PICO2W_SLOT_BYTES, pico_image::RP2350_RAM_END));
  put32(w2, 4, 0x10007001 + PICOW_SLOT_BYTES);  // beyond the Pico W slot, inside the Pico 2 W one
  TEST_ASSERT_FALSE(both(w2, PICOW_SLOT_BYTES, pico_image::RP2350_RAM_END));
  TEST_ASSERT_TRUE(both(w2, PICO2W_SLOT_BYTES, pico_image::RP2350_RAM_END));
  // a plain arduino-pico image (boot2 first on the RP2040; the OTA loader's vectors at the start on the RP2350):
  // refused
  std::string plain(512, '\0');
  put32(plain, 256, 0x20042000);
  put32(plain, 260, 0x100001E3);
  TEST_ASSERT_FALSE(both(plain, PICOW_SLOT_BYTES, pico_image::RP2040_RAM_END));
  std::string plain2(512, '\0');
  put32(plain2, 0, 0x20080000);
  put32(plain2, 4, 0x10000147);
  TEST_ASSERT_FALSE(both(plain2, PICO2W_SLOT_BYTES, pico_image::RP2350_RAM_END));
  // a misaligned or out-of-RAM stack pointer, a short header, a gzipped image
  put32(w, 0, 0x20042002);
  TEST_ASSERT_FALSE(both(w, PICOW_SLOT_BYTES, pico_image::RP2040_RAM_END));
  put32(w, 0, 0x20000000);
  TEST_ASSERT_FALSE(both(w, PICOW_SLOT_BYTES, pico_image::RP2040_RAM_END));
  TEST_ASSERT_FALSE(pico_image::vectorsOk((const uint8_t*)w2.data(), 7, PICO2W_SLOT_BYTES, pico_image::RP2350_RAM_END));
  std::string gz = "\x1f\x8b\x08" + std::string(509, '\0');
  TEST_ASSERT_FALSE(both(gz, PICOW_SLOT_BYTES, pico_image::RP2040_RAM_END));
  // the markers, found across pieces; an image with the board marker but not the implementation's is refused
  MarkerScan scan(pico_image::mcuMarker("pico-w"), pico_image::implMarker());
  std::string img = std::string(3000, 'x') + pico_image::mcuMarker("pico-w") + "PROPTROLLER-IMPL:cpp" + std::string(1, '\0') + std::string(100, 'y');
  for (size_t i = 0; i < img.size(); i += 7) scan.add((const uint8_t*)img.data() + i, std::min<size_t>(7, img.size() - i));
  TEST_ASSERT_TRUE(scan.found());
  MarkerScan half(pico_image::mcuMarker("pico-w"), pico_image::implMarker());
  std::string r = std::string(200, 'x') + pico_image::mcuMarker("pico-w");
  half.add((const uint8_t*)r.data(), r.size());
  TEST_ASSERT_FALSE(half.found());
  MarkerScan other(pico_image::mcuMarker("pico2-w"), pico_image::implMarker());
  other.add((const uint8_t*)img.data(), img.size());
  TEST_ASSERT_FALSE(other.found());  // a Pico W image is not for a Pico 2 W
}

// the serial console and MQTT reach settings and the effect's controls directly: refused while updating too
void update_mode_refuses_the_console_and_mqtt() {
  Sim sim("u");
  sim.addExample();  // a fresh board has no shows
  TEST_ASSERT_TRUE(sim.request(R"({"op":"delete_show","show":1})").isOk());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"effect","value":"pulse_and_recover"}]})").isOk());
  sim.step(3000);
  std::string img = sim_image::build("0.2.0", "x", true, 10000);
  UpdateRequest q;
  q.has_len = true;
  q.len = img.size();
  q.octet_stream = true;
  q.has_sha256 = true;
  q.sha256 = shaHex(img);
  std::string answer;
  TEST_ASSERT_TRUE(sim.ctl().updateBegin(q, answer));
  TEST_ASSERT_EQUAL_STRING("error: a firmware update is running", sim.serial("pixel_count=5").c_str());
  TEST_ASSERT_EQUAL_STRING("error: a firmware update is running", sim.serial("remote_trigger_enable=true").c_str());
  TEST_ASSERT_EQUAL_STRING("error: a firmware update is running", sim.serial("trigger").c_str());
  std::string p = sim.ctl().mqttPrefix();
  auto m = sim.ctl().mqttOnMessage(p + "/config/set/pixel_count", "7");
  TEST_ASSERT_EQUAL(1, m.size());
  TEST_ASSERT_TRUE(m[0].payload.find("a firmware update is running") != std::string::npos);
  sim.ctl().mqttOnMessage(p + "/control/trigger", "");
  sim.step(500);
  TEST_ASSERT_EQUAL(16, sim.ctl().settings().pixel_count);
  TEST_ASSERT_EQUAL(0, sim.level(Channel::Out1));
  sim.ctl().updateAbort();
}

// the board package (BSP) a firmware was built for: info, the discovery answer, mDNS TXT and backups name it
void the_board_package_is_named_everywhere() {
  Info i;
  TEST_ASSERT_EQUAL_STRING("proptroller", i.board.c_str());  // no board support package: the Proptroller board
  i.board = "relay4-button";
  JsonDocument d;
  i.toJson(d.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("relay4-button", d["board"]);
  TEST_ASSERT_TRUE(i.announce(80).find(R"("board":"relay4-button")") != std::string::npos);
  bool txt = false;
  for (auto& kv : i.txt()) txt = txt || (kv.first == "board" && kv.second == "relay4-button");
  TEST_ASSERT_TRUE(txt);
  Identity id;
  TEST_ASSERT_EQUAL_STRING("proptroller", id.board.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(the_board_package_is_named_everywhere);
  RUN_TEST(backup_round_trip_is_exact);
  RUN_TEST(a_bad_backup_changes_nothing);
  RUN_TEST(a_script_that_cannot_be_stored_answers_internal_and_the_old_one_stays);
  RUN_TEST(update_mode_ignores_triggers_and_turns_outputs_off);
  RUN_TEST(a_crash_while_pending_rolls_back);
  RUN_TEST(a_newer_settings_record_is_kept_and_fails_the_self_check);
  RUN_TEST(records_current_older_newer_unreadable);
  RUN_TEST(image_checks);
  RUN_TEST(update_mode_refuses_the_console_and_mqtt);
  RUN_TEST(a_swapping_bootloader_reports_its_install_time);
  return UNITY_END();
}
