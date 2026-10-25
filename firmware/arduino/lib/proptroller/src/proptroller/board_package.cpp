#include "board_package.h"
#include "lz.h"
#include "settings.h"
#include "script.h"
#include <string.h>

namespace pt {

const char* mcuFamily(Mcu m) {
  switch (m) {
    case Mcu::Esp32DevKit: return "esp32-devkit";
    case Mcu::Pico: return "pico";
    case Mcu::Esp32S3: return "esp32-s3";
    case Mcu::Esp32C3: return "esp32-c3";
  }
  return "esp32-devkit";
}

static bool mcuFromFamily(const std::string& s, Mcu& out) {
  const Mcu all[] = {Mcu::Esp32DevKit, Mcu::Pico, Mcu::Esp32S3, Mcu::Esp32C3};
  for (Mcu m : all)
    if (s == mcuFamily(m)) { out = m; return true; }
  return false;
}

const char* boardSourceName(BoardSource s) {
  switch (s) {
    case BoardSource::BuiltIn: return "built-in";
    case BoardSource::Stored: return "stored";
    case BoardSource::None: return "none";
  }
  return "none";
}

// ---------------------------------------------------------------- built-in boards

// The relay board example (bsp/relay4-button): four relays on OUT1-4, a trigger with the board's pull-up on TRIG1
// and a button with a pull-down (active high) on TRIG2; nothing else.
static const PinMap RELAY4_BUTTON = {Mcu::Esp32DevKit, {23, 22, 21, 19}, {NO_PIN, NO_PIN}, {NO_PIN, NO_PIN}, {36, 18},
                                     {NO_PIN, NO_PIN, NO_PIN}, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, 39,
                                     {NO_PIN, NO_PIN}, 2, 0x02, 0x03};
static const char* const PROPTROLLER_NAME = "Spectre Proptroller board";
// The bare devkits (pins.h, firmware/BARE-DEVKIT.md): plain 3.3 V outputs, PIXEL not inverted. The same ids,
// names and maps as bsp/<id>/proptroller_board.h (test/test_bsp_generic).
static const BuiltinBoard BARE_DEVKITS[] = {
    {"esp32-devkit-bare", "ESP32 DevKit, bare (no Proptroller board)", ESP32_DEVKIT_BARE, false},
    {"esp32-s3-devkit", "ESP32-S3 DevKit", ESP32S3_DEVKIT, false},
    {"esp32-c3-supermini", "ESP32-C3 SuperMini", ESP32C3_SUPERMINI, false},
};

const BuiltinBoard* bareDevkit(Mcu m) {
  for (auto& b : BARE_DEVKITS)
    if (b.map.mcu == m) return &b;
  return nullptr;
}

std::vector<BuiltinBoard> builtinBoards(const ChipRules& chip, const BuiltinBoard* image_default) {
  std::vector<BuiltinBoard> v;
  switch (chip.mcu) {
    case Mcu::Esp32DevKit:
      v.push_back({"proptroller", PROPTROLLER_NAME, ESP32_DEVKIT, true});
      v.push_back({"relay4-button", "Four relays and a button (example)", RELAY4_BUTTON, false});
      break;
    case Mcu::Pico: {
      PinMap p = PICO;
      if (chip.radio) p.status_led = STATUS_LED_BUILTIN;  // the Pico W's LED is on the radio chip
      v.push_back({"proptroller", PROPTROLLER_NAME, p, true});
      break;
    }
    // no Proptroller board takes an S3 or a C3: their devkit is the default
    case Mcu::Esp32S3:
    case Mcu::Esp32C3:
      v.push_back(*bareDevkit(chip.mcu));
      break;
  }
  if (image_default && image_default->map.mcu == chip.mcu) {
    for (size_t i = 0; i < v.size(); i++)
      if (!strcmp(v[i].id, image_default->id)) { v.erase(v.begin() + (long)i); break; }
    v.insert(v.begin(), *image_default);
  }
  return v;
}

const BuiltinBoard* findBuiltin(const std::vector<BuiltinBoard>& all, const std::string& id) {
  for (auto& b : all)
    if (id == b.id) return &b;
  return nullptr;
}

PinMap noPins(Mcu m) {
  PinMap p = {m, {NO_PIN, NO_PIN, NO_PIN, NO_PIN}, {NO_PIN, NO_PIN}, {NO_PIN, NO_PIN}, {NO_PIN, NO_PIN}, {NO_PIN, NO_PIN, NO_PIN},
              NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, NO_PIN, {NO_PIN, NO_PIN}, NO_PIN, 0, 0};
  return p;
}

// ---------------------------------------------------------------- pin rules

// What a GPIO is used for: an output drives it, an input reads it (with or without the chip's pull), a bus line
// (I2C, expansion S5) may do either, the analog input needs an ADC pin.
enum class Use : uint8_t { Drive, Read, ReadChipPull, Either, Analog, Led };
struct PinUse {
  const char* name;
  uint8_t pin;
  Use use;
};

static const char* const TERMINAL_NAMES[] = {"OUT1", "OUT2", "OUT3", "OUT4", "RLY1", "RLY2", "CC1", "CC2",
                                             "TRIG1", "TRIG2", "USER1", "USER2", "USER3"};
static const int TERMINAL_COUNT = 13;

static std::vector<PinUse> uses(const PinMap& m) {
  std::vector<PinUse> u;
  for (int c = 0; c < 8; c++) u.push_back({TERMINAL_NAMES[c], channelPin(m, c), Use::Drive});
  for (int k = 0; k < 5; k++) u.push_back({TERMINAL_NAMES[8 + k], inputPin(m, k), inputExternalPull(m, k) ? Use::Read : Use::ReadChipPull});
  u.push_back({"pixel", m.pixel, Use::Drive});
  u.push_back({"sound tx", m.df_tx, Use::Drive});
  u.push_back({"sound rx", m.df_rx, Use::Read});
  u.push_back({"i2c sda", m.sda, Use::Either});
  u.push_back({"i2c scl", m.scl, Use::Either});
  u.push_back({"analog", m.analog_in, Use::Analog});
  u.push_back({"expansion S4", m.exp[0], Use::Drive});  // the DMX port's transmit line
  u.push_back({"expansion S5", m.exp[1], Use::Read});  // read only (GPIO35 on the Proptroller board's ESP32)
  u.push_back({"status_led", m.status_led, Use::Led});
  return u;
}

static bool drives(Use u) { return u == Use::Drive || u == Use::Either || u == Use::Led; }

// ESP32 (30-pin DevKit, WROOM / WROVER module)
static const uint8_t ESP32_LAST_GPIO = 39;
static const uint8_t ESP32_ABSENT[] = {20, 24, 28, 29, 30, 31};
static const uint8_t ESP32_FLASH[] = {6, 7, 8, 9, 10, 11};
static const uint8_t ESP32_USB_SERIAL[] = {1, 3};
static const uint8_t ESP32_PSRAM[] = {16, 17};
static const uint8_t ESP32_FLASH_VOLTAGE = 12;  // a strapping pin that sets the flash voltage: never an output
static const uint8_t ESP32_ADC1[] = {32, 33, 34, 35, 36, 37, 38, 39};  // ADC2 stops working while Wi-Fi runs
// Pico, Pico W, Pico 2 W
static const uint8_t PICO_LAST_GPIO = 29;
static const uint8_t PICO_RADIO[] = {23, 24, 25, 29};
static const uint8_t PICO_BOARD_USE[] = {23, 24, 29};  // the plain Pico: power-supply mode, VBUS sense, VSYS sense
static const uint8_t PICO_LED = 25;                    // the plain Pico's LED
static const uint8_t PICO_ADC[] = {26, 27, 28};
// ESP32-S3 (ESP32-S3-WROOM-1 / -2 modules, as on the DevKitC-1). GPIO 22-25 do not exist; 26-32 are the module's
// flash and PSRAM; 33-37 are its octal flash / PSRAM on the N8R8, N16R8 ... modules (the common devkits: refused on
// every S3, as the image cannot tell); 19 / 20 the USB port; 43 / 44 the USB serial port (UART0); 45 sets the
// flash voltage at power-on; 0, 3, 45, 46 are read at power-on.
static const uint8_t S3_LAST_GPIO = 48;
static const uint8_t S3_ABSENT[] = {22, 23, 24, 25};
static const uint8_t S3_FLASH[] = {26, 27, 28, 29, 30, 31, 32};
static const uint8_t S3_OCTAL[] = {33, 34, 35, 36, 37};
static const uint8_t S3_USB[] = {19, 20};
static const uint8_t S3_USB_SERIAL[] = {43, 44};
static const uint8_t S3_FLASH_VOLTAGE = 45;
static const uint8_t S3_STRAPPING[] = {0, 3, 45, 46};
static const uint8_t S3_ADC1[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
// ESP32-C3 (the SuperMini and other C3 boards). GPIO 0-21; 12-17 are the flash; 18 / 19 the USB port; 2, 8, 9 are
// read at power-on (9 is the BOOT button); 20 / 21 are UART0, where the chip prints its boot messages.
static const uint8_t C3_LAST_GPIO = 21;
static const uint8_t C3_FLASH[] = {12, 13, 14, 15, 16, 17};
static const uint8_t C3_USB[] = {18, 19};
static const uint8_t C3_STRAPPING[] = {2, 8, 9};
static const uint8_t C3_BOOT_UART[] = {20, 21};
static const uint8_t C3_ADC1[] = {0, 1, 2, 3, 4};

template <size_t N>
static bool in(const uint8_t (&set)[N], uint8_t p) {
  for (uint8_t x : set)
    if (x == p) return true;
  return false;
}

static std::string gpio(const ChipRules& c, uint8_t p) { return (c.mcu == Mcu::Pico ? "GP" : "GPIO ") + std::to_string(p); }

static bool pinOk(const PinUse& u, const ChipRules& c, std::vector<std::string>& warnings, std::string& why) {
  const uint8_t p = u.pin;
  const std::string g = gpio(c, p), who = std::string(u.name) + ": ";
  if (u.use == Use::Led && p == STATUS_LED_BUILTIN) return true;
  const std::string power_on = std::string(u.name) + ": " + g + " is read at power-on (a boot-strapping pin): what is wired to it must not pull it the wrong way while the board starts";
  if (c.mcu == Mcu::Esp32S3) {
    if (p > S3_LAST_GPIO || in(S3_ABSENT, p)) { why = who + "the ESP32-S3 has no " + g; return false; }
    if (in(S3_FLASH, p)) { why = who + g + " is wired to the module's flash memory: use another GPIO"; return false; }
    if (in(S3_OCTAL, p)) { why = who + g + " is used by the flash or PSRAM on most ESP32-S3 modules (octal PSRAM): use another GPIO"; return false; }
    if (in(S3_USB, p)) { why = who + g + " is the USB port's data line: use another GPIO"; return false; }
    if (in(S3_USB_SERIAL, p)) { why = who + g + " is the USB serial port: use another GPIO"; return false; }
    if (p == S3_FLASH_VOLTAGE && drives(u.use)) { why = who + g + " sets the flash voltage at power-on: an output there can stop the board from starting"; return false; }
    if (u.use == Use::Analog && !in(S3_ADC1, p)) { why = who + g + " cannot read analog while Wi-Fi runs: use GPIO 1-10"; return false; }
    if (in(S3_STRAPPING, p)) warnings.push_back(power_on);
    return true;
  }
  if (c.mcu == Mcu::Esp32C3) {
    if (p > C3_LAST_GPIO) { why = who + "the ESP32-C3 has no " + g; return false; }
    if (in(C3_FLASH, p)) { why = who + g + " is wired to the flash memory: use another GPIO"; return false; }
    if (in(C3_USB, p)) { why = who + g + " is the USB port's data line: use another GPIO"; return false; }
    if (u.use == Use::Analog && !in(C3_ADC1, p)) { why = who + g + " cannot read analog while Wi-Fi runs: use GPIO 0-4"; return false; }
    if (in(C3_STRAPPING, p)) warnings.push_back(power_on);
    if (in(C3_BOOT_UART, p) && drives(u.use))
      warnings.push_back(std::string(u.name) + ": " + g + " carries the chip's boot messages while it starts: an output there flickers for a moment at power-on");
    return true;
  }
  if (c.mcu == Mcu::Esp32DevKit) {
    if (p > ESP32_LAST_GPIO || in(ESP32_ABSENT, p)) { why = who + "the ESP32 has no " + g; return false; }
    if (in(ESP32_FLASH, p)) { why = who + g + " is wired to the module's flash memory: use another GPIO"; return false; }
    if (in(ESP32_USB_SERIAL, p)) { why = who + g + " is the USB serial port: use another GPIO"; return false; }
    if (c.psram && in(ESP32_PSRAM, p)) { why = who + g + " is used by this module's PSRAM: use another GPIO"; return false; }
    if (drives(u.use) && in(ESP32_INPUT_ONLY, p)) { why = who + g + " is input-only on the ESP32 and cannot drive an output"; return false; }
    if (u.use == Use::ReadChipPull && in(ESP32_INPUT_ONLY, p)) {
      why = who + g + " has no pull resistor of its own on the ESP32: set \"pull\": \"board\" when the board has a resistor on it, or use another GPIO";
      return false;
    }
    if (u.use == Use::Analog && !in(ESP32_ADC1, p)) { why = who + g + " cannot read analog while Wi-Fi runs: use GPIO 32-39"; return false; }
    if (p == ESP32_FLASH_VOLTAGE && drives(u.use)) { why = who + g + " sets the flash voltage at power-on: an output there can stop the board from starting"; return false; }
    if (in(ESP32_STRAPPING, p)) warnings.push_back(power_on);
    return true;
  }
  if (p > PICO_LAST_GPIO) { why = who + "the Pico has no " + g; return false; }
  if (c.radio && in(PICO_RADIO, p)) { why = who + g + " belongs to the radio on a Pico W: use another GPIO"; return false; }
  if (!c.radio && in(PICO_BOARD_USE, p)) { why = who + g + " is used by the Pico board itself: use another GPIO"; return false; }
  if (!c.radio && p == PICO_LED && u.use != Use::Led) { why = who + g + " is the Pico's own LED: use it as status_led only"; return false; }
  if (u.use == Use::Analog && !in(PICO_ADC, p)) { why = who + g + " cannot read analog: use GP26, GP27 or GP28"; return false; }
  return true;
}

bool checkPinMap(const PinMap& m, const ChipRules& chip, std::vector<std::string>& warnings, std::string& why) {
  if (m.mcu != chip.mcu) { why = std::string("this pin map is for the ") + mcuFamily(m.mcu) + " socket; this board has the " + mcuFamily(chip.mcu); return false; }
  std::vector<PinUse> u = uses(m);
  for (size_t i = 0; i < u.size(); i++) {
    if (u[i].pin == NO_PIN) continue;
    if (!pinOk(u[i], chip, warnings, why)) return false;
    for (size_t j = 0; j < i; j++)
      if (u[j].pin == u[i].pin) {
        why = gpio(chip, u[i].pin) + " is used twice: " + u[j].name + " and " + u[i].name;
        return false;
      }
  }
  return true;
}

// ---------------------------------------------------------------- the package's JSON

namespace {

// Reads one package field by field; the first problem wins (`why`, naming the field).
struct Reader {
  std::string why;
  bool fail(const std::string& w) {
    if (why.empty()) why = w;
    return false;
  }
  // only these keys in object `o` (a misspelt field is an error, not silently ignored)
  bool onlyKeys(JsonObjectConst o, const char* where, std::initializer_list<const char*> keys) {
    for (JsonPairConst kv : o) {
      bool known = false;
      for (const char* k : keys) known = known || !strcmp(kv.key().c_str(), k);
      if (!known) return fail(std::string(where) + (*where ? ": " : "") + "unknown field \"" + kv.key().c_str() + "\"");
    }
    return true;
  }
  bool pin(JsonVariantConst v, const std::string& where, uint8_t& out) {
    if (v.isNull()) return true;  // left out: not on this board
    if (!v.is<unsigned>() || v.as<unsigned>() >= STATUS_LED_BUILTIN) return fail(where + " must be a GPIO number");
    out = (uint8_t)v.as<unsigned>();
    return true;
  }
  bool object(JsonVariantConst v, const std::string& where) {
    if (v.isNull() || v.is<JsonObjectConst>()) return true;
    return fail(where + " must be an object ({...})");
  }
};

bool idOk(const std::string& id) {
  if (id.empty() || id.size() > BOARD_ID_MAX || id == BOARD_NONE_ID) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  return true;
}

}  // namespace

bool boardHasDefaults(JsonVariantConst pkg) {
  JsonObjectConst d = pkg["defaults"];
  return !d.isNull() && (!d["settings"].isNull() || !d["profile"].isNull() || !d["script"].isNull());
}

static bool checkDefaults(Reader& rd, JsonVariantConst d) {
  if (d.isNull()) return true;
  if (!rd.object(d, "defaults")) return false;
  if (!rd.onlyKeys(d.as<JsonObjectConst>(), "defaults", {"settings", "profile", "script", "script_name"})) return false;
  JsonVariantConst sn = d["script_name"];
  if (!sn.isNull() && (!sn.is<const char*>() || strlen(sn.as<const char*>()) == 0 || strlen(sn.as<const char*>()) > BOARD_NAME_MAX))
    return rd.fail("defaults: \"script_name\" must be 1 to " + std::to_string(BOARD_NAME_MAX) + " characters (what the script is called)");
  JsonVariantConst s = d["settings"];
  if (!s.isNull()) {
    if (!s.is<JsonObjectConst>()) return rd.fail("defaults: \"settings\" must be an object of setting names and values ({\"name\": \"Lamp\"})");
    for (JsonPairConst kv : s.as<JsonObjectConst>()) {
      JsonVariantConst v = kv.value();
      const std::string k = kv.key().c_str();
      if (!(v.is<const char*>() || v.is<bool>() || v.is<long>() || v.is<double>()))
        return rd.fail("defaults: setting \"" + k + "\" must be text, a number or true / false");
      if (k.empty() || k.find_first_of("=\n#") != std::string::npos) return rd.fail("defaults: \"" + k + "\" is not a setting name");
      if (v.is<const char*>() && strpbrk(v.as<const char*>(), "\r\n#")) return rd.fail("defaults: setting \"" + k + "\" holds a line break or #");
    }
  }
  JsonVariantConst p = d["profile"];
  if (!p.isNull()) {
    Profile prof;
    std::string w;
    if (!p.is<JsonObjectConst>()) return rd.fail("defaults: \"profile\" must be a profile object (set_profile's data)");
    if (!parseProfile(p, prof, w)) return rd.fail("defaults: profile: " + w);
  }
  JsonVariantConst sc = d["script"];
  if (!sc.isNull()) {
    if (!sc.is<const char*>()) return rd.fail("defaults: \"script\" must be the show script as text");
    if (strlen(sc.as<const char*>()) > SCRIPT_MAX_BYTES)
      return rd.fail("defaults: the script is longer than " + std::to_string(SCRIPT_MAX_BYTES / 1024) + " KB");
  }
  return true;
}

bool checkBoardPackage(JsonVariantConst pkg, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins, BoardConfig& out,
                       std::vector<std::string>& warnings, std::string& why) {
  Reader rd;
  auto done = [&](bool ok) {
    if (!ok) why = rd.why.empty() ? "not a board package" : rd.why;
    return ok;
  };
  if (!pkg.is<JsonObjectConst>()) return done(rd.fail("a board package is a JSON object ({\"board_package\": 1, ...})"));
  JsonObjectConst o = pkg.as<JsonObjectConst>();
  JsonVariantConst fmt = o["board_package"];
  if (!fmt.is<unsigned>()) return done(rd.fail("\"board_package\" is missing: a board package starts with \"board_package\": 1"));
  if (fmt.as<unsigned>() != BOARD_PACKAGE_FORMAT)
    return done(rd.fail("this board package is format " + std::to_string(fmt.as<unsigned>()) + "; this firmware reads format " +
                        std::to_string(BOARD_PACKAGE_FORMAT) + ": update the firmware first"));
  BoardConfig c;
  const std::string id = o["id"] | "";
  if (!idOk(id))
    return done(rd.fail("\"id\" must be 1 to " + std::to_string(BOARD_ID_MAX) + " characters of a-z, 0-9, - and _ (not \"none\")"));
  c.id = id;
  c.name = o["name"] | "";
  if (c.name.empty() || c.name.size() > BOARD_NAME_MAX)
    return done(rd.fail("\"name\" must be 1 to " + std::to_string(BOARD_NAME_MAX) + " characters"));
  if (!checkDefaults(rd, o["defaults"])) return done(false);
  JsonVariantConst base = o["base"];
  if (!base.isNull()) {
    if (!rd.onlyKeys(o, "", {"board_package", "id", "name", "base", "mcu", "description", "$schema", "defaults"})) {
      rd.why += " (a package with \"base\" takes its pins from that built-in board)";
      return done(false);
    }
    const BuiltinBoard* b = base.is<const char*>() ? findBuiltin(builtins, base.as<const char*>()) : nullptr;
    if (!b) {
      std::string list;
      for (auto& x : builtins) list += (list.empty() ? "" : ", ") + std::string(x.id);
      return done(rd.fail("\"base\": this firmware has no built-in board \"" + std::string(base | "") + "\" for this chip (it has: " + list + ")"));
    }
    if (!o["mcu"].isNull() && std::string(o["mcu"] | "") != mcuFamily(chip.mcu))
      return done(rd.fail(std::string("this board package is for the ") + (o["mcu"] | "?") + " socket; this board has the " + mcuFamily(chip.mcu)));
    c.map = b->map;
    c.pixel_inverted = b->pixel_inverted;
    if (!checkPinMap(c.map, chip, warnings, rd.why)) return done(false);
    out = c;
    return true;
  }
  if (!rd.onlyKeys(o, "", {"board_package", "id", "name", "mcu", "description", "$schema", "terminals", "pixel", "sound", "i2c",
                           "analog", "expansion", "status_led", "defaults"}))
    return done(false);
  const std::string mcu = o["mcu"] | "";
  Mcu parsed = Mcu::Esp32DevKit;
  if (!mcuFromFamily(mcu, parsed))
    return done(rd.fail("\"mcu\" must be \"esp32-devkit\", \"pico\", \"esp32-s3\" or \"esp32-c3\" (the board's chip)"));
  if (parsed != chip.mcu)
    return done(rd.fail("this board package is for the " + mcu + " socket; this board has the " + mcuFamily(chip.mcu)));
  c.map = noPins(chip.mcu);
  // terminals
  JsonVariantConst t = o["terminals"];
  if (!rd.object(t, "terminals")) return done(false);
  for (JsonPairConst kv : t.as<JsonObjectConst>()) {
    int k = -1;
    for (int i = 0; i < TERMINAL_COUNT; i++)
      if (!strcmp(kv.key().c_str(), TERMINAL_NAMES[i])) k = i;
    const std::string name = kv.key().c_str();
    if (k < 0) return done(rd.fail("terminals: there is no terminal \"" + name + "\" (OUT1-4, RLY1-2, CC1-2, TRIG1-2, USER1-3)"));
    JsonVariantConst v = kv.value();
    if (!v.is<JsonObjectConst>()) return done(rd.fail(name + " must be an object ({\"pin\": 23})"));
    JsonObjectConst to = v.as<JsonObjectConst>();
    if (to["pin"].isNull()) return done(rd.fail(name + ": \"pin\" is missing (leave the terminal out when the board does not have it)"));
    uint8_t p = NO_PIN;
    if (!rd.pin(to["pin"], name + ": \"pin\"", p)) return done(false);
    if (k < 8) {
      if (!rd.onlyKeys(to, name.c_str(), {"pin"})) return done(false);
      if (k < 4) c.map.out[k] = p;
      else if (k < 6) c.map.relay[k - 4] = p;
      else c.map.contact[k - 6] = p;
      continue;
    }
    if (!rd.onlyKeys(to, name.c_str(), {"pin", "active", "pull"})) return done(false);
    const int in_k = k - 8;
    const std::string active = to["active"] | "low", pull = to["pull"] | "chip";
    if (active != "low" && active != "high") return done(rd.fail(name + ": \"active\" must be \"low\" (pressed pulls the pin to ground) or \"high\""));
    if (pull != "chip" && pull != "board")
      return done(rd.fail(name + ": \"pull\" must be \"chip\" (the chip's own pull-up for an input active low, pull-down for one active high) or \"board\" (the board has its own resistor)"));
    if (in_k < 2) c.map.trig[in_k] = p;
    else c.map.user[in_k - 2] = p;
    if (active == "high") c.map.active_high = (uint8_t)(c.map.active_high | (1u << in_k));
    if (pull == "board") c.map.external_pull = (uint8_t)(c.map.external_pull | (1u << in_k));
  }
  // functions
  JsonVariantConst px = o["pixel"];
  if (!rd.object(px, "pixel")) return done(false);
  if (!px.isNull()) {
    if (!rd.onlyKeys(px.as<JsonObjectConst>(), "pixel", {"pin", "inverted"}) || !rd.pin(px["pin"], "pixel: \"pin\"", c.map.pixel)) return done(false);
    if (px["pin"].isNull()) return done(rd.fail("pixel: \"pin\" is missing (leave \"pixel\" out when the board has no pixel port)"));
    if (!px["inverted"].isNull() && !px["inverted"].is<bool>()) return done(rd.fail("pixel: \"inverted\" must be true or false"));
    c.pixel_inverted = px["inverted"] | true;
  }
  JsonVariantConst snd = o["sound"];
  if (!rd.object(snd, "sound")) return done(false);
  if (!snd.isNull()) {
    if (!rd.onlyKeys(snd.as<JsonObjectConst>(), "sound", {"tx", "rx"}) || !rd.pin(snd["tx"], "sound: \"tx\"", c.map.df_tx) ||
        !rd.pin(snd["rx"], "sound: \"rx\"", c.map.df_rx))
      return done(false);
    if (c.map.df_tx == NO_PIN || c.map.df_rx == NO_PIN) return done(rd.fail("sound: the sound port needs both \"tx\" and \"rx\""));
  }
  JsonVariantConst i2c = o["i2c"];
  if (!rd.object(i2c, "i2c")) return done(false);
  if (!i2c.isNull()) {
    if (!rd.onlyKeys(i2c.as<JsonObjectConst>(), "i2c", {"sda", "scl"}) || !rd.pin(i2c["sda"], "i2c: \"sda\"", c.map.sda) ||
        !rd.pin(i2c["scl"], "i2c: \"scl\"", c.map.scl))
      return done(false);
    if (c.map.sda == NO_PIN || c.map.scl == NO_PIN) return done(rd.fail("i2c: needs both \"sda\" and \"scl\""));
  }
  if (!rd.pin(o["analog"], "\"analog\"", c.map.analog_in)) return done(false);
  JsonVariantConst ex = o["expansion"];
  if (!rd.object(ex, "expansion")) return done(false);
  if (!ex.isNull() && (!rd.onlyKeys(ex.as<JsonObjectConst>(), "expansion", {"S4", "S5"}) || !rd.pin(ex["S4"], "expansion: \"S4\"", c.map.exp[0]) ||
                       !rd.pin(ex["S5"], "expansion: \"S5\"", c.map.exp[1])))
    return done(false);
  JsonVariantConst led = o["status_led"];
  if (led.is<const char*>()) {
    if (strcmp(led.as<const char*>(), "builtin") != 0) return done(rd.fail("\"status_led\" must be a GPIO number or \"builtin\" (the MCU board's own LED)"));
    c.map.status_led = STATUS_LED_BUILTIN;
  } else if (!rd.pin(led, "\"status_led\"", c.map.status_led)) {
    return done(false);
  }
  if (!checkPinMap(c.map, chip, warnings, rd.why)) return done(false);
  out = c;
  return true;
}

bool checkBoardPackageText(const std::string& json, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins, BoardConfig& out,
                           std::vector<std::string>& warnings, std::string& why) {
  if (json.size() > BOARD_PACKAGE_MAX_BYTES) {
    why = "the board package is " + std::to_string(json.size()) + " bytes; at most " + std::to_string(BOARD_PACKAGE_MAX_BYTES / 1024) + " KB";
    return false;
  }
  JsonDocument d;
  DeserializationError e = deserializeJson(d, json);
  if (e) { why = std::string("the board package is not valid JSON (") + e.c_str() + ")"; return false; }
  return checkBoardPackage(d.as<JsonVariantConst>(), chip, builtins, out, warnings, why);
}

void builtinToJson(const BuiltinBoard& b, const ChipRules& chip, JsonObject o) {
  const PinMap& m = b.map;
  o["board_package"] = BOARD_PACKAGE_FORMAT;
  o["id"] = b.id;
  o["name"] = b.name;
  o["mcu"] = mcuFamily(chip.mcu);
  JsonObject t = o["terminals"].to<JsonObject>();
  for (int c = 0; c < 8; c++)
    if (channelPin(m, c) != NO_PIN) t[TERMINAL_NAMES[c]]["pin"] = channelPin(m, c);
  for (int k = 0; k < 5; k++) {
    if (inputPin(m, k) == NO_PIN) continue;
    JsonObject x = t[TERMINAL_NAMES[8 + k]].to<JsonObject>();
    x["pin"] = inputPin(m, k);
    x["active"] = inputActiveHigh(m, k) ? "high" : "low";
    x["pull"] = inputExternalPull(m, k) ? "board" : "chip";
  }
  if (m.pixel != NO_PIN) {
    o["pixel"]["pin"] = m.pixel;
    o["pixel"]["inverted"] = b.pixel_inverted;
  }
  if (m.df_tx != NO_PIN && m.df_rx != NO_PIN) {
    o["sound"]["tx"] = m.df_tx;
    o["sound"]["rx"] = m.df_rx;
  }
  if (m.sda != NO_PIN && m.scl != NO_PIN) {
    o["i2c"]["sda"] = m.sda;
    o["i2c"]["scl"] = m.scl;
  }
  if (m.analog_in != NO_PIN) o["analog"] = m.analog_in;
  if (m.exp[0] != NO_PIN) o["expansion"]["S4"] = m.exp[0];
  if (m.exp[1] != NO_PIN) o["expansion"]["S5"] = m.exp[1];
  if (m.status_led == STATUS_LED_BUILTIN) o["status_led"] = "builtin";
  else if (m.status_led != NO_PIN) o["status_led"] = m.status_led;
}

bool boardDefaults(const std::string& package_json, std::string& profile_json, std::string& settings_lines, std::string& script) {
  profile_json.clear();
  settings_lines.clear();
  script.clear();
  JsonDocument d;
  if (deserializeJson(d, package_json)) return false;
  JsonObjectConst def = d["defaults"];
  if (def.isNull()) return true;
  if (!def["profile"].isNull()) serializeJson(def["profile"], profile_json);
  for (JsonPairConst kv : def["settings"].as<JsonObjectConst>()) {
    std::string v;
    if (kv.value().is<const char*>()) v = kv.value().as<const char*>();
    else serializeJson(kv.value(), v);
    settings_lines += std::string(kv.key().c_str()) + "=" + v + "\n";
  }
  script = def["script"] | "";
  return true;
}

std::string boardScriptName(const std::string& package_json) {
  JsonDocument d;
  if (deserializeJson(d, package_json)) return "";
  return d["defaults"]["script_name"] | "";
}

// ---------------------------------------------------------------- the stored record

static void put16(std::string& s, uint16_t v) { s += (char)(v & 0xFF); s += (char)(v >> 8); }
static void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF); }
static uint16_t get16(const uint8_t* b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t get32(const uint8_t* b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }

std::string boardRecordPack(const std::string& package_json, bool apply_defaults) {
  std::string r(BOARD_RECORD_MAGIC, 4);
  put16(r, BOARD_RECORD_VERSION);
  put16(r, apply_defaults ? 1 : 0);
  put32(r, (uint32_t)package_json.size());
  r += lzPack(package_json);
  return r;
}

BoardRecordRead boardRecordUnpack(const std::string& rec, std::string& package_json, bool& apply_defaults, uint16_t& format) {
  const uint8_t* b = (const uint8_t*)rec.data();
  format = 0;
  if (rec.size() < BOARD_RECORD_HEADER || memcmp(b, BOARD_RECORD_MAGIC, 4) != 0) return BoardRecordRead::Damaged;
  format = get16(b + 4);
  if (format > BOARD_RECORD_VERSION) return BoardRecordRead::Newer;
  if (format != BOARD_RECORD_VERSION) return BoardRecordRead::Damaged;
  apply_defaults = (get16(b + 6) & 1) != 0;
  const uint32_t len = get32(b + 8);
  if (len > BOARD_PACKAGE_MAX_BYTES) return BoardRecordRead::Damaged;
  if (!lzUnpack(b + BOARD_RECORD_HEADER, rec.size() - BOARD_RECORD_HEADER, len, package_json)) return BoardRecordRead::Damaged;
  return BoardRecordRead::Ok;
}

static const char* const NO_PINS_TAIL =
    ": no pins are used (nothing is driven) until a board package is sent again (set_board) or the stored one is forgotten";

BoardBoot boardAtBoot(Storage* store, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins) {
  BoardBoot b;
  const BuiltinBoard& def = builtins.front();
  b.cfg.id = def.id;
  b.cfg.name = def.name;
  b.cfg.map = def.map;
  b.cfg.pixel_inverted = def.pixel_inverted;
  std::string rec;
  Stored got = store ? store->fetch(rec) : Stored::None;
  if (got == Stored::None) return b;
  auto none = [&](const std::string& why) {
    b.source = BoardSource::None;
    b.cfg.id = BOARD_NONE_ID;
    b.cfg.name = "No board package";
    b.cfg.map = noPins(chip.mcu);
    b.cfg.pixel_inverted = true;
    b.problem = why + NO_PINS_TAIL;
    b.apply_defaults = false;
    b.has_defaults = false;
    return b;
  };
  if (got == Stored::Invalid) return none("the stored board package could not be read (damaged flash)");
  std::string json;
  uint16_t format = 0;
  bool apply = false;
  switch (boardRecordUnpack(rec, json, apply, format)) {
    case BoardRecordRead::Ok: break;
    case BoardRecordRead::Newer: return none("the stored board package is record format " + std::to_string(format) + ", from a newer firmware");
    case BoardRecordRead::Damaged: return none("the stored board package could not be read (damaged record)");
  }
  std::string().swap(rec);
  JsonDocument d;
  if (deserializeJson(d, json)) return none("the stored board package is not valid JSON");
  std::vector<std::string> warnings;
  std::string why;
  BoardConfig c;
  if (!checkBoardPackage(d.as<JsonVariantConst>(), chip, builtins, c, warnings, why)) return none("the stored board package does not fit this board (" + why + ")");
  b.source = BoardSource::Stored;
  b.cfg = c;
  b.apply_defaults = apply;
  b.has_defaults = boardHasDefaults(d.as<JsonVariantConst>());
  return b;
}

}  // namespace pt
