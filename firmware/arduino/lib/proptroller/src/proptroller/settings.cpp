#include "settings.h"
#include <memory>
#include "password.h"
#include <stdio.h>
#include <string.h>

namespace pt {

const char* const NAME_IDS[13] = {"OUT1", "OUT2", "OUT3", "OUT4", "RLY1", "RLY2", "CC1", "CC2", "TRIG1", "TRIG2", "USER1", "USER2", "USER3"};
static const char* const NAME_KEYS[13] = {"out1", "out2", "out3", "out4", "rly1", "rly2", "cc1", "cc2", "trig1", "trig2", "user1", "user2", "user3"};
static const char* const ROLE_NAMES[] = {"shows", "trigger_repeat", "sound_trigger", "none"};
static const char* const ROLE_LABELS[] = {"shows", "trigger repeat", "sound trigger", "not used"};

const char* roleName(OutputRole r) { return ROLE_NAMES[(int)r]; }
const char* roleLabel(OutputRole r) { return ROLE_LABELS[(int)r]; }
bool parseRole(const std::string& s, OutputRole& out) {
  for (int i = 0; i < 4; i++)
    if (s == ROLE_NAMES[i]) { out = (OutputRole)i; return true; }
  return false;
}

EngineConfig Settings::engine() const {
  EngineConfig e;
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    e.outputs[i].max_on_ms = max_on_ms[i];
    if (i < 4) e.outputs[i].soft_start_ms = soft_start_ms[i];
  }
  if (interlock_relays) e.interlocks.push_back({Channel::Rly1, Channel::Rly2});
  e.has_pause_input = trig2 == Trig2Role::Pause;
  e.pause_input = Input::Trig2;
  return e;
}

SoundConfig Settings::sound() const {
  SoundConfig c;
  c.ambient = ambient;
  c.ambient_track = ambient_track;
  c.ambient_volume = ambient_volume;
  c.trigger_track = trigger_track;
  c.trigger_volume = trigger_volume;
  return c;
}

const char* boardLinkName(BoardLink b) {
  static const char* const N[4] = {"auto", "mqtt", "wifi", "off"};
  return N[(int)b & 3];
}

OutputRole Settings::role(Channel c) const {
  for (int i = 0; i < 4; i++)
    if (ROLE_CHANNELS[i] == c) return roles[i];
  return OutputRole::Shows;
}

static std::string lower(const char* s) {
  std::string o(s);
  for (auto& c : o) c = (char)tolower((unsigned char)c);
  return o;
}

// Every limit of the settings, named.
static const uint16_t NAME_MAX_LEN = 32, PAGE_PASSWORD_MAX_LEN = 32, SSID_MAX_LEN = 32, WIFI_PASS_MAX_LEN = 63, BROKER_MAX_LEN = 64;
// remote_free_effects: a few effect names (an effect name has up to 32 characters)
static const uint16_t REMOTE_FREE_MAX_LEN = 128;
static const uint16_t LINK_GROUP_MAX_LEN = 16, LINK_KEY_MAX_LEN = 64;  // link.h LINK_GROUP_MAX, LINK_KEY_MAX
static const uint16_t TERMINAL_NAME_MAX_LEN = 24;
static const uint32_t SOFT_START_MAX_MS = 2000, MAX_ON_MAX_MS = 3600000;
// the reference's range for both (STATE-MACHINE.md): 10-1000 ms
static const uint32_t TRIG_DEBOUNCE_MIN_MS = 10, TRIG_DEBOUNCE_MAX_MS = 1000, TRIG_RELEASE_MIN_MS = 10, TRIG_RELEASE_MAX_MS = 1000;
static const uint32_t PIXEL_COUNT_MAX = 600;
static const uint32_t VOLUME_MAX = 30, PORT_MAX = 65535;
static const uint32_t ROLE_PULSE_MIN_MS = 20, ROLE_PULSE_MAX_MS = 5000;

// One row of the settings table: what ParamDesc::integer / boolean / text / choice and withHelp / needsReboot
// take, as constant data (one loop instead of a constructor call per setting).
struct ParamRow {
  ParamDesc::Kind kind;
  const char *key, *label, *group;
  uint32_t min, max;     // Int
  const char* unit;      // Int
  uint16_t max_len;      // Text
  bool secret;           // Text
  const char* options;   // Choice: "a|b|c"
  const char* help;
  bool reboot;
};
typedef ParamDesc PD;
#define P_INT(k, l, g, lo, hi, u) PD::Kind::Int, k, l, g, lo, hi, u, 0, false, nullptr
#define P_BOOL(k, l, g) PD::Kind::Bool, k, l, g, 0, 0, "", 0, false, nullptr
#define P_TEXT(k, l, g, n, sec) PD::Kind::Text, k, l, g, 0, 0, "", n, sec, nullptr
#define P_CHOICE(k, l, g, o) PD::Kind::Choice, k, l, g, 0, 0, "", 0, false, o

