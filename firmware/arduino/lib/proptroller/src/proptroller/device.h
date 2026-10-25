// The generic controller, independent of the MCU: it owns the Board, the DFPlayer driver, the pixel port
// and the settings store, runs the show Engine, and answers protocol requests.
//
//   inputs -> debounce / trigger arming -> engine events --+
//   API requests ----------------------------> handle() ---+
//                                                  Engine::tick (10 ms)
//   outputs <- safety guard <- frame levels <--------------+
//   DFPlayer <- frame sound;  PIXEL <- effect render;  status LED <- pattern
//
// The ESP32, Pico and simulator firmwares all build exactly this class with different BoardPins,
// ByteStream, PixelOut and Storage implementations.
#pragma once
#include "board.h"
#include "board_package.h"
#include "netapp.h"
#include "script.h"
#include "settings.h"
#include "storage.h"
#include "update.h"
#include "password.h"
#include "runs.h"
#include <atomic>
#include <deque>
#include <memory>

namespace pt {

// The example show (TRIG1 pulses OUT1 for 2 s with a pixel flash). A fresh board has no shows: the example is added
// only when asked for (op add_example, Shows -> Add an example show), so wiring a board never starts one by surprise.
extern const char* const EXAMPLE_SHOW;
extern const char* const FACTORY_EXAMPLE_SHOW;  // what earlier firmware stored on a fresh board

// What a show script may use on a board with these settings (roles, buttons, DMX port, the built-in effect,
// the terminals' names).
ScriptBoard scriptBoardFor(const Settings& s);
// The terminal or name a checker error is about ("rly1", "lamp (out1)", "lamp"): for "the stored script uses <x>".
std::string scriptSubject(const std::string& message);
// The DFPlayer driver settings (boot: with the power-on delay).
DfConfig dfConfig(const Settings& s, bool boot);

// What a board support package gives a fresh board (nothing stored yet, or after a factory reset): any of an
// alias profile (set_profile's JSON), settings ("key=value" lines, as on the serial console; key aliases of
// that profile work) and a show script. Applied in that order; null or "" leaves that part out. The board
// keeps them like anything set by hand (a later change is not undone at the next power-on).
struct FactoryDefaults {
  const char* profile = nullptr;
  const char* settings = nullptr;
  const char* script = nullptr;
  const char* script_name = nullptr;  // what the script is called (get_defaults), optional
};

class Controller : public Firmware {
 public:
  Controller(BoardPins& pins, ByteStream& uart, PixelOut& pixels, Storage* store, const Identity& id);
  virtual ~Controller() {}

  // One step (call every ~10 ms).
  void tick(Millis now) override;
  Millis now() const { return now_; }

  const Settings& settings() const { return settings_; }
  Engine& engine() { return engine_; }
  const Engine& engine() const { return engine_; }
  Board& board() { return board_; }
  DfPlayer& player() { return df_; }
  const Level* levels() const { return board_.levels(); }
  bool rebootRequested() const override { return reboot_; }
  void setNet(const NetState& n) { net_ = n; }
  void log(const std::string& s) { events_.log(s); }
  EventLog& events() { return events_; }
  Pattern pattern() const;

  // One raw sample of TRIG1 / TRIG2: call at least every 1 ms between ticks (tick() takes one too), so a
  // bounce or a dropout between ticks restarts the debounce (SM-T1).
  void sampleTriggers(Millis now);
  void sample(Millis now) override { sampleTriggers(now); }
  // The whole step on the board (the controller's tick, the sketch's tasks, the 1 ms samples): the lock is never
  // given back inside it, and a change asked from it while another task's request has the lock given back is
  // refused (window_lock.h). Nests; a null controller does nothing.
  class StepScope {
   public:
    explicit StepScope(Controller* c) : c_(c), was_(c && c->in_tick_) { if (c_) c_->in_tick_ = true; }
    ~StepScope() { if (c_) c_->in_tick_ = was_; }
    StepScope(const StepScope&) = delete;
    StepScope& operator=(const StepScope&) = delete;

