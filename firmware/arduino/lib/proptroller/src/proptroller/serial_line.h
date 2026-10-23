// One line of the USB serial port (a JSON request or a console command), held in a buffer of its own that never
// grows past the request limit and is given back after a long line.
//
// The buffer is plain malloc'd memory with an exact size: a std::string doubles as it grows (a 33 KB line asks
// for 64 KB in one piece) and, built without C++ exceptions, ends in abort() when that allocation fails. Here a
// growth that the heap cannot give is answered in plain words instead, and the rest of that line is dropped.
#pragma once
#include "script.h"
#include "storage.h"
#include <stddef.h>
#include <stdint.h>

namespace pt {

// The largest legitimate request body: put_backup with everything a board stores (the settings, shows and profile
// record, STORE_CAPACITY), the largest show script with every character escaped (2 x SCRIPT_MAX_BYTES) and a full
// sound list (SCRIPT_MAX_SOUNDS entries of up to 40 bytes), plus 4 KB for the envelope and the backup's own fields.
// The same limit for an HTTP body, a WebSocket message and a USB serial line (42,952 bytes).
static const size_t MAX_REQUEST_BODY = STORE_CAPACITY + 2 * SCRIPT_MAX_BYTES + SCRIPT_MAX_SOUNDS * 40 + 4096;
static const size_t SERIAL_LINE_MAX = MAX_REQUEST_BODY;
// What the buffer keeps between lines (every console command and most requests fit), and the free block it leaves
// to the rest of the board when it grows (the network stack, the request's own work).
static const size_t SERIAL_LINE_KEEP = 256;
static const size_t SERIAL_LINE_HEAP_MARGIN = 8 * 1024;

class SerialLine {
 public:
  enum Result : uint8_t {
    NONE,       // nothing complete yet (or an empty line)
    LINE,       // a line is ready in data() / size(): handle it, then call done()
    TOO_LONG,   // the line was longer than the limit: it was dropped, answer bad_request
    NO_MEMORY,  // the heap could not hold the line: it was dropped, answer in plain words
  };
  explicit SerialLine(size_t limit = SERIAL_LINE_MAX, size_t keep = SERIAL_LINE_KEEP) : limit_(limit), keep_(keep) {}
  ~SerialLine();
  SerialLine(const SerialLine&) = delete;
  SerialLine& operator=(const SerialLine&) = delete;

  // One byte in; '\n' and '\r' end a line. A dropped line is answered once, at its end ("\r\n" is one end).
  Result push(uint8_t c);
  // After a LINE: empty again, and the memory over `keep` given back.
  void done();
  const char* data() const { return buf_ ? buf_ : ""; }
  size_t size() const { return len_; }
  bool equals(const char* s) const;
  // A line is coming in (also one being dropped): nothing else is written to the port meanwhile.
  bool receiving() const { return len_ > 0 || dropping_ != NONE; }
  size_t capacity() const { return cap_; }
  size_t limit() const { return limit_; }
  // The largest block the heap can give (the ESP32); none: only a failed malloc stops the growth.
  void setBlockProbe(size_t (*probe)()) { block_probe_ = probe; }

 private:
  bool grow();
  void release();
  char* buf_ = nullptr;
  size_t len_ = 0, cap_ = 0;
  size_t limit_, keep_;
  Result dropping_ = NONE;  // TOO_LONG or NO_MEMORY while the rest of a dropped line goes by
  size_t (*block_probe_)() = nullptr;
};

}  // namespace pt
