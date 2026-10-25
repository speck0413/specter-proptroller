#include "pattern_vm.h"
// the interpreter is the strip's hot loop: optimized for speed on the boards (whose default is for size)
#if defined(__GNUC__) && !defined(__clang__) && (defined(ESP32) || defined(ARDUINO_ARCH_RP2040))
#pragma GCC optimize("O2")
#endif
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include "pattern_math.h"

#if PT_PATTERNS
namespace pt {

using namespace pmath;

namespace {

// The opcodes, as Studio's compile.ts numbers them (they only ever grow).
enum Op : int32_t {
  CONST = 1, LOADG, STOREG, LOADL, STOREL, LOADF, POP, DUP, DUP2,
  ADD, SUB, MUL, DIV, MOD, NEG, BAND, BOR, BXOR, BNOT, SHL, SHR, USHR,
  EQ, NE, LT, LE, GT, GE, NOT,
  JMP, JZ, ANDJ, ORJ, CALL, CALLV, CALLB, RET, IDXGET, IDXSET,
  OP_END
};
// The built-ins, as Studio's builtins.ts numbers them (only ever added to at the end).
enum Builtin : int32_t {
  B_TIME, B_WAVE, B_TRIANGLE, B_SQUARE, B_MIX, B_SMOOTHSTEP, B_CLAMP, B_SIN, B_COS, B_TAN, B_ASIN, B_ACOS, B_ATAN,
  B_ATAN2, B_ABS, B_FLOOR, B_CEIL, B_ROUND, B_TRUNC, B_FRAC, B_MIN, B_MAX, B_POW, B_SQRT, B_EXP, B_LOG, B_LOG2,
  B_HYPOT, B_MOD, B_RANDOM, B_PRNG, B_PRNG_SEED, B_HSV, B_HSV24, B_RGB, B_ARRAY, B_FOR_EACH, B_MAP_TO, B_MUTATE,
  B_REDUCE, B_SUM, B_SORT, B_LENGTH, B_LIST,
  B_COUNT
};
int opArgs(int32_t op) {
  switch (op) {
    case CONST: case LOADG: case STOREG: case LOADL: case STOREL: case LOADF: case JMP: case JZ: case ANDJ: case ORJ: case CALLV: return 1;
    case CALL: case CALLB: return 2;
    default: return 0;
  }
}
const int32_t CHUNK = 8192;
const int32_t MAX_CALL_VALUES = 255;

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
int32_t rd32(const uint8_t* p) { return (int32_t)((uint32_t)rd16(p) | (uint32_t)rd16(p + 2) << 16); }

const char* const M_TOO_LONG = "the pattern took too long on one frame and was stopped (a loop that never ends?)";
const char* const M_ROOM = "the pattern ran out of room for its values";
const char* const M_LIST_NUM = "a list cannot be used as a number here";
const char* const M_FUNC_NUM = "a function cannot be used as a number here";
const char* const M_NEEDS_LIST = "this needs a list (make one with array(n) or [1, 2, 3])";
const char* const M_NOT_FUNC = "this is not a function, so it cannot be called";
const char* const M_NEEDS_FUNC = "this needs a function, for example (v, i) => v * 2";
const char* const M_MEMORY = "the board has not enough memory left for this pattern";

inline bool zero(const PatternVal& x) { return x.t == 0 && x.v == 0; }
inline bool same(const PatternVal& x, const PatternVal& y) { return x.t == y.t && x.v == y.v; }
inline PatternVal numv(int32_t v) { PatternVal r; r.v = v; return r; }
}  // namespace

const char* patternControlKindName(PatternControlKind k) {
  static const char* const N[] = {"slider", "hsvPicker", "rgbPicker", "toggle", "trigger", "inputNumber"};
  return (uint8_t)k < 6 ? N[(uint8_t)k] : "slider";
}

bool base64Decode(const char* in, size_t n, std::string& out) {
  out.clear();
  out.reserve(n / 4 * 3);
  uint32_t acc = 0;
  int bits = 0;
  size_t pad = 0;
  for (size_t i = 0; i < n; i++) {
    char c = in[i];
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else if (c == '=') { pad++; continue; }
    else return false;
    if (pad) return false;  // nothing after the padding
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((char)((acc >> bits) & 0xFF));
    }
  }
  return pad <= 2 && (n % 4 == 0 || pad == 0);
}

