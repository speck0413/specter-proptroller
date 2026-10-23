#include "storage_ab.h"
#include <string.h>

namespace pt {

uint32_t crc32Add(uint32_t c, const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
  }
  return c;
}

uint32_t crc32(const uint8_t* d, size_t n) { return crc32Add(0xFFFFFFFFu, d, n) ^ 0xFFFFFFFFu; }

static const size_t HEAD = 16;

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

namespace {
// a record file as it streams past: the header gathered, the rest counted and its CRC taken
struct RecordCheck : ByteSink {
  uint8_t head[HEAD] = {};
  size_t got = 0;
  uint32_t crc = 0xFFFFFFFFu;
  void piece(const uint8_t* d, size_t n) override {
    while (n && got < HEAD) { head[got++] = *d++; n--; }
    got += n;
    crc = crc32Add(crc, d, n);
  }
  bool good() const {
    return got >= HEAD && memcmp(head, "PTC1", 4) == 0 && got - HEAD == le32(head + 8) && (crc ^ 0xFFFFFFFFu) == le32(head + 12);
  }
};
}  // namespace

MountResult mountOrFormatBlank(FsMount& fs) {
  if (fs.mount()) return MountResult::Mounted;
  if (!fs.blank()) return MountResult::Refused;
  return fs.format() && fs.mount() ? MountResult::Formatted : MountResult::Refused;
}

ABStorage::Slot ABStorage::slot(const char* path, uint32_t& seq, uint32_t& len) {
  RecordCheck c;
  if (!fs_.scan(path, c)) return fs_.exists(path) ? Slot::Bad : Slot::Missing;
  if (!c.good()) return Slot::Bad;
  seq = le32(c.head + 4);
  len = le32(c.head + 8);
  return Slot::Good;
}

const char* ABStorage::newest(uint32_t& seq, uint32_t& len, bool& bad) {
  uint32_t sa = 0, sb = 0, la = 0, lb = 0;
  Slot ka = slot(a_, sa, la), kb = slot(b_, sb, lb);
  bad = ka == Slot::Bad || kb == Slot::Bad;
  bool ha = ka == Slot::Good, hb = kb == Slot::Good;
  if (ha && hb) ha = !(hb = (int32_t)(sb - sa) > 0);  // the newer one
  if (ha) { seq = sa; len = la; return a_; }
  if (hb) { seq = sb; len = lb; return b_; }
  return nullptr;
}

Stored ABStorage::fetch(std::string& out) {
  if (!fs_.usable()) return Stored::Invalid;
  uint32_t seq = 0, len = 0;
  bool bad = false;
  const char* f = newest(seq, len, bad);
  known_ = f || !bad;
  known_path_ = f;
  known_seq_ = seq;
  // one damaged copy is a save cut short; with no good copy beside it the record is lost, not absent
  if (!f) return bad ? Stored::Invalid : Stored::None;
  if (len == 0) return Stored::None;  // a tombstone (wipe): nothing stored
  // only the newest file is read, into `out` itself (the header dropped in place)
  if (!fs_.read(f, out) || out.size() != HEAD + (size_t)len ||
      crc32((const uint8_t*)out.data() + HEAD, len) != le32((const uint8_t*)out.data() + 12)) {
    out.clear();
    return Stored::Invalid;
  }
  out.erase(0, HEAD);
  return Stored::Ok;
}

uint32_t ABStorage::sequence() {
  uint32_t seq = 0, len = 0;
  bool bad = false;
  return newest(seq, len, bad) ? seq : 0;
}

bool ABStorage::save(const std::string& data) {
  if (data.size() > capacity() || !fs_.usable()) return false;
  uint32_t seq = known_seq_, len = 0;
  bool bad = false;
  const char* cur = known_ ? known_path_ : newest(seq, len, bad);
  known_ = false;
  // write the slot that does not hold the newest record
  const char* target = cur == a_ ? b_ : a_;
  seq = cur ? seq + 1 : 1;
  uint8_t head[HEAD] = {'P', 'T', 'C', '1'};
  put32(head + 4, seq);
  put32(head + 8, (uint32_t)data.size());
  put32(head + 12, crc32((const uint8_t*)data.data(), data.size()));
  if (!fs_.writeRecord(target, head, HEAD, data)) return false;
  // read back (its CRC streamed): a close or sync that failed without saying so is caught here
  uint32_t got_seq = 0, got_len = 0;
  if (slot(target, got_seq, got_len) != Slot::Good || got_seq != seq || got_len != data.size()) return false;
  known_ = true;
  known_path_ = target;
  known_seq_ = seq;
  return true;
}

bool ABStorage::wipe() {
  if (!fs_.usable()) return fs_.reformat();
  // the tombstone first: from here on the store reads as empty, whatever happens to the removes
  bool tomb = save(std::string());
  uint32_t seq = 0, len = 0;
  bool bad = false;
  const char* top = newest(seq, len, bad);
  if (!top) top = b_;
  const char* other = top == a_ ? b_ : a_;
  known_ = false;
  auto gone = [this](const char* f) { return !fs_.exists(f) || fs_.remove(f); };
  // the older record goes before the tombstone that hides it
  if (!gone(other)) return tomb;
  return gone(top) || tomb;
}

}  // namespace pt
