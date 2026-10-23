#include "lz.h"
#include <memory>
#include <vector>

namespace pt {

namespace {
const uint32_t HASH_BITS = 10;
const uint32_t NO_POS = 0xFFFFFFFFu;

template <class S>
uint32_t hash3(const S& s, size_t p) {
  uint32_t v = (uint32_t)s[p] << 16 | (uint32_t)s[p + 1] << 8 | s[p + 2];
  return (v * 2654435761u) >> (32 - HASH_BITS);
}

// two pieces read as one, without a joined copy
struct Joined {
  const uint8_t* a;
  size_t na;
  const uint8_t* b;
  uint8_t operator[](size_t i) const { return i < na ? a[i] : b[i - na]; }
};

template <class S>
std::string pack(const S& s, const size_t n) {
  std::string out;
  out.reserve(lzMaxPacked(n));
  // head[h]: the latest position with that 3-byte hash; prev[pos % window]: the one before it (a chain)
  std::vector<uint32_t> head(1u << HASH_BITS, NO_POS);
  std::vector<uint16_t> prev(LZ_WINDOW, 0);
  size_t flag_at = 0;
  int items = 8;
  auto insert = [&](size_t p) {
    if (p + LZ_MIN_MATCH > n) return;
    uint32_t h = hash3(s, p);
    uint32_t last = head[h];
    // distance to the previous position with this hash (0: none within the window)
    prev[p % LZ_WINDOW] = (uint16_t)(last != NO_POS && p - last < LZ_WINDOW ? p - last : 0);
    head[h] = (uint32_t)p;
  };
  size_t i = 0;
  while (i < n) {
    if (items == 8) {
      flag_at = out.size();
      out.push_back('\0');
      items = 0;
    }
    size_t best_len = 0, best_dist = 0;
    if (i + LZ_MIN_MATCH <= n) {
      uint32_t cand = head[hash3(s, i)];
      size_t max_len = n - i < LZ_MAX_MATCH ? n - i : LZ_MAX_MATCH;
      for (uint32_t tries = 0; cand != NO_POS && tries < LZ_MAX_CHAIN; tries++) {
        size_t dist = i - cand;
        if (dist == 0 || dist > LZ_WINDOW) break;
        size_t l = 0;
        while (l < max_len && s[cand + l] == s[i + l]) l++;
        if (l > best_len) {
          best_len = l;
          best_dist = dist;
          if (l == max_len) break;
        }
        uint16_t back = prev[cand % LZ_WINDOW];
        if (back == 0 || back > cand) break;
        uint32_t next = cand - back;
        if (i - next > LZ_WINDOW) break;
        cand = next;
      }
    }
    if (best_len >= LZ_MIN_MATCH) {
      uint16_t code = (uint16_t)((best_dist - 1) | (best_len - LZ_MIN_MATCH) << 12);
      out.push_back((char)(code & 0xFF));
      out.push_back((char)(code >> 8));
      for (size_t k = 0; k < best_len; k++) insert(i + k);
      i += best_len;
    } else {
      out[flag_at] = (char)(out[flag_at] | (1 << items));
      out.push_back((char)s[i]);
      insert(i);
      i++;
    }
    items++;
  }
  return out;
}
}  // namespace

std::string lzPack(const std::string& in) { return pack((const uint8_t*)in.data(), in.size()); }

std::string lzPack(const std::string& a, const std::string& b) {
  if (b.empty()) return lzPack(a);
  return pack(Joined{(const uint8_t*)a.data(), a.size(), (const uint8_t*)b.data()}, a.size() + b.size());
}

bool lzUnpack(const uint8_t* p, size_t n, size_t out_len, std::string& out) {
  out.clear();
  out.reserve(out_len);
  size_t at = 0;
  while (out.size() < out_len) {
    if (at >= n) { out.clear(); return false; }
    uint8_t flags = p[at++];
    for (int b = 0; b < 8 && out.size() < out_len; b++) {
      if (flags & (1 << b)) {
        if (at >= n) { out.clear(); return false; }
        out.push_back((char)p[at++]);
        continue;
      }
      if (at + 2 > n) { out.clear(); return false; }
      uint16_t code = (uint16_t)(p[at] | p[at + 1] << 8);
      at += 2;
      size_t dist = (code & 0x0FFFu) + 1, len = (code >> 12) + LZ_MIN_MATCH;
      if (dist > out.size() || out.size() + len > out_len) { out.clear(); return false; }
      size_t from = out.size() - dist;
      for (size_t k = 0; k < len; k++) out.push_back(out[from + k]);  // may overlap: byte by byte
    }
  }
  // every byte of the stream used (a longer stream is damaged)
  if (at != n) { out.clear(); return false; }
  return true;
}

}  // namespace pt
