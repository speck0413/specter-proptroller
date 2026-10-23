#include "serial_line.h"
#include <stdlib.h>
#include <string.h>

namespace pt {

SerialLine::~SerialLine() { free(buf_); }

void SerialLine::release() {
  free(buf_);
  buf_ = nullptr;
  cap_ = 0;
}

// The next size: double, never past the limit, and only from a block the heap can spare (a failed malloc returns
// null on every board; nothing aborts here).
bool SerialLine::grow() {
  size_t want = cap_ ? 2 * cap_ : keep_;
  if (want > limit_) want = limit_;
  if (want <= cap_) return false;
  if (block_probe_ && cap_ >= keep_ && block_probe_() < want + SERIAL_LINE_HEAP_MARGIN) return false;
  char* p = (char*)malloc(want);
  if (!p) return false;
  if (len_) memcpy(p, buf_, len_);
  free(buf_);
  buf_ = p;
  cap_ = want;
  return true;
}

SerialLine::Result SerialLine::push(uint8_t c) {
  if (c == '\n' || c == '\r') {
    if (dropping_ != NONE) {
      Result r = dropping_;
      dropping_ = NONE;
      return r;
    }
    return len_ ? LINE : NONE;
  }
  if (dropping_ != NONE) return NONE;
  if (len_ == cap_) {
    if (len_ >= limit_ || !grow()) {
      dropping_ = len_ >= limit_ ? TOO_LONG : NO_MEMORY;
      // the memory goes back at once, not at the end of the line
      len_ = 0;
      release();
      return NONE;
    }
  }
  buf_[len_++] = (char)c;
  return NONE;
}

void SerialLine::done() {
  len_ = 0;
  if (cap_ > keep_) release();
}

bool SerialLine::equals(const char* s) const {
  size_t n = strlen(s);
  return n == len_ && (n == 0 || memcmp(buf_, s, n) == 0);
}

}  // namespace pt
