// Over-the-air firmware updates (PROTOCOL.md "Firmware updates").
//
//  POST /api/v1/update --> Controller::updateBegin (checks, update mode) --> updateWrite (chunks, SHA-256)
//        --> updateFinish (SHA-256 compared, target finish) --> restart in 1 s --> new image, pending
//        --> 30 s of normal running, records read --> UpdateTarget::markValid --> valid
//  (a reset or crash while pending boots the previous image: rolled_back)
//
// The Controller does what is the same on every board; writing the image is the board's UpdateTarget: the
// ESP32 the other app slot (Update library), the Pico W / Pico 2 W the dfu slot of the Pico bootloader, which
// swaps it in at the next boot (PICO-BOOTLOADER.md; no LittleFS copy any more), the simulator a slot in memory.
#pragma once
#include "types.h"
#include <string>

namespace pt {

// A new image marks itself valid after this long of normal running, ms.
static const Millis UPDATE_SELF_CHECK_MS = 30000;
// The board restarts this long after a successful update, ms (the answer gets out first).
static const Millis UPDATE_RESTART_DELAY_MS = 1000;
// An `update` progress event every this many bytes.
static const uint32_t UPDATE_PROGRESS_STEP = 128 * 1024;
// Hex digits of a SHA-256.
static const size_t SHA256_HEX_LEN = 64;

enum class BootState : uint8_t { Valid, Pending };

// Why a board could not take an image: invalid (answered `invalid`) or internal (answered `internal`).
struct [[nodiscard]] UpdateError {
  enum class Kind : uint8_t { None, Invalid, Internal };
  Kind kind = Kind::None;
  std::string message;
  bool ok() const { return kind == Kind::None; }
  static UpdateError invalid(const std::string& m) { UpdateError e; e.kind = Kind::Invalid; e.message = m; return e; }
  static UpdateError internal(const std::string& m) { UpdateError e; e.kind = Kind::Internal; e.message = m; return e; }
};

// Where a board writes a new image: never over the running one.
class UpdateTarget {
 public:
  virtual ~UpdateTarget() {}
  virtual std::string slot() const = 0;     // the running slot: app0 / app1 (ESP32, sim), active (Pico)
  virtual uint32_t maxBytes() const = 0;    // the largest image it takes
  virtual BootState state() const = 0;
  virtual bool rolledBack() const = 0;      // the last update failed its self-check and went back
  // The longest the bootloader may take to install an update after the restart, before the new image starts,
  // in seconds (info.update.install_max_s); 0: the new image starts at once (ESP32).
  virtual uint32_t installMaxS() const { return 0; }
  virtual size_t headerLen() const = 0;     // bytes headerOk needs
  virtual bool headerOk(const uint8_t* first, size_t n) const = 0;  // the start of an image for this board?
  virtual UpdateError begin(uint32_t len) = 0;
  virtual UpdateError write(const uint8_t* data, size_t n) = 0;
  // All bytes written and the body's SHA-256 matched: check what was written, make it boot next (pending).
  virtual UpdateError finish(const uint8_t sha256[32]) = 0;
  virtual void abort() = 0;
  // The running image passed its self-check: keep booting it.
  virtual UpdateError markValid() = 0;
  // The running image failed its self-check: go back to the previous one. True when a restart now boots the
  // previous image (the controller restarts), false when there is none to go back to (it keeps running).
  virtual bool markInvalid() { return true; }
};

// The headers of a POST /api/v1/update.
struct UpdateRequest {
  bool has_len = false;
  size_t len = 0;
  bool octet_stream = false;
  bool has_sha256 = false;
  std::string sha256;
  bool has_pin = false;
  std::string pin;
};

// Message texts.
namespace update_text {
extern const char* const UNSUPPORTED;
extern const char* const CONTENT_TYPE;
extern const char* const NO_LENGTH;
extern const char* const BAD_SHA_HEADER;
extern const char* const SHOW_RUNNING;
extern const char* const EFFECT_RUNNING;
extern const char* const SCRIPT_RUNNING;
extern const char* const MANUAL_TEST;
extern const char* const ANOTHER_UPDATE;
extern const char* const SELF_CHECK_PENDING;
extern const char* const IN_UPDATE_MODE;
extern const char* const EMPTY;
extern const char* const SHA_MISMATCH;
extern const char* const CONNECTION_LOST;
}  // namespace update_text

// "the image is too big: 2000000 bytes, this board takes at most 1966080"
std::string updateTooBigText(size_t len, uint32_t max);
// "not an image for esp32-devkit"
std::string updateWrongBoardText(const std::string& mcu);
// 64 hex digits (either case, spaces around allowed).
[[nodiscard]] bool parseSha256(const std::string& hex, uint8_t out[32]);
// The answer of a board without updates.
std::string updateUnsupportedJson();

// ESP-IDF app images: magic 0xE9, chip id 0 (ESP32) at byte 12, the app description's magic at byte 32.
namespace esp_image {
static const uint8_t MAGIC = 0xE9;
static const size_t CHIP_ID_OFFSET = 12;
static const uint16_t CHIP_ESP32 = 0x0000;
static const size_t APP_DESC_OFFSET = 32;
static const uint32_t APP_DESC_MAGIC = 0xABCD5432;
static const size_t HEADER_LEN = APP_DESC_OFFSET + 4;
[[nodiscard]] bool isEsp32App(const uint8_t* first, size_t n);
}  // namespace esp_image

// Raspberry Pi Pico W / Pico 2 W images of this firmware, behind the Pico bootloader (picoboot_map.h,
// PICO-BOOTLOADER.md): raw (not gzipped), linked for the active slot. The start is the vector table: a stack
// pointer in RAM (word-aligned) and a Thumb reset handler inside the active slot, so a plain arduino-pico image
// (linked for the start of flash) is refused before anything is written. Somewhere in the image: the markers
// "PROPTROLLER-MCU:<mcu>" NUL and "PROPTROLLER-IMPL:cpp" NUL (checked as the image streams in), which tell a
// Pico W image from a Pico 2 W one. The bootloader checks the same vector table (picoboot_vectors_ok).
namespace pico_image {
static const char* const ID_PREFIX = "PROPTROLLER-MCU:";
static const char* const IMPL_ID = "PROPTROLLER-IMPL:cpp";
static const uint32_t SLOT_BASE = 0x10007000;  // the active slot (PICOBOOT_ACTIVE_ADDRESS, flash_map.h)
static const uint32_t RAM_BASE = 0x20000000, RP2040_RAM_END = 0x20042000, RP2350_RAM_END = 0x20082000;
static const size_t HEADER_LEN = 512;
// slot_len: PICOW_SLOT_BYTES / PICO2W_SLOT_BYTES; ram_end: RP2040_RAM_END / RP2350_RAM_END
[[nodiscard]] bool vectorsOk(const uint8_t* first, size_t n, uint32_t slot_len, uint32_t ram_end);
std::string mcuMarker(const std::string& mcu);  // "PROPTROLLER-MCU:pico-w" NUL
std::string implMarker();                       // "PROPTROLLER-IMPL:cpp" NUL
}  // namespace pico_image

// Finds two byte strings anywhere in a stream fed in pieces (a marker may straddle two pieces).
class MarkerScan {
 public:
  MarkerScan(const std::string& a, const std::string& b) { want_[0] = a; want_[1] = b; }
  void add(const uint8_t* data, size_t n);
  bool found() const { return found_[0] && found_[1]; }
  void reset() { found_[0] = found_[1] = false; tail_.clear(); }

 private:
  std::string want_[2], tail_;
  bool found_[2] = {false, false};
};

}  // namespace pt