static const ParamRow DEVICE_ROWS[] = {
  {P_TEXT("name", "Name", "Device", NAME_MAX_LEN, false), "Shown in the app and used as the network name (name.local)", true},
  {P_BOOL("page_lock", "Page lock", "Device"), "Off: joining the board's Wi-Fi is the only gate. On: changes, restart and the like need the page password (turning it off too)", false},
  {P_TEXT("pin", "Page password", "Device", PAGE_PASSWORD_MAX_LEN, true), "Used only while Page lock is on. Kept as a salted hash; never shown", false},
  {P_TEXT("wifi_ssid", "Wi-Fi network", "Wi-Fi", SSID_MAX_LEN, false), "Empty: the board makes its own setup Wi-Fi. A new network saved with Wi-Fi password empty is joined as an open network", true},
  {P_TEXT("wifi_pass", "Wi-Fi password", "Wi-Fi", WIFI_PASS_MAX_LEN, true), "Never shown. Empty keeps the stored one, except with a new network: then the network is open (no password)", true},
  {P_TEXT("ap_pass", "Setup hotspot password", "Wi-Fi", WIFI_PASS_MAX_LEN, true), "The password of the board's own setup Wi-Fi (Proptroller-XXXX), used when no network is set or it cannot be joined. The default is simply \"password\": change it here, 8-63 characters", true},
};
// the help of trig1_input / trig2_input: the board pulls the tip up (4.7k), so a sensor must drive it low too, and an
// unplugged jack reads high
#define TRIG_INPUT_HELP                                                                                            \
  "switch: switch, button or relay contact (closes to ground). sensor_high: powered sensor (its signal goes high), "   \
  "fed from the jack's ring (3.3 V, 15 mA at most); it must drive its signal low when idle, or it reads as always "    \
  "on. In sensor_high an unplugged cable counts as a trigger: use switch for anything safety-related"
// the help of trig1_wait / trig2_wait (SHOW-SCRIPT.md SS-R4)
#define TRIG_WAIT_HELP                                                                                              \
  "How a show script's first \"wait for\" on this input starts its effect. ready: released since the effect's last " \
  "run, then held for the debounce while the effect could start (a press held through a run starts the next one "  \
  "as soon as it can; taps while it cannot start are not saved). return: a press after a release of about 0.2 s; "  \
  "one while the effect cannot start (its own run too) is saved and starts it when it can if still held then"
