// The page password: kept only as a salted SHA-256, never as text (STATE-MACHINE.md SM-P4).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace pt {

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
// SHA-256 fed in pieces (a firmware image as it streams in).
class Sha256 {
 public:
  Sha256() { reset(); }
  void reset();
  void update(const uint8_t* data, size_t len);
  void finish(uint8_t out[32]);

 private:
  uint32_t h_[8]{};
  uint8_t buf_[64]{};
  size_t used_ = 0;
  uint64_t total_ = 0;
};
// Lower-case hex of bytes.
std::string toHex(const uint8_t* data, size_t len);
// sha256(salt ":" password) in lower-case hex.
std::string passwordHash(const std::string& salt, const std::string& password);
// A new salt (16 hex characters) from anything that differs between boards and changes.
std::string newSalt(const std::string& seed);
// Does `given` match the stored hash? An empty hash never matches.
bool passwordOk(const std::string& salt, const std::string& stored_hash, const std::string& given);

}  // namespace pt