   private:
    Controller* c_;
    bool was_;
  };
  // Where a request comes from: some permissions belong to a live connection (a phone page's WebSocket).
  enum class Origin : uint8_t { Http, Session, Mqtt, Serial };
  std::string callFrom(uint32_t session, const char* body, size_t len) override;
  void sessionClosed(uint32_t session) override;
  // A request body in a project's names (the profile's aliases) turned into the generic names; false and the
  // reason in plain words when an aliased value is out of range (SM-AL1, SM-AL3).
  // `p` is the profile to use (call() passes its own copy, so the parse never reads the settings without the lock).
  static bool aliasIn(const Profile& p, const char* body, size_t len, std::string& out, std::string& why);
  // A terminal name or a terminal alias: the board terminal ("REFILL" -> "TRIG2"), or the name unchanged.
  std::string terminalName(const std::string& n) const;
  // How a terminal is shown: "REFILL (TRIG2)" with an alias, else its board name.
  std::string terminalDisplay(const std::string& board_name) const;
  // One line of the serial console: "key=value", "key", "trigger", "recover", "cancel", "restart".
  std::string consoleLine(const std::string& line) override;
  std::string callSerial(const char* body, size_t len) override;
  // The pulse-and-recover effect, when it is on.
  const EffectMachine* effect() const { return fx_.get(); }
  void setOrigin(Origin o, uint32_t session = 0) { origin_ = o; session_ = session; }
  // The sketch's own values (setup() runs at every boot): true when `value` differs from what the sketch applied
  // last time under `slot`; sketchApplied() records it.
  bool sketchChanged(const std::string& slot, const std::string& value) const {
    auto it = settings_.sketch.find(slot);
    return it == settings_.sketch.end() || it->second != value;
  }
  void sketchApplied(const std::string& slot, const std::string& value) { settings_.sketch[slot] = value; markDirty(); }
  void sketchForget(const std::string& prefix) {
    for (auto it = settings_.sketch.begin(); it != settings_.sketch.end();)
      it = it->first.compare(0, prefix.size(), prefix) == 0 ? settings_.sketch.erase(it) : std::next(it);
  }
  // Answer one request (already parsed).
  Response handle(Request& r);
  // Change settings atomically (all checked first). save = false (another board's tell): applied now, never
  // written to flash, refused for the settings a board does not take from another (scriptTellRefused, secrets,
  // settings that need a restart).
  Response setParams(const Request& r, const std::vector<ParamValue>& values, bool save = true);
  void saveNow();
  void saveIfDirty() { if (dirty_) saveNow(); }
  // The flash writes the step decided on (the settings when their save is due, a script record to write again),
  // for the loop to do: the step never writes the flash itself. Call it after tick(), from the loop, holding the
  // lock: it gives the lock back while the flash is written (setLockHooks), so the step keeps its 10 ms.
  void background();
  // A board support package's defaults (FactoryDefaults): applied only when the board had no stored settings
  // at power-on (call after setScriptStore). Returns what was refused, one line each ("" when everything took).
  // `force`: also on a board that has stored settings (a board package's defaults asked for with set_board).
  std::string applyFactoryDefaults(const FactoryDefaults& d, bool force = false);
  // Board packages (board_package.h, BOARD-PACKAGES.md): the package's own store (nullptr: this firmware takes none,
  // get_board / set_board answer unsupported), what the board runs with since power-on, the chip's pin rules and
  // the image's built-in boards. A stored package that could not be used counts as an unreadable record (a new
  // image then fails its self-check).
  void setBoardPackages(Storage* store, const BoardBoot& running, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins);
  // The board's defaults at power-on (call after setScriptStore and setBoardPackages): the stored package's on a
  // fresh board or once after set_board asked for them, else `image` (a board support package compiled in) on a
  // fresh board. Returns what was refused, one line each ("" when everything took).
  std::string applyBoardDefaults(const FactoryDefaults* image);
  const BoardBoot& boardRunning() const { return board_boot_; }
  bool freshBoard() const { return fresh_; }
  // Shows defined by the sketch: not stored (the sketch adds them again at every boot).
  ShowError addCodeShow(const Show& s);

  Info info() const;
  // The page lock is on (and a page password is set).
  bool locked() const { return settings_.page_lock && !settings_.pin_hash.empty(); }
  // Is this the page password?
  bool pinOk(const Request& r) const;
  // The page password, throttled after a wrong one: "" when right, else the refusal.
  std::string pinRefused(bool has, const std::string& pin);
  std::vector<Terminal> terminals() const;
  // the lowest show id not in use (the board gives new shows their ids: two phones never pick the same one)
  uint16_t freeShowId() const;
  void stateToJson(JsonObject o) const;

