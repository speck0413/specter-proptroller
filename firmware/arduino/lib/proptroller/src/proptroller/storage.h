// Where settings and shows are kept. The platform layer gives a LittleFS file pair (ESP32, Pico); tests and
// the simulator use MemStorage, which also survives a simulated power cycle.
#pragma once
#include <stdint.h>
#include <string>

namespace pt {

// Usable size: two 12 KB slots with a 16-byte header.
static const size_t STORE_CAPACITY = 12 * 1024 - 16;

// What a store holds: nothing, a record, or a record that is there but cannot be read (both copies damaged, or a
// file system that could not be opened). An invalid record is kept, never taken for "nothing stored".
enum class Stored : uint8_t { None, Ok, Invalid };

class Storage {
 public:
  virtual ~Storage() {}
  // The newest complete record.
  [[nodiscard]] virtual Stored fetch(std::string& out) = 0;
  // The newest complete record; false if there is none or it cannot be read.
  bool load(std::string& out) { return fetch(out) == Stored::Ok; }
  // Replace the record; must be power-safe (a cut during a save keeps the previous record).
  [[nodiscard]] virtual bool save(const std::string& data) = 0;
  [[nodiscard]] virtual bool wipe() = 0;
  virtual size_t capacity() const { return STORE_CAPACITY; }
  // False when the flash file system could not be opened (and was not formatted, because it holds data): what
  // is stored there is unreadable, not missing, so nothing is written until a factory reset.
  virtual bool usable() const { return true; }
};

// The show script's own record (script_record.h): the text (8 KB at most) packed, its sound list and the checked
// step table (about 3 KB at most). Kept at the first release's size, so the file pair never moves.
static const size_t SCRIPT_STORE_CAPACITY = 24 * 1024;

class MemStorage : public Storage {
 public:
  explicit MemStorage(size_t capacity = STORE_CAPACITY) : cap_(capacity) {}
  size_t capacity() const override { return cap_; }
  [[nodiscard]] Stored fetch(std::string& out) override {
    if (!has_) return Stored::None;
    if (damaged) return Stored::Invalid;
    out = data_;
    return Stored::Ok;
  }
  [[nodiscard]] bool save(const std::string& d) override {
    if (save_hook) save_hook();
    if (fail_saves || d.size() > capacity()) return false;
    data_ = d;
    has_ = true;
    damaged = false;
    saves++;
    return true;
  }
  [[nodiscard]] bool wipe() override {
    if (fail_wipes) return false;
    has_ = false;
    damaged = false;
    data_.clear();
    wipes++;
    return true;
  }
  bool fail_wipes = false;  // tests: a flash that cannot be cleared
  int saves = 0, wipes = 0;
  bool damaged = false;  // tests: the record is there but both copies fail their check
  bool fail_saves = false;  // tests: a flash that refuses writes
  void (*save_hook)() = nullptr;  // tests: called at every save (a slow flash)

 private:
  size_t cap_;
  bool has_ = false;
  std::string data_;
};

}  // namespace pt
