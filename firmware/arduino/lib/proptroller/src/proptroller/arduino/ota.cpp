#include "ota.h"
#if defined(PT_HAS_OTA)
#include "../flash_map.h"
#include "hal.h"
#include <string.h>
#include <memory>

#if defined(ESP32)
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// The Arduino core marks a PENDING_VERIFY image valid at boot unless this says the firmware will do it: the
// controller marks it after its self-check (Controller::updateTick).
extern "C" bool verifyRollbackLater() { return true; }
#else
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <picoboot.h>
#include "../picoboot_map.h"

// The markers every C++ Pico image carries (pico_image in update.h): which board, which implementation.
#if defined(PICO_RP2350)
extern "C" const char PROPTROLLER_IMAGE_ID[] __attribute__((used)) = "PROPTROLLER-MCU:pico2-w\0PROPTROLLER-IMPL:cpp";
#else
extern "C" const char PROPTROLLER_IMAGE_ID[] __attribute__((used)) = "PROPTROLLER-MCU:pico-w\0PROPTROLLER-IMPL:cpp";
#endif
#endif

namespace pt {

#if defined(ESP32)
// ---------------------------------------------------------------- ESP32: the other app slot

BoardOta::BoardOta() {}

std::string BoardOta::slot() const {
  const esp_partition_t* p = esp_ota_get_running_partition();
  return p ? p->label : "app0";
}

uint32_t BoardOta::maxBytes() const {
  const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
  return p ? p->size : 0;  // ESP32_APP_SLOT_BYTES with the shared table (test_flash_map)
}

// Read once (the step asks every 10 ms, and reading otadata stalls the flash cache); markValid updates it.
BootState BoardOta::state() const {
  if (valid_marked_) return BootState::Valid;
  if (state_read_) return pending_ ? BootState::Pending : BootState::Valid;
  state_read_ = true;
  esp_ota_img_states_t st;
  // ESP_ERR_NOT_FOUND / NOT_SUPPORTED: a fresh USB flash (no otadata entry): valid
  // NEW as well as PENDING_VERIFY: a bootloader without app rollback (espflash's, for one) leaves a new image
  // NEW; it is pending until the self-check all the same (rollback then depends on the bootloader: the Arduino
  // core's has it)
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
      (st == ESP_OTA_IMG_PENDING_VERIFY || st == ESP_OTA_IMG_NEW))
    pending_ = true;
  return pending_ ? BootState::Pending : BootState::Valid;
}

bool BoardOta::rolledBack() const {
  if (!rolled_read_) {
    rolled_read_ = true;
    rolled_back_ = esp_ota_get_last_invalid_partition() != nullptr;
  }
  return rolled_back_;
}

size_t BoardOta::headerLen() const { return esp_image::HEADER_LEN; }
bool BoardOta::headerOk(const uint8_t* first, size_t n) const { return esp_image::isEspApp(first, n, PT_ESP_CHIP_ID); }

UpdateError BoardOta::begin(uint32_t len) {
  total_ = len;
  written_ = 0;
  if (!Update.begin(len, U_FLASH)) return UpdateError::internal(std::string("cannot start writing the update: ") + Update.errorString());
  return UpdateError();
}

UpdateError BoardOta::write(const uint8_t* data, size_t n) {
  // the Update library erases and writes 4 KB sectors as they fill
  if (Update.write((uint8_t*)data, n) != n) return UpdateError::internal(std::string("writing the update failed: ") + Update.errorString());
  written_ += (uint32_t)n;
  return UpdateError();
}

UpdateError BoardOta::finish(const uint8_t sha256[32]) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  // end(): the held first bytes are written, the image is verified and selected to boot next
  if (!next || !Update.end(true)) return UpdateError::internal(std::string("finishing the update failed: ") + Update.errorString());
  // read it back: the bytes in flash must have the SHA-256 the body had. 4 KB reads (each one stalls the flash
  // cache) from the heap, and a yield after each so the step and the network keep running
  Sha256 sha;
  static const uint32_t CHUNK = 4096;
  std::unique_ptr<uint8_t[]> buf(new uint8_t[CHUNK]);
  for (uint32_t off = 0; off < total_; off += CHUNK) {
    uint32_t n = total_ - off < CHUNK ? total_ - off : CHUNK;
    if (esp_partition_read(next, off, buf.get(), n) != ESP_OK) break;
    sha.update(buf.get(), n);
    delay(1);
  }
  uint8_t got[32];
  sha.finish(got);
  if (memcmp(got, sha256, 32) != 0) {
    if (running) esp_ota_set_boot_partition(running);  // keep booting the running image
    return UpdateError::internal("the written image does not read back right");
  }
  return UpdateError();
}

