// The fuzz targets (test/fuzz_harnesses.h) on their seeds and on every input that once failed
// (test/fuzz-regressions/<target>/), in the ordinary host tests: what libFuzzer found stays fixed.
#include <unity.h>
#include <dirent.h>
#include <fstream>
#include <regex>
#include <sstream>
#include "../fuzz_harnesses.h"

void setUp() {}
void tearDown() {}

static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::vector<std::string> files(const std::string& dir, const char* suffix = "") {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n == "." || n == "..") continue;
    if (*suffix && (n.size() < strlen(suffix) || n.compare(n.size() - strlen(suffix), strlen(suffix), suffix) != 0)) continue;
    out.push_back(dir + "/" + n);
  }
  closedir(d);
  return out;
}

static size_t run(int (*target)(const uint8_t*, size_t), const std::vector<std::string>& inputs) {
  for (auto& s : inputs) target((const uint8_t*)s.data(), s.size());
  return inputs.size();
}

static std::vector<std::string> contents(const std::vector<std::string>& paths) {
  std::vector<std::string> v;
  for (auto& p : paths) v.push_back(readFile(p));
  return v;
}

static std::vector<std::string> regressions(const char* target) { return contents(files(std::string("test/fuzz-regressions/") + target)); }

void requests() {
  // the requests of the golden session, and the answers (JSON too)
  std::string session = readFile("test/test_golden/session.inc") + readFile("test/test_golden/test_main.cpp");
  std::vector<std::string> in;
  std::regex r("R\"__\\((.*?)\\)__\"");
  for (auto it = std::sregex_iterator(session.begin(), session.end(), r); it != std::sregex_iterator(); ++it) in.push_back((*it)[1]);
  TEST_ASSERT_TRUE(in.size() > 50);
  for (auto& s : contents(files("test/golden", ".json"))) in.push_back(s);
  for (auto& s : regressions("request")) in.push_back(s);
  run(fuzz::request, in);
}

void scripts() {
  std::vector<std::string> in = contents(files("../show-script-examples", ".txt"));
  for (auto& c : files("../show-script-golden/cases")) in.push_back(readFile(c + "/script.txt"));
  for (auto& s : regressions("script")) in.push_back(s);
  TEST_ASSERT_TRUE(in.size() > 50);
  run(fuzz::script, in);
}

void records() {
  std::vector<std::string> in = {R"({"version":1,"settings":{"name":"Door"},"shows":[]})", "{}", "null"};
  for (auto& s : regressions("records")) in.push_back(s);
  in.push_back("");
  in.push_back(std::string("PTC1\x01\0\0\0\xff\xff\xff\x7f", 12));
  run(fuzz::records, in);
}

void script_records() {
  // the fixture, every third byte changed in turn (the table checks, the header),
  // and every length cut
  std::string v2 = readFile("test/fixtures/records/script-record.bin");
  TEST_ASSERT_TRUE(v2.size() > 100);
  std::vector<std::string> in = {v2, "", "PTSR", "{}"};
  for (size_t i = 0; i < v2.size(); i += 3) {
    std::string x = v2;
    x[i] = (char)(x[i] ^ 0x5A);
    in.push_back(x);
  }
  for (size_t k = 0; k < v2.size(); k += 13) in.push_back(v2.substr(0, k));
  for (auto& s : regressions("script_record")) in.push_back(s);
  run(fuzz::script_record, in);
}

void http() {
  std::vector<std::string> in = contents(files("test/fuzz-seeds/http"));
  for (auto& s : regressions("http")) in.push_back(s);
  run(fuzz::http, in);
}

void backups() {
  JsonDocument d;
  deserializeJson(d, readFile("test/golden/94-fl-get-backup.json"));
  std::string b;
  serializeJson(d["result"]["data"], b);
  std::vector<std::string> in = {b, "{}", "null", "[]", R"({"format":"proptroller-backup","format_version":1})"};
  for (auto& s : regressions("backup")) in.push_back(s);
  run(fuzz::backup, in);
}

void images() {
  std::vector<std::string> in = {pt::sim_image::build("0.2.0", "x", true, 3000), pt::sim_image::build("0.2.0", "x", false, 10), std::string(40, '\xe9')};
  for (auto& s : regressions("image")) in.push_back(s);
  run(fuzz::image, in);
}