  // NetApp
  std::string call(const char* body, size_t len) override;
  std::string infoJson() override;
  std::string stateJson() override;
  uint32_t eventsSince(uint32_t seq, std::vector<std::string>& out) override { return events_.since(seq, out); }
  uint32_t eventsSeq() override { return events_.seq(); }
  std::string announceJson(uint16_t port) override { return info().announce(port); }
  std::vector<std::pair<std::string, std::string>> txt() override { return info().txt(); }
  void netChanged(const NetState& n) override { net_ = n; }
  NetConfig netConfig() override;
  void wifiScanReady(bool scan_now) override { wifi_scan_.setAvailable(scan_now, now_); }
  bool wifiScanWanted() override { return wifi_scan_.takeStart(now_); }
  void wifiScanDone(const std::vector<WifiNetwork>& found, bool ok) override { wifi_scan_.finish(now_, found, ok); }
  std::vector<std::string> takeCuesOut() override;
  void cueIn(const std::string& name) override { if (!updating()) engine_.cue(now_, name); }  // nothing starts during an update
  bool mqttTarget(std::string& host, uint16_t& port) override;
  uint32_t mqttEpoch() override { return mqtt_.epoch; }
  std::string mqttClientId() override { return "proptroller-" + identity_.id; }
  MqttMsg mqttWill() override;
  std::vector<MqttMsg> mqttOnConnect() override;
  std::vector<std::string> mqttSubscriptions() override;
  std::vector<MqttMsg> mqttOnMessage(const std::string& topic, const std::string& payload) override;
  std::vector<MqttMsg> mqttPoll() override;
  std::string mqttPrefix() const;
  std::string mqttPrefixNow() const;
  // The board link (device_link.cpp, SHOW-SCRIPT.md §11, PROTOCOL.md "Board link"): this board's tell lines go out
  // (linkTake: peer to peer; MQTT through mqttPoll), other boards' tells come in (linkIn, MQTT control/ and
  // config/now/), their answers to ours (linkAnswer).
  uint32_t linkEpoch() override { return link_epoch_.load(std::memory_order_relaxed); }
  bool linkWork() override { return link_work_.load(std::memory_order_relaxed); }
  bool linkConfig(LinkConfig& c) override;
  std::vector<LinkOut> linkTake() override;
  LinkReply linkIn(const LinkIn& in) override;
  void linkAnswer(uint32_t seq, const std::string& from, bool ok, const std::string& text) override;
  // Settings another board changed: in use now, not saved (key -> the value kept in flash). A change made here, a
  // backup restored or a restart ends them.
  const std::map<std::string, Value>& linkKept() const { return link_kept_; }
  // How this board's tells go out now: off, MQTT or Wi-Fi peer to peer (board_link; auto: MQTT with a broker)
  enum class LinkVia : uint8_t { Off, Mqtt, Wifi };
  LinkVia linkVia() const;
  static const Millis LINK_ANSWER_MS = 2000;  // a told board that has not answered by then: "no answer"
  static const uint8_t LINK_MAX_PER_S = 20;   // tells sent in a second at most (more are dropped and logged)
  static const uint8_t LINK_PENDING_MAX = 16; // tells waiting for answers
  static const uint8_t LINK_NAMES_MAX = 16;   // boards known over MQTT for tell all

  // The board's lock around the controller (the step takes it every 10 ms): a request gives it back while it does
  // long work on its own data (parsing a body, checking a script), so the step keeps running. `release` gives up
  // every level this thread holds and says how many (0: it held none); `retake` takes them again.
  void setLockHooks(int (*release)(void*), void (*retake)(void*, int), void* user) {
    lock_release_ = release;
    lock_retake_ = retake;
    lock_user_ = user;
  }

  // Advanced: answer extra request ops (custom protocol messages). Return true when handled.
  typedef bool (*RequestHook)(Controller& c, Request& r, Response& out, void* user);
  void setRequestHook(RequestHook h, void* user) { request_hook_ = h; request_hook_user_ = user; }
  bool hasRequestHook() const { return request_hook_ != nullptr; }
  // The board's memory report for the console's `mem` (Proptroller::memoryReport on the boards; none on the host).
  void setMemProbe(std::string (*probe)()) { mem_probe_ = probe; }
  // A bench build with -DPROPTROLLER_STEP_PROFILE: the microsecond clock, and the tick's time per part (sum and
  // longest since the last report, then cleared), for `mem`.
  void setUsClock(uint32_t (*us)()) { us_clock_ = us; }
  std::string profileReport();
  // The free heap in all (the boards; none on the host) and the largest block it can give in one piece (the ESP32;
  // the RP2040 has no such count): a big request that would not fit is refused in plain words instead of running
  // out of memory. The whole need is compared with the free heap, the largest single allocation with the block.
  void setHeapProbe(size_t (*probe)()) { heap_probe_ = probe; }
  void setBlockProbe(size_t (*probe)()) { block_probe_ = probe; }
  // The heap a request body of `len` bytes may need at its peak while it is parsed, checked and applied.
  // test_heap_peak checks that it covers check_script, set_script and put_backup of the largest script as
  // counted on the 64-bit host (put_backup of a 21 KB backup: 80 KB). Kept at the rule set from the board
  // (QEMU.md finding 1, where the peak ran above the host's count) until a board measurement allows less.
  static size_t requestHeapNeed(size_t len) { return 4 * len + 48 * 1024; }
  // The largest single allocation of such a request: the body and the script text it carries are each one string
  // (a string that grows can ask for twice its size), the parsed document's pools are small blocks.
  static size_t requestBlockNeed(size_t len) { return 2 * len + 4 * 1024; }
  // Answers longer than ANSWER_CHECK_BYTES are measured before they are written: refused when the free heap is not
  // their length plus ANSWER_HEAP_SPARE, or the largest block not their length plus ANSWER_BLOCK_SPARE.
  static const size_t ANSWER_CHECK_BYTES = 4 * 1024;
  static const size_t ANSWER_HEAP_SPARE = 32 * 1024;
  static const size_t ANSWER_BLOCK_SPARE = 1024;

