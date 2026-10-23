/* The Pico bootloader of the Spectre Proptroller C++ firmware (PICO-BOOTLOADER.md): flashed once over USB in
 * front of the firmware; over-the-air updates never replace it.
 *
 * At every power-on:
 *   1. clk_ref (and so clk_sys) from the 12 MHz crystal, so the watchdog's tick is exact; then the watchdog,
 *      8 s, fed before every flash step. The firmware takes it over (arduino/ota.cpp) and feeds it from its loop:
 *      an image that hangs before its self-check restarts and is swapped back.
 *   2. the boot decision (lib/picoboot): swap a marked update in, or swap back an image that was swapped in at
 *      the last power-on and never marked itself good. A power cut resumes where it stopped.
 *   3. the active slot must hold a firmware linked for it (vector table); with none (a board that has only the
 *      bootloader) the board goes to USB boot mode (BOOTSEL) for the first firmware.
 *   4. jump to the firmware: vector table, stack pointer, reset handler. Nothing else is touched: the firmware
 *      starts much as after a plain power-on (its own clock set-up handles clk_ref already on the crystal).
 *
 * Bare metal: this file's start-up code, src/picoboot/boot_<chip>.ld, and the pico-sdk's flash, watchdog, XOSC
 * and boot ROM functions from the arduino-pico package (tools/picoboot_boot.py). */
#include <stdint.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/structs/clocks.h"
#include "hardware/watchdog.h"
#include "hardware/xosc.h"
#include "pico/bootrom.h"
#if PICO_RP2350
#include "boot/picobin.h"
#include "hardware/ticks.h"
#endif

#include "picoboot.h"
#include "picoboot_map.h"

#define WATCHDOG_MS 8000u
#define XTAL_MHZ 12u
#if PICO_RP2350
#define RAM_END 0x20082000u
#else
#define RAM_END 0x20042000u
#endif

extern uint32_t __stack_top, __data_start, __data_end, __data_load, __bss_start, __bss_end;
void picoboot_reset(void);
static void fault(void);

/* ---------------------------------------------------------------- start-up */

__attribute__((section(".vectors"), used)) static const void* const vectors[16] = {
    &__stack_top, (void*)picoboot_reset, (void*)fault, (void*)fault, (void*)fault, (void*)fault, (void*)fault, 0, 0, 0, 0,
    (void*)fault, (void*)fault, 0, (void*)fault, (void*)fault,
};

#if PICO_RP2350
/* the image definition the RP2350 boot ROM looks for in the first 4 KB: an Arm secure executable for the
 * RP2350, entered through the vector table at the start of flash (the pico-sdk's embedded_start_block) */
__attribute__((section(".embedded_block"), used)) static const uint32_t image_def[5] = {
    PICOBIN_BLOCK_MARKER_START,
    PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE | (1u << 8) |
        ((uint32_t)(PICOBIN_IMAGE_TYPE_IMAGE_TYPE_AS_BITS(EXE) | PICOBIN_IMAGE_TYPE_EXE_SECURITY_AS_BITS(S) |
                    PICOBIN_IMAGE_TYPE_EXE_CPU_AS_BITS(ARM) | PICOBIN_IMAGE_TYPE_EXE_CHIP_AS_BITS(RP2350))
         << 16),
    PICOBIN_BLOCK_ITEM_2BS_LAST | (1u << 8), /* one word of items */
    0,                                       /* a single block: links to itself */
    PICOBIN_BLOCK_MARKER_END,
};
#endif

/* a fault restarts the board (the watchdog, as fast as it can) */
static void fault(void) {
  watchdog_reboot(0, 0, 1);
  for (;;) {}
}

/* the pico-sdk's hard_assert */
void hard_assertion_failure(void) { fault(); }

static void boot_main(void);

__attribute__((naked, noreturn)) void picoboot_reset(void) {
  __asm volatile(
      "ldr r0, =__stack_top\n"
      "mov sp, r0\n"
      "bl picoboot_c_start\n");
}