void http_heads() {
  std::vector<std::string> in = contents(files("test/fuzz-seeds/http"));
  in.push_back("GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 99999999999999999999\r\n\r\n");
  for (auto& s : regressions("http_head")) in.push_back(s);
  run(fuzz::http_head, in);
}

void ws_frames() {
  std::vector<std::string> in = {"", std::string("\x00\x81\x05hello", 8), std::string("\x02\x81\x85\x01\x02\x03\x04iiiii", 12),
                                 std::string("\x01\x82\x7f\xff\xff\xff\xff\xff\xff\xff\xff", 11)};
  for (auto& s : regressions("ws_frame")) in.push_back(s);
  run(fuzz::ws_frame, in);
}

void mqtt_messages() {
  std::vector<std::string> in = {"rpc\n{\"op\":\"info\"}", "config/set/name\nDoor", "control/trigger", "status/get/run_count", "cue\n door ",
                                 "/proptroller/broadcast/getName", "script/check\n{\"text\":\"\",\"sounds\":{}}", "config/get/", ""};
  for (auto& s : regressions("mqtt")) in.push_back(s);
  run(fuzz::mqtt, in);
}

void profiles() {
  std::vector<std::string> in = {R"({"topic_prefix":"myprop","aliases":[{"alias":"rest_s","key":"out2_on_ms","scale":1000},)"
                                 R"({"alias":"skip","key":"out3_enabled","invert":true},{"alias":"go","control":"trigger"},)"
                                 R"({"alias":"presses","status":"run_count"},{"alias":"VALVE","terminal":"OUT1"}]})",
                                 "{}", "null", R"({"aliases":[]})"};
  for (auto& s : regressions("profile")) in.push_back(s);
  run(fuzz::profile, in);
}

void settings_records() {
  std::vector<std::string> in = {R"({"name":"Door","soft_start_ms":[0,10],"roles":["shows","not_used"],"sketch":{"a":"1"}})", "{}", "[]", "1"};
  for (auto& s : regressions("settings")) in.push_back(s);
  run(fuzz::settings, in);
}

void lz_streams() {
  std::vector<std::string> in = {std::string("\x03\x00\x07" "abc", 6), std::string("\x06\x00\x07" "abc\x02\x00", 8), std::string("\x10\x00\x00\xff\xff", 5)};
  for (auto& s : regressions("lz")) in.push_back(s);
  run(fuzz::lz, in);
}

void picoboot_states() {
  std::vector<std::string> in;
  for (int magic : {0xF0, 0xC0, 0xD0, 0xE0, 0xFF}) {
    std::string st(64, '\xff');
    st[0] = (char)magic;
    in.push_back(st + std::string("\x02\x00\x00\x00\xfd\xff\xff\xff", 8) + std::string(100, '\x00'));
  }
  for (auto& s : regressions("picoboot")) in.push_back(s);
  run(fuzz::picoboot, in);
}

void console_lines() {
  std::vector<std::string> in = {"help\nstatus\nsettings", "name=Front Door\nname", "pixel_count=abc", "=", "pin=1234\npage_lock=true\npin",
                                 R"({"proptroller":"discover","v":1})", ""};
  for (auto& s : regressions("console")) in.push_back(s);
  run(fuzz::console, in);
}

void locked_board() {
  std::vector<std::string> in = {"{\"op\":\"set_params\",\"values\":[{\"key\":\"name\",\"value\":\"x\"}]}\nname=y\ncontrol/trigger",
                                 "config/set/name Door\ncue fog\n{\"op\":\"factory_reset\"}", ""};
  for (auto& s : regressions("locked")) in.push_back(s);
  run(fuzz::locked, in);
}

void http_small() {
  std::vector<std::string> in;
  for (auto& s : contents(files("test/fuzz-seeds/http"))) in.push_back(s + s);
  for (auto& s : regressions("http_small")) in.push_back(s);
  run(fuzz::http_small, in);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(requests);
  RUN_TEST(scripts);
  RUN_TEST(records);
  RUN_TEST(script_records);
  RUN_TEST(http);
  RUN_TEST(backups);
  RUN_TEST(images);
  RUN_TEST(http_heads);
  RUN_TEST(ws_frames);
  RUN_TEST(mqtt_messages);
  RUN_TEST(profiles);
  RUN_TEST(settings_records);
  RUN_TEST(lz_streams);
  RUN_TEST(picoboot_states);
  RUN_TEST(console_lines);
  RUN_TEST(locked_board);
  RUN_TEST(http_small);
  return UNITY_END();
}