std::string patternWords(const std::string& s) {
  std::string o;
  bool space = false;
  for (char c : s) {
    if (c == ' ' || c == '\t') { space = !o.empty(); continue; }
    if (space) o += ' ';
    space = false;
    o += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }
  return o;
}

// ------------------------------------------------------------------ the program

bool PatternProgram::decode(const uint8_t* b, size_t n, std::string& why) {
  code.clear(); fns.clear(); controls.clear();
  const char* const DAMAGED = "the pattern's bytes are damaged or incomplete: send it again from Studio";
  if (n < 22 || memcmp(b, "SPAT", 4) != 0) { why = "these are not pattern bytes (send the pattern from Studio)"; return false; }
  if (b[4] != PATTERN_FORMAT) { why = "the pattern was made by a newer Studio (format " + std::to_string(b[4]) + "): update the board's firmware"; return false; }
  uint16_t ncode = rd16(b + 6), nfn = rd16(b + 10);
  globals = rd16(b + 8);
  uint8_t nctl = b[12];
  if (ncode > PATTERN_MAX_CODE) { why = "the pattern is too big for this board (" + std::to_string(ncode) + " code steps, at most " + std::to_string(PATTERN_MAX_CODE) + ")"; return false; }
  if (globals > PATTERN_MAX_GLOBALS) { why = "the pattern has too many variables for this board (" + std::to_string(globals) + ", at most " + std::to_string(PATTERN_MAX_GLOBALS) + ")"; return false; }
  if (nfn > PATTERN_MAX_FUNCTIONS) { why = "the pattern has too many functions for this board (" + std::to_string(nfn) + ", at most " + std::to_string(PATTERN_MAX_FUNCTIONS) + ")"; return false; }
  if (nctl > PATTERN_MAX_CONTROLS) { why = "the pattern has too many controls for this board (" + std::to_string(nctl) + ", at most " + std::to_string(PATTERN_MAX_CONTROLS) + ")"; return false; }
  int32_t* entries[4] = {&before_render, &render, &render2d, &render3d};
  for (int i = 0; i < 4; i++) {
    uint16_t e = rd16(b + 14 + 2 * i);
    *entries[i] = e == 0xffff ? -1 : e;
  }
  size_t at = 22;
  if (n < at + 4u * nfn) { why = DAMAGED; return false; }
  fns.resize(nfn);
  for (auto& f : fns) {
    f.addr = rd16(b + at);
    f.params = b[at + 2];
    f.locals = b[at + 3];
    at += 4;
  }
  controls.resize(nctl);
  for (auto& c : controls) {
    if (n < at + 4) { why = DAMAGED; return false; }
    if (b[at] > (uint8_t)PatternControlKind::InputNumber) { why = DAMAGED; return false; }
    c.kind = (PatternControlKind)b[at];
    c.fn = rd16(b + at + 1);
    at += 3;
    for (std::string* s : {&c.name, &c.label}) {
      if (n < at + 1 || b[at] > PATTERN_MAX_TEXT || n < at + 1 + b[at]) { why = DAMAGED; return false; }
      s->assign((const char*)b + at + 1, b[at]);
      for (char ch : *s)
        if (ch < 0x20 || ch > 0x7e) { why = DAMAGED; return false; }
      at += 1 + b[at];
    }
  }
  if (n != at + 4u * ncode) { why = DAMAGED; return false; }
  code.resize(ncode);
  for (size_t i = 0; i < ncode; i++) code[i] = rd32(b + at + 4 * i);
  if (!verify(why)) {
    code.clear(); fns.clear(); controls.clear();
    return false;
  }
  return true;
}

