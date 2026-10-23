// The Pico bootloader's boot decision (lib/picoboot, PICO-BOOTLOADER.md) on a simulated NOR flash: an erase
// sets a sector to 0xFF, a program only clears bits, and a power cut can stop the flash at any call, leaving that
// call half done (half a sector erased, half the bytes programmed). After every cut the board "powers on" again
// and the bootloader runs until it boots; the image that boots must always be a whole image, the right one.
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "picoboot.h"
#include "picoboot_map.h"

void setUp() {}
void tearDown() {}

static const uint32_t PAGE = 4096;

struct Sim {
  std::vector<uint8_t> mem;
  long budget = -1;  // mutating calls left before the power cut (-1: none)
  bool cut = false;
  long ops = 0;
  bool cut_erase_does_nothing = false;  // an interrupted erase: half the sector (default) or nothing
  uint8_t partial_bits = 0;  // non-zero: an interrupted program clears only the bits outside this mask, in every byte
  explicit Sim(uint32_t size) : mem(size, 0xFF) {}
};

static int sim_read(void* c, uint32_t off, uint8_t* buf, uint32_t n) {
  Sim& s = *(Sim*)c;
  if (s.cut) return -1;
  TEST_ASSERT_TRUE_MESSAGE(off + n <= s.mem.size(), "read out of flash");
  memcpy(buf, &s.mem[off], n);
  return 0;
}

// what a mutating call does: all of it, half of it (the power goes during it), or nothing (no power)
enum Fate { FULL, HALF, NONE };
static Fate spend(Sim& s) {
  if (s.cut) return NONE;
  s.ops++;
  if (s.budget < 0) return FULL;
  if (s.budget == 0) { s.cut = true; return HALF; }
  s.budget--;
  return FULL;
}

static int sim_erase(void* c, uint32_t off, uint32_t n) {
  Sim& s = *(Sim*)c;
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, off % PAGE, "erase not on a sector");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, n % PAGE, "erase not whole sectors");
  TEST_ASSERT_TRUE(off + n <= s.mem.size());
  Fate fate = spend(s);
  if (fate == NONE) return -1;
  uint32_t len = fate == FULL ? n : (s.cut_erase_does_nothing ? 0 : n / 2);
  memset(&s.mem[off], 0xFF, len);
  return fate == FULL ? 0 : -1;
}

static int sim_program(void* c, uint32_t off, const uint8_t* buf, uint32_t n) {
  Sim& s = *(Sim*)c;
  TEST_ASSERT_TRUE(off + n <= s.mem.size());
  Fate fate = spend(s);
  if (fate == NONE) return -1;
  if (fate == HALF && s.partial_bits) {
    for (uint32_t i = 0; i < n; i++) s.mem[off + i] &= (uint8_t)(buf[i] | s.partial_bits);  // some bits of each byte
    return -1;
  }
  uint32_t len = fate == FULL ? n : n / 2;
  for (uint32_t i = 0; i < len; i++) s.mem[off + i] &= buf[i];  // NOR: bits only clear
  return fate == FULL ? 0 : -1;
}

// A small board: state at 0x6000, a 4-page active slot, a 5-page dfu slot.
static const uint32_t ACTIVE_LEN = 4 * PAGE;

struct Board {
  Sim sim;
  picoboot_flash f;
  picoboot_layout l;
  std::vector<uint8_t> buf;
  Board() : sim(PICOBOOT_DFU_OFFSET(ACTIVE_LEN) + PICOBOOT_DFU_LEN(ACTIVE_LEN)), buf(PAGE) {
    f = {&sim, sim_read, sim_erase, sim_program};
    l = picoboot_layout_for(ACTIVE_LEN);
  }
  void fill(uint32_t off, uint32_t len, uint8_t tag) {
    for (uint32_t i = 0; i < len; i++) sim.mem[off + i] = (uint8_t)(tag ^ (i * 7) ^ (i >> 8));
  }
  bool holds(uint32_t off, uint32_t len, uint8_t tag) const {
    for (uint32_t i = 0; i < len; i++)
      if (sim.mem[off + i] != (uint8_t)(tag ^ (i * 7) ^ (i >> 8))) return false;
    return true;
  }
  picoboot_state state() {
    picoboot_state s;
    TEST_ASSERT_EQUAL(0, picoboot_read_state(&f, &l, &s));
    return s;
  }
  // power on: no cut this time; the bootloader runs to the end
  picoboot_action powerOn() {
    sim.cut = false;
    sim.budget = -1;
    return picoboot_prepare(&f, &l, buf.data());
  }
};

