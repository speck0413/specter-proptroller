// The LED patterns a board keeps (firmware/PROTOCOL.md "LED patterns", FLASH-MAP.md "Stored records"): each one by
// its name, with the program's bytes as Studio sent them and the control values to start it with. All of them
// are one record in their own A/B file pair (/patterns-a.bin, /patterns-b.bin), written like the script's: only
// when the board is quiet, never while a new image waits for its self-check, a newer or unreadable record kept.
//
// Record format 1 (little endian):
//   0   4  "PTPN"
//   4   2  record format (PATTERN_RECORD_VERSION)
//   6   2  0
//   8   4  the body's bytes unpacked
//   12     the body packed (lz.h)
// The body, per pattern: name (1 byte length, then the name), program (2 bytes length, then its bytes),
// control values (1 byte count; each: 1 byte name length, the name, 1 byte value count, 4 bytes per value).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace pt {

static const uint16_t PATTERN_RECORD_VERSION = 1;
static const size_t PATTERN_STORE_CAPACITY = 8 * 1024;   // one file of the pair, packed: two 4 KB blocks
static const size_t PATTERN_STORE_RAM_MAX = 24 * 1024;   // every stored program unpacked, in RAM
static const uint8_t PATTERN_MAX_STORED = 8;
static const size_t PATTERN_NAME_MAX = 32;
static const uint8_t PATTERN_MAX_VALUES = 8;             // values per control (a colour is three)

struct PatternValues {
  std::string control;  // the control's function name (sliderSpeed)
  std::vector<int32_t> values;  // 16.16
};

struct StoredPattern {
  std::string name;
  std::string program;  // the bytes (pattern_vm.h decodes them)
  std::vector<PatternValues> controls;
  // what the board knows from the program without keeping it decoded: its trigger buttons (control index, label
  // words) for `pixels press`
  std::vector<std::pair<uint8_t, std::string>> buttons;
  uint32_t gen = 0;  // a new number every time it is put (the player starts the new program)
};

// A pattern's name: 1-32 characters, letters, digits, spaces and - _ ( ) only (no #: it starts a comment in a
// script; no . : a script line drops a final one). False with the reason in plain words.
[[nodiscard]] bool patternNameOk(const std::string& name, std::string& why);
// The name as stored: spaces around it trimmed, runs of spaces made one (its case kept).
std::string patternNameClean(const std::string& name);

// `with` (optional): the record as it would be with that pattern in place `at` (-1: added at the end), without a
// copy of the list.
std::string patternRecordEncode(const std::vector<StoredPattern>& p, const StoredPattern* with = nullptr, int at = -1);
enum class PatternRecordKind : uint8_t { Current, Newer, Unreadable };
// `version` the record's format (for the message about a newer one).
PatternRecordKind patternRecordDecode(const std::string& bytes, std::vector<StoredPattern>& out, uint32_t& version);

}  // namespace pt
