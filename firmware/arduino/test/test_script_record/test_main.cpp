// The show script's stored record (script_record.h): the LZ codec (byte for byte, its ratio on the
// example scripts and the largest one), the step table as bytes, the record round trip, damaged and newer
// records, the stored table run at power-on without a new check unless the firmware, the settings or the table format changed,
// and the worst case against the store's size.
#include <unity.h>
#include <dirent.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "proptroller/lz.h"
#include "proptroller/script_record.h"
#include "proptroller/sim/sim.h"
#include "proptroller/storage.h"

using namespace pt;
void setUp() {}
void tearDown() {}

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
static std::vector<std::string> files(const std::string& dir, const std::string& ext) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0) out.push_back(dir + "/" + n);
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}
static const std::string FIX = "test/fixtures/";

// 8 effects, 224 lines, 8168 bytes: every limit at once (as test_script)
static std::string largest() {
  std::string t;
  for (int e = 0; e < SCRIPT_MAX_EFFECTS; e++) {
    t += "effect effect number " + std::to_string(e) + " with a long name:\n  wait for trig1\n";
    for (int i = 0; i < 26; i++) t += "  fade dmx" + std::to_string(e + 1) + " to 37.5% over 1.25 s # c\n";
  }
  return t;
}

static bool roundTrip(const std::string& s, size_t* packed = nullptr) {
  std::string p = lzPack(s), back;
  if (packed) *packed = p.size();
  return p.size() <= lzMaxPacked(s.size()) && lzUnpack((const uint8_t*)p.data(), p.size(), s.size(), back) && back == s;
}

void lz_gives_the_text_back_byte_for_byte() {
  TEST_ASSERT_TRUE(roundTrip(""));
  TEST_ASSERT_TRUE(roundTrip("a"));
  TEST_ASSERT_TRUE(roundTrip(std::string(5000, 'x')));  // overlapping matches
  std::mt19937 r(7);
  for (int t = 0; t < 300; t++) {
    std::string s;
    size_t n = r() % 9000;
    unsigned alphabet = 1 + (unsigned)(r() % 255);
    for (size_t i = 0; i < n; i++) s.push_back((char)(r() % alphabet));
    TEST_ASSERT_TRUE(roundTrip(s));
  }
  // damaged streams are refused, never read past their end
  std::string p = lzPack(largest()), back;
  TEST_ASSERT_FALSE(lzUnpack((const uint8_t*)p.data(), p.size() - 1, largest().size(), back));
  TEST_ASSERT_TRUE(back.empty());
  TEST_ASSERT_FALSE(lzUnpack((const uint8_t*)p.data(), p.size(), largest().size() + 1, back));
  TEST_ASSERT_FALSE(lzUnpack((const uint8_t*)p.data(), p.size(), largest().size() - 1, back));  // bytes left over
  const uint8_t before_start[3] = {0x00, 0x05, 0x00};  // a match 6 back, at the start
  TEST_ASSERT_FALSE(lzUnpack(before_start, 3, 3, back));
}

// two pieces packed as one give the same bytes as the joined text (the record packs text and sound list so)
void lz_of_two_pieces_is_lz_of_the_joined_text() {
  std::string t = largest();
  for (std::string snd : {std::string(), std::string("{}"), std::string(R"({"door creak":3,"scream":12})"), t.substr(100, 900)}) {
    TEST_ASSERT_TRUE(lzPack(t, snd) == lzPack(t + snd));
    TEST_ASSERT_TRUE(lzPack(snd, t) == lzPack(snd + t));
    for (size_t cut : {0, 1, 2, 3, 4095, 4096, 4097, 7000}) {
      if (cut > t.size()) continue;
      TEST_ASSERT_TRUE(lzPack(t.substr(0, cut), t.substr(cut) + snd) == lzPack(t + snd));
    }
  }
}

void lz_ratio_on_the_examples_and_the_largest_script() {
  size_t raw = 0, packed = 0;
  char m[160];
  for (auto& f : files("../show-script-examples", ".txt")) {
    std::string t = readFile(f);
    size_t p = 0;
    TEST_ASSERT_TRUE(roundTrip(t, &p));
    raw += t.size();
    packed += p;
    snprintf(m, sizeof m, "%s: %zu -> %zu B (%.0f %%)", f.substr(f.rfind('/') + 1).c_str(), t.size(), p, 100.0 * (double)p / (double)t.size());
    TEST_MESSAGE(m);
  }
  TEST_ASSERT_TRUE(raw > 0);
  snprintf(m, sizeof m, "the 7 examples: %zu -> %zu B (%.0f %%)", raw, packed, 100.0 * (double)packed / (double)raw);
  TEST_MESSAGE(m);
  TEST_ASSERT_TRUE(packed < raw * 85 / 100);
  size_t p = 0;
  TEST_ASSERT_TRUE(roundTrip(largest(), &p));
  snprintf(m, sizeof m, "the largest script: %zu -> %zu B (%.0f %%)", largest().size(), p, 100.0 * (double)p / (double)largest().size());
  TEST_MESSAGE(m);
  TEST_ASSERT_TRUE(p < largest().size() / 4);
}

