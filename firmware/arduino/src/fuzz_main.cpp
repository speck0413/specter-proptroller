// libFuzzer entry for every fuzz target (test/fuzz_harnesses.h). The environment variable PT_FUZZ_TARGET picks
// one: request, script, records, script_record, http, backup, image, http_head, ws_frame, mqtt, profile, settings,
// lz, picoboot, console, locked, http_small (tools/fuzz.sh sets it).
#include <stdio.h>
#include <stdlib.h>
#include "../test/fuzz_harnesses.h"

typedef int (*Target)(const uint8_t*, size_t);
static Target g_target = nullptr;

extern "C" int LLVMFuzzerInitialize(int*, char***) {
  static const struct { const char* name; Target fn; } TARGETS[] = {
      {"request", fuzz::request}, {"script", fuzz::script}, {"records", fuzz::records}, {"script_record", fuzz::script_record},
      {"http", fuzz::http},       {"backup", fuzz::backup}, {"image", fuzz::image},     {"http_head", fuzz::http_head},
      {"ws_frame", fuzz::ws_frame}, {"mqtt", fuzz::mqtt},     {"profile", fuzz::profile}, {"settings", fuzz::settings},
      {"lz", fuzz::lz},           {"picoboot", fuzz::picoboot}, {"console", fuzz::console},
      {"locked", fuzz::locked},   {"http_small", fuzz::http_small}};
  const char* want = getenv("PT_FUZZ_TARGET");
  for (auto& t : TARGETS)
    if (want && strcmp(want, t.name) == 0) g_target = t.fn;
  if (!g_target) {
    fprintf(stderr, "set PT_FUZZ_TARGET to one of:");
    for (auto& t : TARGETS) fprintf(stderr, " %s", t.name);
    fprintf(stderr, "\n");
    exit(2);
  }
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) { return g_target(data, size); }
