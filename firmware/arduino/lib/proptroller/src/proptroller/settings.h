// Settings of the generic firmware, their protocol parameters (the Settings tab of the phone UI) and the
// stored record.
#pragma once
#include <map>
#include "effect.h"
#include "proto.h"
#include "input.h"
#include "sound.h"

namespace pt {

enum class Trig2Role : uint8_t { Trigger, Pause };

// The setup hotspot's password out of the box and after a factory reset (owner's choice: simple on purpose, as
// on the original controller; the ap_pass setting changes it). WPA2 needs 8-63 characters.
static const char AP_PASS_DEFAULT[] = "password";
static const size_t AP_PASS_MIN_LEN = 8;

// What a relay or contact output does when it is not driven by shows.
enum class OutputRole : uint8_t { Shows, TriggerRepeat, SoundTrigger, None };
const char* roleName(OutputRole r);   // "shows", "trigger_repeat", ...
const char* roleLabel(OutputRole r);  // "shows", "trigger repeat", "sound trigger", "not used"
bool parseRole(const std::string& s, OutputRole& out);

// Channels that can take a role, in settings order.
static const Channel ROLE_CHANNELS[4] = {Channel::Rly1, Channel::Rly2, Channel::Cc1, Channel::Cc2};
// Ids of the nameable terminals, in Settings::names order.
extern const char* const NAME_IDS[13];

// A project's own names for keys, controls, status values and terminals (STATE-MACHINE.md §11).
struct AliasEntry {
  enum class Kind : uint8_t { Key, Control, Status, Terminal };
  std::string alias;
  Kind kind = Kind::Key;
  std::string target;  // the key, control, status name or terminal
  std::string label, unit;
  bool invert = false;  // bool keys only
  int64_t scale = 0;    // int keys only: key value = alias value * scale (0 = none)
};
// A scaled alias's range in its own units: key value = alias value * scale, so the alias allows
// ceil(min / scale) to floor(max / scale). Computed without (min + scale - 1), which overflows for a large scale.
inline int64_t aliasMin(int64_t min, int64_t scale) { return min / scale + (min % scale > 0 ? 1 : 0); }
inline int64_t aliasMax(int64_t max, int64_t scale) { return max / scale - (max % scale < 0 ? 1 : 0); }
struct Profile {
  std::string topic_prefix;  // "" = the default "proptroller"
  std::vector<AliasEntry> aliases;
  bool empty() const { return topic_prefix.empty() && aliases.empty(); }
  void toJson(JsonObject o) const;
};
static const size_t PROFILE_MAX_ALIASES = 64, ALIAS_MAX_LEN = 32, ALIAS_LABEL_MAX_LEN = 32, ALIAS_UNIT_MAX_LEN = 8,
                    TOPIC_PREFIX_MAX_LEN = 32, TERMINAL_ALIAS_MAX_LEN = 16;
// The controls and the status values of the effect (the names aliases may point at).
extern const char* const EFFECT_CONTROLS[4];  // trigger, recover, cancel, restart
extern const char* const EFFECT_STATUS[9];    // run_count, runs_left, ...
extern const char* const RUN_STATUS[3];       // runs_since_power_on, runs_lifetime, last_run_uptime_ms (runs.h)
// Parse and check a table: false and the reason in plain words when it is refused (SM-AL4).
[[nodiscard]] bool parseProfile(JsonVariantConst v, Profile& out, std::string& why);

static const uint8_t PIXEL_BRIGHTNESS_MAX = 100;  // %

struct Settings {
  std::string name = "Proptroller";
  // Page lock (SM-P): when on, changes from the network need the page password, kept only as a salted
  // hash (setting key "pin", set-only).
  bool page_lock = false;
  std::string pin_hash, pin_salt;
  std::string wifi_ssid, wifi_pass;
  std::string ap_pass = AP_PASS_DEFAULT;
  uint32_t soft_start_ms[4] = {0, 0, 0, 0};
  uint32_t max_on_ms[CHANNEL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
  bool interlock_relays = false;
  Trig2Role trig2 = Trig2Role::Trigger;
  // TRIG1 / TRIG2 input type: false = switch (closes to ground, active low), true = sensor_high (a powered sensor
  // whose signal goes high). Combined (XOR) with the board support package's own polarity.
  bool trig_sensor_high[2] = {false, false};
  uint32_t trig_debounce_ms = TRIGGER_DEBOUNCE_MS;
  uint32_t trig_release_ms = TRIGGER_DEBOUNCE_MS;
  uint16_t pixel_count = 16;
  uint8_t pixel_brightness = PIXEL_BRIGHTNESS_MAX;  // %, a plain scale of every channel
  bool dmx_s4 = false;  // dmx_port = s4: show-script DMX frames on expansion S4 (SHOW-SCRIPT.md 7.4)
  bool ambient = true;
  uint8_t ambient_folder = 1, ambient_track = 1, ambient_volume = 15;
  uint16_t trigger_track = 1;
  uint8_t trigger_volume = 25;
  OutputRole roles[4] = {OutputRole::Shows, OutputRole::Shows, OutputRole::TriggerRepeat, OutputRole::SoundTrigger};
  uint32_t role_pulse_ms = 200;
  std::string names[13];
  bool df_ack = true, df_checksum = true;
  bool status_led = true;
  std::string mqtt_broker;
  uint16_t mqtt_port = 1883;
  // The pulse-and-recover effect (firmware/STATE-MACHINE.md): off unless a profile turns it on.
  bool effect_on = false;  // key "effect": "none" | "pulse_and_recover"
  EffectConfig fx;         // its settings (remote_trigger_enable is never stored)
  bool out4_ready_lamp = false;
  uint32_t trigger_out_pulse_ms = EFFECT_TRIGGER_OUT_PULSE_DEFAULT_MS;
  // The profile's alias table (SM-AL): a project's own names; stored with the settings.
  Profile profile;
  // What the sketch last applied (slot -> value): setup() runs at every boot, and a value the sketch did not change
  // keeps what the board was given since (SM-S2). Stored, not a setting.
  std::map<std::string, std::string> sketch;

  EngineConfig engine() const;
  SoundConfig sound() const;
  OutputRole role(Channel c) const;
  static const std::vector<ParamDesc>& params();
  static const ParamDesc* find(const std::string& key);
  std::vector<ParamValue> values() const;
  bool get(const std::string& key, Value& out) const;
  // Set one value (already checked against its ParamDesc); nullptr on success, else the reason.
  [[nodiscard]] const char* set(const std::string& key, const Value& v);
  bool operator==(const Settings& o) const;

  void toJson(JsonObject o) const;
  void fromJson(JsonVariantConst v);  // missing fields keep their defaults
};

// The settings record's format (FLASH-MAP.md "Stored records"). Bump it only for a change an older firmware would
// misread (a renamed or re-meant field) and add the step to STORED_MIGRATIONS; a new field with a default keeps the
// number (older firmwares ignore fields they do not know).
static const uint16_t STORED_VERSION = 1;
// The show script's record format (script_record.h): 1 was JSON (the first release), 2 is the compressed text with
// the checked step table. Format 1 is still read and written again as 2 once the image passed its self-check.
static const uint16_t SCRIPT_RECORD_VERSION = 1;

// One migration step: turns a record of format n into format n + 1, in place, on its JSON.
typedef void (*RecordMigration)(JsonDocument& record);
// STORED_MIGRATIONS[i] turns a format i + 1 settings record into format i + 2 (none yet: format 1 is the first).
extern const RecordMigration* const STORED_MIGRATIONS;
extern const size_t STORED_MIGRATION_COUNT;

// What a stored record turned out to be.
enum class RecordKind : uint8_t {
  Current,     // written in the current format
  Migrated,    // written by an older firmware, converted to the current format (in RAM only)
  Newer,       // written by a newer firmware: not loaded, and never overwritten
  Unreadable,  // not a record this firmware can read
};
// Read a record that carries "version": the current format directly, an older one through `steps` (steps[i]
// turns format i + 1 into i + 2), a newer one not at all. `out` holds the record (current format) for Current and
// Migrated; `version` the format found.
[[nodiscard]] RecordKind readRecord(const std::string& bytes, uint32_t current, const RecordMigration* steps, size_t n_steps,
                      JsonDocument& out, uint32_t& version);

}  // namespace pt