static std::unique_ptr<ScriptProgram> compiled(const std::string& text, const ScriptBoard& b = ScriptBoard(), const SoundList& s = {}) {
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  ScriptCheck c = scriptCompile(text, s, b, *p);
  TEST_ASSERT_TRUE(c.ok());
  return p;
}
static std::string tableJson(const ScriptProgram& p) {
  JsonDocument d;
  scriptTableJson(p, d.to<JsonObject>());
  std::string s;
  serializeJson(d, s);
  return s;
}

void the_table_as_bytes_and_back() {
  ScriptBoard b;
  b.dmx_on = true;
  std::unique_ptr<ScriptProgram> p = compiled(largest(), b);
  std::string bytes = scriptTableEncode(*p);
  std::unique_ptr<ScriptProgram> q(new ScriptProgram());
  TEST_ASSERT_TRUE(scriptTableDecode((const uint8_t*)bytes.data(), bytes.size(), *q));
  TEST_ASSERT_EQUAL_STRING(tableJson(*p).c_str(), tableJson(*q).c_str());
  TEST_ASSERT_EQUAL(p->channels, q->channels);
  TEST_ASSERT_EQUAL(p->inputs, q->inputs);
  TEST_ASSERT_EQUAL(p->dmx, q->dmx);
  char m[96];
  snprintf(m, sizeof m, "the largest table: %zu B as bytes (%zu B in RAM)", bytes.size(), sizeof(ScriptProgram));
  TEST_MESSAGE(m);
  for (auto& f : files("../show-script-examples", ".txt")) {
    SoundList s = {{"chime.mp3", 3}, {"creak.mp3", 4}, {"scream.mp3", 5}, {"thunder.mp3", 6}, {"laugh.mp3", 7}};
    std::unique_ptr<ScriptProgram> e(new ScriptProgram());
    ScriptCheck c = scriptCompile(readFile(f), s, b, *e);
    if (!c.ok()) continue;  // an example that needs other settings or sounds
    std::string eb = scriptTableEncode(*e);
    TEST_ASSERT_TRUE(scriptTableDecode((const uint8_t*)eb.data(), eb.size(), *q));
    TEST_ASSERT_EQUAL_STRING(tableJson(*e).c_str(), tableJson(*q).c_str());
  }
  // every damage the runner could trip over is refused: wrong length, too many effects, an effect past the
  // steps, an op beyond the last, a wait on a missing input, a target past the DMX universe, a zero-time fade
  auto refused = [&](std::string x) { return !scriptTableDecode((const uint8_t*)x.data(), x.size(), *q); };
  TEST_ASSERT_TRUE(refused(bytes.substr(0, bytes.size() - 1)));
  TEST_ASSERT_TRUE(refused(bytes + "x"));
  std::string x = bytes;
  x[0] = (char)(SCRIPT_MAX_EFFECTS + 1);
  TEST_ASSERT_TRUE(refused(x));
  x = bytes;
  x[6 + SCRIPT_MAX_NAME + 3] = (char)0xFF;  // effect 0's count
  TEST_ASSERT_TRUE(refused(x));
  size_t step0 = 6 + p->effects * (SCRIPT_MAX_NAME + 15);
  x = bytes;
  x[step0 + 10] = 11;  // an op beyond the last
  TEST_ASSERT_TRUE(refused(x));
  x = bytes;
  x[step0 + 4] = (char)INPUT_COUNT;  // step 0 is the first effect's wait for trig1
  TEST_ASSERT_TRUE(refused(x));
  x = bytes;
  x[step0 + 12 + 4] = (char)0xFF;  // step 1 (a fade): target 0xFFxx
  x[step0 + 12 + 5] = (char)0xFF;
  TEST_ASSERT_TRUE(refused(x));
  x = bytes;
  for (int k = 0; k < 4; k++) x[step0 + 12 + k] = 0;  // the fade's time
  TEST_ASSERT_TRUE(refused(x));
  x = bytes;
  x[6 + SCRIPT_MAX_NAME] = 'x';  // a name without its end
  TEST_ASSERT_TRUE(refused(x));
}

