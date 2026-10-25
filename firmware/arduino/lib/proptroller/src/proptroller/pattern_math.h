// The LED pattern language's maths in 16.16 fixed point (a 32-bit integer holding value x 65536), integers only.
// Step for step the same as Specter Studio's src/patterns/fixed.ts, so a board lights exactly what Studio's
// preview shows: firmware/pattern-golden holds the shared vectors (test/test_pattern_vm checks them). Wrapping
// follows JavaScript's 32-bit integer rules (sums, negation and shifts on uint32_t, never signed overflow).
//
// Written from the textbook series (sine, the exponential) and the Abramowitz and Stegun arctangent.
#pragma once
#include <stdint.h>

namespace pt {
namespace pmath {

static const int32_t ONE = 65536;
static const int32_t Q30 = 1 << 30;
static const int32_t INT_MIN32 = INT32_MIN;
static const int32_t INT_MAX32 = INT32_MAX;

inline int32_t wrap(int64_t v) { return (int32_t)(uint32_t)(uint64_t)v; }
inline int32_t add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
inline int32_t sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
inline int32_t neg(int32_t a) { return (int32_t)(0u - (uint32_t)a); }
// floor(a x b / 2^s), wrapped to 32 bits (s 16 to 31)
inline int32_t mulS(int32_t a, int32_t b, int s) { return wrap(((int64_t)a * b) >> s); }
inline int32_t fmul(int32_t a, int32_t b) { return mulS(a, b, 16); }
// rounded toward zero; / 0 gives the largest number of the right sign, 0 / 0 is 0
inline int32_t fdiv(int32_t a, int32_t b) {
  if (b == 0) return a > 0 ? INT_MAX32 : a < 0 ? INT_MIN32 : 0;
  return wrap(((int64_t)a * ONE) / b);
}

inline uint64_t isqrt(uint64_t v) {
  uint64_t r = 0, bit = (uint64_t)1 << 62;
  while (bit > v) bit >>= 2;
  while (bit) {
    if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
    else r >>= 1;
    bit >>= 2;
  }
  return r;
}

inline int32_t sinTurn24(int32_t t) {  // t: a fraction of a turn in 24 bits; Q30 result
  static const int32_t C[] = {172272, -5026995, 85569306, -693598668, 1686629713};
  int32_t quarter = t >> 22;
  int32_t u = t & 0x3fffff;
  if (quarter & 1) u = 0x400000 - u;
  int32_t x = u * 256;
  int32_t x2 = mulS(x, x, 30);
  int32_t p = C[0];
  for (int i = 1; i < 5; i++) p = add(C[i], mulS(p, x2, 30));
  int32_t s = mulS(p, x, 30);
  return quarter >= 2 ? neg(s) : s;
}
inline int32_t q30to16(int32_t v) { return add(v, 8192) >> 14; }
inline int32_t turns24(int32_t x) { return mulS(x, 170891319, 22) & 0xffffff; }
inline int32_t fsin(int32_t x) { return q30to16(sinTurn24(turns24(x))); }
inline int32_t fcos(int32_t x) { return q30to16(sinTurn24((turns24(x) + 0x400000) & 0xffffff)); }
inline int32_t ftan(int32_t x) { return fdiv(fsin(x), fcos(x)); }
inline int32_t fwave(int32_t x) { return (q30to16(sinTurn24((x & 0xffff) * 256)) + ONE) >> 1; }

inline int32_t atanQ30(int32_t r) {
  static const int32_t C[] = {-4353012, 23473316, -60032783, 103530234, -149341741, 214174299, -357876604, 1073741108};
  int32_t z = mulS(r, r, 30);
  int32_t p = C[0];
  for (int i = 1; i < 8; i++) p = add(C[i], mulS(p, z, 30));
  return mulS(p, r, 30);
}
inline int32_t fatan2(int32_t y, int32_t x) {
  if (x == 0 && y == 0) return 0;
  int64_t ax = x < 0 ? -(int64_t)x : x, ay = y < 0 ? -(int64_t)y : y;
  int32_t a;
  if (ay <= ax) a = atanQ30((int32_t)((ay * 4194304) / ax) * 256);
  else a = 1686629713 - atanQ30((int32_t)((ax * 4194304) / ay) * 256);
  int32_t r = q30to16(a);
  if (x < 0) r = 205887 - r;
  return y < 0 ? neg(r) : r;
}
inline int32_t fatan(int32_t x) { return fatan2(x, ONE); }
inline int32_t clampOne(int32_t x) { return x < -ONE ? -ONE : x > ONE ? ONE : x; }
inline int32_t cosOf(int32_t x) { return (int32_t)isqrt(((uint64_t)1 << 32) - (uint64_t)((int64_t)x * x)); }
inline int32_t fasin(int32_t x) { x = clampOne(x); return fatan2(x, cosOf(x)); }
inline int32_t facos(int32_t x) { x = clampOne(x); return fatan2(cosOf(x), x); }

inline int32_t fsqrt(int32_t x) { return x <= 0 ? 0 : (int32_t)isqrt((uint64_t)x << 16); }
inline int32_t fhypot(int32_t a, int32_t b) {
  int64_t A = a < 0 ? -(int64_t)a : a, B = b < 0 ? -(int64_t)b : b;
  int k = 0;
  while (A >= ((int64_t)1 << 26) || B >= ((int64_t)1 << 26)) { A >>= 1; B >>= 1; k++; }
  int64_t r = (int64_t)isqrt((uint64_t)(A * A + B * B)) << k;
  return r > INT_MAX32 ? INT_MAX32 : (int32_t)r;
}

inline int32_t log2u(uint32_t x) {  // x > 0, read as 16.16
  int m = 31 - __builtin_clz(x);
  int32_t mm = (int32_t)(m >= 29 ? x >> (m - 29) : x << (29 - m));
  int32_t frac = 0;
  for (int bit = 15; bit >= 0; bit--) {
    mm = mulS(mm, mm, 29);
    if (mm >= Q30) { mm >>= 1; frac += (int32_t)1 << bit; }
  }
  return (m - 16) * ONE + frac;
}
inline int32_t flog2(int32_t x) { return x <= 0 ? INT_MIN32 : log2u((uint32_t)x); }
inline int32_t flog(int32_t x) { return x <= 0 ? INT_MIN32 : mulS(log2u((uint32_t)x), 744261118, 30); }

inline int32_t fexp2(int32_t y) {
  static const int32_t C[] = {1419, 16377, 165394, 1431680, 10327387, 59597083, 257941248, 744261118};
  int32_t k = y >> 16, f = (y & 0xffff) * 16384;
  int32_t p = C[0];
  for (int i = 1; i < 8; i++) p = add(C[i], mulS(p, f, 30));
  int64_t v = (int64_t)Q30 + mulS(p, f, 30);
  int32_t shift = 14 - k;
  if (shift < 0) return INT_MAX32;
  if (shift == 0) return v > INT_MAX32 ? INT_MAX32 : (int32_t)v;
  if (shift >= 32) return 0;
  return (int32_t)((v + ((int64_t)1 << (shift - 1))) >> shift);
}
inline int32_t fexp(int32_t x) {
  if (x >= 11 * ONE) return INT_MAX32;
  if (x <= -12 * ONE) return 0;
  return fexp2(mulS(x, 1549082005, 30));
}
inline int32_t fpow(int32_t a, int32_t b) {
  if (b == 0) return ONE;
  if (a == 0) return b > 0 ? 0 : INT_MAX32;
  bool whole = (b & 0xffff) == 0;
  if (a < 0 && !whole) return 0;
  int32_t l = log2u(a < 0 ? (uint32_t)0 - (uint32_t)a : (uint32_t)a);
  int64_t y = ((int64_t)b * l) >> 16;
  int32_t mag = y >= 15 * ONE ? INT_MAX32 : y < -17 * ONE ? 0 : fexp2((int32_t)y);
  return a < 0 && ((b >> 16) & 1) ? neg(mag) : mag;
}

inline int32_t smoothstep(int32_t lo, int32_t hi, int32_t x) {
  if (hi == lo) return x < lo ? 0 : ONE;
  int32_t t = fdiv(sub(x, lo), sub(hi, lo));
  t = t < 0 ? 0 : t > ONE ? ONE : t;
  return fmul(fmul(t, t), 3 * ONE - 2 * t);
}

inline void hsv(int32_t h, int32_t s, int32_t v, int32_t& r, int32_t& g, int32_t& b) {
  h &= 0xffff;
  s = s < 0 ? 0 : s > ONE ? ONE : s;
  v = v < 0 ? 0 : v > ONE ? ONE : v;
  int32_t h6 = h * 6, i = h6 >> 16, fr = h6 & 0xffff;
  int32_t p = fmul(v, ONE - s), q = fmul(v, ONE - fmul(fr, s)), t = fmul(v, ONE - fmul(ONE - fr, s));
  switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}
// a colour part (0 to 1) as 0 to 255, halves up
inline uint8_t channel(int32_t c) { return (uint8_t)((c * 255 + 32768) >> 16); }

}  // namespace pmath
}  // namespace pt
