// The stored records on the boards' file layer (ABStorage over FileIo, as LittleFS gives it): opening the file
// system, damaged and newer records, wiping, sizes and flash wear.
#include <unity.h>
#include <map>
#include <set>
#include "proptroller/sim/sim.h"
#include "proptroller/storage_ab.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static Response setp(Sim& sim, const std::string& key, const std::string& v) {
  return sim.request(R"({"op":"set_params","values":[{"key":")" + key + R"(","value":)" + v + "}]}");
}
static void ok(const Response& r) { TEST_ASSERT_TRUE_MESSAGE(r.isOk(), r.json().substr(0, 300).c_str()); }

// LittleFS as the controller sees it, in memory, with the faults a board can have
struct MemFs : FileIo {
  std::map<std::string, std::string> files;
  bool mounted = true;
  int reformats = 0, writes = 0, reads = 0;
  std::set<std::string> fail_remove;  // these files cannot be removed (a power cut before it, or a flash fault)
  bool fail_writes = false;
  long lose_after = -1;  // a write that says it worked but keeps only this many bytes (a failed close or sync)
  bool read(const char* p, std::string& out) override {
    auto it = files.find(p);
    if (!mounted || it == files.end()) return false;
    reads++;
    out = it->second;
    return true;
  }
  bool write(const char* p, const std::string& d) override {
    if (!mounted || fail_writes) return false;
    writes++;
    files[p] = lose_after >= 0 ? d.substr(0, (size_t)lose_after) : d;
    return true;
  }
  bool remove(const char* p) override { return mounted && !fail_remove.count(p) && files.erase(p) > 0; }
  bool usable() override { return mounted; }
  bool reformat() override {
    reformats++;
    files.clear();
    mounted = true;
    return true;
  }
};

// a board's two stores on one file system
struct TestBoard {
  MemFs fs;
  ABStorage store{fs};
  ABStorage script{fs, "/script-a.bin", "/script-b.bin", SCRIPT_STORE_CAPACITY};
  Sim sim{"t"};
  TestBoard() {
    sim.useStores(&store, &script);
    sim.powerCycle();
  }
};

static std::string stateStore(Sim& sim) {
  JsonDocument st;
  deserializeJson(st, sim.ctl().stateJson());
  return st["store"] | "";
}

// ---------------------------------------------------------------- S1: never formatted on a failed mount

struct FakeMount : FsMount {
  bool mounts = false, is_blank = false, formats_ok = true;
  int formats = 0;
  bool mount() override { return mounts; }
  bool blank() override { return is_blank; }
  bool format() override {
    formats++;
    if (formats_ok) mounts = true;
    return formats_ok;
  }
};

void a_file_system_is_formatted_only_when_the_partition_is_blank() {
  FakeMount m;
  m.mounts = true;
  TEST_ASSERT_EQUAL((int)MountResult::Mounted, (int)mountOrFormatBlank(m));
  TEST_ASSERT_EQUAL(0, m.formats);
  FakeMount held;  // data there, but it does not mount (an older firmware's file system, damaged flash)
  TEST_ASSERT_EQUAL((int)MountResult::Refused, (int)mountOrFormatBlank(held));
  TEST_ASSERT_EQUAL(0, held.formats);
  FakeMount fresh;  // a new board: every byte 0xFF
  fresh.is_blank = true;
  TEST_ASSERT_EQUAL((int)MountResult::Formatted, (int)mountOrFormatBlank(fresh));
  TEST_ASSERT_EQUAL(1, fresh.formats);
  FakeMount bad;
  bad.is_blank = true;
  bad.formats_ok = false;
  TEST_ASSERT_EQUAL((int)MountResult::Refused, (int)mountOrFormatBlank(bad));
}

