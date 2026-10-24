// The show script's stored record (FLASH-MAP.md "Stored records"), kept in its own A/B file pair.
//
// Format 1 (binary, little endian):
//   0   4  "PTSR"
//   4   2  record format (SCRIPT_RECORD_VERSION)
//   6   2  table format (SCRIPT_TABLE_VERSION; 0: no table stored)
//   8   4  table key: what the table was checked against (scriptTableKey: the table format, the firmware's
//          version and build, the checker's board settings, the sound list); a restart runs the stored table only
//          when the key is the same, otherwise it checks the text again
//   12  4  text bytes (exact, as sent)
//   16  2  sound list bytes (its JSON, {"chime.mp3":3})
//   18  4  packed bytes: the text, then the sound list, compressed together (lz.h)
//   22  2  table bytes
//   24     the packed bytes, then the table (scriptTableEncode)
#pragma once
#include <stdint.h>
#include <string>
#include "script.h"

namespace pt {

static const uint16_t SCRIPT_TABLE_VERSION = 2;  // 2: limits, pulses, settings as times, audio_loop off; raise with any change to scriptTableEncode or ScriptProgram's meaning
static const char SCRIPT_RECORD_MAGIC[4] = {'P', 'T', 'S', 'R'};
static const size_t SCRIPT_RECORD_HEADER = 24;

// The step table as bytes (independent of the struct's layout) and back. Decoding checks every index the runner
// uses (effects, steps, targets, inputs), so a damaged table is refused rather than run.
std::string scriptTableEncode(const ScriptProgram& p);
[[nodiscard]] bool scriptTableDecode(const uint8_t* b, size_t n, ScriptProgram& out);

// What a stored table was checked against. fw: the firmware's version and build.
uint32_t scriptTableKey(const ScriptBoard& board, const std::string& sounds_json, const std::string& fw);

// A record; table may be null (nothing stored: the text is checked at every start). Empty when the text
// is longer than SCRIPT_MAX_BYTES or the sound list's JSON longer than 65,535 bytes (it cannot be stored).
std::string scriptRecordEncode(const std::string& text, const std::string& sounds_json, const ScriptProgram* table,
                               uint32_t key);

enum class ScriptRecordKind : uint8_t {
  Current,
  Newer,       // a newer firmware's record: not loaded, never overwritten
  Unreadable,  // damaged, or not a script record
};
struct ScriptRecordRead {
  ScriptRecordKind kind = ScriptRecordKind::Unreadable;
  uint32_t version = 0;
  std::string text, sounds_json;
  bool has_table = false;  // a table that decoded and passed its checks is in `table`
  uint32_t key = 0;
};
// Read a record; `table` receives the stored table when there is one that decodes (has_table).
ScriptRecordRead scriptRecordDecode(const std::string& bytes, ScriptProgram& table);

}  // namespace pt
