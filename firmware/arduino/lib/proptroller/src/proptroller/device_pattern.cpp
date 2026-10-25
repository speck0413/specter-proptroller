// LED patterns on the controller (PROTOCOL.md "LED patterns", SHOW-SCRIPT.md "The pixel strip"): the stored
// patterns and their record, the protocol ops, and the player.
//
// Who does what, so the 1 ms samples and the 10 ms step never wait for a pattern:
//   - requests (with the lock) keep the list of stored patterns (pattern_store.h) and mark the record to write;
//   - the step (with the lock) only picks what the strip shows: a stored pattern as an Effect naming it (its
//     place and its number), a play number that changes when it starts again, and the script's button presses,
//     put in a queue for the pixel side; it never runs pattern code;
//   - the loop (background(), with the lock) hands the pixel side its own copy of the program the strip should
//     play (a mailbox, one pointer exchanged: whoever holds a copy owns it), writes the record when the board is
//     quiet, and logs what the pixel side said;
//   - the pixel side (pixelService: a task on core 0 on the ESP32, loop() on the Pico 2 W, after each tick in the
//     simulator; no lock) runs the interpreter with a step budget and a deadline per frame. A pattern that fails
//     or overruns stops and the strip goes dark; the reason goes to the event log through a note.
#include <stdio.h>
#include <string.h>
#include <atomic>
#include "device.h"
#include "pattern_vm.h"
#if PT_HOST
#include <chrono>
#endif

namespace pt {

#if PT_PATTERNS

namespace {
const char* const NO_PLAYER = "this board has no pattern player: the Pico W and the Pico have no room for it (an ESP32 or a Pico 2 W has one)";
// The deadline of one frame (or one button press), as Studio's preview's. The step budget (PATTERN_FRAME_STEPS)
// stops a loop that never ends, the same way everywhere; this catches only what steps do not count (a long sort)
// and is far above a real frame (a heavy pattern on 600 lights: about 33 ms in QEMU, arduino/QEMU.md), so a flash
// write that stalls the core for a moment never stops a pattern. A long frame is followed by as long a rest.
#ifndef PT_PATTERN_FRAME_US
#define PT_PATTERN_FRAME_US 250000
#endif
const uint32_t PATTERN_FRAME_US = PT_PATTERN_FRAME_US;
const uint32_t PRESS_RING = 32;          // presses on their way to the pixel side
const size_t NOTE_BYTES = 240;

// what the loop hands the pixel side: its own copy of everything the player needs
struct PatternLoad {
  uint32_t gen = 0;
  std::string name;
  PatternProgram prog;
  std::vector<PatternValues> controls;
};

uint32_t xorshift(void* ctx) {
  uint32_t& x = *(uint32_t*)ctx;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return x >> 16;
}

#if PT_HOST
uint32_t hostMicros() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif
}  // namespace

struct PatternSide {
  // ---- the lock side (requests, the step, the loop)
  Storage* store = nullptr;
  std::vector<StoredPattern> list;
  uint32_t next_gen = 1;
  bool dirty = false;
  Millis dirty_at = 0, retry_at = 0;
  int newer = 0;          // the record's format when it is from a newer firmware (kept)
  bool unreadable = false;
  // the script's names (ScriptProgram::name) as stored patterns: the pattern's gen, 0 none
  uint32_t script_gen[SCRIPT_MAX_NAMES] = {0};
  uint32_t slots_for = UINT32_MAX;  // the script table (script_gen_) they were found for
  Millis missing_told = UINT64_MAX; // the pixels line run whose missing pattern was told
  std::string test_name;            // the pattern the pixel test shows (a new copy sent meanwhile is followed)
  // the step's last pattern picture, and its play number
  bool last_pattern = false;
  uint32_t last_gen = 0;
  Millis last_start = 0;
  uint32_t play = 0;
  uint32_t offered = 0;  // the pattern (gen) last handed to the pixel side
  // ---- step -> pixel side
  struct Press { uint32_t play; uint8_t control; };
  Press ring[PRESS_RING];
  std::atomic<uint32_t> head{0}, tail{0};
  uint32_t presses_lost = 0;
  // ---- loop -> pixel side
  std::atomic<PatternLoad*> mail{nullptr};
  // ---- pixel side -> loop
  std::atomic<uint8_t> note_ready{0};
  char note[NOTE_BYTES] = {0};
  std::atomic<uint32_t> frame_us_max{0}, frame_us_sum{0}, steps_max{0}, frames{0};
  std::atomic<uint32_t> vm_bytes{0};
  // ---- the pixel side's own
  PatternLoad* own = nullptr;
  PatternVM* vm = nullptr;
  uint32_t vm_play = 0;
  bool failed = false;
  Millis last_frame = 0;
  Millis next_at = 0;
  uint32_t rng = 0x9E3779B9u;