void a_file_system_that_cannot_be_opened_is_kept_until_a_factory_reset() {
  TestBoard b;
  ok(setp(b.sim, "name", "\"Porch\""));
  b.sim.step(5000);
  b.fs.mounted = false;  // the next power-on cannot mount it
  b.sim.powerCycle();
  TEST_ASSERT_FALSE(b.sim.ctl().recordsOk());  // a new image fails its self-check
  TEST_ASSERT_TRUE(stateStore(b.sim).find("could not be read and are kept") != std::string::npos);
  int w = b.fs.writes;
  ok(setp(b.sim, "name", "\"Hall\""));
  b.sim.step(70000);
  TEST_ASSERT_EQUAL(w, b.fs.writes);  // nothing written, nothing formatted
  TEST_ASSERT_EQUAL(0, b.fs.reformats);
  TEST_ASSERT_TRUE(b.fs.files.count("/cfg-a.bin") || b.fs.files.count("/cfg-b.bin"));
  // a factory reset formats it
  ok(b.sim.request(R"({"op":"factory_reset"})"));
  TEST_ASSERT_EQUAL(1, b.fs.reformats);
  b.sim.powerCycle();
  TEST_ASSERT_TRUE(b.sim.ctl().recordsOk());
  ok(setp(b.sim, "name", "\"Hall\""));
  b.sim.step(5000);
  b.sim.powerCycle();
  TEST_ASSERT_EQUAL_STRING("Hall", b.sim.ctl().settings().name.c_str());
}

// ---------------------------------------------------------------- S2: none / ok / present but invalid

void a_damaged_record_is_not_taken_for_an_empty_store() {
  MemFs fs;
  ABStorage st(fs);
  std::string out;
  TEST_ASSERT_EQUAL((int)Stored::None, (int)st.fetch(out));
  TEST_ASSERT_TRUE(st.save("one"));
  TEST_ASSERT_TRUE(st.save("two"));
  fs.files["/cfg-b.bin"][18] ^= 1;  // the newer copy damaged (a save cut short): the older one is read
  TEST_ASSERT_EQUAL((int)Stored::Ok, (int)st.fetch(out));
  TEST_ASSERT_EQUAL_STRING("one", out.c_str());
  fs.files["/cfg-a.bin"][18] ^= 1;  // both damaged
  TEST_ASSERT_EQUAL((int)Stored::Invalid, (int)st.fetch(out));
  TEST_ASSERT_FALSE(st.load(out));
  fs.files.erase("/cfg-a.bin");  // one missing, the other damaged
  TEST_ASSERT_EQUAL((int)Stored::Invalid, (int)st.fetch(out));
  fs.files["/cfg-b.bin"] = "PTC";  // shorter than a header
  TEST_ASSERT_EQUAL((int)Stored::Invalid, (int)st.fetch(out));
  fs.mounted = false;
  TEST_ASSERT_EQUAL((int)Stored::Invalid, (int)st.fetch(out));
}

void a_damaged_settings_record_is_kept() {
  TestBoard b;
  ok(setp(b.sim, "name", "\"Porch\""));
  b.sim.step(5000);
  ok(setp(b.sim, "name", "\"Hall\""));
  b.sim.step(35000);
  for (auto& f : b.fs.files)
    if (f.first.compare(0, 5, "/cfg-") == 0) f.second[20] ^= 1;  // both copies
  auto before = b.fs.files;
  b.sim.powerCycle();
  TEST_ASSERT_FALSE(b.sim.ctl().recordsOk());
  TEST_ASSERT_TRUE(stateStore(b.sim).find("could not be read and are kept") != std::string::npos);
  ok(setp(b.sim, "name", "\"Attic\""));
  b.sim.step(70000);
  TEST_ASSERT_TRUE(before == b.fs.files);  // never overwritten
  Response g = b.sim.request(R"({"op":"get_backup"})");
  std::string data;
  serializeJson(g.doc()["result"]["data"], data);
  TEST_ASSERT_FALSE(b.sim.request(R"({"op":"put_backup","data":)" + data + "}").isOk());
}

void a_damaged_script_record_fails_the_self_check() {
  TestBoard b;
  ok(b.sim.request(R"({"op":"set_script","text":"effect a:\n  wait for trig2\n  out2 = on\n  sleep 1\n  out2 = off\n"})"));
  for (auto& f : b.fs.files)
    if (f.first.compare(0, 8, "/script-") == 0) f.second[20] ^= 1;
  b.sim.powerCycle();
  TEST_ASSERT_FALSE(b.sim.ctl().recordsOk());
}

// ---------------------------------------------------------------- S3: a newer or unreadable script record is kept

static const char* const SCRIPT = R"({"op":"set_script","text":"effect a:\n  wait for trig2\n  out2 = on\n  sleep 1\n  out2 = off\n"})";