void BoardOta::abort() { Update.abort(); }

UpdateError BoardOta::markValid() {
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return UpdateError::internal("marking the new firmware valid failed");
  valid_marked_ = true;
  return UpdateError();
}

bool BoardOta::markInvalid() {
  // boots the previous image (restarts right away)
  // returns only when there is no previous image to go back to
  return esp_ota_mark_app_invalid_rollback_and_reboot() == ESP_OK;
}

#else
// ---------------------------------------------------------------- Pico W / Pico 2 W behind the Pico bootloader
// (PICO-BOOTLOADER.md): the image streams into the dfu slot one 4 KB sector at a time, is read back and checked,
// then marked for the bootloader, which swaps it in at the restart and swaps it back if it never marks itself
// good (lib/picoboot).

extern "C" uint8_t __flash_binary_start, __flash_binary_end;

namespace {
#if defined(PICO_RP2350)
const uint32_t RAM_END = pico_image::RP2350_RAM_END;
#else
const uint32_t RAM_END = pico_image::RP2040_RAM_END;
#endif
// The bootloader starts the watchdog (8 s) before it swaps and leaves it running: the firmware takes it over
// at once (before setup()) and the step feeds it (Proptroller.cpp feedWatchdog), so a new image that hangs before
// its self-check restarts and is swapped back.
const uint32_t WATCHDOG_MS = 8000;
__attribute__((constructor)) void takeOverWatchdog() { watchdog_enable(WATCHDOG_MS, true); }

// lib/picoboot's flash interface over the pico-sdk: the other core idled and interrupts off while the flash is
// busy (as arduino-pico's LittleFS does); a program is padded to the 256-byte flash page with 0xFF
int fRead(void*, uint32_t off, uint8_t* buf, uint32_t n) {
  memcpy(buf, (const void*)(PICOBOOT_XIP_BASE + off), n);
  return 0;
}
int fErase(void*, uint32_t off, uint32_t n) {
  noInterrupts();
  rp2040.idleOtherCore();
  flash_range_erase(off, n);
  rp2040.resumeOtherCore();
  interrupts();
  return 0;
}
int fProgram(void*, uint32_t off, const uint8_t* buf, uint32_t n) {
  static uint8_t page[FLASH_PAGE_SIZE];
  while (n > 0) {
    uint32_t base = off & ~(uint32_t)(FLASH_PAGE_SIZE - 1), at = off - base;
    uint32_t k = FLASH_PAGE_SIZE - at < n ? FLASH_PAGE_SIZE - at : n;
    const uint8_t* src = buf;
    if (at != 0 || k != FLASH_PAGE_SIZE) {
      memset(page, 0xFF, sizeof page);  // 0xFF leaves a NOR byte as it is
      memcpy(page + at, buf, k);
      src = page;
    }
    noInterrupts();
    rp2040.idleOtherCore();
    flash_range_program(base, src, FLASH_PAGE_SIZE);
    rp2040.resumeOtherCore();
    interrupts();
    off += k;
    buf += k;
    n -= k;
  }
  return 0;
}
const picoboot_flash FLASH_IO = {nullptr, fRead, fErase, fProgram};
const picoboot_layout LAYOUT = picoboot_layout_for(PICOBOOT_ACTIVE_LEN);
uint8_t g_sector[PICOBOOT_SECTOR];  // the sector being received
}  // namespace


BoardOta::BoardOta() : scan_(pico_image::mcuMarker(mcuName()), pico_image::implMarker()) {
  (void)*(volatile const char*)PROPTROLLER_IMAGE_ID;  // keep the markers in the image
}

std::string BoardOta::slot() const { return "active"; }
uint32_t BoardOta::maxBytes() const { return PICOBOOT_ACTIVE_LEN; }

void BoardOta::readBootState() const {
  if (checked_) return;
  checked_ = true;
  picoboot_state s = PICOBOOT_BOOT;
  picoboot_read_state(&FLASH_IO, &LAYOUT, &s);
  pending_ = s == PICOBOOT_SWAP;      // swapped in, not yet marked good
  reverted_ = s == PICOBOOT_REVERT;   // the bootloader swapped the previous image back
}