  ~PatternSide() {
    delete vm;
    delete own;
    delete mail.exchange(nullptr);
  }
  const StoredPattern* byGen(uint32_t g) const {
    for (auto& p : list)
      if (p.gen == g) return &p;
    return nullptr;
  }
  int byName(const std::string& name) const {
    std::string w = patternWords(name);
    for (size_t i = 0; i < list.size(); i++)
      if (patternWords(list[i].name) == w) return (int)i;
    return -1;
  }
  size_t ramBytes() const {
    size_t n = 0;
    for (auto& p : list) n += p.program.size();
    return n;
  }
  // the pixel side: a note for the event log (one at a time; a second before the first is read is dropped)
  void tell(const std::string& s) {
    if (note_ready.load(std::memory_order_acquire)) return;
    snprintf(note, sizeof note, "%s", s.c_str());
    note_ready.store(1, std::memory_order_release);
  }
};

Controller::~Controller() { delete pat_; }

bool Controller::hasPatterns() const { return pat_ != nullptr; }

// the trigger buttons of a program (control index, label words)
static void buttonsOf(const PatternProgram& p, std::vector<std::pair<uint8_t, std::string>>& out) {
  out.clear();
  for (size_t i = 0; i < p.controls.size(); i++)
    if (p.controls[i].kind == PatternControlKind::Trigger) out.push_back({(uint8_t)i, patternWords(p.controls[i].label)});
}

void Controller::setPatternStore(Storage* store) {
#if PT_PATTERNS
  delete pat_;
  pat_ = new PatternSide();
  pat_->store = store;
  std::string rec;
  Stored got = store ? store->fetch(rec) : Stored::None;
  if (got == Stored::Invalid) {
    pat_->unreadable = true;
    records_ok_ = false;
    log("the stored LED patterns could not be read: they are kept (factory reset clears them)");
  } else if (got == Stored::Ok) {
    uint32_t v = 0;
    PatternRecordKind k = patternRecordDecode(rec, pat_->list, v);
    if (k == PatternRecordKind::Newer) {
      pat_->newer = (int)v;
      records_ok_ = false;
      log("the stored LED patterns are from a newer firmware (record format " + std::to_string(v) + "): they are kept and do not play here");
    } else if (k == PatternRecordKind::Unreadable) {
      pat_->unreadable = true;
      records_ok_ = false;
      log("the stored LED patterns could not be read: they are kept (factory reset clears them)");
    }
    for (auto it = pat_->list.begin(); it != pat_->list.end();) {
      PatternProgram prog;
      std::string why;
      if (!prog.decode((const uint8_t*)it->program.data(), it->program.size(), why)) {
        // a firmware update can change what a board runs: the pattern is dropped from the list (said once)
        log("the stored LED pattern " + it->name + " does not run on this firmware and was removed: " + why);
        it = pat_->list.erase(it);
        pat_->dirty = true;
        continue;
      }
      buttonsOf(prog, it->buttons);
      it->gen = pat_->next_gen++;
      ++it;
    }
  }
  patternScriptSlots();
#else
  (void)store;
#endif
}

std::string Controller::patternsHeldText() const {
  if (wiped_) return "a factory reset cleared the flash: the board is restarting";
  if (update_target_ && update_target_->state() == BootState::Pending) return update_text::SELF_CHECK_PENDING;
  if (pat_->newer)
    return "the stored LED patterns are from a newer firmware (record format " + std::to_string(pat_->newer) +
           ") and are kept: factory reset first, then send patterns";
  if (pat_->unreadable) return "the stored LED patterns could not be read and are kept: factory reset first, then send patterns";
  return "";
}

void Controller::patternScriptSlots() {
  if (!pat_) return;
  for (auto& g : pat_->script_gen) g = 0;
  pat_->slots_for = script_gen_;
  if (!script_prog_) return;
  for (int k = 0; k < script_prog_->names && k < SCRIPT_MAX_NAMES; k++) {
    int i = pat_->byName(script_prog_->name[k]);
    if (i >= 0) pat_->script_gen[k] = pat_->list[(size_t)i].gen;
  }
}

void Controller::patternBoard(ScriptBoard& b) const {
#if !PT_PATTERNS
  b.pattern_player = false;
#endif
  if (!pat_) return;
  for (auto& p : pat_->list) {
    ScriptBoard::PatternButtons pb;
    pb.pattern = patternWords(p.name);
    for (auto& x : p.buttons) pb.buttons.push_back(x.second);
    b.patterns.push_back(pb);
  }
}

bool Controller::patternEffect(const std::string& name, Effect& out) {
  if (!pat_) return false;
  int i = pat_->byName(name);
  if (i < 0) return false;
  pat_->test_name = name;
  out = Effect();
  out.kind = Effect::Kind::Pattern;
  out.pattern = (uint8_t)i;
  out.pattern_gen = pat_->list[(size_t)i].gen;
  return true;
}

// ---------------------------------------------------------------- the step

PT_STEP_CODE void Controller::patternPixels(Frame& f) const {
  if (!f.has_pixel || f.pixel.kind != Effect::Kind::Pattern || !pat_) return;
  uint32_t gen = 0;
  if (engine_.manualPixel()) {
    // the pixel test: the pattern of that name as it is stored now (sent again: the new copy; deleted: dark)
    int i = pat_->byName(pat_->test_name);
    gen = i >= 0 ? pat_->list[(size_t)i].gen : 0;
  } else {
    // a script's pixels = pattern line: the stored pattern of that name (dark when there is none)
    const ScriptStep* st = nullptr;
    Millis since = 0;
    if (!script_run_ || !script_run_->pixels(st, since)) return;
    gen = st->value < SCRIPT_MAX_NAMES ? pat_->script_gen[st->value] : 0;
  }
  const StoredPattern* p = pat_->byGen(gen);
  if (!p) { f.pixel = Effect(); return; }
  f.pixel.pattern = (uint8_t)(p - pat_->list.data());
  f.pixel.pattern_gen = gen;
}

PT_STEP_CODE void Controller::patternPlay(PixelRequest& r) {
  if (!pat_) return;
  bool is = r.has && r.effect.kind == Effect::Kind::Pattern;
  if (is && (!pat_->last_pattern || pat_->last_gen != r.effect.pattern_gen || pat_->last_start != r.start)) pat_->play++;
  pat_->last_pattern = is;
  if (is) { pat_->last_gen = r.effect.pattern_gen; pat_->last_start = r.start; }
  r.play = pat_->play;
}

PT_STEP_CODE void Controller::patternPresses(Millis now) {
  (void)now;
  if (!script_run_ || !script_prog_) return;
  uint8_t pressed[SCRIPT_MAX_PRESSES];
  uint8_t n = script_run_->takePresses(pressed);
  if (!pat_) {
    if (n) log("pixels press ignored: " + std::string(NO_PLAYER));
    return;
  }
  if (pat_->slots_for != script_gen_) patternScriptSlots();
  // a pixels = pattern line whose pattern the board does not have: said once per time the line runs
  const ScriptStep* st = nullptr;
  Millis since = 0;
  if (!engine_.manualPixel() && script_run_->pixels(st, since) && st->target == (uint16_t)Effect::Kind::Pattern &&
      since != pat_->missing_told && st->value < script_prog_->names && !pat_->byGen(pat_->script_gen[st->value])) {
    pat_->missing_told = since;
    log("pattern " + std::string(script_prog_->name[st->value]) + " is not on this board: the strip stays dark (send it from Studio)");
  }
  for (uint8_t i = 0; i < n; i++) {
    std::string button = pressed[i] < script_prog_->names ? script_prog_->name[pressed[i]] : "";
    const PixelRequest& cur = pixel_req_;
    const StoredPattern* p = cur.has && cur.effect.kind == Effect::Kind::Pattern ? pat_->byGen(cur.effect.pattern_gen) : nullptr;
    if (!p) { log("pixels press " + button + " ignored: the strip is not showing a pattern"); continue; }
    int control = -1;
    std::string list;
    for (size_t k = 0; k < p->buttons.size(); k++) {
      if (p->buttons[k].second == button) control = p->buttons[k].first;
      list += (k == 0 ? "" : k + 1 == p->buttons.size() ? " and " : ", ") + p->buttons[k].second;
    }
    if (control < 0) {
      log("pixels press " + button + " ignored: pattern " + p->name + " has no button " + button +
          (list.empty() ? std::string(" (it has none)") : " (its buttons are " + list + ")"));
      continue;
    }
    uint32_t h = pat_->head.load(std::memory_order_relaxed);
    if (h - pat_->tail.load(std::memory_order_acquire) >= PRESS_RING) { pat_->presses_lost++; continue; }
    pat_->ring[h % PRESS_RING] = PatternSide::Press{cur.play, (uint8_t)control};
    pat_->head.store(h + 1, std::memory_order_release);
  }
}

// ---------------------------------------------------------------- the loop

void Controller::patternBackground(bool flush) {
  if (!pat_) return;
  // what the pixel side said
  if (pat_->note_ready.load(std::memory_order_acquire)) {
    log(pat_->note);
    pat_->note_ready.store(0, std::memory_order_release);
  }
  // the program the strip should play, handed over once
  const PixelRequest& r = pixel_req_;
  uint32_t want = r.has && r.effect.kind == Effect::Kind::Pattern ? r.effect.pattern_gen : 0;  // read before any window
  if (want && want != pat_->offered && !window_open_) {
    const StoredPattern* p = pat_->byGen(want);
    if (p) {
      PatternLoad* l = new PatternLoad();
      l->gen = p->gen;
      l->name = p->name;
      l->controls = p->controls;
      std::string why;
      bool ok;
      {
        Unlocked u(*this);  // the stored list changes only in requests, which wait meanwhile (window_lock.h)
        ok = l->prog.decode((const uint8_t*)p->program.data(), p->program.size(), why);
      }
      if (ok) delete pat_->mail.exchange(l, std::memory_order_acq_rel);
      else { log("pattern " + p->name + " cannot play: " + why); delete l; }
      pat_->offered = want;
    }
  }
  // the record: written when nothing runs (a flash write stalls both cores), at the latest a minute after the change
  if (pat_->dirty && pat_->store && !window_open_ && !wiped_ && now_ >= pat_->retry_at && patternsHeldText().empty() &&
      (flush || quietNow() || now_ - pat_->dirty_at >= 60000)) {
    bool ok = false;
    {
      Unlocked u(*this);
      std::string rec = patternRecordEncode(pat_->list);
      ok = pat_->list.empty() ? pat_->store->wipe() : rec.size() <= pat_->store->capacity() && pat_->store->save(rec);
    }
    if (ok) pat_->dirty = false;
    else {
      store_errors++;
      pat_->retry_at = now_ + 10000;
      log("the LED patterns could not be written to flash: trying again in 10 s");
    }
  }
}

bool Controller::patternsWipe() {
  if (!pat_ || !pat_->store) return true;
  pat_->dirty = false;
  return pat_->store->wipe();
}

// ---------------------------------------------------------------- the pixel side

int Controller::patternRender(const PixelRequest& f, Millis now) {
  if (!pat_ || !f.has || f.effect.kind != Effect::Kind::Pattern) return -1;
  PatternSide& s = *pat_;
  PatternLoad* l = s.mail.exchange(nullptr, std::memory_order_acq_rel);
  if (l) {
    // another pattern's program: the interpreter of the old one goes first (it reads its program)
    delete s.vm;
    s.vm = nullptr;
    delete s.own;
    s.own = l;
  }
  if (s.vm && s.vm_play == f.play && now < s.next_at) return 0;  // the last frame was long: the core gets a rest
  Rgb* out = pixel_buf_.data();
  uint16_t count = (uint16_t)pixel_buf_.size();
  auto dark = [&]() { for (size_t i = 0; i < pixel_buf_.size(); i++) out[i] = Rgb(); };
  if (!s.own || s.own->gen != f.effect.pattern_gen) { dark(); return 1; }  // its program is on its way
  uint32_t (*us)() = us_clock_;
#if PT_HOST
  if (!us) us = hostMicros;
#endif
  auto stop = [&](const std::string& why) {
    s.failed = true;
    s.tell("pattern " + s.own->name + " stopped: " + why + ". The strip is dark until the pattern starts again");
    dark();
  };
  if (!s.vm || s.vm_play != f.play) {
    // a new start: a fresh interpreter, the top-level code once, then the control values it was sent with
    delete s.vm;
    s.rng = (uint32_t)now * 2654435761u ^ (us ? us() : 0) ^ 0x9E3779B9u;
    if (!s.rng) s.rng = 1;
    s.vm = new PatternVM(s.own->prog, xorshift, &s.rng, us, PATTERN_FRAME_US);
    s.vm_play = f.play;
    s.failed = false;
    s.last_frame = now;
    s.next_at = 0;
    s.vm->setPixelCount(count);
    if (!s.vm->init()) { stop(s.vm->error()); return 1; }
    for (auto& c : s.own->controls)
      if (!s.vm->setControl(c.control, c.values.data(), c.values.size())) { stop(s.vm->error()); return 1; }
    s.vm_bytes.store((uint32_t)(s.vm->bytes() + s.own->prog.bytes()), std::memory_order_relaxed);
  }
  if (s.failed) { dark(); return 1; }
  // the presses for this play, in order (an older play's are dropped; a newer play's wait for it)
  for (uint32_t t = s.tail.load(std::memory_order_relaxed); t != s.head.load(std::memory_order_acquire); t++) {
    const PatternSide::Press& p = s.ring[t % PRESS_RING];
    if ((int32_t)(p.play - s.vm_play) > 0) break;
    bool ok = p.play != s.vm_play || s.vm->callControl(p.control, nullptr, 0);
    s.tail.store(t + 1, std::memory_order_release);
    if (!ok) { stop(s.vm->error()); return 1; }
  }
  if (s.vm->pixelCount() != count) s.vm->setPixelCount(count);
  uint32_t t0 = us ? us() : 0;
  uint32_t delta = (uint32_t)(now - s.last_frame);
  s.last_frame = now;
  if (!s.vm->frame(delta, out)) { stop(s.vm->error()); return 1; }
  uint32_t took = us ? us() - t0 : 0;
  if (took > s.frame_us_max.load(std::memory_order_relaxed)) s.frame_us_max.store(took, std::memory_order_relaxed);
  s.frame_us_sum.fetch_add(took, std::memory_order_relaxed);
  uint32_t steps = s.vm->takeMaxSteps();
  if (steps > s.steps_max.load(std::memory_order_relaxed)) s.steps_max.store(steps, std::memory_order_relaxed);
  s.frames.fetch_add(1, std::memory_order_relaxed);
  // a long frame is followed by at least as long without one (half of the pixel side's time at most), so a
  // heavy pattern never keeps the core it shares (the network on the Pico 2 W) busy
  s.next_at = now + (took / 1000 + 1) * 2;
  return 1;
}

std::string Controller::patternReport() {
  if (!pat_ || !pat_->frames.load(std::memory_order_relaxed)) return "";
  char b[200];
  uint32_t n = pat_->frames.exchange(0, std::memory_order_relaxed), sum = pat_->frame_us_sum.exchange(0, std::memory_order_relaxed);
  snprintf(b, sizeof b, "; pattern frames %u, frame average %u us, max %u us, max %u steps, player %u B", (unsigned)n,
           (unsigned)(n ? sum / n : 0), (unsigned)pat_->frame_us_max.exchange(0, std::memory_order_relaxed),
           (unsigned)pat_->steps_max.exchange(0, std::memory_order_relaxed), (unsigned)pat_->vm_bytes.load(std::memory_order_relaxed));
  return b;
}

// ---------------------------------------------------------------- requests

static void patternJson(const StoredPattern& p, JsonObject o) {
  o["name"] = p.name;
  o["bytes"] = p.program.size();
  PatternProgram prog;
  std::string why;
  if (prog.decode((const uint8_t*)p.program.data(), p.program.size(), why)) {
    JsonArray cs = o["controls"].to<JsonArray>();
    for (auto& c : prog.controls) {
      JsonObject x = cs.add<JsonObject>();
      x["kind"] = patternControlKindName(c.kind);
      x["name"] = c.name;
      x["label"] = c.label;
    }
  }
  JsonArray bs = o["buttons"].to<JsonArray>();
  for (auto& b : p.buttons) bs.add(b.second);
}

Response Controller::patternOp(Request& r) {
  if (!pat_) return Response::err(r, ErrorCode::Unsupported, NO_PLAYER);
  PatternSide& s = *pat_;
  if (r.op == "list_patterns") {
    Response resp = Response::ok(r, "patterns");
    JsonObject o = resp.result();
    JsonArray a = o["patterns"].to<JsonArray>();
    for (auto& p : s.list) patternJson(p, a.add<JsonObject>());
    o["max"] = PATTERN_MAX_STORED;
    o["store_capacity"] = PATTERN_STORE_CAPACITY;
    o["saved"] = !s.dirty;
    const PixelRequest& cur = pixel_req_;
    const StoredPattern* playing = cur.has && cur.effect.kind == Effect::Kind::Pattern ? s.byGen(cur.effect.pattern_gen) : nullptr;
    if (playing) o["playing"] = playing->name;
    return resp;
  }
  std::string held = patternsHeldText();
  if (!held.empty()) return Response::err(r, ErrorCode::Refused, held);
  std::string why;
  std::string name = patternNameClean(r["name"] | "");
  if (r.op == "delete_pattern") {
    int i = s.byName(name);
    if (i < 0) return Response::err(r, ErrorCode::NotFound, "there is no pattern called " + name + " on this board");
    std::string was = s.list[(size_t)i].name;
    s.list.erase(s.list.begin() + i);
    s.dirty = true;
    s.dirty_at = now_;
    patternScriptSlots();
    events_.push("patterns_changed");
    log("pattern " + was + " removed");
    return Response::done(r);
  }
  // put_pattern
  if (!patternNameOk(name, why)) return Response::err(r, ErrorCode::Invalid, why);
  StoredPattern np;
  np.name = name;
  const char* b64 = r["program"] | "";
  if (!base64Decode(b64, strlen(b64), np.program)) return Response::err(r, ErrorCode::BadRequest, "program: not base64");
  PatternProgram prog;
  if (!prog.decode((const uint8_t*)np.program.data(), np.program.size(), why)) return Response::err(r, ErrorCode::Invalid, why);
  buttonsOf(prog, np.buttons);
  for (JsonPairConst kv : r["controls"].as<JsonObjectConst>()) {
    PatternValues v;
    v.control = kv.key().c_str();
    if (prog.findControl(v.control) < 0) return Response::err(r, ErrorCode::Invalid, "the pattern has no control called " + v.control);
    JsonArrayConst vals = kv.value().as<JsonArrayConst>();
    if (vals.size() > PATTERN_MAX_VALUES) return Response::err(r, ErrorCode::Invalid, "control " + v.control + ": at most 8 values");
    for (JsonVariantConst x : vals) {
      if (!x.is<int32_t>()) return Response::err(r, ErrorCode::Invalid, "control " + v.control + ": its values are whole numbers (16.16: 65536 is 1)");
      v.values.push_back(x.as<int32_t>());
    }
    np.controls.push_back(v);
  }
  int at = s.byName(name);
  if (at < 0 && s.list.size() >= PATTERN_MAX_STORED)
    return Response::err(r, ErrorCode::Refused, "the board keeps at most 8 patterns: delete one first");
  // does the new list fit the board's memory and its store?
  size_t ram = np.program.size();
  for (size_t i = 0; i < s.list.size(); i++)
    if ((int)i != at) ram += s.list[i].program.size();
  if (ram > PATTERN_STORE_RAM_MAX)
    return Response::err(r, ErrorCode::Refused, "the board's patterns would take " + std::to_string(ram / 1024 + 1) + " KB, more than its " +
                                                   std::to_string(PATTERN_STORE_RAM_MAX / 1024) + " KB: delete a pattern first");
  size_t packed = 0;
  {
    Unlocked u(*this);  // the stored list changes only in requests, which wait meanwhile (window_lock.h)
    packed = patternRecordEncode(s.list, &np, at).size();
  }
  if (packed > PATTERN_STORE_CAPACITY)
    return Response::err(r, ErrorCode::Refused, "the board's pattern store is full (" + std::to_string(packed) + " of " +
                                                   std::to_string(PATTERN_STORE_CAPACITY) + " bytes): delete a pattern first");
  np.gen = s.next_gen++;
  if (at >= 0) s.list[(size_t)at] = np;
  else s.list.push_back(np);
  s.dirty = true;
  s.dirty_at = now_;
  patternScriptSlots();
  events_.push("patterns_changed");
  log("pattern " + name + " stored (" + std::to_string(np.program.size()) + " bytes" + (quietNow() ? "" : "; written to flash when nothing runs") + ")");
  Response resp = Response::ok(r, "pattern_put");
  JsonObject o = resp.result();
  o["name"] = name;
  o["bytes"] = np.program.size();
  o["patterns"] = s.list.size();
  o["store_bytes"] = packed;
  o["store_capacity"] = PATTERN_STORE_CAPACITY;
  JsonArray bs = o["buttons"].to<JsonArray>();
  for (auto& b : np.buttons) bs.add(b.second);
  return resp;
}


#else  // !PT_PATTERNS: the Pico W and the Pico have no pattern player (no room); the checker says so

struct PatternSide {};
Controller::~Controller() { delete pat_; }
bool Controller::hasPatterns() const { return false; }
void Controller::setPatternStore(Storage* store) { (void)store; }
std::string Controller::patternsHeldText() const { return ""; }
void Controller::patternScriptSlots() {}
void Controller::patternBoard(ScriptBoard& b) const { b.pattern_player = false; }
bool Controller::patternEffect(const std::string& name, Effect& out) { (void)name; (void)out; return false; }
void Controller::patternPixels(Frame& f) const { if (f.has_pixel && f.pixel.kind == Effect::Kind::Pattern) f.pixel = Effect(); }
void Controller::patternPlay(PixelRequest& r) { (void)r; }
void Controller::patternPresses(Millis now) {
  (void)now;
  uint8_t pressed[SCRIPT_MAX_PRESSES];
  if (script_run_) (void)script_run_->takePresses(pressed);
}
void Controller::patternBackground(bool flush) { (void)flush; }
bool Controller::patternsWipe() { return true; }
int Controller::patternRender(const PixelRequest& f, Millis now) { (void)f; (void)now; return -1; }
std::string Controller::patternReport() { return ""; }
Response Controller::patternOp(Request& r) {
  return Response::err(r, ErrorCode::Unsupported, "this board has no pattern player: the Pico W and the Pico have no room for it (an ESP32 or a Pico 2 W has one)");
}

#endif

}  // namespace pt