static void scriptRecordIsKept(Sim& sim, const std::string& rec) {
  std::string got;
  Response r = sim.request(SCRIPT);
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("factory reset first") != std::string::npos, r.errorMessage().c_str());
  r = sim.request(R"({"op":"set_script","text":""})");  // an empty script would remove it
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE(sim.scriptFlash().load(got) || sim.scriptFlash().damaged);
  if (!sim.scriptFlash().damaged) TEST_ASSERT_TRUE(got == rec);
  Response g = sim.request(R"({"op":"get_backup"})");
  std::string data;
  serializeJson(g.doc()["result"]["data"], data);
  r = sim.request(R"({"op":"put_backup","data":)" + data + "}");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("factory reset first") != std::string::npos, r.errorMessage().c_str());
  if (!sim.scriptFlash().damaged) {
    TEST_ASSERT_TRUE(sim.scriptFlash().load(got));
    TEST_ASSERT_TRUE(got == rec);
  }
  TEST_ASSERT_EQUAL(0, sim.scriptFlash().wipes);
  TEST_ASSERT_TRUE(stateStore(sim).find("show script") != std::string::npos);
  // a factory reset clears it, and the script can be set again
  ok(sim.request(R"({"op":"factory_reset"})"));
  sim.powerCycle();
  ok(sim.request(SCRIPT));
}

void a_newer_script_record_is_never_overwritten() {
  Sim sim("s");
  std::string rec = "PTSR";
  rec += (char)9;
  rec += (char)0;  // record format 9
  rec.append(18, '\0');
  rec += "future";
  TEST_ASSERT_TRUE(sim.scriptFlash().save(rec));
  sim.powerCycle();
  TEST_ASSERT_FALSE(sim.ctl().recordsOk());
  scriptRecordIsKept(sim, rec);
}

void an_unreadable_script_record_is_never_overwritten() {
  Sim sim("s");
  ok(sim.request(SCRIPT));
  std::string rec = "PTSR garbage";
  TEST_ASSERT_TRUE(sim.scriptFlash().save(rec));
  sim.powerCycle();
  TEST_ASSERT_FALSE(sim.ctl().recordsOk());
  scriptRecordIsKept(sim, rec);
  // both copies damaged on the file layer: the same
  ok(sim.request(SCRIPT));
  sim.scriptFlash().damaged = true;
  sim.scriptFlash().wipes = 0;
  sim.powerCycle();
  TEST_ASSERT_FALSE(sim.ctl().recordsOk());
  scriptRecordIsKept(sim, rec);
}

// ---------------------------------------------------------------- S4: wiping is power-safe and reported

void a_wipe_cut_short_never_brings_an_older_record_back() {
  MemFs fs;
  ABStorage st(fs);
  std::string out;
  TEST_ASSERT_TRUE(st.save("one"));  // A
  TEST_ASSERT_TRUE(st.save("two"));  // B (newest)
  fs.fail_remove = {"/cfg-b.bin"};   // the older-to-be copy cannot be removed
  TEST_ASSERT_TRUE(st.wipe());       // the tombstone hides it
  TEST_ASSERT_EQUAL((int)Stored::None, (int)st.fetch(out));
  // a power cut right after the tombstone: neither file removed
  TEST_ASSERT_TRUE(st.save("three"));
  TEST_ASSERT_TRUE(st.save("four"));
  fs.fail_remove = {"/cfg-a.bin", "/cfg-b.bin"};
  TEST_ASSERT_TRUE(st.wipe());
  TEST_ASSERT_EQUAL((int)Stored::None, (int)st.fetch(out));
  // and the next save after it is read back
  fs.fail_remove.clear();
  TEST_ASSERT_TRUE(st.save("five"));
  TEST_ASSERT_EQUAL((int)Stored::Ok, (int)st.fetch(out));
  TEST_ASSERT_EQUAL_STRING("five", out.c_str());
  TEST_ASSERT_TRUE(st.wipe());
  TEST_ASSERT_TRUE(fs.files.empty());
  // no tombstone and nothing removed: the wipe failed
  TEST_ASSERT_TRUE(st.save("six"));
  fs.fail_writes = true;
  fs.fail_remove = {"/cfg-a.bin", "/cfg-b.bin"};
  TEST_ASSERT_FALSE(st.wipe());
  TEST_ASSERT_EQUAL((int)Stored::Ok, (int)st.fetch(out));
}

