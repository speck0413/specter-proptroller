// The boards' UpdateTarget (update.h, PROTOCOL.md "Firmware updates", FLASH-MAP.md).
//
// ESP32: the Update library writes the app slot the board does not run from (app0 / app1 of the shared
//   partitions.csv); the written image is read back and its SHA-256 compared before it is made the one that
//   boots next. The Arduino core's bootloader has app rollback: a new image boots PENDING_VERIFY, marks itself
//   valid after the controller's self-check (verifyRollbackLater() returns true, so the core does not do it at
//   boot), and a reset before that boots the previous image again (rolled_back).
// Pico W / Pico 2 W behind the Pico bootloader (envs picow, pico2w: PT_PICOBOOT, set by tools/picoboot_ld.py;
//   PICO-BOOTLOADER.md): the image streams into the dfu slot, is read back and checked, and is marked for the
//   bootloader, which swaps it in at the restart; an image that does not pass its self-check (or hangs before
//   it: the watchdog) is swapped back (rolled_back). Images must be linked for the active slot (0x10007000).
//   A Pico W build without the bootloader's linker script (the Arduino IDE, `pio ci`) has no update target.
// The plain Pico has no update target (updated over USB).
#pragma once
#if defined(ARDUINO) && (defined(ESP32) || (defined(ARDUINO_ARCH_RP2040) && defined(PICO_CYW43_SUPPORTED) && defined(PT_PICOBOOT)))
#include "../update.h"
#include "../password.h"
#define PT_HAS_OTA 1

namespace pt {

class BoardOta : public UpdateTarget {
 public:
  BoardOta();
  std::string slot() const override;
  uint32_t maxBytes() const override;
  BootState state() const override;
  bool rolledBack() const override;
#if defined(PT_PICOBOOT)
  uint32_t installMaxS() const override;
#endif
  size_t headerLen() const override;
  bool headerOk(const uint8_t* first, size_t n) const override;
  UpdateError begin(uint32_t len) override;
  UpdateError write(const uint8_t* data, size_t n) override;
  UpdateError finish(const uint8_t sha256[32]) override;
  void abort() override;
  UpdateError markValid() override;
  bool markInvalid() override;

 private:
  uint32_t total_ = 0, written_ = 0;
  bool valid_marked_ = false;
#if defined(ESP32)
  mutable bool state_read_ = false, pending_ = false, rolled_read_ = false, rolled_back_ = false;
#else
  MarkerScan scan_;
  mutable bool checked_ = false, pending_ = false;
  mutable bool reverted_ = false;
  uint32_t fill_ = 0;  // bytes of the sector being received
  void readBootState() const;
  UpdateError flushSector();
#endif
};

}  // namespace pt
#endif