__attribute__((used, noreturn)) void picoboot_c_start(void) {
  uint32_t* src = &__data_load;
  for (uint32_t* dst = &__data_start; dst < &__data_end;) *dst++ = *src++;
  for (uint32_t* p = &__bss_start; p < &__bss_end;) *p++ = 0;
  boot_main();
  for (;;) {}
}

/* ---------------------------------------------------------------- flash for lib/picoboot */

/* nothing runs on core 1 and no interrupt is enabled here: the flash routines (in RAM) can switch XIP off */
static int fRead(void* c, uint32_t off, uint8_t* buf, uint32_t n) {
  (void)c;
  memcpy(buf, (const void*)(PICOBOOT_XIP_BASE + off), n);
  return 0;
}
static int fErase(void* c, uint32_t off, uint32_t n) {
  (void)c;
  watchdog_update();
  flash_range_erase(off, n);
  return 0;
}
static int fProgram(void* c, uint32_t off, const uint8_t* buf, uint32_t n) {
  (void)c;
  static uint8_t page[FLASH_PAGE_SIZE];
  watchdog_update();
  while (n > 0) {
    uint32_t base = off & ~(uint32_t)(FLASH_PAGE_SIZE - 1), at = off - base;
    uint32_t k = FLASH_PAGE_SIZE - at < n ? FLASH_PAGE_SIZE - at : n;
    const uint8_t* src = buf;
    if (at != 0 || k != FLASH_PAGE_SIZE) {
      memset(page, 0xFF, sizeof page); /* 0xFF leaves a NOR byte as it is */
      memcpy(page + at, buf, k);
      src = page;
    }
    flash_range_program(base, src, FLASH_PAGE_SIZE);
    off += k;
    buf += k;
    n -= k;
  }
  return 0;
}

static uint8_t g_page[PICOBOOT_SECTOR];

/* ---------------------------------------------------------------- boot */

static void clk_ref_from_xosc(void) {
  xosc_init();
  clock_hw_t* ref = &clocks_hw->clk[clk_ref];
  ref->div = 1u << CLOCKS_CLK_REF_DIV_INT_LSB;
  ref->ctrl = (ref->ctrl & ~CLOCKS_CLK_REF_CTRL_SRC_BITS) | (CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC << CLOCKS_CLK_REF_CTRL_SRC_LSB);
  while (!(ref->selected & (1u << CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC))) {}
}

__attribute__((noreturn)) static void jump(uint32_t vt) {
  uint32_t sp = ((const uint32_t*)vt)[0], pc = ((const uint32_t*)vt)[1];
  *(volatile uint32_t*)0xE000ED08u = vt; /* VTOR */
  __asm volatile(
#if PICO_RP2350
      "msr msplim, %2\n"
#endif
      "msr msp, %0\n"
      "dsb\n"
      "isb\n"
      "bx %1\n" ::"r"(sp),
      "r"(pc), "r"(0u));
  for (;;) {}
}

static void boot_main(void) {
  clk_ref_from_xosc();
#if PICO_RP2350
  tick_start(TICK_WATCHDOG, XTAL_MHZ);
#else
  watchdog_start_tick(XTAL_MHZ);
#endif
  watchdog_enable(WATCHDOG_MS, true);

  const picoboot_flash io = {0, fRead, fErase, fProgram};
  const picoboot_layout l = picoboot_layout_for(PICOBOOT_ACTIVE_LEN);
  /* a flash error: restart and try again (a swap resumes where it stopped) */
  if (picoboot_prepare(&io, &l, g_page) == PICOBOOT_ERROR) fault();

  const uint32_t active = PICOBOOT_XIP_BASE + PICOBOOT_ACTIVE_OFFSET;
  if (!picoboot_vectors_ok((const uint8_t*)active, active, PICOBOOT_ACTIVE_LEN, 0x20000000u, RAM_END)) {
    /* no firmware yet: USB boot mode, so the first one can be written (BOOTSEL drive or picotool) */
    watchdog_disable();
    rom_reset_usb_boot_extra(-1, 0, false);
  }
  jump(active);
}