// Every function's code: known steps only, jumps to the start of a step in the same function, every index in
// range, and one stack height at every step that never goes below what the step takes; the last step returns.
bool PatternProgram::verify(std::string& why) {
  const char* const DAMAGED = "the pattern's program is damaged: send it again from Studio";
  why = DAMAGED;
  if (globals < 1 || fns.empty() || code.empty()) return false;
  if (render < 0 && render2d < 0 && render3d < 0) { why = "the pattern has no render function"; return false; }
  for (int32_t e : {before_render, render, render2d, render3d})
    if (e >= (int32_t)fns.size()) return false;
  for (auto& c : controls)
    if (c.fn >= fns.size()) return false;
  // the functions' ranges: their starts sorted, the first at 0
  std::vector<uint16_t> order(fns.size());
  for (size_t i = 0; i < fns.size(); i++) order[i] = (uint16_t)i;
  std::sort(order.begin(), order.end(), [&](uint16_t a, uint16_t b) { return fns[a].addr < fns[b].addr; });
  if (fns[order[0]].addr != 0) return false;
  for (size_t k = 0; k < order.size(); k++) {
    PatternFunction& f = fns[order[k]];
    f.end = k + 1 < order.size() ? fns[order[k + 1]].addr : (uint16_t)code.size();
    if (f.end <= f.addr || f.params > f.locals) return false;
  }
  std::vector<uint8_t> starts(code.size(), 0);
  std::vector<int16_t> height(code.size(), -1);
  std::vector<uint16_t> work;
  for (auto& f : fns) {
    // where each step starts
    uint32_t last = f.addr;
    for (uint32_t pc = f.addr; pc < f.end;) {
      int32_t op = code[pc];
      if (op < CONST || op >= OP_END) return false;
      starts[pc] = 1;
      last = pc;
      pc += 1 + opArgs(op);
      if (pc > f.end) return false;
    }
    if (code[last] != RET) return false;
    // the stack height at every step reached from the start
    work.clear();
    height[f.addr] = 0;
    work.push_back(f.addr);
    while (!work.empty()) {
      uint32_t pc = work.back();
      work.pop_back();
      int32_t h = height[pc], op = code[pc];
      int32_t x = opArgs(op) >= 1 ? code[pc + 1] : 0, y = opArgs(op) >= 2 ? code[pc + 2] : 0;
      int32_t need = 0, next_h = h;
      bool falls = true;
      int32_t target = -1, target_h = 0;
      switch (op) {
        case CONST: next_h = h + 1; break;
        case LOADG: if (x < 0 || x >= globals) return false; next_h = h + 1; break;
        case STOREG: if (x < 0 || x >= globals) return false; need = 1; next_h = h - 1; break;
        case LOADL: if (x < 0 || x >= f.locals) return false; next_h = h + 1; break;
        case STOREL: if (x < 0 || x >= f.locals) return false; need = 1; next_h = h - 1; break;
        case LOADF: if (x < 0 || x >= (int32_t)fns.size()) return false; next_h = h + 1; break;
        case POP: need = 1; next_h = h - 1; break;
        case DUP: need = 1; next_h = h + 1; break;
        case DUP2: need = 2; next_h = h + 2; break;
        case NEG: case BNOT: case NOT: need = 1; break;
        case JMP: target = x; target_h = h; falls = false; break;
        case JZ: need = 1; next_h = h - 1; target = x; target_h = h - 1; break;
        case ANDJ: case ORJ: need = 1; next_h = h - 1; target = x; target_h = h; break;
        case CALL:
          if (x < 0 || x >= (int32_t)fns.size() || y < 0 || y > MAX_CALL_VALUES) return false;
          need = y; next_h = h - y + 1; break;
        case CALLV: if (x < 0 || x > MAX_CALL_VALUES) return false; need = x + 1; next_h = h - x; break;
        case CALLB:
          if (x < 0 || x >= B_COUNT || y < 0 || y > MAX_CALL_VALUES) return false;
          need = y; next_h = h - y + 1; break;
        case RET: need = 1; falls = false; break;
        case IDXGET: need = 2; next_h = h - 1; break;
        case IDXSET: need = 3; next_h = h - 2; break;
        default: need = 2; next_h = h - 1; break;  // the operators that take two values
      }
      if (h < need || next_h > 4 * PATTERN_MAX_STACK) return false;
      auto reach = [&](int32_t to, int32_t th) {
        if (to < f.addr || to >= f.end || !starts[to]) return false;
        if (height[to] < 0) { height[to] = (int16_t)th; work.push_back((uint16_t)to); return true; }
        return height[to] == th;
      };
      if (target >= 0 || op == JMP || op == JZ || op == ANDJ || op == ORJ)
        if (!reach(target, target_h)) return false;
      if (falls && !reach((int32_t)(pc + 1 + opArgs(op)), next_h)) return false;
    }
  }
  why.clear();
  return true;
}

int PatternProgram::findButton(const std::string& words) const {
  std::string w = patternWords(words);
  for (size_t i = 0; i < controls.size(); i++)
    if (controls[i].kind == PatternControlKind::Trigger && patternWords(controls[i].label) == w) return (int)i;
  return -1;
}

std::string PatternProgram::buttonList() const {
  std::vector<std::string> b;
  for (auto& c : controls)
    if (c.kind == PatternControlKind::Trigger) b.push_back(patternWords(c.label));
  std::string o;
  for (size_t i = 0; i < b.size(); i++) o += (i == 0 ? "" : i + 1 == b.size() ? " and " : ", ") + b[i];
  return o;
}