void a_factory_reset_that_cannot_clear_the_flash_says_so() {
  Sim sim("t");
  ok(setp(sim, "name", "\"Porch\""));
  sim.step(5000);
  sim.flash().fail_wipes = true;
  Response r = sim.request(R"({"op":"factory_reset"})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("factory reset failed") != std::string::npos, r.errorMessage().c_str());
  TEST_ASSERT_FALSE(sim.ctl().rebootRequested());
  sim.flash().fail_wipes = false;
  ok(sim.request(SCRIPT));
  sim.scriptFlash().fail_wipes = true;
  r = sim.request(R"({"op":"set_script","text":""})");  // removing the script fails: said, the script stays
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE(sim.request(R"({"op":"get_script"})").json().find("effect a") != std::string::npos);
  TEST_ASSERT_FALSE(sim.request(R"({"op":"factory_reset"})").isOk());
  sim.scriptFlash().fail_wipes = false;
  ok(sim.request(R"({"op":"factory_reset"})"));
  TEST_ASSERT_TRUE(sim.ctl().rebootRequested());
}

// ---------------------------------------------------------------- S5: a backup with more shows than the board takes

static Response putBackupWithShows(Sim& sim, int n) {
  Response g = sim.request(R"({"op":"get_backup"})");
  JsonDocument d;
  d["op"] = "put_backup";
  d["data"] = g.doc()["result"]["data"];
  JsonArray a = d["data"]["shows"].to<JsonArray>();
  for (int i = 1; i <= n; i++) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = i;
    o["name"] = "s";
    o["triggers"].to<JsonArray>();
    o["tracks"].to<JsonArray>();
  }
  std::string body;
  serializeJson(d, body);
  return sim.request(body);
}

static size_t showCount(Sim& sim) { return sim.request(R"({"op":"list_shows"})").doc()["result"]["shows"].size(); }

void a_backup_with_too_many_shows_is_refused() {
  Sim sim("s");
  size_t before = showCount(sim);
  Response r = putBackupWithShows(sim, 20);
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING("the backup has 20 shows; this board takes at most 16 shows", r.errorMessage().c_str());
  TEST_ASSERT_EQUAL(before, showCount(sim));  // nothing changed
  r = putBackupWithShows(sim, (int)MAX_SHOWS + 1);
  TEST_ASSERT_FALSE(r.isOk());
  ok(putBackupWithShows(sim, (int)MAX_SHOWS));
  TEST_ASSERT_EQUAL(MAX_SHOWS, showCount(sim));
}

// ---------------------------------------------------------------- S6: a profile or a name that would not fit

void a_profile_or_name_that_would_not_fit_is_refused() {
  Sim sim("s");
  ok(setp(sim, "name", "\"Porch\""));
  sim.step(5000);
  std::string rec;
  TEST_ASSERT_TRUE(sim.flash().load(rec));
  MemStorage small(rec.size() + 8);  // a store with 8 bytes to spare
  TEST_ASSERT_TRUE(small.save(rec));
  sim.useStores(&small, nullptr);
  sim.powerCycle();
  Response r = setp(sim, "name", "\"A much longer board name\"");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("storage full") == 0, r.errorMessage().c_str());
  TEST_ASSERT_EQUAL_STRING("Porch", sim.ctl().settings().name.c_str());
  ok(setp(sim, "name", "\"Hall\""));  // shorter: fits
  r = sim.request(R"({"op":"set_profile","data":{"aliases":[{"alias":"VALVE","terminal":"OUT1","label":"The big valve"}]}})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_TRUE_MESSAGE(r.errorMessage().find("storage full") == 0, r.errorMessage().c_str());
  TEST_ASSERT_EQUAL(0, (int)sim.ctl().settings().profile.aliases.size());
  sim.step(70000);
  TEST_ASSERT_EQUAL(0, (int)sim.ctl().store_errors);  // every change that was taken was saved
  sim.useStores(nullptr, nullptr);
  sim.powerCycle();  // off `small` while it is still there
}

// ---------------------------------------------------------------- S7: flash wear