void a_record_round_trip_and_its_damage() {
  std::string text = readFile("../show-script-examples/07_doorbell_chime.txt");
  SoundList sounds = {{"chime.mp3", 3}};
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  ScriptCheck c = scriptCompile(text, sounds, ScriptBoard(), *p);
  TEST_ASSERT_TRUE(c.ok());
  std::string rec = scriptRecordEncode(text, soundListJson(sounds), p.get(), 0x12345678);
  std::unique_ptr<ScriptProgram> q(new ScriptProgram());
  ScriptRecordRead r = scriptRecordDecode(rec, *q);
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Current, (int)r.kind);
  TEST_ASSERT_EQUAL(SCRIPT_RECORD_VERSION, r.version);
  TEST_ASSERT_EQUAL_STRING(text.c_str(), r.text.c_str());
  TEST_ASSERT_EQUAL_STRING(R"({"chime.mp3":3})", r.sounds_json.c_str());
  TEST_ASSERT_TRUE(r.has_table);
  TEST_ASSERT_EQUAL_HEX32(0x12345678, r.key);
  TEST_ASSERT_EQUAL_STRING(tableJson(*p).c_str(), tableJson(*q).c_str());
  char m[128];
  snprintf(m, sizeof m, "doorbell example: record %zu B (text %zu B, table %zu B)", rec.size(), text.size(), scriptTableEncode(*p).size());
  TEST_MESSAGE(m);
  // without a table: the text is checked at power-on
  r = scriptRecordDecode(scriptRecordEncode(text, "", nullptr, 0), *q);
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Current, (int)r.kind);
  TEST_ASSERT_FALSE(r.has_table);
  // a damaged table: the text is still there, the table is not used
  std::string bad = rec;
  bad[bad.size() - 1] = 99;  // the last step's op
  r = scriptRecordDecode(bad, *q);
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Current, (int)r.kind);
  TEST_ASSERT_FALSE(r.has_table);
  TEST_ASSERT_EQUAL_STRING(text.c_str(), r.text.c_str());
  // damaged elsewhere: unreadable
  for (size_t cut : {(size_t)0, (size_t)3, SCRIPT_RECORD_HEADER - 1, SCRIPT_RECORD_HEADER + 5, rec.size() - 1})
    TEST_ASSERT_EQUAL((int)ScriptRecordKind::Unreadable, (int)scriptRecordDecode(rec.substr(0, cut), *q).kind);
  bad = rec;
  bad[12] = (char)0xFF;  // the text length
  bad[13] = (char)0xFF;
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Unreadable, (int)scriptRecordDecode(bad, *q).kind);
  bad = rec;
  bad[4] = 0;  // no such format
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Unreadable, (int)scriptRecordDecode(bad, *q).kind);
  // a newer firmware's record: left alone
  bad = rec;
  bad[4] = 3;
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Newer, (int)scriptRecordDecode(bad, *q).kind);
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Unreadable, (int)scriptRecordDecode(R"({"version":1,"text":"x"})", *q).kind);
  // what cannot be stored is not encoded
  TEST_ASSERT_TRUE(scriptRecordEncode(std::string(SCRIPT_MAX_BYTES + 1, 'a'), "", nullptr, 0).empty());
  TEST_ASSERT_TRUE(scriptRecordEncode("x", std::string(70000, 'a'), nullptr, 0).empty());
}

