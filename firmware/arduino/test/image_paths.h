// Where the tests that check built images find them, the same rule as tools/stack_check.py: PLATFORMIO_BUILD_DIR
// (as `pio run` uses it; a relative one is relative to the project, which is the tests' working folder), else
// .pio/build. An image that is not built is skipped, unless PT_REQUIRE_IMAGES is set (not empty, not 0): CI sets it
// after building every image, so a missing one fails there instead of passing unchecked.
#pragma once
#include <unity.h>
#include <cstdlib>
#include <string>

static inline std::string imagePath(const std::string& rel) {
  const char* d = getenv("PLATFORMIO_BUILD_DIR");
  std::string root = d && *d ? d : ".pio/build";
  const char* home = getenv("HOME");
  if (home && root.compare(0, 2, "~/") == 0) root = std::string(home) + root.substr(1);
  return root + "/" + rel;
}

static inline bool imagesRequired() {
  const char* v = getenv("PT_REQUIRE_IMAGES");
  return v && *v && std::string(v) != "0";
}

// An image that is not there: a failure when images are required, else a message and the caller skips it.
static inline void imageMissing(const std::string& path) {
  std::string m = path + ": not built";
  if (imagesRequired()) TEST_FAIL_MESSAGE(m.c_str());
  TEST_MESSAGE((m + ", skipped").c_str());
}