int PatternProgram::findControl(const std::string& name) const {
  for (size_t i = 0; i < controls.size(); i++)
    if (controls[i].name == name) return (int)i;
  return -1;
}

size_t PatternProgram::bytes() const {
  size_t n = sizeof(*this) + code.capacity() * 4 + fns.capacity() * sizeof(PatternFunction);
  for (auto& c : controls) n += sizeof(c) + c.name.capacity() + c.label.capacity();
  return n;
}

// ------------------------------------------------------------------ the interpreter

PatternVM::PatternVM(const PatternProgram& prog, uint32_t (*random16)(void*), void* random_ctx, uint32_t (*us)(), uint32_t frame_us)
    : p_(prog), random16_(random16), random_ctx_(random_ctx), us_(us), frame_us_(frame_us) {
  G_ = (PatternVal*)calloc(prog.globals, sizeof(PatternVal));
  S_ = (PatternVal*)calloc(PATTERN_MAX_STACK + 4, sizeof(PatternVal));
  L_ = (PatternVal*)calloc(PATTERN_MAX_LOCALS, sizeof(PatternVal));
  if (!G_ || !S_ || !L_) err_ = M_MEMORY;
}

PatternVM::~PatternVM() {
  free(G_);
  free(S_);
  free(L_);
  free(cells_);
}

size_t PatternVM::bytes() const {
  return sizeof(*this) + (p_.globals + PATTERN_MAX_STACK + 4 + PATTERN_MAX_LOCALS + (cells_ ? PATTERN_MAX_CELLS : 0)) * sizeof(PatternVal);
}

void PatternVM::setPixelCount(uint16_t n) {
  pixels_ = n < 1 ? 1 : n > PATTERN_MAX_PIXELS ? PATTERN_MAX_PIXELS : n;
  if (G_) G_[0] = numv((int32_t)pixels_ << 16);
}

void PatternVM::fail(const char* msg) {
  if (err_.empty()) err_ = msg;
}

bool PatternVM::timeUp() const { return us_ && (int32_t)(us_() - deadline_) > 0; }

void PatternVM::start() {
  left_ = CHUNK;
  spent_ = 0;
  sp_ = 0;
  depth_ = 0;
  locals_used_ = 0;
  if (us_) deadline_ = us_() + frame_us_;
}

void PatternVM::endCall() {
  uint32_t steps = spent_ + (uint32_t)(CHUNK - left_);
  if (steps > max_steps_) max_steps_ = steps;
}

bool PatternVM::init() {
  if (failed()) return false;
  G_[0] = numv((int32_t)pixels_ << 16);
  start();
  exec(0, nullptr, 0);
  endCall();
  return !failed();
}

bool PatternVM::frame(uint32_t delta_ms, Rgb* out) {
  if (failed()) return false;
  clock_ += delta_ms;
  G_[0] = numv((int32_t)pixels_ << 16);
  start();
  if (p_.before_render >= 0) {
    PatternVal d = numv((int32_t)(delta_ms < 32767 ? delta_ms : 32767) << 16);
    exec((uint16_t)p_.before_render, &d, 1);
    if (failed()) return false;
  }
  int32_t n = pixels_;
  for (int32_t i = 0; i < n; i++) {
    r_ = g_ = b_ = 0;
    PatternVal a[4];
    a[0] = numv(i << 16);
    if (p_.render >= 0) exec((uint16_t)p_.render, a, 1);
    else {
      // i / n as 16.16, halves up (n is at most 4096, so 32 bits hold it)
      a[1] = numv((int32_t)(((uint32_t)i * 2 * ONE + (uint32_t)n) / (uint32_t)(2 * n)));
      if (p_.render2d >= 0) exec((uint16_t)p_.render2d, a, 3);
      else exec((uint16_t)p_.render3d, a, 4);
    }
    if (failed()) return false;
    out[i] = Rgb(channel(r_), channel(g_), channel(b_));
  }
  endCall();
  return true;
}

bool PatternVM::callControl(int index, const int32_t* values, size_t n) {
  if (failed()) return false;
  if (index < 0 || index >= (int)p_.controls.size()) return true;
  PatternVal a[8];
  int k = n > 8 ? 8 : (int)n;
  for (int i = 0; i < k; i++) a[i] = numv(values[i]);
  start();
  exec(p_.controls[(size_t)index].fn, a, k);
  endCall();
  return !failed();
}