static const ParamRow BOARD_ROWS[] = {
  {P_BOOL("interlock_relays", "RLY1 / RLY2 never together", "Outputs"), "For reversing a motor with the two relays", false},
  {P_CHOICE("trig2", "TRIG2 role", "Inputs", "trigger|pause"), "", false},
  {P_CHOICE("trig1_input", "TRIG1 input type", "Inputs", "switch|sensor_high"), TRIG_INPUT_HELP, false},
  {P_CHOICE("trig2_input", "TRIG2 input type", "Inputs", "switch|sensor_high"), TRIG_INPUT_HELP, false},
  {P_CHOICE("trig1_wait", "TRIG1 starts a script effect", "Inputs", "ready|return"), TRIG_WAIT_HELP, false},
  {P_CHOICE("trig2_wait", "TRIG2 starts a script effect", "Inputs", "ready|return"), TRIG_WAIT_HELP, false},
  {P_TEXT("remote_free_effects", "Script effects started remotely without the remote trigger", "Inputs", REMOTE_FREE_MAX_LEN, false),
   "Names of show-script effects, separated by commas, that the web page, MQTT and serial may start without "
   "remote_trigger_enable (one start waits while they are busy). Every other effect needs it", false},
  {P_INT("trig_debounce_ms", "Trigger debounce", "Inputs", TRIG_DEBOUNCE_MIN_MS, TRIG_DEBOUNCE_MAX_MS, "ms"), "For the pulse-and-recover effect: the hold-to-confirm time (TRIG1 closed this long while ready starts a run)", false},
  {P_INT("trig_release_ms", "Trigger re-arm after release", "Inputs", TRIG_RELEASE_MIN_MS, TRIG_RELEASE_MAX_MS, "ms"), "", false},
  {P_INT("pixel_count", "Pixels", "Pixels", 0, PIXEL_COUNT_MAX, ""), "", false},
  {P_INT("pixel_brightness", "Pixel brightness", "Pixels", 0, PIXEL_BRIGHTNESS_MAX, "%"), "A cap on every colour (100 = full); the firmware does not measure pixel current", false},
  {P_INT("pixel_speed", "Pixel speed", "Pixels", PIXEL_SPEED_MIN, PIXEL_SPEED_MAX, "%"), "How fast a show script's pixel effects run: 100 = normal, 200 = twice as fast, 50 = half as fast", false},
  {P_CHOICE("dmx_port", "DMX port", "Expansion", "off|s4"), "s4 sends DMX to a MAX485 module on expansion S4 (SHOW-SCRIPT.md \xc2\xa7" "7.4)", false},
  {P_BOOL("ambient", "Ambient sound while idle", "Sound"), "Loops the ambient sound; skipped when the card has no such file", false},
  {P_INT("ambient_track", "Ambient sound", "Sound", 1, SOUND_MAX_TRACK, ""), "Sound 1 is the one file in the card's folder 01 (01/001-<name>.mp3), sound 2 the one in folder 02, \xe2\x80\xa6", false},
  {P_INT("ambient_volume", "Ambient volume", "Sound", 0, VOLUME_MAX, ""), "", false},
  {P_INT("trigger_track", "Trigger sound", "Sound", 1, SOUND_MAX_TRACK, ""), "Sound 1 is the one file in the card's folder 01 (01/001-<name>.mp3), sound 2 the one in folder 02, \xe2\x80\xa6", false},
  {P_INT("trigger_volume", "Trigger sound volume", "Sound", 0, VOLUME_MAX, ""), "", false},
  {P_BOOL("df_ack", "Ask the player for ACKs", "Sound"), "Turn off for clones that never answer", false},
  {P_BOOL("df_checksum", "Send checksums", "Sound"), "", false},
  {P_BOOL("status_led", "Status LED", "Device"), "", false},
  {P_BOOL("stop_button", "STOP button on the page", "Device"), "Off hides the phone page's STOP (for a prop whose emergency stop is pulling the power)", false},
  {P_CHOICE("start_buttons", "Start buttons on the page", "Device", "click|hold"), "How the phone page's buttons start a script effect: a click, or a press held about a second (letting go early sends nothing)", false},
  {P_TEXT("mqtt_broker", "MQTT broker", "MQTT", BROKER_MAX_LEN, false), "Host name or address; empty = MQTT off. Topics: proptroller/<name>/\xe2\x80\xa6", true},
  {P_INT("mqtt_port", "MQTT port", "MQTT", 1, PORT_MAX, ""), "", true},
  {P_CHOICE("board_link", "Tell lines go", "Board link", "auto|mqtt|wifi|off"),
   "How this board's tell lines reach other boards. auto: MQTT when a broker is set, otherwise straight to the "
   "boards on the same Wi-Fi. mqtt: through the broker only. wifi: straight to the other boards (an ESP32 also sends "
   "by ESP-NOW). off: sends none and takes none peer to peer", false},
  {P_TEXT("link_group", "Board link group", "Board link", LINK_GROUP_MAX_LEN, false),
   "Boards with the same group hear each other's tell lines (empty is a group too)", false},
  {P_TEXT("link_key", "Board link key", "Board link", LINK_KEY_MAX_LEN, true),
   "A shared key for the group: with one set, a board takes tells peer to peer only from boards with the same key, "
   "signed and never replayed. Empty: anyone on the network can send. Never shown", false},
};
static const ParamRow ROLE_PULSE_ROW[] = {
  {P_INT("role_pulse_ms", "Role pulse length", "Output roles", ROLE_PULSE_MIN_MS, ROLE_PULSE_MAX_MS, "ms"), "", false},
};
// the pulse-and-recover effect (firmware/STATE-MACHINE.md)
#define G "Effect"
static const ParamRow EFFECT_ROWS[] = {
  {P_CHOICE("effect", "Built-in effect", G, "none|pulse_and_recover"), "pulse_and_recover: TRIG1 starts a run (OUT1 pulse, then the OUT2 recovery), TRIG2 recovers only. Those terminals are then the effect's", false},
  {P_INT("out1_on_ms", "OUT1 pulse time", G, EFFECT_OUT1_ON_MIN_MS, EFFECT_OUT1_ON_MAX_MS, "ms"), "", false},
  {P_INT("out2_on_ms", "Recovery time (OUT2)", G, EFFECT_OUT2_ON_MIN_MS, EFFECT_OUT2_ON_MAX_MS, "ms"), "", false},
  {P_BOOL("out3_enabled", "OUT3 after-pulse", G), "OUT3 on after the pulse; the effect is ready again when OUT3 goes off while OUT2 recovers in the background", false},
  {P_INT("out3_on_ms", "OUT3 after-pulse time", G, EFFECT_OUT3_ON_MIN_MS, EFFECT_OUT3_ON_MAX_MS, "ms"), "", false},
  {P_BOOL("out4_ready_lamp", "OUT4 ready lamp", G), "OUT4 steady when ready, blinking otherwise", false},
  {P_INT("cooldown_ms", "Cooldown", G, EFFECT_COOLDOWN_MIN_MS, EFFECT_COOLDOWN_MAX_MS, "ms"), "Counted from the first run; 0 = no cooldown", false},
  {P_INT("runs_per_cooldown", "Runs per cooldown", G, EFFECT_RUNS_MIN, EFFECT_RUNS_MAX, ""), "", false},
  {P_BOOL("auto_arm", "Safety off at power-on", G), "Off: hold TRIG1 2 s after power-on to release the safety", false},
  {P_CHOICE("recover_mode", "Recover", G, "cycle|timed|hold"), "cycle = the recovery without the pulse; timed = OUT2 for the recover time; hold = OUT2 while TRIG2 is held (30 s at most)", false},
  {P_INT("recover_time_ms", "Recover time (timed)", G, EFFECT_RECOVER_TIME_MIN_MS, EFFECT_RECOVER_TIME_MAX_MS, "ms"), "", false},
  {P_INT("trigger_out_pulse_ms", "Trigger out / sound out pulse", G, EFFECT_TRIGGER_OUT_PULSE_MIN_MS, EFFECT_TRIGGER_OUT_PULSE_MAX_MS, "ms"), "Every run pulses the outputs whose role is trigger repeat or sound trigger this long", false},
  {P_BOOL("remote_trigger_enable", "Remote trigger", G), "Lets the web, MQTT and serial start runs. Off at every power-on; from a phone page it stays on only while that page is open", false},
};
#undef G
#undef P_INT
#undef P_BOOL
#undef P_TEXT
#undef P_CHOICE