BootState BoardOta::state() const {
  readBootState();
  return pending_ && !valid_marked_ ? BootState::Pending : BootState::Valid;
}
bool BoardOta::rolledBack() const { readBootState(); return reverted_; }
// The swap copies two 4 KB pages per image page before the new image starts: about 16 s for a Pico W image
// with typical flash timings, about 2.5 min with the datasheet's worst case (PICO-BOOTLOADER.md "Time").
static const uint32_t INSTALL_MAX_S = 180;
uint32_t BoardOta::installMaxS() const { return INSTALL_MAX_S; }

size_t BoardOta::headerLen() const { return pico_image::HEADER_LEN; }
bool BoardOta::headerOk(const uint8_t* first, size_t n) const {
  // linked for the active slot (a plain arduino-pico image, linked for the start of flash, is refused)
  // (the same rule as the bootloader's picoboot_vectors_ok: test_fleet image_checks)
  return pico_image::vectorsOk(first, n, PICOBOOT_ACTIVE_LEN, RAM_END);
}

UpdateError BoardOta::begin(uint32_t len) {
  readBootState();
  if (len > PICOBOOT_ACTIVE_LEN) return UpdateError::invalid(updateTooBigText(len, PICOBOOT_ACTIVE_LEN));
  if (pending_ && !valid_marked_) return UpdateError::invalid("the running firmware has not passed its self-check yet: try again in a minute");
  // the flash itself, not the state read at boot: an update marked since then is waiting for the restart, and
  // writing dfu now would change what the bootloader swaps in (Controller also refuses while it restarts)
  if (!picoboot_may_write_dfu(&FLASH_IO, &LAYOUT)) return UpdateError::invalid("an update is already waiting for the restart");
  // a "reverted" mark ends with this update (the bootloader then reads plain boot)
  if (reverted_) {
    if (picoboot_mark_booted(&FLASH_IO, &LAYOUT) != 0) return UpdateError::internal("clearing the rollback mark failed");
    reverted_ = false;
  }
  total_ = len;
  written_ = 0;
  fill_ = 0;
  scan_.reset();
  return UpdateError();
}

UpdateError BoardOta::flushSector() {
  if (fill_ == 0) return UpdateError();
  if (fill_ < PICOBOOT_SECTOR) memset(g_sector + fill_, 0xFF, PICOBOOT_SECTOR - fill_);
  uint32_t off = LAYOUT.dfu + (written_ - fill_);
  watchdog_update();
  if (fErase(nullptr, off, PICOBOOT_SECTOR) || fProgram(nullptr, off, g_sector, PICOBOOT_SECTOR))
    return UpdateError::internal("writing the update failed");
  fill_ = 0;
  return UpdateError();
}

UpdateError BoardOta::write(const uint8_t* data, size_t n) {
  if (written_ + n > total_) return UpdateError::invalid("the update is longer than it said");
  scan_.add(data, n);
  while (n > 0) {
    size_t k = PICOBOOT_SECTOR - fill_ < n ? PICOBOOT_SECTOR - fill_ : n;
    memcpy(g_sector + fill_, data, k);
    fill_ += (uint32_t)k;
    written_ += (uint32_t)k;
    data += k;
    n -= k;
    if (fill_ == PICOBOOT_SECTOR) {
      UpdateError e = flushSector();
      if (!e.ok()) return e;
    }
  }
  return UpdateError();
}

UpdateError BoardOta::finish(const uint8_t sha256[32]) {
  UpdateError e = flushSector();
  if (!e.ok()) return e;
  if (!scan_.found()) return UpdateError::invalid(updateWrongBoardText(mcuName()));
  // read the dfu slot back: what the bootloader swaps in is what was sent
  Sha256 sha;
  sha.update((const uint8_t*)(PICOBOOT_XIP_BASE + LAYOUT.dfu), total_);
  uint8_t got[32];
  sha.finish(got);
  if (memcmp(got, sha256, 32) != 0) return UpdateError::internal("the update does not read back as the image that was sent");
  uint32_t running = (uint32_t)(&__flash_binary_end - &__flash_binary_start);
  if (picoboot_mark_updated(&FLASH_IO, &LAYOUT, total_, running) != 0) return UpdateError::internal("marking the update failed");
  return UpdateError();
}

void BoardOta::abort() { fill_ = 0; written_ = 0; }  // a half-written dfu slot is never marked: harmless

UpdateError BoardOta::markValid() {
  if (picoboot_mark_booted(&FLASH_IO, &LAYOUT) != 0) return UpdateError::internal("marking the new firmware valid failed");
  valid_marked_ = true;
  return UpdateError();
}

bool BoardOta::markInvalid() {
  // not marked good: the restart makes the bootloader swap the previous image back
  return true;
}

#endif

}  // namespace pt
#endif