// The largest text that nothing compresses and the largest table, with 254 sound names that do not compress
// either (32 random characters each), fit the store. Sound names have no length limit, so a long enough list
// does not fit: set_script then says so and keeps the old script.
static std::string randomName(std::mt19937& r, size_t n) {
  static const char ALNUM[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::string s;
  for (size_t i = 0; i < n; i++) s.push_back(ALNUM[r() % (sizeof ALNUM - 1)]);
  return s + ".mp3";
}
void the_worst_case_fits_the_store() {
  std::mt19937 r(3);
  std::string text;
  for (size_t i = 0; i < SCRIPT_MAX_BYTES; i++) text.push_back((char)(33 + r() % 90));
  JsonDocument d;
  for (int i = 0; i < SCRIPT_MAX_SOUNDS - 1; i++) d[randomName(r, 32)] = 1 + i % SCRIPT_MAX_TRACK;
  std::string sounds;
  serializeJson(d, sounds);
  ScriptBoard b;
  b.dmx_on = true;
  std::unique_ptr<ScriptProgram> p = compiled(largest(), b);
  std::string rec = scriptRecordEncode(text, sounds, p.get(), 1);
  char m[256];
  snprintf(m, sizeof m, "worst case: %zu B of the %zu B store (text %zu B, %d random 32-character sound names %zu B, table %zu B)",
           rec.size(), SCRIPT_STORE_CAPACITY, text.size(), SCRIPT_MAX_SOUNDS - 1, sounds.size(), scriptTableEncode(*p).size());
  TEST_MESSAGE(m);
  TEST_ASSERT_TRUE(rec.size() <= SCRIPT_STORE_CAPACITY);
  // longer names: too big for the store; the board says so and the stored script stays
  JsonDocument big;
  for (int i = 0; i < SCRIPT_MAX_SOUNDS - 1; i++) big[randomName(r, 120)] = 1 + i % SCRIPT_MAX_TRACK;
  Sim sim("w");
  JsonDocument q;
  q["op"] = "set_script";
  q["text"] = "effect lamp:\n  wait for trig1\n  out2 = on\n";
  std::string j;
  serializeJson(q, j);
  TEST_ASSERT_TRUE(sim.request(j).doc()["result"]["ok"].as<bool>());
  q["text"] = "effect lamp two:\n  wait for trig2\n  out3 = on\n";
  q["sounds"] = big.as<JsonObject>();
  serializeJson(q, j = std::string());
  Response res = sim.request(j);
  TEST_ASSERT_FALSE(res.isOk());
  TEST_ASSERT_TRUE(res.json().find("do not fit the script store") != std::string::npos);
  TEST_ASSERT_TRUE(sim.request(R"({"op":"get_script"})").json().find("effect lamp") != std::string::npos);
}

static const char* const LAMP = "effect lamp:\n  wait for trig1\n  out2 = on\n  sleep 1\n  out2 = off\n";
static Response setScript(Sim& sim, const std::string& text) {
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = text;
  std::string j;
  serializeJson(d, j);
  return sim.request(j);
}
static bool lampRuns(Sim& sim) {
  sim.step(2500);
  sim.press(Input::Trig1, 150);
  sim.step(100);
  return sim.level(Channel::Out2) == 1000;
}

// Power-on runs the stored table: no new check while the firmware, the settings and the table format are the
// same; a new check (and the record written again) when one of them changed or the table is damaged.
void power_on_runs_the_stored_table() {
  Sim sim("k");
  TEST_ASSERT_TRUE(setScript(sim, LAMP).doc()["result"]["ok"].as<bool>());
  std::string stored;
  TEST_ASSERT_TRUE(sim.scriptFlash().load(stored));
  TEST_ASSERT_EQUAL_STRING("PTSR", stored.substr(0, 4).c_str());
  sim.powerCycle();
  TEST_ASSERT_EQUAL(0, sim.ctl().script_checks_at_boot);
  TEST_ASSERT_TRUE(lampRuns(sim));
  TEST_ASSERT_EQUAL_STRING(LAMP, sim.request(R"({"op":"get_script"})").doc()["result"]["text"]);
  // a setting the checker reads (the DMX port): checked again once, written again, then the table is used
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"dmx_port","value":"s4"}]})").isOk());
  sim.ctl().saveNow();
  sim.powerCycle();
  TEST_ASSERT_EQUAL(1, sim.ctl().script_checks_at_boot);
  sim.step(20);
  sim.powerCycle();
  TEST_ASSERT_EQUAL(0, sim.ctl().script_checks_at_boot);
  TEST_ASSERT_TRUE(lampRuns(sim));
  // a damaged table: checked from the text, still runs
  TEST_ASSERT_TRUE(sim.scriptFlash().load(stored));
  stored[stored.size() - 1] = 99;
  TEST_ASSERT_TRUE(sim.scriptFlash().save(stored));
  sim.powerCycle();
  TEST_ASSERT_EQUAL(1, sim.ctl().script_checks_at_boot);
  TEST_ASSERT_TRUE(lampRuns(sim));
}

