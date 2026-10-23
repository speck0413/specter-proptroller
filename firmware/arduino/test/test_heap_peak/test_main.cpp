// The heap a big request needs at its peak (Controller::call's "need" rule is set from these numbers):
// malloc and free are counted here (glibc, not under the sanitizers, which bring their own allocator), so
// every allocation counts: std::string, std::vector and ArduinoJson's pools. The host is 64-bit; the boards'
// pointers are half the size, so these are upper bounds for them.
#include <unity.h>
#include <atomic>
#include <map>
#include "proptroller/sim/sim.h"
#include "proptroller/storage_ab.h"
#if defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace pt;
void setUp() {}
void tearDown() {}

#if !defined(__GLIBC__)  // the counting wraps glibc's __libc_malloc (not on macOS)
#define PT_COUNT_HEAP 0
#elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define PT_COUNT_HEAP 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PT_COUNT_HEAP 0
#endif
#endif
#ifndef PT_COUNT_HEAP
#define PT_COUNT_HEAP 1
#endif

static std::atomic<long> g_live{0}, g_peak{0};
#if PT_COUNT_HEAP
extern "C" {
void* __libc_malloc(size_t);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
void __libc_free(void*);
static void counted(void* p) {
  if (!p) return;
  long now = g_live += (long)malloc_usable_size(p);
  long pk = g_peak.load();
  while (now > pk && !g_peak.compare_exchange_weak(pk, now)) {}
}
void* malloc(size_t n) { void* p = __libc_malloc(n); counted(p); return p; }
void* calloc(size_t a, size_t b) { void* p = __libc_calloc(a, b); counted(p); return p; }
void free(void* p) { if (p) g_live -= (long)malloc_usable_size(p); __libc_free(p); }
void* realloc(void* p, size_t n) {
  if (p) g_live -= (long)malloc_usable_size(p);
  void* q = __libc_realloc(p, n);
  if (q) counted(q);
  else if (p && n) counted(p);  // failed: the old block stays
  return q;
}
}
#endif

// the bytes above the heap in use before the call, at the call's peak
[[maybe_unused]] static long peakOf(Sim& sim, const std::string& body, std::string* out = nullptr) {
  g_peak = g_live.load();
  long base = g_live.load();
  std::string r = sim.ctl().call(body.data(), body.size());
  long pk = g_peak.load() - base;
  if (out) *out = r;
  return pk;
}

// every script limit at once, just under SCRIPT_MAX_BYTES
[[maybe_unused]] static std::string largestScript() {
  std::string t;
  for (int e = 0; e < SCRIPT_MAX_EFFECTS; e++) {
    t += "effect effect number " + std::to_string(e) + " with a long name:\n  wait for trig1\n";
    for (int i = 0; i < 26; i++) t += "  fade dmx" + std::to_string(e + 1) + " to 37.5% over 1.25 s # c\n";
  }
  return t;
}

// the stores the next scenario runs on (nullptr: the simulator's own)
static Storage* g_store = nullptr;
static Storage* g_script_store = nullptr;

[[maybe_unused]] static void ok(const std::string& out) { TEST_ASSERT_TRUE_MESSAGE(out.find("\"ok\":true") != std::string::npos, out.substr(0, 300).c_str()); }

void put_backup_of_the_largest_script_stays_under_the_need_rule() {
#if !PT_COUNT_HEAP
  TEST_IGNORE_MESSAGE("heap counting is off under the sanitizers and without glibc");
#else
  Sim sim("t");
  sim.useStores(g_store, g_script_store);
  sim.powerCycle();
  TEST_ASSERT_TRUE(sim.request(R"({"op":"set_params","values":[{"key":"dmx_port","value":"s4"}]})").isOk());
  // shows: as many 2-track, 20-cue shows as the store takes
  for (int id = 2; id <= 12; id++) {
    JsonDocument s;
    s["id"] = id;
    s["name"] = "show number " + std::to_string(id);
    s["triggers"][0]["on"] = "input";
    s["triggers"][0]["input"] = "TRIG1";
    for (int t = 0; t < 2; t++) {
      s["tracks"][t]["target"] = t ? "OUT2" : "OUT1";
      for (int c = 0; c < 20; c++) {
        s["tracks"][t]["cues"][c]["at"] = c * 100;
        s["tracks"][t]["cues"][c]["do"] = c % 2 ? "off" : "on";
      }
    }
    JsonDocument q;
    q["op"] = "put_show";
    q["data"] = s;
    std::string j;
    serializeJson(q, j);
    if (!sim.request(j).isOk()) break;
  }
  JsonDocument d;
  d["op"] = "set_script";
  d["text"] = largestScript();
  std::string set, check;
  serializeJson(d, set);
  d["op"] = "check_script";
  serializeJson(d, check);
  std::string out;
  long pc = peakOf(sim, check, &out);
  ok(out);
  long ps = peakOf(sim, set, &out);
  ok(out);
  Response b = sim.request(R"({"op":"get_backup"})");
  JsonDocument p;
  p["op"] = "put_backup";
  p["data"] = b.doc()["result"]["data"];
  std::string backup;
  serializeJson(p, backup);
  long pb = peakOf(sim, backup, &out);
  ok(out);
  char msg[200];
  snprintf(msg, sizeof msg, "peak bytes (64-bit host): check_script %ld for %zu, set_script %ld for %zu, put_backup %ld for %zu", pc, check.size(), ps,
           set.size(), pb, backup.size());
  TEST_MESSAGE(msg);
  // the rule Controller::call refuses by must cover the measured peak
  for (auto x : {std::make_pair(pc, check.size()), std::make_pair(ps, set.size()), std::make_pair(pb, backup.size())})
    TEST_ASSERT_TRUE_MESSAGE((size_t)x.first <= Controller::requestHeapNeed(x.second), msg);
  // and the peak stays where these fixes put it (was 103 KB on this host before put_backup stopped keeping
  // a second copy of the body, the script text and every show beside the parsed document)
  TEST_ASSERT_TRUE_MESSAGE(pb < 84 * 1024, msg);
  TEST_ASSERT_TRUE_MESSAGE(pc < 44 * 1024 && ps < 52 * 1024, msg);
#endif
}

