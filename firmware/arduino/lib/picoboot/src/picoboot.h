/* The boot decision of the Pico bootloader and the firmware's side of it (firmware/arduino/PICO-BOOTLOADER.md).
 *
 * Plain C over a small flash interface, so the same code runs in the bootloader, in the firmware (marking an
 * update, marking itself good) and in the host tests (a simulated NOR flash with power cuts).
 *
 * The state sector (4 KB), one byte per step, as embassy-boot lays it out (its algorithm, MIT OR Apache-2.0,
 * ported to C):
 *
 *   byte 0             magic: 0xF0 swap, 0xC0 reverted, 0xE0 (unused here), anything else: boot
 *   byte 1             progress valid: 0xFF valid, anything else: invalid
 *   bytes 2 .. 2+2N    swap progress: page step i done when byte 2+i is 0x00
 *   bytes 2+2N .. 2+4N revert progress
 *   bytes 4088 .. 4095 the number of pages N to swap (u32 little endian, then its complement), written by the
 *                      firmware when it marks an update, before the "swap" byte; missing or damaged: no swap
 *                      (a half-programmed "boot" or "reverted" byte can read as "swap")
 *
 * Swap (power-cut safe: every page is copied before its source is erased and each step is recorded once its
 * copy is written): for page p from the last down to 0, active[p] -> dfu[p+1], then dfu[p] -> active[p]. After
 * it the new image is in active and the old one in dfu, one page up. Revert: for page p from 0 up,
 * active[p] -> dfu[p], then dfu[p+1] -> active[p].
 */
#ifndef PICOBOOT_H
#define PICOBOOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PICOBOOT_MAGIC_SWAP 0xF0u
#define PICOBOOT_MAGIC_BOOT 0xD0u
#define PICOBOOT_MAGIC_REVERT 0xC0u
#define PICOBOOT_MAGIC_DFU_DETACH 0xE0u
#define PICOBOOT_PAGES_AT 4088u /* offset of the page count in the state sector */

/* Flash access; offsets are from the start of flash. erase() takes whole sectors; program() only clears bits
 * (NOR), any length and alignment (the board adapter pads to its program page with 0xFF). Each returns 0 on
 * success. */
typedef struct picoboot_flash {
  void* ctx;
  int (*read)(void* ctx, uint32_t off, uint8_t* buf, uint32_t n);
  int (*erase)(void* ctx, uint32_t off, uint32_t n);
  int (*program)(void* ctx, uint32_t off, const uint8_t* buf, uint32_t n);
} picoboot_flash;

typedef struct picoboot_layout {
  uint32_t state;      /* offset of the state sector */
  uint32_t active;     /* offset of the active slot */
  uint32_t active_len; /* bytes, a multiple of the page */
  uint32_t dfu;        /* offset of the dfu slot (active_len + one page) */
  uint32_t page;       /* the unit of a swap step: the 4 KB erase sector */
} picoboot_layout;

typedef enum picoboot_state {
  PICOBOOT_BOOT = 0,   /* nothing to do */
  PICOBOOT_SWAP = 1,   /* an update is marked (bootloader), or swapped in and not yet marked good (firmware) */
  PICOBOOT_REVERT = 2, /* the bootloader swapped the previous image back */
  PICOBOOT_DFU_DETACH = 3,
} picoboot_state;

typedef enum picoboot_action {
  PICOBOOT_BOOTED = 0,   /* nothing done */
  PICOBOOT_SWAPPED = 1,  /* the new image was swapped in */
  PICOBOOT_REVERTED = 2, /* the new image never marked itself good: the previous one is back */
  PICOBOOT_ERROR = -1,   /* a flash call failed */
} picoboot_action;

/* The map of this chip (picoboot_map.h). */
picoboot_layout picoboot_layout_for(uint32_t active_len);

/* The state magic. */
int picoboot_read_state(const picoboot_flash* f, const picoboot_layout* l, picoboot_state* out);

/* Pages a swap and its revert move (from the state sector; 0, no swap, when it is missing or damaged). */
uint32_t picoboot_swap_pages(const picoboot_flash* f, const picoboot_layout* l);

/* The bootloader at every power-on: swap a marked update in, or swap it back when the image swapped in last
 * time never marked itself good. buf is scratch of at least l->page bytes. */
picoboot_action picoboot_prepare(const picoboot_flash* f, const picoboot_layout* l, uint8_t* buf);

/* The firmware: the update in dfu is complete and checked; swap it in at the next restart. image_len is the
 * new image's length and running_len the running image's (the swap covers the larger). Refused (-2) while the
 * running image is itself not yet marked good. */
int picoboot_mark_updated(const picoboot_flash* f, const picoboot_layout* l, uint32_t image_len, uint32_t running_len);

/* The firmware: its self-check passed (also clears a "reverted" mark). */
int picoboot_mark_booted(const picoboot_flash* f, const picoboot_layout* l);

/* May the firmware write dfu now? (embassy-boot's verify_booted: not while it is itself pending.) */
int picoboot_may_write_dfu(const picoboot_flash* f, const picoboot_layout* l);

/* Does a vector table (the first 8 bytes of an image) look like a firmware linked for the active slot? */
int picoboot_vectors_ok(const uint8_t* first8, uint32_t slot_base, uint32_t slot_len, uint32_t ram_lo, uint32_t ram_hi);

#ifdef __cplusplus
}
#endif
#endif