  uint32_t store_errors = 0, pixel_errors = 0;
  uint32_t pixel_skipped = 0;  // frames held back a tick because the last one was still on the wire
  // The strip's frame is rendered and sent here, never in the step: the step only hands over what the strip should
  // show (publishPixels). Called outside the step, without the controller's lock, from one place only: a task on
  // core 0 (ESP32), loop() (RP2040), right after each tick (simulator). Wait-free on the step's side.
  void pixelService(Millis now);
  uint32_t script_checks_at_boot = 0;  // stored scripts checked again from their text at power-on
  // Accepted presses per input since power-on (TRIG: armed presses only; USER: every press). Sketches use
  // these through Proptroller::wasPressed().
  uint32_t presses[INPUT_COUNT] = {0, 0, 0, 0, 0};
  bool isActive(Input i) const { return index(i) < 2 ? trig_[index(i)].isActive() : buttons_[index(i) - 2].isPressed(); }
  // Show scripts (SHOW-SCRIPT.md). The script's own record (text and sound list) is kept in `store`; the
  // stored script is checked again (a firmware update may have changed the rules) and runs only if it passes.
  void setScriptStore(Storage* store);
  // The run counters (runs.h): the lifetime count's own record is kept in `store` (nullptr: RAM only).
  void setRunStore(Storage* store);
  const RunCounter& runs() const { return runs_; }
  void setDmx(DmxOut* dmx) { dmx_ = dmx; }
  // Check a script against the board as it is set now; nothing changes.
  ScriptCheck checkScript(const std::string& text, const SoundList& sounds, ScriptProgram& table) const;
  // Check and, when it passes, store and run it (an empty script removes the stored one).
  // `store_error`: set when it passed but could not be stored (the old script is loaded back).
  ScriptCheck saveScript(const std::string& text, const SoundList& sounds, std::string* store_error = nullptr);
  const std::string& scriptText() const { return script_text_; }
  const ScriptProgram* scriptProgram() const { return script_prog_.get(); }
  const ScriptRunner* scriptRunner() const { return script_run_.get(); }
  uint32_t dmx_frames = 0;
  // Over-the-air updates (update.h). The target is the board's (not owned); the board then has the `update`
  // capability.
  void setUpdateTarget(UpdateTarget* t) { update_target_ = t; }
  // In update mode: receiving an image, or restarting into it (outputs off, changes refused).
  bool updating() const { return upd_.active || upd_.restart_at_set; }
  // POST /api/v1/update: false and `answer` (a response JSON) when refused; nothing was written.
  [[nodiscard]] bool updateBegin(const UpdateRequest& req, std::string& answer) override;
  // The next bytes of the body: false and `answer` when the update stopped (the running image untouched).
  [[nodiscard]] bool updateWrite(const uint8_t* data, size_t n, std::string& answer) override;
  // The body ended: check it and switch to the new image. The answer.
  std::string updateFinish() override;
  // The connection ended before the whole image arrived.
  void updateAbort() override;
  // The stored records were read at power-on (or there were none): a new image's self-check needs it.
  bool recordsOk() const { return records_ok_; }
  // Backups (PROTOCOL.md "Backups"): get_backup's document into `o`.
  void backupJson(JsonObject o) const;
  // Expert: let a trigger held at power-on start shows (the held-at-boot rule no longer applies to it).
  void armNow(Input i) { if (index(i) < 2) trig_[index(i)].forceArm(); }

 protected:
  virtual bool handleExtra(Request& r, Response& out);
  void applySettings();
  void accepted(Millis now, const std::vector<uint16_t>& started);
  void pulseRole(Millis now, OutputRole role, uint32_t ms = 0);
  void ownedLevels(Millis now);
  void scriptPixels(Frame& f) const;
  void readInputs(bool raw[INPUT_COUNT]);
  bool sensor_high_[2] = {false, false};  // the input types the triggers were armed with
  void stopOwned(Millis now);
  // the effect
  void effectSync();
  void effectEvents(Millis now, const std::vector<EffectEvent>& evs);
  void effectSample(Millis now, const bool raw[INPUT_COUNT]);
  bool effectOwns(Channel c) const;
  bool effectOwns(Input i) const;
  std::string effectOwnsShow(const Show& s) const;
  Response effectAction(const Request& r, const std::string& control);
  std::string doTrigger();  // "" or why it was refused
  std::string doRecover();
  std::string refusedText(EffectRefused why) const;
  void effectStatus(std::vector<ParamValue>& out, bool aliased = true) const;
  // the status values as numbers (the effect's, else the script's), then as key / value lines (the keys with the
  // profile's aliases): the state copies the numbers under the lock and writes the lines without it
  struct StatusNums {
    bool any = false, fx = false, has_limit = false;
    uint32_t run_count = 0, runs_left = 0, cooldown_ms = 0;
    bool pulsing = false, recovering = false, ready = false, remote = false, queued = false;
    std::string by;
  };
  void statusNums(StatusNums& n) const;
  void statusLines(const StatusNums& n, std::vector<ParamValue>& out, bool aliased) const;
  // a show script with an effect that says "or remote" / "or remote recover": its controls and status
  int scriptEffect(const std::string& name) const;
  uint8_t scriptFreeEffects() const;
  Response scriptStart(const Request& r, int effect);

