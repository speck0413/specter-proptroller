// A small LZ77 compressor (LZSS, in the style of heatshrink) for the stored show script: the text comes back
// byte for byte, the code is a page long and decoding needs no memory beyond the output.
//
// The stream: groups of eight items, each group led by a flag byte (bit 0 first; 1 = a literal byte follows,
// 0 = a match follows). A match is two bytes, little endian: the low 12 bits are the distance back minus 1
// (1 to LZ_WINDOW), the high 4 bits the length minus LZ_MIN_MATCH (LZ_MIN_MATCH to LZ_MAX_MATCH bytes). The
// last group may be short. Worst case (nothing repeats): 9 bytes for every 8, lzMaxPacked().
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace pt {

static const uint32_t LZ_WINDOW = 4096;    // how far back a match may start
static const uint32_t LZ_MIN_MATCH = 3;    // shorter repeats stay literals
static const uint32_t LZ_MAX_MATCH = 18;   // 4 bits of length
static const uint32_t LZ_MAX_CHAIN = 48;   // candidates tried per position (speed against ratio)

// The largest packed size of n bytes.
constexpr size_t lzMaxPacked(size_t n) { return n + (n + 7) / 8; }

// Compress `in` (any bytes). Temporary heap: 12 KB of match chains (4 KB of heads, 8 KB of links) plus the output.
std::string lzPack(const std::string& in);
// The same as lzPack(a + b), without the joined copy.
std::string lzPack(const std::string& a, const std::string& b);
// Decompress exactly `out_len` bytes from `packed`; false (and `out` cleared) when the stream is damaged, too
// short, too long, or refers before the start.
[[nodiscard]] bool lzUnpack(const uint8_t* packed, size_t n, size_t out_len, std::string& out);

}  // namespace pt