static const uint8_t A = 0xA5, B = 0x3C;

// image A running, image B (lenB bytes) written to dfu
static void setupUpdate(Board& b, uint32_t lenA, uint32_t lenB) {
  b.fill(b.l.active, lenA, A);
  b.fill(b.l.dfu, lenB, B);
}

void fresh_flash_boots() {
  Board b;
  TEST_ASSERT_EQUAL(PICOBOOT_BOOT, b.state());
  TEST_ASSERT_EQUAL(PICOBOOT_BOOTED, b.powerOn());
  TEST_ASSERT_TRUE(picoboot_may_write_dfu(&b.f, &b.l));
  TEST_ASSERT_EQUAL(0, b.sim.ops);  // nothing written at a plain boot
}

void an_update_swaps_in_and_stays_after_it_is_marked_good() {
  Board b;
  setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
  TEST_ASSERT_EQUAL(PICOBOOT_SWAP, b.state());
  TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, b.powerOn());
  TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, B));
  TEST_ASSERT_TRUE(b.holds(b.l.dfu + PAGE, ACTIVE_LEN, A));  // the previous image, one page up
  // the new firmware: pending, may not start another update
  TEST_ASSERT_EQUAL(PICOBOOT_SWAP, b.state());
  TEST_ASSERT_FALSE(picoboot_may_write_dfu(&b.f, &b.l));
  TEST_ASSERT_EQUAL(-2, picoboot_mark_updated(&b.f, &b.l, PAGE, PAGE));
  TEST_ASSERT_EQUAL(0, picoboot_mark_booted(&b.f, &b.l));
  TEST_ASSERT_EQUAL(PICOBOOT_BOOT, b.state());
  TEST_ASSERT_EQUAL(PICOBOOT_BOOTED, b.powerOn());
  TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, B));
}

void an_image_that_never_marks_itself_good_is_swapped_back() {
  Board b;
  setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
  TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, b.powerOn());
  // crash or watchdog before the self-check: the next power-on reverts
  TEST_ASSERT_EQUAL(PICOBOOT_REVERTED, b.powerOn());
  TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, A));
  TEST_ASSERT_EQUAL(PICOBOOT_REVERT, b.state());  // the firmware reports rolled_back
  TEST_ASSERT_EQUAL(PICOBOOT_BOOTED, b.powerOn());  // and it stays
  TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, A));
  // the next update clears the mark first, then works as usual
  TEST_ASSERT_TRUE(picoboot_may_write_dfu(&b.f, &b.l));
  TEST_ASSERT_EQUAL(0, picoboot_mark_booted(&b.f, &b.l));
  TEST_ASSERT_EQUAL(PICOBOOT_BOOT, b.state());
  b.fill(b.l.dfu, ACTIVE_LEN, B);
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
  TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, b.powerOn());
  TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, B));
}

void a_swap_moves_only_the_pages_of_the_larger_image() {
  Board b;
  setupUpdate(b, PAGE + 10, 2 * PAGE - 1);  // A: 2 pages, B: 2 pages
  b.fill(b.l.active + 3 * PAGE, PAGE, 0x77);  // past both images: untouched
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, 2 * PAGE - 1, PAGE + 10));
  TEST_ASSERT_EQUAL_UINT32(2, picoboot_swap_pages(&b.f, &b.l));
  b.sim.ops = 0;
  TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, b.powerOn());
  TEST_ASSERT_EQUAL(2 * 2 * 3, b.sim.ops);  // 2 pages x 2 copies x (erase, program, progress)
  TEST_ASSERT_TRUE(b.holds(b.l.active, 2 * PAGE - 1, B));
  TEST_ASSERT_TRUE(b.holds(b.l.active + 3 * PAGE, PAGE, 0x77));
  TEST_ASSERT_EQUAL(PICOBOOT_REVERTED, b.powerOn());
  TEST_ASSERT_TRUE(b.holds(b.l.active, PAGE + 10, A));
  TEST_ASSERT_TRUE(b.holds(b.l.active + 3 * PAGE, PAGE, 0x77));
}