  void scriptValues();  // the settings a script line may name, to the runner
  // the run counters as status values (runs_since_power_on, runs_lifetime, last_run_uptime_ms when there was one)
  void runStatus(std::vector<ParamValue>& out, bool aliased = true) const;
  // a run started (runs.h): counted, and a "run" event (MQTT <p>/event/run) says what started it
  void countRun(Millis now, const char* source, const char* kind, const std::string& name, const std::string& detail, int show = -1);
  // the lifetime count to flash now (from the loop or a request, never from the step)
  void runsSaveNow();
  // nothing runs: no show, effect cycle or script effect (a flash write then delays nothing)
  bool quietNow() const;
  Storage* run_store_ = nullptr;
  RunCounter runs_;
  uint32_t script_runs_seen_[SCRIPT_MAX_EFFECTS] = {0};
  std::string resolveControl(const std::string& name) const;
  std::string resolveKey(const std::string& name) const;
  bool statusRead(const std::string& name, Value& out) const;
  bool getOut(const std::string& name, Value& out) const;
  std::string consoleAnswer(const std::string& line);
  std::string aliasFor(AliasEntry::Kind k, const std::string& target) const;
  static std::vector<ParamValue> withAlso(const AliasEntry* a, const std::string& key, const Value& v);
  const AliasEntry* aliasNamed(const std::string& alias) const;
  std::string topicRoot() const;
  void noticeOut(const Notice& n);
  std::string checkReserved(const Show& s, const Settings& roles_from) const;
  void publishPixels(const Frame& f);
  void markDirty() {
    if (!dirty_) dirty_since_ = now_;
    dirty_ = true;
    dirty_at_ = now_;
  }
  std::string storedJson(const Show* replace = nullptr) const;
  // the settings record's size (storedJson's), without the string
  size_t storedSize(const Show* replace = nullptr) const;
  void storedDoc(JsonDocument& d, const Show* replace) const;
  // the state's parts the step changes, copied under the lock (stateJson renders the JSON from it in a window)
  struct StateSnap {
    bool valid = false;
    Millis now = 0;
    BoardReport board{};
    uint32_t store_errors = 0;
    struct Out {
      bool manual = false, ramping = false, owned = false;
      Level requested = 0;
      Driver driver;
      Block blocked = Block::None;
    } out[CHANNEL_COUNT];
    struct In {
      bool active = false, waiting = false, test = false, test_starts = false, armed = false;
    } in[INPUT_COUNT];
    struct ShowSnap {
      uint16_t id = 0;
      ShowStatus status;
      bool has_runs = false;
      uint32_t left = 0, window = 0;
    };
    std::vector<ShowSnap> shows;
    bool paused = false;
    uint32_t runs_since = 0;
    uint64_t runs_lifetime = 0;
    bool has_run = false;
    Millis last_run = 0;
    bool sound_playing = false, sound_online = false, has_track = false, ambient_missing = false;
    SoundCommand track;
    Playing::Kind playing = Playing::Kind::Idle;
    Pattern pattern;
    StatusNums status;
    bool has_pixel = false;
    Effect pixel;
    bool has_fx = false;
    const char* fx_state = "";
    uint32_t fx_cooldown_left = 0;
    int script_effects = 0;
    bool script_running[SCRIPT_MAX_EFFECTS] = {false};
    uint16_t script_line[SCRIPT_MAX_EFFECTS] = {0};
    bool upd_active = false, upd_restarting = false;
    uint32_t upd_done = 0, upd_total = 0;
  };
  void captureState(StateSnap& s) const;
  std::unique_ptr<StateSnap> state_snap_;  // a copy made by the reader itself (no fresh one from the step)
  // The step's copies of the state, for the network and the serial port (a triple buffer, so neither side ever
  // waits): while someone asked within STATE_WANT_MS, the step copies the state every STATE_COPY_TICKS into
  // snap_back_ and hands it over as snap_mid_ (the index, | 0x80 while unread); the reader takes it as its front.
  StateSnap snap_buf_[3];
  uint8_t snap_back_ = 0, snap_front_ = 2, snap_tick_ = 0;
  std::atomic<uint8_t> snap_mid_{1};
  std::atomic<uint32_t> snap_wanted_until_{0};  // Millis, low 32 bits
  void publishState(Millis now);
  const StateSnap* takeState(Millis now);
  const StateSnap* stateSnapshot();  // the step's latest copy, or null when there is none recent enough
  void renderState(const StateSnap& s, JsonObject o) const;

