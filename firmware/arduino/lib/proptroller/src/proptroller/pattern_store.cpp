#include "pattern_store.h"
#include <string.h>
#include "lz.h"
#include "pattern_vm.h"

#if PT_PATTERNS
namespace pt {

namespace {
void put8(std::string& b, uint32_t v) { b.push_back((char)(v & 0xFF)); }
void put16(std::string& b, uint32_t v) { put8(b, v); put8(b, v >> 8); }
void put32(std::string& b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t get32(const uint8_t* p) { return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16; }
const size_t HEAD = 12;
}  // namespace

std::string patternNameClean(const std::string& name) {
  std::string o;
  bool space = false;
  for (char c : name) {
    if (c == ' ' || c == '\t') { space = !o.empty(); continue; }
    if (space) o += ' ';
    space = false;
    o += c;
  }
  return o;
}

bool patternNameOk(const std::string& name, std::string& why) {
  std::string n = patternNameClean(name);
  if (n.empty()) { why = "give the pattern a name"; return false; }
  if (n.size() > PATTERN_NAME_MAX) { why = "a pattern's name is at most 32 characters"; return false; }
  for (char c : n) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' ||
              c == '(' || c == ')';
    if (!ok) { why = "a pattern's name has letters, digits, spaces and - _ ( ) only"; return false; }
  }
  return true;
}

std::string patternRecordEncode(const std::vector<StoredPattern>& ps, const StoredPattern* with, int at) {
  std::string body;
  size_t n = ps.size() + (with && at < 0 ? 1 : 0);
  for (size_t i = 0; i < n; i++) {
    const StoredPattern& p = with && ((at >= 0 && (size_t)at == i) || (at < 0 && i == ps.size())) ? *with : ps[i];
    put8(body, (uint32_t)p.name.size());
    body += p.name;
    put16(body, (uint32_t)p.program.size());
    body += p.program;
    put8(body, (uint32_t)p.controls.size());
    for (auto& c : p.controls) {
      put8(body, (uint32_t)c.control.size());
      body += c.control;
      put8(body, (uint32_t)c.values.size());
      for (int32_t v : c.values) put32(body, (uint32_t)v);
    }
  }
  std::string rec("PTPN", 4);
  put16(rec, PATTERN_RECORD_VERSION);
  put16(rec, 0);
  put32(rec, (uint32_t)body.size());
  rec += lzPack(body);
  return rec;
}

PatternRecordKind patternRecordDecode(const std::string& bytes, std::vector<StoredPattern>& out, uint32_t& version) {
  out.clear();
  version = 0;
  const uint8_t* b = (const uint8_t*)bytes.data();
  if (bytes.size() < HEAD || memcmp(b, "PTPN", 4) != 0) return PatternRecordKind::Unreadable;
  version = get16(b + 4);
  if (version > PATTERN_RECORD_VERSION) return PatternRecordKind::Newer;
  if (version != PATTERN_RECORD_VERSION) return PatternRecordKind::Unreadable;
  uint32_t n = get32(b + 8);
  if (n > PATTERN_STORE_RAM_MAX + 4096) return PatternRecordKind::Unreadable;
  std::string body;
  if (!lzUnpack(b + HEAD, bytes.size() - HEAD, n, body)) return PatternRecordKind::Unreadable;
  const uint8_t* p = (const uint8_t*)body.data();
  size_t at = 0;
  auto need = [&](size_t k) { return at + k <= body.size(); };
  while (at < body.size()) {
    StoredPattern sp;
    if (!need(1) || !need(1 + p[at])) { out.clear(); return PatternRecordKind::Unreadable; }
    sp.name.assign((const char*)p + at + 1, p[at]);
    at += 1 + p[at];
    if (!need(2)) { out.clear(); return PatternRecordKind::Unreadable; }
    uint16_t pl = get16(p + at);
    if (!need(2 + pl)) { out.clear(); return PatternRecordKind::Unreadable; }
    sp.program.assign((const char*)p + at + 2, pl);
    at += 2 + pl;
    if (!need(1)) { out.clear(); return PatternRecordKind::Unreadable; }
    uint8_t nc = p[at++];
    for (uint8_t i = 0; i < nc; i++) {
      PatternValues v;
      if (!need(1) || !need(1 + p[at])) { out.clear(); return PatternRecordKind::Unreadable; }
      v.control.assign((const char*)p + at + 1, p[at]);
      at += 1 + p[at];
      if (!need(1)) { out.clear(); return PatternRecordKind::Unreadable; }
      uint8_t nv = p[at++];
      if (nv > PATTERN_MAX_VALUES || !need(4u * nv)) { out.clear(); return PatternRecordKind::Unreadable; }
      for (uint8_t k = 0; k < nv; k++) v.values.push_back((int32_t)get32(p + at + 4 * k));
      at += 4u * nv;
      sp.controls.push_back(v);
    }
    out.push_back(sp);
    if (out.size() > PATTERN_MAX_STORED) { out.clear(); return PatternRecordKind::Unreadable; }
  }
  return PatternRecordKind::Current;
}

}  // namespace pt
#endif  // PT_PATTERNS