void nothing_changed_means_nothing_written() {
  Sim sim("s");
  ok(setp(sim, "ambient_volume", "25"));
  sim.step(5000);
  int s0 = sim.flash().saves;
  TEST_ASSERT_TRUE(s0 >= 1);
  for (int i = 0; i < 10; i++) ok(sim.request(R"({"op":"action","key":"save"})"));
  TEST_ASSERT_EQUAL(s0, sim.flash().saves);
  for (int i = 0; i < 10; i++) {
    ok(setp(sim, "ambient_volume", "25"));
    sim.step(2100);
  }
  TEST_ASSERT_EQUAL(s0, sim.flash().saves);
  // a retained MQTT config/set redelivered at every reconnect, and a value that is never stored
  for (int i = 0; i < 5; i++) {
    (void)sim.ctl().mqttOnMessage(sim.ctl().mqttPrefix() + "/config/set/ambient_volume", "25");
    (void)sim.ctl().mqttOnMessage(sim.ctl().mqttPrefix() + "/config/set/remote_trigger_enable", i % 2 ? "false" : "true");
    sim.step(3000);
  }
  TEST_ASSERT_EQUAL(s0, sim.flash().saves);
  // a change that is undone before it is saved is not written either
  ok(setp(sim, "ambient_volume", "26"));
  ok(setp(sim, "ambient_volume", "25"));
  sim.step(70000);
  TEST_ASSERT_EQUAL(s0, sim.flash().saves);
}

void settings_writes_are_at_least_30_s_apart_and_at_most_60_s_late() {
  Sim sim("s");
  sim.step(5000);
  int s0 = sim.flash().saves;
  // a value changed every 2.1 s for a minute: one write per 30 s at most
  for (int i = 0; i < 30; i++) {
    ok(setp(sim, "ambient_volume", std::to_string(i)));
    sim.step(2100);
  }
  sim.step(31000);
  int writes = sim.flash().saves - s0;
  TEST_ASSERT_TRUE_MESSAGE(writes >= 2 && writes <= 4, std::to_string(writes).c_str());
  std::string rec;
  TEST_ASSERT_TRUE(sim.flash().load(rec));
  TEST_ASSERT_TRUE(rec.find("\"ambient_volume\":29") != std::string::npos);  // the last value is stored
  // changed every second, never quiet for 2 s: still written within 60 s of the first change
  sim.step(40000);
  s0 = sim.flash().saves;
  for (int i = 0; i < 61; i++) {
    ok(setp(sim, "ambient_volume", std::to_string(i % 2 ? 11 : 12)));
    sim.step(1000);
  }
  TEST_ASSERT_TRUE(sim.flash().saves > s0);
}

// ---------------------------------------------------------------- N4: a write that fails without saying so

void a_save_is_read_back() {
  MemFs fs;
  ABStorage st(fs);
  std::string out;
  TEST_ASSERT_TRUE(st.save("one"));
  fs.lose_after = 18;  // the close lost the end of the file
  TEST_ASSERT_FALSE(st.save("two, lost on close"));
  TEST_ASSERT_TRUE(st.load(out));
  TEST_ASSERT_EQUAL_STRING("one", out.c_str());
  fs.lose_after = -1;
  // on a board: counted, said and tried again
  TestBoard b;
  ok(setp(b.sim, "name", "\"Porch\""));
  b.sim.step(5000);
  b.fs.lose_after = 30;
  ok(setp(b.sim, "name", "\"Hall\""));
  b.sim.step(40000);
  TEST_ASSERT_TRUE(b.sim.ctl().store_errors > 0);
  b.fs.lose_after = -1;
  b.sim.step(70000);
  b.sim.powerCycle();
  TEST_ASSERT_EQUAL_STRING("Hall", b.sim.ctl().settings().name.c_str());
}

// ---------------------------------------------------------------- N11: between a factory reset and the restart

void nothing_is_written_between_a_factory_reset_and_the_restart() {
  Sim sim("t");
  ok(setp(sim, "name", "\"Porch\""));
  ok(sim.request(SCRIPT));
  sim.step(5000);
  ok(sim.request(R"({"op":"factory_reset"})"));
  ok(setp(sim, "name", "\"Hall\""));  // a change in the moment before the restart
  ok(sim.request(R"({"op":"action","key":"save"})"));
  TEST_ASSERT_FALSE(sim.request(SCRIPT).isOk());
  sim.step(70000);
  std::string rec;
  TEST_ASSERT_FALSE(sim.flash().load(rec));
  TEST_ASSERT_FALSE(sim.scriptFlash().load(rec));
  sim.powerCycle();
  TEST_ASSERT_EQUAL_STRING(Settings().name.c_str(), sim.ctl().settings().name.c_str());
}