// "swap" without a valid page count moves nothing: only mark_updated writes "swap", always after the count, so
// such a state is a half-written "boot" or "reverted" byte (or a damaged count): the running image boots.
void a_damaged_page_count_means_no_swap() {
  Board b;
  setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, PAGE, PAGE));
  TEST_ASSERT_EQUAL_UINT32(1, picoboot_swap_pages(&b.f, &b.l));
  b.sim.mem[b.l.state + PICOBOOT_PAGES_AT] &= 0xFE;  // a flipped bit: count and complement disagree
  TEST_ASSERT_EQUAL_UINT32(0, picoboot_swap_pages(&b.f, &b.l));
  TEST_ASSERT_EQUAL(PICOBOOT_BOOTED, b.powerOn());
  TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, A));
}

static bool wholeImage(Board& x) { return x.holds(x.l.active, ACTIVE_LEN, A) || x.holds(x.l.active, ACTIVE_LEN, B); }

// A power cut in the middle of programming a byte leaves some of its bits cleared (any subset): "boot" (0xD0) or
// "reverted" (0xC0) can read as "swap" (0xF0). Cut every flash call of marking, swapping, marking good and
// reverting that way, with several bit patterns: the board always boots a whole image.
void a_half_programmed_byte_never_scrambles_an_image() {
  long cases = 0;
  for (int mask : {0xF0, 0x0F, 0xAA, 0x55, 0x20, 0x10})
    for (int phase = 0; phase < 4; phase++)
      for (long k = 0;; k++) {
        Board b;
        setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
        if (phase >= 1) TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
        if (phase >= 2) TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, b.powerOn());
        b.sim.partial_bits = (uint8_t)mask;
        b.sim.budget = k;
        if (phase == 0) (void)picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN);
        else if (phase == 2) (void)picoboot_mark_booted(&b.f, &b.l);
        else (void)picoboot_prepare(&b.f, &b.l, b.buf.data());  // 1: the swap, 3: the revert
        if (!b.sim.cut) break;  // past the last call
        b.sim.partial_bits = 0;
        for (int i = 0; i < 3; i++) {
          b.powerOn();
          char why[80];
          snprintf(why, sizeof why, "mask 0x%02X phase %d cut at call %ld", mask, phase, k);
          TEST_ASSERT_TRUE_MESSAGE(wholeImage(b), why);
        }
        cases++;
      }
  TEST_ASSERT_TRUE(cases > 100);
}

// Two power cuts in a row (at any pair of calls) in the swap and then in the revert, erases that do half or
// nothing: the image that boots is whole and the state says which.
void two_power_cuts_in_a_row_lose_nothing() {
  long runs = 0;
  auto whole = [](Board& x) {
    picoboot_state st = x.state();
    return (st == PICOBOOT_SWAP && x.holds(x.l.active, ACTIVE_LEN, B)) || (st != PICOBOOT_SWAP && x.holds(x.l.active, ACTIVE_LEN, A));
  };
  for (int mode = 0; mode < 2; mode++)
    for (long k = 0; k < 40; k++)
      for (long j = 0; j < 40; j++) {
        Board b;
        b.sim.cut_erase_does_nothing = mode;
        setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
        TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
        b.sim.budget = k;
        (void)picoboot_prepare(&b.f, &b.l, b.buf.data());
        b.sim.cut = false;
        b.sim.budget = j;
        (void)picoboot_prepare(&b.f, &b.l, b.buf.data());
        b.powerOn();
        runs++;
        TEST_ASSERT_TRUE(whole(b));
        if (b.state() != PICOBOOT_SWAP) continue;
        b.sim.cut = false;
        b.sim.budget = k;
        (void)picoboot_prepare(&b.f, &b.l, b.buf.data());
        b.sim.cut = false;
        b.sim.budget = j;
        (void)picoboot_prepare(&b.f, &b.l, b.buf.data());
        b.powerOn();
        TEST_ASSERT_TRUE(whole(b));
      }
  TEST_ASSERT_EQUAL(3200, runs);
}

