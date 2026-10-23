// Power-safe record storage on any small file system (LittleFS on the boards): two files, A and B. Each save
// writes the file that does NOT hold the newest record, with a sequence number one higher, so a power cut
// during a save always leaves the previous record intact. Load picks the valid record with the highest
// sequence number. Record: "PTC1" | seq u32 | len u32 | crc32 u32 (little endian) | payload.
// An empty payload is a tombstone: wipe() writes one as the newest record before it removes the files, so a
// power cut (or a failed remove) part way through never brings an older record back.
#pragma once
#include "storage.h"
#include <stdint.h>

namespace pt {

uint32_t crc32(const uint8_t* data, size_t n);
// the same CRC over pieces: c = crc32Add(0xFFFFFFFF, ...) for each piece, then c ^ 0xFFFFFFFF
uint32_t crc32Add(uint32_t c, const uint8_t* data, size_t n);

// What scan() hands over: a file in pieces, in order.
class ByteSink {
 public:
  virtual ~ByteSink() {}
  virtual void piece(const uint8_t* data, size_t n) = 0;
};

class FileIo {
 public:
  virtual ~FileIo() {}
  [[nodiscard]] virtual bool read(const char* path, std::string& out) = 0;
  [[nodiscard]] virtual bool write(const char* path, const std::string& data) = 0;
  [[nodiscard]] virtual bool remove(const char* path) = 0;
  // The file is there (a failed read of a file that exists is a damaged record, not a missing one).
  virtual bool exists(const char* path) {
    std::string t;
    return read(path, t);
  }
  // The whole file in pieces, without a copy of it (LittleFsIo: 256 B at a time); false when it is missing or a
  // read fails. The default reads it whole.
  [[nodiscard]] virtual bool scan(const char* path, ByteSink& sink) {
    std::string t;
    if (!read(path, t)) return false;
    sink.piece((const uint8_t*)t.data(), t.size());
    return true;
  }
  // A header and the data as one file, without joining them first; false when it may not all be on flash. The
  // default joins them.
  [[nodiscard]] virtual bool writeRecord(const char* path, const uint8_t* head, size_t head_n, const std::string& data) {
    std::string t((const char*)head, head_n);
    t += data;
    return write(path, t);
  }
  // False when the file system could not be opened (see mountOrFormatBlank).
  virtual bool usable() { return true; }
  // A factory reset of a file system that could not be opened: format it (everything on it goes).
  [[nodiscard]] virtual bool reformat() { return false; }
};

// Opening the file system at power-on. It is formatted only when the partition was never written (every byte
// 0xFF, a new board); any other mount failure (an older firmware's file system after a rollback, damaged flash)
// leaves the flash as it is: the records count as unreadable, nothing is written, a new image fails its
// self-check, and only a factory reset formats it.
class FsMount {
 public:
  virtual ~FsMount() {}
  virtual bool mount() = 0;
  virtual bool blank() = 0;   // every byte of the partition is 0xFF
  virtual bool format() = 0;
};
enum class MountResult : uint8_t { Mounted, Formatted, Refused };
MountResult mountOrFormatBlank(FsMount& fs);

class ABStorage : public Storage {
 public:
  ABStorage(FileIo& fs, const char* path_a = "/cfg-a.bin", const char* path_b = "/cfg-b.bin", size_t capacity = STORE_CAPACITY)
      : fs_(fs), a_(path_a), b_(path_b), cap_(capacity) {}
  size_t capacity() const override { return cap_; }
  [[nodiscard]] Stored fetch(std::string& out) override;
  [[nodiscard]] bool save(const std::string& data) override;
  [[nodiscard]] bool wipe() override;
  bool usable() const override { return fs_.usable(); }
  uint32_t sequence();

 private:
  enum class Slot : uint8_t { Missing, Bad, Good };
  // a file's header checked and its payload's CRC streamed (no copy of the file): seq and len when Good
  Slot slot(const char* path, uint32_t& seq, uint32_t& len);
  // the newest good slot (nullptr when none); `bad`: a file that is there but fails its check
  const char* newest(uint32_t& seq, uint32_t& len, bool& bad);
  FileIo& fs_;
  const char* a_;
  const char* b_;
  size_t cap_;
  // which file holds the newest good record, as this object last read or wrote it: a save then writes the
  // other file without reading both first (flash time); forgotten on any failure and on wipe
  bool known_ = false;
  const char* known_path_ = nullptr;  // nullptr: no good record
  uint32_t known_seq_ = 0;
};

}  // namespace pt
