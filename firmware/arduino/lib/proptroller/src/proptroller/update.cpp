#include "update.h"
#include "flash_map.h"
#include <stdio.h>
#include <string.h>

namespace pt {


namespace update_text {
const char* const UNSUPPORTED = "this board takes no over-the-air updates";
const char* const CONTENT_TYPE = "Content-Type must be application/octet-stream";
const char* const NO_LENGTH = "Content-Length is required";
const char* const BAD_SHA_HEADER = "X-Proptroller-Sha256 must be the SHA-256 of the image as 64 hex digits";
const char* const SHOW_RUNNING = "a show is running: stop it first";
const char* const EFFECT_RUNNING = "the pulse-and-recover effect is running: wait for it to finish";
const char* const SCRIPT_RUNNING = "the show script is running an effect: wait for it to finish";
const char* const MANUAL_TEST = "a manual output test is on: release it first";
const char* const ANOTHER_UPDATE = "another update is running";
const char* const SELF_CHECK_PENDING = "the running firmware has not finished its self-check yet: try again in 30 s";
const char* const IN_UPDATE_MODE = "a firmware update is running";
const char* const EMPTY = "the image is empty";
const char* const SHA_MISMATCH = "the image's SHA-256 does not match X-Proptroller-Sha256";
const char* const CONNECTION_LOST = "the connection ended before the whole image arrived";
}  // namespace update_text

std::string updateTooBigText(size_t len, uint32_t max) {
  char b[96];
  (void)snprintf(b, sizeof b, "the image is too big: %lu bytes, this board takes at most %lu", (unsigned long)len, (unsigned long)max);
  return b;
}

std::string updateWrongBoardText(const std::string& mcu) { return "not an image for " + mcu; }

static int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseSha256(const std::string& in, uint8_t out[32]) {
  size_t a = 0, b = in.size();
  while (a < b && (in[a] == ' ' || in[a] == '\t' || in[a] == '\r' || in[a] == '\n')) a++;
  while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\r' || in[b - 1] == '\n')) b--;
  if (b - a != SHA256_HEX_LEN) return false;
  for (size_t k = 0; k < 32; k++) {
    int hi = hexDigit(in[a + 2 * k]), lo = hexDigit(in[a + 2 * k + 1]);
    if (hi < 0 || lo < 0) return false;
    out[k] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

std::string updateUnsupportedJson() {
  return std::string(R"({"v":1,"ok":false,"error":{"code":"unsupported","message":")") + update_text::UNSUPPORTED + "\"}}";
}

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

namespace esp_image {
bool isEsp32App(const uint8_t* first, size_t n) {
  if (n < HEADER_LEN) return false;
  uint16_t chip = (uint16_t)(first[CHIP_ID_OFFSET] | first[CHIP_ID_OFFSET + 1] << 8);
  return first[0] == MAGIC && chip == CHIP_ESP32 && le32(first + APP_DESC_OFFSET) == APP_DESC_MAGIC;
}
}  // namespace esp_image

namespace pico_image {
static_assert(SLOT_BASE == PICOBOOT_ACTIVE_ADDRESS, "the active slot of picoboot_map.h");
bool vectorsOk(const uint8_t* first, size_t n, uint32_t slot_len, uint32_t ram_end) {
  if (n < 8) return false;
  uint32_t sp = le32(first), reset = le32(first + 4);
  if (sp <= RAM_BASE || sp > ram_end || (sp & 3u) != 0) return false;
  if ((reset & 1u) == 0) return false;  // Thumb
  reset &= ~1u;
  return reset >= SLOT_BASE + 8u && reset < SLOT_BASE + slot_len;
}
std::string mcuMarker(const std::string& mcu) {
  std::string m = std::string(ID_PREFIX) + mcu;
  m.push_back('\0');
  return m;
}
std::string implMarker() {
  std::string m = IMPL_ID;
  m.push_back('\0');
  return m;
}
}  // namespace pico_image

void MarkerScan::add(const uint8_t* data, size_t n) {
  if (found()) return;
  size_t keep = want_[0].size() > want_[1].size() ? want_[0].size() : want_[1].size();
  std::string buf = tail_;
  buf.append((const char*)data, n);
  for (int k = 0; k < 2; k++)
    if (!found_[k] && buf.find(want_[k]) != std::string::npos) found_[k] = true;
  tail_ = buf.size() > keep ? buf.substr(buf.size() - keep) : buf;
}

}  // namespace pt