static ParamDesc rowDesc(const ParamRow& r) {
  ParamDesc d;
  d.key = r.key; d.label = r.label; d.group = r.group; d.kind = r.kind;
  d.min = r.min; d.max = r.max; d.unit = r.unit; d.max_len = r.max_len; d.secret = r.secret;
  if (r.options)
    for (const char* o = r.options; *o;) {
      const char* e = strchr(o, '|');
      size_t n = e ? (size_t)(e - o) : strlen(o);
      d.options.emplace_back(o, n);
      o += n + (e ? 1 : 0);
    }
  d.help = r.help;
  d.reboot = r.reboot;
  return d;
}

template <size_t N>
static void addRows(std::vector<ParamDesc>& p, const ParamRow (&rows)[N]) {
  for (const ParamRow& r : rows) p.push_back(rowDesc(r));
}

static std::vector<ParamDesc> buildParams() {
  std::vector<ParamDesc> p;
  addRows(p, DEVICE_ROWS);
  char k[32], l[40];
  for (int i = 1; i <= 4; i++) {
    (void)snprintf(k, sizeof k, "out%d_soft_start_ms", i);
    (void)snprintf(l, sizeof l, "OUT%d soft start", i);
    p.push_back(ParamDesc::integer(k, l, "Outputs", 0, SOFT_START_MAX_MS, "ms"));
  }
  for (auto c : ALL_CHANNELS) {
    std::string key = lower(name(c)) + "_max_on_ms";
    p.push_back(ParamDesc::integer(key, std::string(name(c)) + " max on", "Watchdogs", 0, MAX_ON_MAX_MS, "ms").withHelp("0 = no limit"));
  }
  addRows(p, BOARD_ROWS);
  for (auto c : ROLE_CHANNELS) {
    p.push_back(ParamDesc::choice(lower(name(c)) + "_role", std::string(name(c)) + " role", "Output roles", {"shows", "trigger_repeat", "sound_trigger", "none"})
                    .withHelp("shows = driven by shows; trigger_repeat = a pulse for each accepted trigger (chain the next controller); sound_trigger = a pulse when a sound starts. An output with another role cannot be used in shows"));
  }
  addRows(p, ROLE_PULSE_ROW);
  for (int i = 0; i < 13; i++)
    p.push_back(ParamDesc::text(std::string(NAME_KEYS[i]) + "_name", std::string(NAME_IDS[i]) + " is connected to", "Names", TERMINAL_NAME_MAX_LEN, false));
  addRows(p, EFFECT_ROWS);
  return p;
}

const std::vector<ParamDesc>& Settings::params() {
  static const std::vector<ParamDesc> p = buildParams();
  return p;
}

const ParamDesc* Settings::find(const std::string& key) {
  for (auto& d : params())
    if (d.key == key) return &d;
  return nullptr;
}

std::vector<ParamValue> Settings::values() const {
  std::vector<ParamValue> v;
  for (auto& d : params()) {
    ParamValue pv;
    pv.key = d.key;
    if (d.isSecret() || !get(d.key, pv.value)) pv.value = Value::text("");
    v.push_back(pv);
  }
  return v;
}

static bool endsWith(const std::string& s, const char* suf, std::string& head) {
  size_t n = strlen(suf);
  if (s.size() < n || s.compare(s.size() - n, n, suf) != 0) return false;
  head = s.substr(0, s.size() - n);
  return true;
}

static int roleKey(const std::string& key) {
  std::string h;
  Channel c = Channel::Out1;
  if (!endsWith(key, "_role", h) || !parseChannel(h.c_str(), c)) return -1;
  for (int i = 0; i < 4; i++)
    if (ROLE_CHANNELS[i] == c) return i;
  return -1;
}

static int nameKey(const std::string& key) {
  std::string h;
  if (!endsWith(key, "_name", h)) return -1;
  for (int i = 0; i < 13; i++)
    if (h == NAME_KEYS[i]) return i;
  return -1;
}

static bool maxOnKey(const std::string& key, Channel& c) {
  std::string h;
  return endsWith(key, "_max_on_ms", h) && parseChannel(h.c_str(), c);
}

// out<N>_<field>: returns N-1 or -1
static int outKey(const std::string& key, std::string& field) {
  if (key.compare(0, 3, "out") != 0) return -1;
  size_t u = key.find('_', 3);
  if (u == std::string::npos || u == 3) return -1;
  std::string n = key.substr(3, u - 3);
  for (char ch : n)
    if (!isdigit((unsigned char)ch)) return -1;
  long i = strtol(n.c_str(), nullptr, 10);  // digits only (checked above)
  if (i < 1 || i > 4) return -1;
  field = key.substr(u + 1);
  if (field != "soft_start_ms") return -1;  // out1_on_ms and the like are the effect's
  return (int)i - 1;
}