bool PatternVM::setControl(const std::string& name, const int32_t* values, size_t n) {
  return callControl(p_.findControl(name), values, n);
}

void PatternVM::notNum(const PatternVal& x) { fail(x.t == 1 ? M_LIST_NUM : M_FUNC_NUM); }
void PatternVM::notList() { fail(M_NEEDS_LIST); }

bool PatternVM::newList(int32_t len, int32_t& at) {
  if (cells_used_ + len + 1 > PATTERN_MAX_CELLS) {
    fail("the pattern's lists are too big (at most " + std::to_string(PATTERN_MAX_CELLS) + " values in all: make lists once, at the top of the pattern)");
    return false;
  }
  if (!cells_) {
    cells_ = (PatternVal*)calloc(PATTERN_MAX_CELLS, sizeof(PatternVal));
    if (!cells_) { fail(M_MEMORY); return false; }
  }
  at = cells_used_;
  cells_[at] = numv(len);
  for (int32_t i = 1; i <= len; i++) cells_[at + i] = PatternVal();
  cells_used_ += len + 1;
  return true;
}

PatternVal PatternVM::call(const PatternVal& f, const PatternVal* args, int n, uint32_t pc) {
  (void)pc;
  if (f.t != 2) { fail(M_NEEDS_FUNC); return PatternVal(); }
  return exec((uint16_t)f.v, args, n);
}

#define PT_NUM(x, out) \
  int32_t out;         \
  if (!num((x), out)) return PatternVal()