// Power cut at every flash call of a swap, then again at every call of the revert that follows if the new image
// never marks itself good. Whatever the cut, the board ends with a whole image: B after the swap, A after the
// revert, and the state says which.
void a_power_cut_anywhere_in_a_swap_or_revert_loses_nothing() {
  Board ref;
  setupUpdate(ref, ACTIVE_LEN, ACTIVE_LEN);
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&ref.f, &ref.l, ACTIVE_LEN, ACTIVE_LEN));
  ref.sim.ops = 0;
  ref.powerOn();
  long swap_ops = ref.sim.ops;
  TEST_ASSERT_TRUE(swap_ops > 0);
  for (long k = 0; k < swap_ops; k++) {
    Board b;
    setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
    TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
    b.sim.budget = k;
    picoboot_prepare(&b.f, &b.l, b.buf.data());
    // power back: the swap resumes and completes
    picoboot_action a = b.powerOn();
    TEST_ASSERT_TRUE_MESSAGE(a == PICOBOOT_SWAPPED, "a resumed swap");
    TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, B));
    TEST_ASSERT_EQUAL(PICOBOOT_SWAP, b.state());
    // never marked good: revert, cut at every call of it
    Board r = b;
    r.f.ctx = &r.sim;
    r.sim.ops = 0;
    r.powerOn();
    long revert_ops = r.sim.ops;
    for (long j = 0; j <= revert_ops; j += (k % 7 == 0 ? 1 : 5)) {
      Board c = b;
      c.f.ctx = &c.sim;
      c.sim.budget = j;
      picoboot_prepare(&c.f, &c.l, c.buf.data());
      picoboot_action ra = c.powerOn();
      if (j < revert_ops) TEST_ASSERT_TRUE_MESSAGE(ra == PICOBOOT_REVERTED || ra == PICOBOOT_BOOTED, "a resumed revert");
      TEST_ASSERT_TRUE(c.holds(c.l.active, ACTIVE_LEN, A));
      // "reverted" (rolled_back), or plain boot when the cut hit the final erase of the state sector: the image
      // is the previous one either way, only the report is lost
      picoboot_state st = c.state();
      TEST_ASSERT_TRUE(st == PICOBOOT_REVERT || st == PICOBOOT_BOOT);
      TEST_ASSERT_EQUAL(PICOBOOT_BOOTED, c.powerOn());
      TEST_ASSERT_TRUE(c.holds(c.l.active, ACTIVE_LEN, A));
    }
  }
}

// Power cut while the firmware marks an update or marks itself good: the board boots either image whole, never a
// mix, and a mark that did not finish is as if it was never made (or it is complete).
void a_power_cut_while_marking_is_safe() {
  for (long k = 0; k < 8; k++) {
    Board b;
    setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
    b.sim.budget = k;
    int r = picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN);
    b.sim.cut = false;
    b.sim.budget = -1;
    picoboot_state s = b.state();
    if (r == 0) TEST_ASSERT_EQUAL(PICOBOOT_SWAP, s);
    picoboot_action a = b.powerOn();
    if (s == PICOBOOT_SWAP) {
      TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, a);
      TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, B));
    } else {
      TEST_ASSERT_EQUAL(PICOBOOT_BOOTED, a);
      TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, A));
    }
  }
  for (int mode = 0; mode < 2; mode++)
    for (long k = 0; k < 6; k++) {
      Board b;
      b.sim.cut_erase_does_nothing = mode == 1;
      setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
      TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
      TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, b.powerOn());
      b.sim.budget = k;
      int r = picoboot_mark_booted(&b.f, &b.l);
      b.sim.cut = false;
      b.sim.budget = -1;
      picoboot_state s = b.state();
      picoboot_action a = b.powerOn();
      if (r == 0) TEST_ASSERT_EQUAL(PICOBOOT_BOOT, s);
      if (s == PICOBOOT_SWAP) {
        TEST_ASSERT_EQUAL(PICOBOOT_REVERTED, a);
        // the mark did not land: as a crash before the self-check, the previous image comes back; or (a cut after
        // the progress was marked invalid, before the erase took effect) nothing is copied and the new image stays,
        // reported as reverted. Either way a whole image.
        TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, A) || (mode == 1 && b.holds(b.l.active, ACTIVE_LEN, B)));
      } else {
        TEST_ASSERT_EQUAL(PICOBOOT_BOOTED, a);
        TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, B));
      }
    }
}

// the swap and the revert with an interrupted erase that erases nothing
void a_power_cut_that_erases_nothing_loses_nothing() {
  Board ref;
  setupUpdate(ref, ACTIVE_LEN, ACTIVE_LEN);
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&ref.f, &ref.l, ACTIVE_LEN, ACTIVE_LEN));
  ref.sim.ops = 0;
  ref.powerOn();
  long swap_ops = ref.sim.ops;
  for (long k = 0; k < swap_ops; k++) {
    Board b;
    b.sim.cut_erase_does_nothing = true;
    setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
    TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
    b.sim.budget = k;
    picoboot_prepare(&b.f, &b.l, b.buf.data());
    TEST_ASSERT_EQUAL(PICOBOOT_SWAPPED, b.powerOn());
    TEST_ASSERT_TRUE(b.holds(b.l.active, ACTIVE_LEN, B));
    for (long j = 0; j < swap_ops; j += 3) {
      Board c = b;
      c.f.ctx = &c.sim;
      c.sim.budget = j;
      picoboot_prepare(&c.f, &c.l, c.buf.data());
      c.powerOn();
      TEST_ASSERT_TRUE(c.holds(c.l.active, ACTIVE_LEN, A));
    }
  }
}