// ---------------------------------------------------------------- N2: no script record before the self-check

void no_script_record_is_written_before_the_self_check() {
  Sim sim("t");
  ok(sim.request(SCRIPT));
  std::string before, after;
  TEST_ASSERT_TRUE(sim.scriptFlash().load(before));
  // a new image boots, pending its self-check
  sim.slots().slots[1] = sim.slots().slots[0];
  sim.slots().slots[1].fw_build = "new";
  sim.slots().boot_next = 1;
  sim.powerCycle();
  int saves = sim.scriptFlash().saves;
  Response r = sim.request(R"({"op":"set_script","text":"effect b:\n  wait for trig1\n  out3 = on\n  sleep 1\n  out3 = off\n"})");
  TEST_ASSERT_FALSE(r.isOk());
  TEST_ASSERT_EQUAL_STRING(update_text::SELF_CHECK_PENDING, r.errorMessage().c_str());
  TEST_ASSERT_FALSE(sim.request(R"({"op":"set_script","text":""})").isOk());
  TEST_ASSERT_EQUAL(saves, sim.scriptFlash().saves);
  TEST_ASSERT_EQUAL(0, sim.scriptFlash().wipes);
  sim.step(UPDATE_SELF_CHECK_MS + 1000);  // passed: allowed again
  ok(sim.request(R"({"op":"set_script","text":"effect b:\n  wait for trig1\n  out3 = on\n  sleep 1\n  out3 = off\n"})"));
  TEST_ASSERT_TRUE(sim.scriptFlash().load(after));
  TEST_ASSERT_FALSE(after == before);
}

// a save knows which file holds the newest record (from the last load or save): it reads back only what it wrote
void a_save_reads_only_the_file_it_wrote() {
  MemFs fs;
  ABStorage st(fs);
  TEST_ASSERT_TRUE(st.save("one"));
  TEST_ASSERT_TRUE(st.save("two"));
  fs.reads = 0;
  TEST_ASSERT_TRUE(st.save("three"));
  TEST_ASSERT_EQUAL(1, fs.reads);
  ABStorage fresh(fs);  // after a power-on: the load finds the newest, the save then needs no scan
  std::string out;
  TEST_ASSERT_TRUE(fresh.load(out) && out == "three");
  fs.reads = 0;
  TEST_ASSERT_TRUE(fresh.save("four"));
  TEST_ASSERT_EQUAL(1, fs.reads);
  fs.fail_writes = true;  // a failed save forgets it: the next one checks both files again
  TEST_ASSERT_FALSE(fresh.save("five"));
  fs.fail_writes = false;
  fs.reads = 0;
  TEST_ASSERT_TRUE(fresh.save("five"));
  TEST_ASSERT_EQUAL(3, fs.reads);
  TEST_ASSERT_TRUE(fresh.load(out) && out == "five");
  ABStorage other(fs);
  TEST_ASSERT_TRUE(other.load(out) && out == "five");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(a_file_system_is_formatted_only_when_the_partition_is_blank);
  RUN_TEST(a_file_system_that_cannot_be_opened_is_kept_until_a_factory_reset);
  RUN_TEST(a_damaged_record_is_not_taken_for_an_empty_store);
  RUN_TEST(a_damaged_settings_record_is_kept);
  RUN_TEST(a_damaged_script_record_fails_the_self_check);
  RUN_TEST(a_newer_script_record_is_never_overwritten);
  RUN_TEST(an_unreadable_script_record_is_never_overwritten);
  RUN_TEST(a_wipe_cut_short_never_brings_an_older_record_back);
  RUN_TEST(a_factory_reset_that_cannot_clear_the_flash_says_so);
  RUN_TEST(a_backup_with_too_many_shows_is_refused);
  RUN_TEST(a_profile_or_name_that_would_not_fit_is_refused);
  RUN_TEST(nothing_changed_means_nothing_written);
  RUN_TEST(settings_writes_are_at_least_30_s_apart_and_at_most_60_s_late);
  RUN_TEST(a_save_is_read_back);
  RUN_TEST(nothing_is_written_between_a_factory_reset_and_the_restart);
  RUN_TEST(no_script_record_is_written_before_the_self_check);
  RUN_TEST(a_save_reads_only_the_file_it_wrote);
  return UNITY_END();
}