PatternVal PatternVM::exec(uint16_t fi, const PatternVal* args, int nargs) {
  const PatternFunction& fn = p_.fns[fi];
  if (++depth_ > PATTERN_MAX_DEPTH) {
    depth_ = 0;
    fail("functions call each other too deeply (more than " + std::to_string(PATTERN_MAX_DEPTH) + " at once)");
    return PatternVal();
  }
  if (locals_used_ + fn.locals > PATTERN_MAX_LOCALS) { fail(M_ROOM); return PatternVal(); }
  PatternVal* L = L_ + locals_used_;
  locals_used_ += fn.locals;
  for (int i = 0; i < fn.locals; i++) L[i] = i < fn.params && i < nargs ? args[i] : PatternVal();
  const int32_t* code = p_.code.data();
  PatternVal* S = S_;
  PatternVal* G = G_;
  int sp = sp_;
  const int base = sp;
  uint32_t pc = fn.addr;
  int32_t left = left_;  // the steps left in this chunk, kept here and handed back around every call
  for (;;) {
    if (--left <= 0) {
      spent_ += CHUNK;
      left = CHUNK;
      if (spent_ >= PATTERN_FRAME_STEPS || timeUp()) { fail(M_TOO_LONG); return PatternVal(); }
    }
    if (sp > PATTERN_MAX_STACK) { fail(M_ROOM); return PatternVal(); }
    const uint32_t at = pc;
    switch (code[pc++]) {
      case CONST: S[sp++] = numv(code[pc++]); break;
      case LOADG: S[sp++] = G[code[pc++]]; break;
      case STOREG: G[code[pc++]] = S[--sp]; break;
      case LOADL: S[sp++] = L[code[pc++]]; break;
      case STOREL: L[code[pc++]] = S[--sp]; break;
      case LOADF: { PatternVal f; f.t = 2; f.v = code[pc++]; S[sp++] = f; break; }
      case POP: sp--; break;
      case DUP: S[sp] = S[sp - 1]; sp++; break;
      case DUP2: S[sp] = S[sp - 2]; S[sp + 1] = S[sp - 1]; sp += 2; break;
#define PT_BIN(OPC, EXPR)                     \
  case OPC: {                                 \
    sp--;                                     \
    PT_NUM(S[sp - 1], a);                     \
    PT_NUM(S[sp], b);                         \
    S[sp - 1] = numv(EXPR);                   \
    break;                                    \
  }
      PT_BIN(ADD, add(a, b))
      PT_BIN(SUB, sub(a, b))
      PT_BIN(MUL, fmul(a, b))
      PT_BIN(DIV, fdiv(a, b))
      PT_BIN(MOD, (b == 0 || b == -1) ? 0 : a % b)
      PT_BIN(BAND, a & b)
      PT_BIN(BOR, a | b)
      PT_BIN(BXOR, a ^ b)
      PT_BIN(SHL, (int32_t)((uint32_t)a << ((b >> 16) & 31)))
      PT_BIN(SHR, a >> ((b >> 16) & 31))
      PT_BIN(USHR, (int32_t)((uint32_t)a >> ((b >> 16) & 31)))
      PT_BIN(LT, a < b ? ONE : 0)
      PT_BIN(LE, a <= b ? ONE : 0)
      PT_BIN(GT, a > b ? ONE : 0)
      PT_BIN(GE, a >= b ? ONE : 0)
#undef PT_BIN
      case NEG: { PT_NUM(S[sp - 1], a); S[sp - 1] = numv(neg(a)); break; }
      case BNOT: { PT_NUM(S[sp - 1], a); S[sp - 1] = numv(~a); break; }
      case EQ: sp--; S[sp - 1] = numv(same(S[sp - 1], S[sp]) ? ONE : 0); break;
      case NE: sp--; S[sp - 1] = numv(same(S[sp - 1], S[sp]) ? 0 : ONE); break;
      case NOT: S[sp - 1] = numv(zero(S[sp - 1]) ? ONE : 0); break;
      case JMP: pc = (uint32_t)code[pc]; break;
      case JZ: if (zero(S[--sp])) pc = (uint32_t)code[pc]; else pc++; break;
      case ANDJ: if (zero(S[sp - 1])) pc = (uint32_t)code[pc]; else { sp--; pc++; } break;
      case ORJ: if (!zero(S[sp - 1])) pc = (uint32_t)code[pc]; else { sp--; pc++; } break;
      case CALL: {
        int32_t id = code[pc++], n = code[pc++];
        sp -= n;
        sp_ = sp;
        left_ = left;
        PatternVal r = exec((uint16_t)id, S + sp, n);
        left = left_;
        if (failed()) return PatternVal();
        S[sp++] = r;
        break;
      }
      case CALLV: {
        int32_t n = code[pc++];
        sp -= n;
        PatternVal c = S[sp - 1];
        if (c.t != 2) { fail(M_NOT_FUNC); return PatternVal(); }
        // the values move down over the function they were given to (as Studio's copy of them does)
        for (int32_t i = 0; i < n; i++) S[sp - 1 + i] = S[sp + i];
        sp--;
        sp_ = sp;
        left_ = left;
        PatternVal r = exec((uint16_t)c.v, S + sp, n);
        left = left_;
        if (failed()) return PatternVal();
        S[sp++] = r;
        break;
      }
      case CALLB: {
        int32_t id = code[pc++], n = code[pc++];
        sp -= n;
        sp_ = sp;
        PatternVal r;
        if (id == B_LIST) {
          int32_t l;
          if (!newList(n, l)) return PatternVal();
          for (int32_t i = 0; i < n; i++) cells_[l + 1 + i] = S[sp + i];
          r.t = 1;
          r.v = l;
        } else {
          PatternVal a[3];
          for (int32_t i = 0; i < 3 && i < n; i++) a[i] = S[sp + i];
          left_ = left;
          r = builtin(id, a, n < 3 ? n : 3, at);
          left = left_;
          if (failed()) return PatternVal();
        }
        S[sp++] = r;
        break;
      }
      case RET: {
        PatternVal v = S[sp - 1];
        left_ = left;
        sp_ = base;
        depth_--;
        locals_used_ -= fn.locals;
        return v;
      }
      case IDXGET: {
        sp--;
        int32_t l, i;
        if (!list(S[sp - 1], l) || !num(S[sp], i)) return PatternVal();
        i >>= 16;
        S[sp - 1] = i >= 0 && i < cells_[l].v ? cells_[l + 1 + i] : PatternVal();
        break;
      }
      case IDXSET: {
        sp -= 2;
        int32_t l, i;
        if (!list(S[sp - 1], l) || !num(S[sp], i)) return PatternVal();
        i >>= 16;
        PatternVal v = S[sp + 1];
        if (i >= 0 && i < cells_[l].v) cells_[l + 1 + i] = v;
        S[sp - 1] = v;
        break;
      }
      default: fail("the compiled pattern is damaged"); return PatternVal();
    }
  }
}