  Board board_;
  ByteStream& uart_;
  DfPlayer df_;
  PixelOut& pixels_;
  Storage* store_;
  Settings settings_;
  Engine engine_;
  Trigger trig_[2];
  Button buttons_[3];
  Identity identity_;
  NetState net_;
  WifiScan wifi_scan_;  // the networks this board sees (op wifi_scan)
  EventLog events_;
  bool dirty_ = false;
  bool save_due_ = false;  // the step says: save now (background() does)
  Millis dirty_at_ = 0;
  Millis dirty_since_ = 0;      // the first change not yet saved (saved at the latest SAVE_MAX_DELAY_MS after it)
  Millis save_not_before_ = 0;  // flash wear: SAVE_MIN_GAP_MS between two settings writes
  uint32_t stored_crc_ = 0;     // the record in flash (crc32), so an unchanged record is not written again
  bool stored_crc_known_ = false;
  bool reboot_ = false;
  std::vector<std::pair<uint8_t, TriggerEvent>> trig_events_;  // from the samples since the last tick
  // Since when each input reads closed / open without a break (1 ms samples); at power-on as if held, so a
  // release must be seen first.
  bool has_closed_[INPUT_COUNT] = {true, true, true, true, true};
  Millis closed_since_[INPUT_COUNT] = {0, 0, 0, 0, 0};
  bool has_opened_[INPUT_COUNT] = {false, false, false, false, false};
  Millis opened_since_[INPUT_COUNT] = {0, 0, 0, 0, 0};
  void sampleTriggersWith(Millis now, const bool raw[INPUT_COUNT]);
  // what the strip shows: the step's own copy (state "pixel"), and the one handed to pixelService through a
  // sequence lock (pixel_seq_ odd while the step writes pixel_mail_; the service copies it and checks the count)
  struct PixelRequest {
    bool has = false;
    Effect effect;
    Millis start = 0;
    uint8_t brightness = 100;
    uint16_t count = 0;
  };
  PixelRequest pixel_req_, pixel_mail_;
  // scriptFreeEffects' answer for this script and this list (it runs on every sample)
  uint32_t script_gen_ = 0;           // counts installed tables (a new one may reuse the old one's address)
  mutable uint32_t free_gen_ = UINT32_MAX;
  mutable std::string free_list_;
  mutable uint8_t free_mask_ = 0;
  std::atomic<uint32_t> pixel_seq_{0};
  bool pixel_published_ = false;
  // pixelService's own
  std::vector<Rgb> pixel_buf_;
  bool pixel_have_last_ = false;
  PixelRequest pixel_last_;
  Millis pixel_last_at_ = 0;
  std::vector<std::string> cues_out_;
  SoundDirector sound_;
  bool role_on_[CHANNEL_COUNT] = {false};
  Millis manual_until_[CHANNEL_COUNT] = {0};      // a manual pulse ends at (0: held)
  uint32_t manual_session_[CHANNEL_COUNT] = {0};  // the phone page holding a manual output (0: none)
  uint32_t pixel_session_ = 0;
  // test presses (op test_input): held until let go, its time is up (at most TEST_PRESS_MAX_MS) or the page holding
  // it closes. They join the pins' readings in readInputs, as a real press, when a remote start is allowed
  // (remote_trigger_enable); without it they show on the input and start nothing, except the script effects named
  // in remote_free_effects (scriptInputs)
  Millis test_until_[INPUT_COUNT] = {0, 0, 0, 0, 0};    // a held test press ends at (0: none held)
  uint32_t test_session_[INPUT_COUNT] = {0, 0, 0, 0, 0}; // the page holding it (0: none)
  Millis test_on_since_[INPUT_COUNT] = {0, 0, 0, 0, 0};  // when it was pressed
  Millis test_off_since_[INPUT_COUNT] = {0, 0, 0, 0, 0}; // when it was let go
  bool testHeld(int k) const { return test_until_[k] != 0 && !updating(); }
  bool testStarts() const { return settings_.fx.remote_trigger_enable; }
  void testRelease(int k, Millis now);
  void expireTests(Millis now);
  Millis pin_wait_ms_ = 0, pin_wait_until_ = 0;  // the page password's throttle
  size_t (*heap_probe_)() = nullptr;
  size_t (*block_probe_)() = nullptr;
  // shared by MQTT (control/, config/set/, config/now/) and the board link: false and why (as <p>/error says it)
  bool remoteControl(const std::string& name, bool from_board, std::string& why);
  bool remoteSetting(const std::string& name, const std::string& value, bool save, std::string& why);
  // the value of a setting as the flash keeps it (another board's change left out)
  bool storedValue(const std::string& key, Value& out) const;
  void mqttLinkOut(const LinkOut& o);
  struct LinkPending {
    uint32_t seq = 0;
    bool all = false;
    std::string to, line;  // line: "tell porch blackout", for the log
    Millis until = 0;
    std::vector<std::string> answered;
  };
  std::deque<LinkPending> link_pending_;
  std::vector<MqttMsg> link_mqtt_out_;   // tells for mqttPoll
  std::vector<std::string> mqtt_names_;  // boards online on the broker (<root>/+/online, for MQTT tell all)
  uint32_t link_seq_ = 0;
  Millis link_window_at_ = 0;
  uint8_t link_window_n_ = 0;
  uint32_t link_dropped_ = 0;
  std::map<std::string, Value> link_kept_;
  std::atomic<uint32_t> link_epoch_{1};
  std::atomic<bool> link_work_{false};
  std::string remote_by_;  // the board whose tell starts an effect (for the log)
  int (*lock_release_)(void*) = nullptr;
  void (*lock_retake_)(void*, int) = nullptr;
  void* lock_user_ = nullptr;
  bool in_tick_ = false;  // in the step (Controller::tick, or a StepScope): the lock is never given back there
  // A window is open: a request has given the lock back for work on its own data (Unlocked). Only the step can
  // run controller code meanwhile (the board's lock makes every other task wait: window_lock.h); a change asked
  // from the step then is refused (BUSY_TEXT), so the window's reads (the profile, the stored script, the
  // settings) never change under it. Written and read only with the lock held.
  mutable bool window_open_ = false;
  static constexpr const char* BUSY_TEXT = "the board is busy with a request: try again in a moment";
  // the lock given back for this scope (work on data only this request touches, or the profile and stored script,
  // which nothing changes meanwhile); nothing on the host without lock hooks, nothing in the step, and nothing
  // while another window is open
  class Unlocked {
   public:
    explicit Unlocked(const Controller& c) : c_(c) {
      if (c.in_tick_ || !c.lock_release_ || c.window_open_) return;
      c.window_open_ = true;  // before the lock goes: the step reads it with the lock
      n_ = c.lock_release_(c.lock_user_);
      if (!n_) c.window_open_ = false;
    }
    ~Unlocked() {
      if (!n_) return;
      c_.lock_retake_(c_.lock_user_, n_);
      c_.window_open_ = false;
    }
    Unlocked(const Unlocked&) = delete;
    Unlocked& operator=(const Unlocked&) = delete;

