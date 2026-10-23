#include "script_record.h"
#include <string.h>
#include "lz.h"
#include "settings.h"

namespace pt {

namespace {
void put8(std::string& b, uint32_t v) { b.push_back((char)(v & 0xFF)); }
void put16(std::string& b, uint32_t v) { put8(b, v); put8(b, v >> 8); }
void put32(std::string& b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t get32(const uint8_t* p) { return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16; }

const size_t EFFECT_BYTES = SCRIPT_MAX_NAME + 1 + 6;  // name (NUL padded), first, count, line
const size_t STEP_BYTES = 11;                          // ms, target, value, line, op
const size_t TABLE_HEAD = 6;                           // effects, steps, channels, inputs, dmx

uint32_t fnv1a(uint32_t h, const void* data, size_t n) {
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
}  // namespace

std::string scriptTableEncode(const ScriptProgram& p) {
  std::string b;
  b.reserve(TABLE_HEAD + p.effects * EFFECT_BYTES + p.steps * STEP_BYTES);
  put8(b, p.effects);
  put16(b, p.steps);
  put8(b, p.channels);
  put8(b, p.inputs);
  put8(b, p.dmx ? 1 : 0);
  for (int e = 0; e < p.effects; e++) {
    const ScriptEffect& f = p.fx[e];
    b.append(f.name, SCRIPT_MAX_NAME + 1);
    put16(b, f.first);
    put16(b, f.count);
    put16(b, f.line);
  }
  for (int s = 0; s < p.steps; s++) {
    const ScriptStep& st = p.step[s];
    put32(b, st.ms);
    put16(b, st.target);
    put16(b, st.value);
    put16(b, st.line);
    put8(b, (uint8_t)st.op);
  }
  return b;
}

bool scriptTableDecode(const uint8_t* b, size_t n, ScriptProgram& out) {
  out.reset();
  if (n < TABLE_HEAD) return false;
  uint8_t effects = b[0];
  uint16_t steps = get16(b + 1);
  if (effects == 0 || effects > SCRIPT_MAX_EFFECTS || steps > SCRIPT_MAX_LINES) return false;
  if (n != TABLE_HEAD + effects * EFFECT_BYTES + steps * STEP_BYTES) return false;
  if (b[4] >> INPUT_COUNT || b[5] > 1) return false;
  out.channels = b[3];
  out.inputs = b[4];
  out.dmx = b[5] == 1;
  const uint8_t* p = b + TABLE_HEAD;
  for (int e = 0; e < effects; e++, p += EFFECT_BYTES) {
    ScriptEffect& f = out.fx[e];
    if (p[SCRIPT_MAX_NAME] != 0) { out.reset(); return false; }  // the name ends inside its field
    memcpy(f.name, p, SCRIPT_MAX_NAME + 1);
    f.first = get16(p + SCRIPT_MAX_NAME + 1);
    f.count = get16(p + SCRIPT_MAX_NAME + 3);
    f.line = get16(p + SCRIPT_MAX_NAME + 5);
    if (f.count == 0 || f.count > SCRIPT_MAX_LINES_PER_EFFECT || (uint32_t)f.first + f.count > steps) { out.reset(); return false; }
  }
  for (int s = 0; s < steps; s++, p += STEP_BYTES) {
    ScriptStep& st = out.step[s];
    st.ms = get32(p);
    st.target = get16(p + 4);
    st.value = get16(p + 6);
    st.line = get16(p + 8);
    uint8_t op = p[10];
    if (op > (uint8_t)ScriptStep::Op::Stop) { out.reset(); return false; }
    st.op = (ScriptStep::Op)op;
    bool ok = true;
    switch (st.op) {
      case ScriptStep::Op::Wait: ok = st.target < INPUT_COUNT; break;
      case ScriptStep::Op::Set:
      case ScriptStep::Op::Flicker: ok = st.target < SCRIPT_DMX + DMX_CHANNELS; break;
      case ScriptStep::Op::Fade: ok = st.target < SCRIPT_DMX + DMX_CHANNELS && st.ms > 0; break;
      case ScriptStep::Op::Sleep:
      case ScriptStep::Op::Single:
      case ScriptStep::Op::Loop:
      case ScriptStep::Op::Stop: break;
    }
    if (!ok || st.ms > SCRIPT_MAX_TIME_MS) { out.reset(); return false; }
  }
  out.effects = effects;
  out.steps = steps;
  return true;
}

uint32_t scriptTableKey(const ScriptBoard& board, const std::string& sounds_json, const std::string& fw) {
  uint32_t h = 2166136261u;
  uint8_t head[2] = {(uint8_t)(SCRIPT_TABLE_VERSION & 0xFF), (uint8_t)(SCRIPT_TABLE_VERSION >> 8)};
  h = fnv1a(h, head, sizeof head);
  uint8_t bd[8] = {(uint8_t)board.dmx_on, board.roles[0], board.roles[1], board.roles[2], board.roles[3],
                   (uint8_t)board.buttons_are_triggers, board.effect_inputs, board.effect_channels};
  h = fnv1a(h, bd, sizeof bd);
  for (auto& n : board.names) {  // a renamed terminal: the text is checked again (no names: the key as before)
    h = fnv1a(h, &n.terminal, 1);
    h = fnv1a(h, n.name.data(), n.name.size());
    h = fnv1a(h, "\0", 1);
  }
  h = fnv1a(h, fw.data(), fw.size());
  h = fnv1a(h, "\0", 1);
  return fnv1a(h, sounds_json.data(), sounds_json.size());
}

std::string scriptRecordEncode(const std::string& text, const std::string& sounds_json, const ScriptProgram* table, uint32_t key) {
  if (text.size() > SCRIPT_MAX_BYTES || sounds_json.size() > 0xFFFF) return std::string();
  std::string packed = lzPack(text, sounds_json);  // as one piece, without a joined copy
  std::string tb = table ? scriptTableEncode(*table) : std::string();
  std::string b;
  b.reserve(SCRIPT_RECORD_HEADER + packed.size() + tb.size());
  b.append(SCRIPT_RECORD_MAGIC, 4);
  put16(b, SCRIPT_RECORD_VERSION);
  put16(b, table ? SCRIPT_TABLE_VERSION : 0);
  put32(b, table ? key : 0);
  put32(b, (uint32_t)text.size());
  put16(b, (uint32_t)sounds_json.size());
  put32(b, (uint32_t)packed.size());
  put16(b, (uint32_t)tb.size());
  b += packed;
  b += tb;
  return b;
}

ScriptRecordRead scriptRecordDecode(const std::string& bytes, ScriptProgram& table) {
  ScriptRecordRead r;
  table.reset();
  const uint8_t* b = (const uint8_t*)bytes.data();
  size_t n = bytes.size();
  if (n < SCRIPT_RECORD_HEADER || memcmp(b, SCRIPT_RECORD_MAGIC, 4) != 0) return r;
  r.version = get16(b + 4);
  if (r.version > SCRIPT_RECORD_VERSION) { r.kind = ScriptRecordKind::Newer; return r; }
  if (r.version != SCRIPT_RECORD_VERSION) return r;
  uint16_t table_version = get16(b + 6);
  uint32_t key = get32(b + 8), text_len = get32(b + 12), sounds_len = get16(b + 16), packed_len = get32(b + 18);
  uint16_t table_len = get16(b + 22);
  if (text_len > SCRIPT_MAX_BYTES || (size_t)packed_len + table_len != n - SCRIPT_RECORD_HEADER) return r;
  std::string all;
  if (!lzUnpack(b + SCRIPT_RECORD_HEADER, packed_len, (size_t)text_len + sounds_len, all)) return r;
  r.sounds_json = all.substr(text_len);
  all.resize(text_len);
  r.text.swap(all);
  r.kind = ScriptRecordKind::Current;
  // the table: only one of this format that decodes and passes its checks (anything else: check the text)
  if (table_version == SCRIPT_TABLE_VERSION && table_len > 0 &&
      scriptTableDecode(b + SCRIPT_RECORD_HEADER + packed_len, table_len, table)) {
    r.has_table = true;
    r.key = key;
  }
  return r;
}

}  // namespace pt
