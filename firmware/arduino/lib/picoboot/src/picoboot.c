/* The boot decision of the Pico bootloader (picoboot.h). A C port of embassy-boot's swap and revert
 * (https://github.com/embassy-rs/embassy, embassy-boot, MIT OR Apache-2.0), with one change: the number of
 * pages to move is recorded with the update, so a swap moves the image, not the whole slot. */
#include "picoboot.h"
#include "picoboot_map.h"

#define ERASED 0xFFu

picoboot_layout picoboot_layout_for(uint32_t active_len) {
  picoboot_layout l;
  l.state = PICOBOOT_STATE_OFFSET;
  l.active = PICOBOOT_ACTIVE_OFFSET;
  l.active_len = active_len;
  l.dfu = PICOBOOT_DFU_OFFSET(active_len);
  l.page = PICOBOOT_SECTOR;
  return l;
}

static uint32_t page_count(const picoboot_layout* l) { return l->active_len / l->page; }

/* the state sector has room for the progress of a whole-slot swap and its revert */
static int layout_ok(const picoboot_layout* l) {
  return l->page != 0 && l->active_len % l->page == 0 && l->dfu >= l->active + l->active_len &&
         2u + 4u * page_count(l) <= PICOBOOT_PAGES_AT;
}

static int read_byte(const picoboot_flash* f, uint32_t off, uint8_t* b) { return f->read(f->ctx, off, b, 1); }
static int write_byte(const picoboot_flash* f, uint32_t off, uint8_t b) { return f->program(f->ctx, off, &b, 1); }

int picoboot_read_state(const picoboot_flash* f, const picoboot_layout* l, picoboot_state* out) {
  uint8_t m;
  if (read_byte(f, l->state, &m)) return -1;
  if (m == PICOBOOT_MAGIC_SWAP) *out = PICOBOOT_SWAP;
  else if (m == PICOBOOT_MAGIC_DFU_DETACH) *out = PICOBOOT_DFU_DETACH;
  else if (m == PICOBOOT_MAGIC_REVERT) *out = PICOBOOT_REVERT;
  else *out = PICOBOOT_BOOT;
  return 0;
}

static uint32_t get_u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put_u32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

uint32_t picoboot_swap_pages(const picoboot_flash* f, const picoboot_layout* l) {
  uint8_t b[8];
  uint32_t all = page_count(l);
  if (f->read(f->ctx, l->state + PICOBOOT_PAGES_AT, b, 8)) return all;
  uint32_t n = get_u32(b), c = get_u32(b + 4);
  if ((n ^ c) != 0xFFFFFFFFu || n == 0 || n > all) return 0;  /* no valid count: no swap */
  return n;
}

/* the first step not yet done; every step counts as done when the progress is marked invalid */
static int current_progress(const picoboot_flash* f, const picoboot_layout* l, uint32_t* out) {
  const uint32_t max_index = PICOBOOT_PAGES_AT - 1u - 2u;
  uint8_t chunk[64];
  if (read_byte(f, l->state + 1, chunk)) return -1;
  if (chunk[0] != ERASED) { *out = max_index; return 0; }
  for (uint32_t i = 0; i < max_index; i += sizeof chunk) {
    uint32_t n = max_index - i < sizeof chunk ? max_index - i : (uint32_t)sizeof chunk;
    if (f->read(f->ctx, l->state + 2 + i, chunk, n)) return -1;
    for (uint32_t k = 0; k < n; k++)
      if (chunk[k] == ERASED) { *out = i + k; return 0; }
  }
  *out = max_index;
  return 0;
}

/* copy one page, then record the step (a step already recorded is skipped: a resumed swap) */
static int copy_step(const picoboot_flash* f, const picoboot_layout* l, uint32_t* progress, uint32_t index,
                     uint32_t from, uint32_t to, uint8_t* buf) {
  if (*progress > index) return 0;
  if (f->read(f->ctx, from, buf, l->page)) return -1;
  if (f->erase(f->ctx, to, l->page)) return -1;
  if (f->program(f->ctx, to, buf, l->page)) return -1;
  if (write_byte(f, l->state + 2 + index, 0x00)) return -1;
  *progress = index + 1;
  return 0;
}

static int swap(const picoboot_flash* f, const picoboot_layout* l, uint32_t n, uint32_t progress, uint8_t* buf) {
  for (uint32_t i = 0; i < n; i++) {
    uint32_t p = n - 1 - i;
    if (copy_step(f, l, &progress, 2 * i, l->active + p * l->page, l->dfu + (p + 1) * l->page, buf)) return -1;
    if (copy_step(f, l, &progress, 2 * i + 1, l->dfu + p * l->page, l->active + p * l->page, buf)) return -1;
  }
  return 0;
}