   private:
    const Controller& c_;
    int n_ = 0;
  };
  Millis role_until_[CHANNEL_COUNT] = {0};
  std::vector<uint16_t> code_shows_;
  std::unique_ptr<EffectMachine> fx_;
  Origin origin_ = Origin::Http;
  uint32_t session_ = 0;
  uint32_t fx_session_ = 0;     // the WebSocket session that gave the remote enable (SM-R2)
  std::string fx_by_;           // who gave it: "web:<n>", "mqtt", "serial"
  Millis sample_now_ = 0;  // the latest 1 ms input sample: a request between steps acts at that millisecond
  Millis fxNow() const { return sample_now_ > now_ ? sample_now_ : now_; }
  bool trig1_held_ = false;  // TRIG1's debounced press, for the safety hold ...
  Millis trig1_since_ = 0;   // ... since
  bool recover_lease_ = false;      // a held recover from the web, renewed by the page's repeats ...
  Millis recover_lease_until_ = 0;  // ... until // the web Trigger button is held (down:true without down:false)
  std::vector<std::pair<std::string, std::string>> status_published_;
  Millis now_ = 0;
  RequestHook request_hook_ = nullptr;
  std::string (*mem_probe_)() = nullptr;
  uint32_t (*us_clock_)() = nullptr;
  static const int PROFILE_PARTS = 8;
  uint32_t prof_sum_[PROFILE_PARTS] = {0}, prof_max_[PROFILE_PARTS] = {0}, prof_n_ = 0, prof_at_ = 0;
  uint32_t prof_capture_max_ = 0;  // the state's copy under the lock
  void profMark(int part);
  void* request_hook_user_ = nullptr;
  struct MqttTrack {
    std::string last_state;
    Millis last_at = 0;
    uint32_t seq = 0;
    std::vector<std::pair<std::string, std::string>> published;
    std::string prefix;
    bool has_target = false;
    std::string host;
    uint16_t port = 0;
    uint32_t epoch = 0;
    Millis status_at = 0, time_left_at = 0;
    bool names = false;    // subscribed to <root>/+/online (a script that tells all over MQTT)
    bool answers = false;  // subscribed to <p>/link/answer (the board link goes over MQTT)
  } mqtt_;
  bool mqttWantsNames() const { return linkVia() == LinkVia::Mqtt && script_prog_ && script_prog_->tell_all; }
  // every setting as MQTT messages, built from a copy without the lock
  std::vector<MqttMsg> configMsgsUnlocked(const std::string& prefix, const std::vector<ParamValue>& status);
  // the effect's status values go out when they change, at most every MQTT_STATUS_MIN_MS (a short gap between two
  // runs still shows); a time left (counting down every ms) at most every MQTT_STATUS_PERIOD_MS
  static const Millis MQTT_STATUS_MIN_MS = 100;
  static const Millis MQTT_STATUS_PERIOD_MS = 1000;
  // show scripts
  Storage* script_store_ = nullptr;
  std::string script_text_;
  SoundList script_sounds_;
  std::string script_error_;  // the stored script failed its re-check at boot
  std::unique_ptr<ScriptProgram> script_prog_;
  std::unique_ptr<ScriptRunner> script_run_;
  DmxOut* dmx_ = nullptr;
  bool dmx_on_ = false;
  Millis dmx_at_ = 0;
  bool booted_ = false;
  Millis boot_ms_ = 0;
  ScriptInputs scriptInputs() const;
  void scriptInstall(std::unique_ptr<ScriptProgram> prog);
  void scriptTick(Millis now);
  bool scriptWaitsFor(int input) const { return script_prog_ && (script_prog_->inputs & (1u << input)); }
  // the show is the factory example exactly as earlier firmware stored it on a fresh board (FACTORY_EXAMPLE_SHOW)
  static bool isFactoryExample(const Show& s);
  Response scriptOp(Request& r);
  SoundConfig soundConfig() const;
  // the script's record: written (or removed when empty); false and the reason when it could not be stored
  // (the old script is loaded back from its store)
  [[nodiscard]] bool scriptWrite(const std::string& text, const SoundList& sounds, const ScriptProgram* table, bool empty,
                                 std::string& why);
  // the record to store (script_record.h): the text packed, the checked table and what it was checked against;
  // empty when it cannot be stored
  std::string scriptRecord(const std::string& text, const SoundList& sounds, const ScriptProgram* table) const;
  uint32_t scriptKey(const SoundList& sounds) const;
  // a table checked with other settings does what the running one does (a name moved to another terminal
  // passes the check but changes the table: SS-N)
  bool scriptSameTable(const ScriptProgram& t) const;
  // the stored record must be written again (a format-1 record, or a table checked again at power-on); held
  // while a new image has not passed its self-check, so a rollback still finds a record it can read
  bool script_rewrite_ = false;
  void scriptRewrite();
  [[nodiscard]] bool scriptLoad();  // from the store, checked again; false when a record is there but cannot be read
  bool scriptEffectRunning() const;
  // over-the-air updates
  UpdateTarget* update_target_ = nullptr;
  struct UpdateSession {
    bool active = false;
    uint32_t total = 0, done = 0, next_event = 0;
    uint8_t expected[32]{};
    Sha256 sha;
    std::string head;  // the first bytes, held until the header is checked
    bool header_checked = false;
    bool restart_at_set = false;
    Millis restart_at = 0;
  } upd_;
  bool records_ok_ = true;
  bool self_check_done_ = false;
  bool store_migrated_ = false;   // the settings record was converted from an older format (in RAM)
  bool fresh_ = false;            // nothing was stored at power-on (a new board, or after a factory reset)
  int store_newer_ = 0;           // the settings record's format when it is from a newer firmware (kept)
  bool store_unreadable_ = false; // the settings record could not be read (kept until a factory reset)
  bool wiped_ = false;             // a factory reset cleared the flash: nothing is written until the restart
  int script_newer_ = 0;           // the script record's format when it is from a newer firmware (kept)
  bool script_unreadable_ = false; // the script record could not be read (kept until a factory reset)
  // the script record must not be written: "" or why (newer or unreadable, kept until a factory reset)
  std::string scriptHeldText() const;
  Millis save_retry_ms_ = 0;      // after a failed save
  bool storeHeld() const;
  const char* updateBusy() const;
  std::string updateFailed(const UpdateError& e);
  void updateEvent(const char* phase, uint32_t done, uint32_t total, const std::string* message);
  void updateTick(Millis now);
  bool self_check_due_ = false;  // the step says: the self-check's time has come (background() marks the image)
  void selfCheckVerdict();
  Response backupOp(Request& r);
  Response boardOp(Request& r);
  // get_defaults / apply_defaults: the running board's defaults (its board package's, else the image's own)
  Response defaultsOp(Request& r);
  // the running board's defaults as text; false when they cannot be read (`source`: "board_package", "firmware" or
  // "none"; `why` when false)
  bool runningDefaults(std::string& source, std::string& profile, std::string& settings, std::string& script, std::string& script_name,
                       std::string& why);
  FactoryDefaults image_defaults_;  // the image's own (compiled in), kept from applyBoardDefaults
  // factory_reset keep_wifi: what the reset kept (name, Wi-Fi, hotspot password), put back after the defaults
  std::unique_ptr<Settings> reset_kept_;
  void restoreKept();
  // board packages
  Storage* board_store_ = nullptr;
  BoardBoot board_boot_;
  ChipRules chip_;
  std::vector<BuiltinBoard> builtins_;
  bool board_stored_ = false;             // a package is in the store (as this board last read or wrote it) ...
  std::string board_stored_id_, board_stored_name_;  // ... this one
  bool board_pending_ = false;            // the store differs from what runs: a restart applies it
  std::string restore(Request& r, bool keep_name, std::vector<std::string>& skipped);
};

}  // namespace pt