void PatternVM::sortList(int32_t at) {
  int32_t n = cells_[at].v;
  PatternVal* v = cells_ + at + 1;
  auto key = [](const PatternVal& x) { return x.t == 0 ? x.v : 0; };
  // stable: a merge sort in the free list values when they have room, else insertion (both give the one stable order)
  if (cells_used_ + n <= PATTERN_MAX_CELLS) {
    PatternVal* tmp = cells_ + cells_used_;
    for (int32_t w = 1; w < n; w *= 2) {
      for (int32_t lo = 0; lo < n - w; lo += 2 * w) {
        int32_t mid = lo + w, hi = std::min(lo + 2 * w, n), i = lo, j = mid, k = 0;
        while (i < mid && j < hi) tmp[k++] = key(v[j]) < key(v[i]) ? v[j++] : v[i++];
        while (i < mid) tmp[k++] = v[i++];
        while (j < hi) tmp[k++] = v[j++];
        for (int32_t m = 0; m < k; m++) v[lo + m] = tmp[m];
      }
    }
    return;
  }
  for (int32_t i = 1; i < n; i++) {
    PatternVal x = v[i];
    int32_t j = i - 1;
    while (j >= 0 && key(v[j]) > key(x)) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
}

PatternVal PatternVM::builtin(int32_t id, const PatternVal* a, int n, uint32_t pc) {
  int32_t x[3] = {0, 0, 0};
  // the numbers a built-in reads (a missing one is 0)
  auto nums = [&](int k) {
    for (int i = 0; i < k; i++)
      if (i < n && !num(a[i], x[i])) return false;
    return true;
  };
  PatternVal one;
  switch (id) {
    case B_TIME: {
      if (!nums(1)) return PatternVal();
      if (x[0] <= 0) return numv(0);
      // the same sums in 32 bits while they fit (a 64-bit division is slow on the boards)
      uint32_t p = (uint32_t)x[0];
      uint32_t r = clock_ < 0xFFFFFFFFull ? (uint32_t)clock_ % p : (uint32_t)(clock_ % p);
      if (r < 16000 && p < 0x40000000u) return numv((int32_t)((r * 2 * (uint32_t)ONE + p) / (2 * p)) & 0xffff);
      return numv((int32_t)(((int64_t)2 * r * ONE + p) / (2 * (int64_t)p)) & 0xffff);
    }
    case B_WAVE: if (!nums(1)) break; return numv(fwave(x[0]));
    case B_TRIANGLE: { if (!nums(1)) break; int32_t t = x[0] & 0xffff; return numv(t < 0x8000 ? t * 2 : (0x10000 - t) * 2); }
    case B_SQUARE: if (!nums(2)) break; return numv((x[0] & 0xffff) < x[1] ? ONE : 0);
    case B_MIX: if (!nums(3)) break; return numv(add(x[0], fmul(sub(x[1], x[0]), x[2])));
    case B_SMOOTHSTEP: if (!nums(3)) break; return numv(smoothstep(x[0], x[1], x[2]));
    case B_CLAMP: if (!nums(3)) break; return numv(x[0] < x[1] ? x[1] : x[0] > x[2] ? x[2] : x[0]);
    case B_SIN: if (!nums(1)) break; return numv(fsin(x[0]));
    case B_COS: if (!nums(1)) break; return numv(fcos(x[0]));
    case B_TAN: if (!nums(1)) break; return numv(ftan(x[0]));
    case B_ASIN: if (!nums(1)) break; return numv(fasin(x[0]));
    case B_ACOS: if (!nums(1)) break; return numv(facos(x[0]));
    case B_ATAN: if (!nums(1)) break; return numv(fatan(x[0]));
    case B_ATAN2: if (!nums(2)) break; return numv(fatan2(x[0], x[1]));
    case B_ABS: if (!nums(1)) break; return numv(x[0] < 0 ? neg(x[0]) : x[0]);
    case B_FLOOR: if (!nums(1)) break; return numv(x[0] & (int32_t)0xffff0000);
    case B_CEIL: if (!nums(1)) break; return numv(neg(neg(x[0]) & (int32_t)0xffff0000));
    case B_ROUND: if (!nums(1)) break; return numv(add(x[0], 0x8000) & (int32_t)0xffff0000);
    case B_TRUNC: if (!nums(1)) break; return numv(x[0] < 0 ? neg(neg(x[0]) & (int32_t)0xffff0000) : x[0] & (int32_t)0xffff0000);
    case B_FRAC: if (!nums(1)) break; return numv(x[0] < 0 ? neg(neg(x[0]) & 0xffff) : x[0] & 0xffff);
    case B_MIN: if (!nums(2)) break; return numv(x[0] < x[1] ? x[0] : x[1]);
    case B_MAX: if (!nums(2)) break; return numv(x[0] > x[1] ? x[0] : x[1]);
    case B_POW: if (!nums(2)) break; return numv(fpow(x[0], x[1]));
    case B_SQRT: if (!nums(1)) break; return numv(fsqrt(x[0]));
    case B_EXP: if (!nums(1)) break; return numv(fexp(x[0]));
    case B_LOG: if (!nums(1)) break; return numv(flog(x[0]));
    case B_LOG2: if (!nums(1)) break; return numv(flog2(x[0]));
    case B_HYPOT: if (!nums(2)) break; return numv(fhypot(x[0], x[1]));
    case B_MOD: {
      if (!nums(2)) break;
      if (x[1] == 0 || x[1] == -1) return numv(0);
      int32_t r = x[0] % x[1];
      if (r != 0 && (r < 0) != (x[1] < 0)) r = add(r, x[1]);
      return numv(r);
    }
    case B_RANDOM: if (!nums(1)) break; return numv(fmul((int32_t)(random16_(random_ctx_) & 0xffff), x[0]));
    case B_PRNG: {
      if (!nums(1)) break;
      uint32_t s = (uint32_t)seed_;
      s ^= s << 13; s ^= s >> 17; s ^= s << 5;
      seed_ = (int32_t)s;
      return numv(fmul((int32_t)(s >> 16), x[0]));
    }
    case B_PRNG_SEED: if (!nums(1)) break; seed_ = x[0] != 0 ? x[0] : 0x2545f491; return numv(0);
    case B_HSV: case B_HSV24: if (!nums(3)) break; hsv(x[0], x[1], x[2], r_, g_, b_); return numv(0);
    case B_RGB: {
      if (!nums(3)) break;
      auto c = [](int32_t v) { return v < 0 ? 0 : v > ONE ? ONE : v; };
      r_ = c(x[0]); g_ = c(x[1]); b_ = c(x[2]);
      return numv(0);
    }
    case B_ARRAY: {
      if (!nums(1)) break;
      int32_t l, len = x[0] >> 16;
      if (!newList(len < 0 ? 0 : len, l)) break;
      PatternVal r; r.t = 1; r.v = l;
      return r;
    }
    case B_LENGTH: { int32_t l; if (!list(n > 0 ? a[0] : one, l)) break; return numv(cells_[l].v << 16); }
    case B_FOR_EACH: case B_MUTATE: {
      int32_t l;
      if (!list(n > 0 ? a[0] : one, l)) break;
      PatternVal f = n > 1 ? a[1] : one, lv = a[0];
      for (int32_t i = 0; i < cells_[l].v; i++) {
        PatternVal args[3] = {cells_[l + 1 + i], numv(i << 16), lv};
        PatternVal r = call(f, args, 3, pc);
        if (failed()) return PatternVal();
        if (id == B_MUTATE) cells_[l + 1 + i] = r;
      }
      return numv(0);
    }
    case B_MAP_TO: {
      int32_t s, d;
      if (!list(n > 0 ? a[0] : one, s) || !list(n > 1 ? a[1] : one, d)) break;
      PatternVal f = n > 2 ? a[2] : one, sv = a[0];
      for (int32_t i = 0; i < cells_[s].v && i < cells_[d].v; i++) {
        PatternVal args[3] = {cells_[s + 1 + i], numv(i << 16), sv};
        PatternVal r = call(f, args, 3, pc);
        if (failed()) return PatternVal();
        cells_[d + 1 + i] = r;
      }
      return numv(0);
    }
    case B_REDUCE: {
      int32_t l;
      if (!list(n > 0 ? a[0] : one, l)) break;
      PatternVal f = n > 1 ? a[1] : one, acc = n > 2 ? a[2] : one, lv = a[0];
      for (int32_t i = 0; i < cells_[l].v; i++) {
        PatternVal args[4] = {acc, cells_[l + 1 + i], numv(i << 16), lv};
        acc = call(f, args, 4, pc);
        if (failed()) return PatternVal();
      }
      return acc;
    }
    case B_SUM: {
      int32_t l;
      if (!list(n > 0 ? a[0] : one, l)) break;
      int32_t s = 0;
      for (int32_t i = 0; i < cells_[l].v; i++)
        if (cells_[l + 1 + i].t == 0) s = add(s, cells_[l + 1 + i].v);
      return numv(s);
    }
    case B_SORT: { int32_t l; if (!list(n > 0 ? a[0] : one, l)) break; sortList(l); return numv(0); }
    default: fail("the compiled pattern is damaged"); break;
  }
  return PatternVal();
}

}  // namespace pt
#endif  // PT_PATTERNS