void a_failing_flash_is_reported() {
  Board b;
  setupUpdate(b, ACTIVE_LEN, ACTIVE_LEN);
  TEST_ASSERT_EQUAL(0, picoboot_mark_updated(&b.f, &b.l, ACTIVE_LEN, ACTIVE_LEN));
  b.sim.budget = 3;
  TEST_ASSERT_EQUAL(PICOBOOT_ERROR, picoboot_prepare(&b.f, &b.l, b.buf.data()));
}

void vector_tables_are_checked() {
  const uint32_t base = PICOBOOT_XIP_BASE + PICOBOOT_ACTIVE_OFFSET;
  auto vt = [](uint32_t sp, uint32_t reset, uint8_t* o) {
    for (int i = 0; i < 4; i++) { o[i] = (uint8_t)(sp >> (8 * i)); o[4 + i] = (uint8_t)(reset >> (8 * i)); }
  };
  uint8_t v[8];
  vt(0x20042000, base + 0x1F7, v);
  TEST_ASSERT_TRUE(picoboot_vectors_ok(v, base, PICOBOOT_RP2040_ACTIVE_LEN, 0x20000000, 0x20042000));
  vt(0x20042000, base + 0x1F6, v);  // not Thumb
  TEST_ASSERT_FALSE(picoboot_vectors_ok(v, base, PICOBOOT_RP2040_ACTIVE_LEN, 0x20000000, 0x20042000));
  vt(0x20042000, 0x100001F7, v);  // linked for the start of flash (a plain arduino-pico image)
  TEST_ASSERT_FALSE(picoboot_vectors_ok(v, base, PICOBOOT_RP2040_ACTIVE_LEN, 0x20000000, 0x20042000));
  vt(0xFFFFFFFF, 0xFFFFFFFF, v);  // erased
  TEST_ASSERT_FALSE(picoboot_vectors_ok(v, base, PICOBOOT_RP2040_ACTIVE_LEN, 0x20000000, 0x20042000));
  vt(0x20082000, base + 0x101, v);  // RP2350 RAM end
  TEST_ASSERT_TRUE(picoboot_vectors_ok(v, base, PICOBOOT_RP2350_ACTIVE_LEN, 0x20000000, 0x20082000));
  TEST_ASSERT_FALSE(picoboot_vectors_ok(v, base, PICOBOOT_RP2040_ACTIVE_LEN, 0x20000000, 0x20042000));
}

// both chips' real maps fit the state sector (progress of a whole-slot swap and revert)
void the_real_maps_fit_the_state_sector() {
  uint32_t lens[] = {PICOBOOT_RP2040_ACTIVE_LEN, PICOBOOT_RP2350_ACTIVE_LEN};
  for (uint32_t len : lens) {
    picoboot_layout l = picoboot_layout_for(len);
    TEST_ASSERT_TRUE(2 + 4 * (len / PAGE) <= PICOBOOT_PAGES_AT);
    TEST_ASSERT_EQUAL_UINT32(l.active + len, l.dfu);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(fresh_flash_boots);
  RUN_TEST(an_update_swaps_in_and_stays_after_it_is_marked_good);
  RUN_TEST(an_image_that_never_marks_itself_good_is_swapped_back);
  RUN_TEST(a_swap_moves_only_the_pages_of_the_larger_image);
  RUN_TEST(a_damaged_page_count_means_no_swap);
  RUN_TEST(a_half_programmed_byte_never_scrambles_an_image);
  RUN_TEST(two_power_cuts_in_a_row_lose_nothing);
  RUN_TEST(a_power_cut_anywhere_in_a_swap_or_revert_loses_nothing);
  RUN_TEST(a_power_cut_while_marking_is_safe);
  RUN_TEST(a_power_cut_that_erases_nothing_loses_nothing);
  RUN_TEST(a_failing_flash_is_reported);
  RUN_TEST(vector_tables_are_checked);
  RUN_TEST(the_real_maps_fit_the_state_sector);
  return UNITY_END();
}