bool Settings::get(const std::string& key, Value& out) const {
  Channel c = Channel::Out1;
  if (maxOnKey(key, c)) { out = Value::integer(max_on_ms[index(c)]); return true; }
  int r = roleKey(key);
  if (r >= 0) { out = Value::text(roleName(roles[r])); return true; }
  int n = nameKey(key);
  if (n >= 0) { out = Value::text(names[n]); return true; }
  std::string field;
  int o = outKey(key, field);
  if (o >= 0) {
    if (field == "soft_start_ms") { out = Value::integer(soft_start_ms[o]); return true; }
    return false;
  }
  if (key == "name") out = Value::text(name);
  else if (key == "pin") out = Value::text("");  // set-only: reads are blank
  else if (key == "page_lock") out = Value::boolean(page_lock);
  else if (key == "wifi_ssid") out = Value::text(wifi_ssid);
  else if (key == "wifi_pass") out = Value::text(wifi_pass);
  else if (key == "ap_pass") out = Value::text(ap_pass);
  else if (key == "interlock_relays") out = Value::boolean(interlock_relays);
  else if (key == "trig2") out = Value::text(trig2 == Trig2Role::Pause ? "pause" : "trigger");
  else if (key == "trig1_input" || key == "trig2_input") out = Value::text(trig_sensor_high[key[4] - '1'] ? "sensor_high" : "switch");
  else if (key == "trig1_wait" || key == "trig2_wait") out = Value::text(trig_wait_return[key[4] - '1'] ? "return" : "ready");
  else if (key == "remote_free_effects") out = Value::text(remote_free_effects);
  else if (key == "trig_debounce_ms") out = Value::integer(trig_debounce_ms);
  else if (key == "trig_release_ms") out = Value::integer(trig_release_ms);
  else if (key == "dmx_port") out = Value::text(dmx_s4 ? "s4" : "off");
  else if (key == "pixel_count") out = Value::integer(pixel_count);
  else if (key == "pixel_brightness") out = Value::integer(pixel_brightness);
  else if (key == "pixel_speed") out = Value::integer(pixel_speed);
  else if (key == "ambient") out = Value::boolean(ambient);
  else if (key == "ambient_track") out = Value::integer(ambient_track);
  else if (key == "ambient_volume") out = Value::integer(ambient_volume);
  else if (key == "trigger_track") out = Value::integer(trigger_track);
  else if (key == "trigger_volume") out = Value::integer(trigger_volume);
  else if (key == "role_pulse_ms") out = Value::integer(role_pulse_ms);
  else if (key == "df_ack") out = Value::boolean(df_ack);
  else if (key == "df_checksum") out = Value::boolean(df_checksum);
  else if (key == "status_led") out = Value::boolean(status_led);
  else if (key == "stop_button") out = Value::boolean(stop_button);
  else if (key == "start_buttons") out = Value::text(start_hold ? "hold" : "click");
  else if (key == "mqtt_broker") out = Value::text(mqtt_broker);
  else if (key == "mqtt_port") out = Value::integer(mqtt_port);
  else if (key == "board_link") out = Value::text(boardLinkName(board_link));
  else if (key == "link_group") out = Value::text(link_group);
  else if (key == "link_key") out = Value::text(link_key);
  else if (key == "effect") out = Value::text(effect_on ? "pulse_and_recover" : "none");
  else if (key == "out1_on_ms") out = Value::integer(fx.out1_on_ms);
  else if (key == "out2_on_ms") out = Value::integer(fx.out2_on_ms);
  else if (key == "out3_enabled") out = Value::boolean(fx.out3_enabled);
  else if (key == "out3_on_ms") out = Value::integer(fx.out3_on_ms);
  else if (key == "out4_ready_lamp") out = Value::boolean(out4_ready_lamp);
  else if (key == "cooldown_ms") out = Value::integer(fx.cooldown_ms);
  else if (key == "runs_per_cooldown") out = Value::integer(fx.runs_per_cooldown);
  else if (key == "auto_arm") out = Value::boolean(fx.auto_arm);
  else if (key == "recover_mode") out = Value::text(recoverModeName(fx.recover_mode));
  else if (key == "recover_time_ms") out = Value::integer(fx.recover_time_ms);
  else if (key == "trigger_out_pulse_ms") out = Value::integer(trigger_out_pulse_ms);
  else if (key == "remote_trigger_enable") out = Value::boolean(fx.remote_trigger_enable);
  else return false;
  return true;
}