static int revert(const picoboot_flash* f, const picoboot_layout* l, uint32_t n, uint32_t progress, uint8_t* buf) {
  for (uint32_t p = 0; p < n; p++) {
    uint32_t index = 2 * n + 2 * p;
    if (copy_step(f, l, &progress, index, l->active + p * l->page, l->dfu + p * l->page, buf)) return -1;
    if (copy_step(f, l, &progress, index + 1, l->dfu + (p + 1) * l->page, l->active + p * l->page, buf)) return -1;
  }
  return 0;
}

/* embassy-boot's set_magic: progress marked invalid, the sector erased, then the magic (and, for a swap, the
 * page count before it). A power cut at any point leaves either the old magic or an erased sector (boot). */
static int set_magic(const picoboot_flash* f, const picoboot_layout* l, uint8_t magic, uint32_t pages) {
  uint8_t m, valid;
  if (read_byte(f, l->state, &m)) return -1;
  if (m == magic) return 0;
  if (read_byte(f, l->state + 1, &valid)) return -1;
  if (valid == ERASED && write_byte(f, l->state + 1, 0x00)) return -1;
  if (f->erase(f->ctx, l->state, l->page)) return -1;
  if (pages) {
    uint8_t b[8];
    put_u32(b, pages);
    put_u32(b + 4, ~pages);
    if (f->program(f->ctx, l->state + PICOBOOT_PAGES_AT, b, 8)) return -1;
  }
  return write_byte(f, l->state, magic);
}

picoboot_action picoboot_prepare(const picoboot_flash* f, const picoboot_layout* l, uint8_t* buf) {
  picoboot_state s;
  if (!layout_ok(l) || picoboot_read_state(f, l, &s)) return PICOBOOT_ERROR;
  if (s != PICOBOOT_SWAP) return PICOBOOT_BOOTED;
  uint32_t n = picoboot_swap_pages(f, l), progress;
  /* only mark_updated writes "swap", and it writes the count first: "swap" without a count is a half-written
   * "boot" or "reverted" magic (NOR clears some bits of a byte first) and must not move anything */
  if (n == 0) return PICOBOOT_BOOTED;
  if (current_progress(f, l, &progress)) return PICOBOOT_ERROR;
  if (progress < 2 * n) {
    // a marked update (or a swap a power cut stopped): swap it in; the state stays "swap" until the new image
    // marks itself good
    return swap(f, l, n, progress, buf) ? PICOBOOT_ERROR : PICOBOOT_SWAPPED;
  }
  // swapped at an earlier power-on and never marked good: swap the previous image back
  if (revert(f, l, n, progress, buf)) return PICOBOOT_ERROR;
  if (write_byte(f, l->state + 1, 0x00)) return PICOBOOT_ERROR;
  if (f->erase(f->ctx, l->state, l->page)) return PICOBOOT_ERROR;
  if (write_byte(f, l->state, PICOBOOT_MAGIC_REVERT)) return PICOBOOT_ERROR;
  return PICOBOOT_REVERTED;
}

int picoboot_may_write_dfu(const picoboot_flash* f, const picoboot_layout* l) {
  picoboot_state s;
  if (picoboot_read_state(f, l, &s)) return 0;
  return s != PICOBOOT_SWAP;
}

int picoboot_mark_updated(const picoboot_flash* f, const picoboot_layout* l, uint32_t image_len, uint32_t running_len) {
  if (!layout_ok(l)) return -1;
  if (!picoboot_may_write_dfu(f, l)) return -2;
  uint32_t len = image_len > running_len ? image_len : running_len;
  uint32_t pages = (len + l->page - 1) / l->page;
  if (pages == 0 || pages > page_count(l)) pages = page_count(l);
  return set_magic(f, l, PICOBOOT_MAGIC_SWAP, pages);
}

int picoboot_mark_booted(const picoboot_flash* f, const picoboot_layout* l) {
  picoboot_state s;
  if (picoboot_read_state(f, l, &s)) return -1;
  if (s == PICOBOOT_BOOT) return 0;  // nothing to clear: no erase
  return set_magic(f, l, PICOBOOT_MAGIC_BOOT, 0);
}

int picoboot_vectors_ok(const uint8_t* first8, uint32_t slot_base, uint32_t slot_len, uint32_t ram_lo, uint32_t ram_hi) {
  uint32_t sp = get_u32(first8), reset = get_u32(first8 + 4);
  if (sp <= ram_lo || sp > ram_hi || (sp & 3u)) return 0;
  if (!(reset & 1u)) return 0;  // Thumb
  reset &= ~1u;
  return reset >= slot_base + 8u && reset < slot_base + slot_len;
}