// LittleFS in memory: each file's room is taken before anything is measured (it stands for the flash, not the
// heap), so only what ABStorage itself allocates counts
struct FlashFiles : FileIo {
  std::map<std::string, std::string> files;
  FlashFiles() {
    for (const char* p : {"/cfg-a.bin", "/cfg-b.bin", "/script-a.bin", "/script-b.bin"}) files[p].reserve(SCRIPT_STORE_CAPACITY + 64);
  }
  bool there(const char* p) {
    auto it = files.find(p);
    return it != files.end() && it->second.size() > 0;
  }
  bool read(const char* p, std::string& out) override {
    if (!there(p)) return false;
    out = files[p];
    return true;
  }
  bool write(const char* p, const std::string& d) override { return writeRecord(p, nullptr, 0, d); }
  bool writeRecord(const char* p, const uint8_t* head, size_t n, const std::string& d) override {
    std::string& f = files[p];
    f.assign((const char*)head, n);
    f.append(d);
    return true;
  }
  bool remove(const char* p) override {
    if (!there(p)) return false;
    files[p].clear();
    return true;
  }
  bool exists(const char* p) override { return there(p); }
  bool scan(const char* p, ByteSink& sink) override {
    if (!there(p)) return false;
    const std::string& f = files[p];
    for (size_t at = 0; at < f.size(); at += 256) sink.piece((const uint8_t*)f.data() + at, std::min<size_t>(256, f.size() - at));
    return true;
  }
};

void ab_storage_reads_and_writes_without_copies() {
#if !PT_COUNT_HEAP
  TEST_IGNORE_MESSAGE("heap counting is off under the sanitizers and without glibc");
#else
  FlashFiles fs;
  ABStorage st(fs, "/script-a.bin", "/script-b.bin", SCRIPT_STORE_CAPACITY);
  std::string rec(22 * 1024, 'x');
  TEST_ASSERT_TRUE(st.save(rec) && st.save(rec));
  long base = g_live.load();
  g_peak = base;
  TEST_ASSERT_TRUE(st.save(rec));
  long save_peak = g_peak.load() - base;
  std::string out;
  base = g_live.load();
  g_peak = base;
  TEST_ASSERT_TRUE(st.load(out) && out == rec);
  long load_peak = g_peak.load() - base;
  char msg[160];
  snprintf(msg, sizeof msg, "ABStorage, a %zu B record: save %ld B, load %ld B above the heap in use", rec.size(), save_peak, load_peak);
  TEST_MESSAGE(msg);
  // was 3.0 x the record for each (both files read whole, then a copy of the payload)
  TEST_ASSERT_TRUE_MESSAGE(save_peak < 1024, msg);
  TEST_ASSERT_TRUE_MESSAGE(load_peak < (long)rec.size() + 1024, msg);
#endif
}

void put_backup_on_the_boards_file_layer_stays_under_the_need_rule() {
  FlashFiles fs;
  ABStorage store(fs);
  ABStorage script(fs, "/script-a.bin", "/script-b.bin", SCRIPT_STORE_CAPACITY);
  g_store = &store;
  g_script_store = &script;
  put_backup_of_the_largest_script_stays_under_the_need_rule();
  g_store = g_script_store = nullptr;
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(put_backup_of_the_largest_script_stays_under_the_need_rule);
  RUN_TEST(ab_storage_reads_and_writes_without_copies);
  RUN_TEST(put_backup_on_the_boards_file_layer_stays_under_the_need_rule);
  return UNITY_END();
}