static std::string trimmed(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) a++;
  while (b > a && isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

const char* Settings::set(const std::string& key, const Value& v) {
  bool isInt = v.type == Value::Type::Int, isText = v.type == Value::Type::Text, isBool = v.type == Value::Type::Bool;
  Channel c = Channel::Out1;
  if (maxOnKey(key, c)) { if (!isInt) return "wrong type"; max_on_ms[index(c)] = (uint32_t)v.i; return nullptr; }
  int r = roleKey(key);
  if (r >= 0) {
    if (!isText) return "wrong type";
    if (!parseRole(v.s, roles[r])) return "not a role";
    return nullptr;
  }
  int n = nameKey(key);
  if (n >= 0) { if (!isText) return "wrong type"; names[n] = v.s; return nullptr; }
  std::string field;
  int o = outKey(key, field);
  if (o >= 0) {
    if (!isInt) return "wrong type";
    if (field == "soft_start_ms") soft_start_ms[o] = (uint32_t)v.i;
    else return "unknown key";
    return nullptr;
  }
  if (key == "name") {
    if (!isText) return "wrong type";
    if (trimmed(v.s).empty()) return "empty name";
    // `broadcast` is the topic every board listens to; MQTT wildcards cannot be in a topic
    if (hostLabel(v.s, "x") == "broadcast" || v.s.find_first_of("+#/") != std::string::npos)
      return "that name cannot be used: not \"broadcast\", and no + # or /";
    name = v.s;
    return nullptr;
  }
  if (key == "ap_pass") {
    if (!isText) return "wrong type";
    if (v.s.size() < AP_PASS_MIN_LEN) return "hotspot password needs 8 characters";
    ap_pass = v.s;
    return nullptr;
  }
  if (key == "pin") {
    if (!isText) return "wrong type";
    if (v.s.empty()) {
      pin_hash.clear();
    } else {
      pin_salt = newSalt(pin_salt + ":" + std::to_string(v.s.size()));
      pin_hash = passwordHash(pin_salt, v.s);
    }
    return nullptr;
  }
  struct S { const char* k; std::string* p; };
  S texts[] = { {"wifi_ssid", &wifi_ssid}, {"wifi_pass", &wifi_pass}, {"mqtt_broker", &mqtt_broker}, {"remote_free_effects", &remote_free_effects},
                {"link_group", &link_group}, {"link_key", &link_key}};
  for (auto& t : texts)
    if (key == t.k) { if (!isText) return "wrong type"; *t.p = v.s; return nullptr; }
  struct B { const char* k; bool* p; };
  B bools[] = {{"page_lock", &page_lock}, {"interlock_relays", &interlock_relays}, {"ambient", &ambient}, {"df_ack", &df_ack}, {"df_checksum", &df_checksum}, {"status_led", &status_led}, {"stop_button", &stop_button}};
  for (auto& b : bools)
    if (key == b.k) { if (!isBool) return "wrong type"; *b.p = v.b; return nullptr; }
  if (key == "effect") { if (!isText) return "wrong type"; effect_on = v.s == "pulse_and_recover"; return nullptr; }
  if (key == "recover_mode") { if (!isText || !parseRecoverMode(v.s, fx.recover_mode)) return "wrong type"; return nullptr; }
  {
    struct FB { const char* k; bool* p; };
    FB fb[] = {{"out3_enabled", &fx.out3_enabled}, {"out4_ready_lamp", &out4_ready_lamp},
               {"auto_arm", &fx.auto_arm}, {"remote_trigger_enable", &fx.remote_trigger_enable}};
    for (auto& b : fb)
      if (key == b.k) { if (!isBool) return "wrong type"; *b.p = v.b; return nullptr; }
    struct FI { const char* k; uint32_t* p; };
    FI fi[] = {{"out1_on_ms", &fx.out1_on_ms}, {"out2_on_ms", &fx.out2_on_ms}, {"out3_on_ms", &fx.out3_on_ms}, {"cooldown_ms", &fx.cooldown_ms},
               {"recover_time_ms", &fx.recover_time_ms}, {"trigger_out_pulse_ms", &trigger_out_pulse_ms}};
    for (auto& i : fi)
      if (key == i.k) { if (!isInt) return "wrong type"; *i.p = (uint32_t)v.i; return nullptr; }
    if (key == "runs_per_cooldown") { if (!isInt) return "wrong type"; fx.runs_per_cooldown = (uint8_t)v.i; return nullptr; }
  }
  if (key == "trig2") { if (!isText) return "wrong type"; trig2 = v.s == "pause" ? Trig2Role::Pause : Trig2Role::Trigger; return nullptr; }
  if (key == "trig1_input" || key == "trig2_input") {
    if (!isText) return "wrong type";
    trig_sensor_high[key[4] - '1'] = v.s == "sensor_high";
    return nullptr;
  }
  if (key == "trig1_wait" || key == "trig2_wait") {
    if (!isText) return "wrong type";
    trig_wait_return[key[4] - '1'] = v.s == "return";
    return nullptr;
  }
  if (key == "dmx_port") { if (!isText) return "wrong type"; dmx_s4 = v.s == "s4"; return nullptr; }
  if (key == "start_buttons") { if (!isText) return "wrong type"; start_hold = v.s == "hold"; return nullptr; }
  if (key == "board_link") {
    if (!isText) return "wrong type";
    for (int i = 0; i < 4; i++)
      if (v.s == boardLinkName((BoardLink)i)) { board_link = (BoardLink)i; return nullptr; }
    return "not a choice";
  }
  static const char* const INT_KEYS[] = {"trig_debounce_ms", "trig_release_ms", "pixel_count",
                                         "pixel_brightness", "pixel_speed", "ambient_track", "ambient_volume", "trigger_track",
                                         "trigger_volume", "role_pulse_ms", "mqtt_port"};
  bool known = false;
  for (auto k : INT_KEYS) known |= key == k;
  if (!known) return "unknown key";
  if (!isInt) return "wrong type";
  uint64_t x = (uint64_t)v.i;
  if (key == "trig_debounce_ms") trig_debounce_ms = (uint32_t)x;
  else if (key == "trig_release_ms") trig_release_ms = (uint32_t)x;
  else if (key == "pixel_count") pixel_count = (uint16_t)x;
  else if (key == "pixel_brightness") pixel_brightness = (uint8_t)x;
  else if (key == "pixel_speed") pixel_speed = (uint16_t)x;
  else if (key == "ambient_track") ambient_track = (uint16_t)x;
  else if (key == "ambient_volume") ambient_volume = (uint8_t)x;
  else if (key == "trigger_track") trigger_track = (uint16_t)x;
  else if (key == "trigger_volume") trigger_volume = (uint8_t)x;
  else if (key == "role_pulse_ms") role_pulse_ms = (uint32_t)x;
  else if (key == "mqtt_port") mqtt_port = (uint16_t)x;
  else return "unknown key";
  return nullptr;
}

bool Settings::operator==(const Settings& o) const {
  JsonDocument a, b;
  toJson(a.to<JsonObject>());
  o.toJson(b.to<JsonObject>());
  std::string x, y;
  serializeJson(a, x);
  serializeJson(b, y);
  return x == y;
}

void Settings::toJson(JsonObject o) const {
  o["name"] = name;
  o["page_lock"] = page_lock;
  o["pin_hash"] = pin_hash;
  o["pin_salt"] = pin_salt;
  o["wifi_ssid"] = wifi_ssid;
  o["wifi_pass"] = wifi_pass;
  o["ap_pass"] = ap_pass;
  JsonArray a = o["soft_start_ms"].to<JsonArray>();
  for (auto x : soft_start_ms) a.add(x);
  a = o["max_on_ms"].to<JsonArray>();
  for (auto x : max_on_ms) a.add(x);
  o["interlock_relays"] = interlock_relays;
  o["trig2"] = trig2 == Trig2Role::Pause ? "pause" : "trigger";
  // only when not the default: a record from before the setting is written back unchanged
  if (trig_sensor_high[0]) o["trig1_input"] = "sensor_high";
  if (trig_sensor_high[1]) o["trig2_input"] = "sensor_high";
  if (trig_wait_return[0]) o["trig1_wait"] = "return";
  if (trig_wait_return[1]) o["trig2_wait"] = "return";
  if (!remote_free_effects.empty()) o["remote_free_effects"] = remote_free_effects;
  o["trig_debounce_ms"] = trig_debounce_ms;
  o["trig_release_ms"] = trig_release_ms;
  o["dmx_port"] = dmx_s4 ? "s4" : "off";
  o["pixel_count"] = pixel_count;
  o["pixel_brightness"] = pixel_brightness;
  o["pixel_speed"] = pixel_speed;
  o["ambient"] = ambient;
  o["ambient_track"] = ambient_track;
  o["ambient_volume"] = ambient_volume;
  o["trigger_track"] = trigger_track;
  o["trigger_volume"] = trigger_volume;
  a = o["roles"].to<JsonArray>();
  for (auto r : roles) a.add(roleName(r));
  o["role_pulse_ms"] = role_pulse_ms;
  a = o["names"].to<JsonArray>();
  for (auto& n : names) a.add(n);
  o["df_ack"] = df_ack;
  o["df_checksum"] = df_checksum;
  o["status_led"] = status_led;
  // only when not the default: a record from before the setting is written back unchanged
  if (!stop_button) o["stop_button"] = false;
  if (start_hold) o["start_buttons"] = "hold";
  o["mqtt_broker"] = mqtt_broker;
  o["mqtt_port"] = mqtt_port;
  // only when not the default: a record from before the setting is written back unchanged
  if (board_link != BoardLink::Auto) o["board_link"] = boardLinkName(board_link);
  if (!link_group.empty()) o["link_group"] = link_group;
  if (!link_key.empty()) o["link_key"] = link_key;
  // the effect (remote_trigger_enable is never stored, SM-R1)
  for (auto& d : params()) {
    if (d.group != "Effect" || d.key == "remote_trigger_enable") continue;
    Value v;
    if (get(d.key, v)) v.toJson(o, d.key.c_str());
  }
  if (!profile.empty()) profile.toJson(o["profile"].to<JsonObject>());
  if (!sketch.empty()) {
    JsonObject k = o["sketch"].to<JsonObject>();
    for (auto& e : sketch) k[e.first] = e.second;
  }
}

void Settings::fromJson(JsonVariantConst v) {
  auto s = [&](const char* k, std::string& out) { if (v[k].is<const char*>()) out = v[k].as<const char*>(); };
  auto b = [&](const char* k, bool& out) { if (v[k].is<bool>()) out = v[k].as<bool>(); };
  struct U {
    JsonVariantConst v;
    void operator()(const char* k, uint8_t& out) const { if (v[k].is<uint8_t>()) out = v[k].as<uint8_t>(); }
    void operator()(const char* k, uint16_t& out) const { if (v[k].is<uint16_t>()) out = v[k].as<uint16_t>(); }
    void operator()(const char* k, uint32_t& out) const { if (v[k].is<uint32_t>()) out = v[k].as<uint32_t>(); }
  } u{v};
  s("name", name);
  b("page_lock", page_lock);
  s("pin_hash", pin_hash);
  s("pin_salt", pin_salt);
  s("wifi_ssid", wifi_ssid);
  s("wifi_pass", wifi_pass);
  s("ap_pass", ap_pass);
  for (int i = 0; i < 4; i++) {
    if (v["soft_start_ms"][i].is<uint32_t>()) soft_start_ms[i] = v["soft_start_ms"][i].as<uint32_t>();
  }
  for (int i = 0; i < CHANNEL_COUNT; i++)
    if (v["max_on_ms"][i].is<uint32_t>()) max_on_ms[i] = v["max_on_ms"][i].as<uint32_t>();
  b("interlock_relays", interlock_relays);
  if (v["trig2"].is<const char*>()) trig2 = !strcmp(v["trig2"], "pause") ? Trig2Role::Pause : Trig2Role::Trigger;
  if (v["trig1_input"].is<const char*>()) trig_sensor_high[0] = !strcmp(v["trig1_input"], "sensor_high");
  if (v["trig2_input"].is<const char*>()) trig_sensor_high[1] = !strcmp(v["trig2_input"], "sensor_high");
  if (v["trig1_wait"].is<const char*>()) trig_wait_return[0] = !strcmp(v["trig1_wait"], "return");
  if (v["trig2_wait"].is<const char*>()) trig_wait_return[1] = !strcmp(v["trig2_wait"], "return");
  if (v["remote_free_effects"].is<const char*>()) remote_free_effects = v["remote_free_effects"].as<const char*>();
  u("trig_debounce_ms", trig_debounce_ms);
  u("trig_release_ms", trig_release_ms);
  if (v["dmx_port"].is<const char*>()) dmx_s4 = !strcmp(v["dmx_port"], "s4");
  u("pixel_count", pixel_count);
  u("pixel_brightness", pixel_brightness);
  u("pixel_speed", pixel_speed);
  if (pixel_brightness > PIXEL_BRIGHTNESS_MAX) pixel_brightness = PIXEL_BRIGHTNESS_MAX;
  b("ambient", ambient);
  u("ambient_track", ambient_track);
  u("ambient_volume", ambient_volume);
  u("trigger_track", trigger_track);
  u("trigger_volume", trigger_volume);
  for (int i = 0; i < 4; i++)
    if (v["roles"][i].is<const char*>()) parseRole(v["roles"][i].as<const char*>(), roles[i]);
  u("role_pulse_ms", role_pulse_ms);
  for (int i = 0; i < 13; i++)
    if (v["names"][i].is<const char*>()) names[i] = v["names"][i].as<const char*>();
  b("df_ack", df_ack);
  b("df_checksum", df_checksum);
  b("status_led", status_led);
  b("stop_button", stop_button);
  if (v["start_buttons"].is<const char*>()) start_hold = !strcmp(v["start_buttons"], "hold");
  s("mqtt_broker", mqtt_broker);
  u("mqtt_port", mqtt_port);
  if (v["board_link"].is<const char*>()) (void)set("board_link", Value::text(v["board_link"].as<const char*>()));
  s("link_group", link_group);
  s("link_key", link_key);
  for (auto& d : params()) {
    if (d.group != "Effect" || d.key == "remote_trigger_enable") continue;
    Value x;
    if (!Value::fromJson(v[d.key.c_str()], x) || d.check(x)) continue;
    (void)set(d.key, x);
  }
  // a stored number out of its range (a damaged or hand-made record) is replaced by the default, as set() would
  // refuse it
  static const char* const RANGED[] = {"trig_debounce_ms", "trig_release_ms", "pixel_count", "pixel_speed", "role_pulse_ms", "mqtt_port"};
  std::unique_ptr<Settings> defaults;  // on the heap (about 1 KB), only when needed
  for (const char* k : RANGED) {
    const ParamDesc* d = find(k);
    Value x, def;
    if (!d || !Value::fromJson(v[k], x) || !d->check(x)) continue;
    if (!defaults) defaults.reset(new Settings());
    if (defaults->get(k, def)) (void)set(k, def);
  }
  for (JsonPairConst e : v["sketch"].as<JsonObjectConst>()) sketch[e.key().c_str()] = e.value().as<std::string>();
  if (v["profile"].is<JsonObjectConst>()) {
    Profile p;
    std::string why;
    if (parseProfile(v["profile"], p, why)) profile = p;
  }
}

// ---------------------------------------------------------------- stored records

static const RecordMigration NO_MIGRATIONS[1] = {nullptr};
const RecordMigration* const STORED_MIGRATIONS = NO_MIGRATIONS;
const size_t STORED_MIGRATION_COUNT = 0;

RecordKind readRecord(const std::string& bytes, uint32_t current, const RecordMigration* steps, size_t n_steps, JsonDocument& out,
                      uint32_t& version) {
  version = 0;
  out.clear();
  if (deserializeJson(out, bytes) || !out.is<JsonObject>() || !out["version"].is<uint32_t>()) return RecordKind::Unreadable;
  version = out["version"].as<uint32_t>();
  if (version == current) return RecordKind::Current;
  if (version > current) { out.clear(); return RecordKind::Newer; }
  if (version == 0 || current - 1 > n_steps) { out.clear(); return RecordKind::Unreadable; }
  for (uint32_t v = version; v < current; v++) steps[v - 1](out);
  out["version"] = current;
  return RecordKind::Migrated;
}

}  // namespace pt