// A new firmware (another build) checks the stored text again, and writes the record again only after its
// self-check (a rollback finds the record as it was).
void a_new_firmware_checks_again_and_rewrites_after_its_self_check() {
  Sim sim("u");
  TEST_ASSERT_TRUE(setScript(sim, LAMP).doc()["result"]["ok"].as<bool>());
  std::string before;
  TEST_ASSERT_TRUE(sim.scriptFlash().load(before));
  // an update to a new build, as test_fleet sends it
  sim.slots().slots[1] = sim.slots().slots[0];
  sim.slots().slots[1].fw_build = "next";
  sim.slots().boot_next = 1;
  sim.powerCycle();
  TEST_ASSERT_EQUAL(1, sim.ctl().script_checks_at_boot);
  sim.step(1000);
  std::string now;
  TEST_ASSERT_TRUE(sim.scriptFlash().load(now));
  TEST_ASSERT_TRUE_MESSAGE(now == before, "not written while the new image is pending");
  sim.step(31000);  // the self-check passes
  TEST_ASSERT_TRUE(sim.infoJson().find(R"("state":"valid")") != std::string::npos);
  TEST_ASSERT_TRUE(sim.scriptFlash().load(now));
  TEST_ASSERT_TRUE_MESSAGE(now != before, "written again with the new build's table");
  sim.powerCycle();
  TEST_ASSERT_EQUAL(0, sim.ctl().script_checks_at_boot);
  TEST_ASSERT_TRUE(lampRuns(sim));
}

// The text the committed records hold: the doorbell example as it was written when they were made (with `sleep`,
// the older spelling of `wait`, which a stored script keeps, SS-C2, and which still checks and runs).
static std::string storedDoorbell() { return readFile(FIX + "records/script-record-text.txt"); }

// A record stored by a firmware with the first table format (rc.2): its text reads back, its table does not
// (has_table false), so the board checks the text again at the next power-on.
void a_table_format_1_record_is_checked_again() {
  std::string rec = readFile(FIX + "records/script-record-table1.bin");
  std::unique_ptr<ScriptProgram> q(new ScriptProgram());
  ScriptRecordRead r = scriptRecordDecode(rec, *q);
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Current, (int)r.kind);
  TEST_ASSERT_EQUAL_STRING(storedDoorbell().c_str(), r.text.c_str());
  TEST_ASSERT_FALSE(r.has_table);
}

// The committed record (FIXTURE_WRITE=1 writes it again) reads back byte for byte, and runs on a board.
void the_record_fixture_reads_back() {
  const std::string f2 = FIX + "records/script-record.bin";
  const std::string text = storedDoorbell();
  SoundList sounds = {{"chime.mp3", 3}};
  std::unique_ptr<ScriptProgram> p(new ScriptProgram());
  TEST_ASSERT_TRUE(scriptCompile(text, sounds, ScriptBoard(), *p).ok());
  if (getenv("FIXTURE_WRITE")) {
    std::ofstream o(f2, std::ios::binary);
    std::string rec = scriptRecordEncode(text, soundListJson(sounds), p.get(), 0x0F0E0D0C);
    o.write(rec.data(), (std::streamsize)rec.size());
  }
  std::string rec = readFile(f2);
  std::unique_ptr<ScriptProgram> q(new ScriptProgram());
  ScriptRecordRead r = scriptRecordDecode(rec, *q);
  TEST_ASSERT_EQUAL((int)ScriptRecordKind::Current, (int)r.kind);
  TEST_ASSERT_EQUAL_STRING(text.c_str(), r.text.c_str());
  TEST_ASSERT_TRUE(r.has_table);
  TEST_ASSERT_EQUAL_STRING(tableJson(*p).c_str(), tableJson(*q).c_str());
  // and on a board: another key (another build), so checked again, and it runs
  Sim b("f");
  TEST_ASSERT_TRUE(b.scriptFlash().save(rec));
  b.powerCycle();
  TEST_ASSERT_TRUE(b.ctl().recordsOk());
  TEST_ASSERT_EQUAL(1, b.ctl().script_checks_at_boot);
  TEST_ASSERT_EQUAL_STRING(text.c_str(), b.request(R"({"op":"get_script"})").doc()["result"]["text"]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(lz_gives_the_text_back_byte_for_byte);
  RUN_TEST(lz_of_two_pieces_is_lz_of_the_joined_text);
  RUN_TEST(lz_ratio_on_the_examples_and_the_largest_script);
  RUN_TEST(the_table_as_bytes_and_back);
  RUN_TEST(a_record_round_trip_and_its_damage);
  RUN_TEST(the_worst_case_fits_the_store);
  RUN_TEST(power_on_runs_the_stored_table);
  RUN_TEST(a_new_firmware_checks_again_and_rewrites_after_its_self_check);
  RUN_TEST(the_record_fixture_reads_back);
  RUN_TEST(a_table_format_1_record_is_checked_again);
  return UNITY_END();
}
