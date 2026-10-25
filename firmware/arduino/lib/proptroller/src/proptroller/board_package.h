// Board packages (firmware/BOARD-PACKAGES.md): which GPIO each terminal is on, the input polarity, the pixel
// inversion, the board's id and name and, optionally, its defaults (settings, profile, show script). One generic
// firmware image per chip; the board package is data, sent with set_board and kept in its own A/B record, and read
// at power-on before any pin is touched.
//
// A package is JSON:
//   {"board_package": 1, "id": "relay4-button", "name": "Four relays and a button", "mcu": "esp32-devkit",
//    "terminals": {"OUT1": {"pin": 23}, "TRIG2": {"pin": 18, "active": "high", "pull": "board"}, ...},
//    "pixel": {"pin": 33, "inverted": true}, "sound": {"tx": 17, "rx": 34}, "i2c": {"sda": 4, "scl": 16},
//    "analog": 39, "expansion": {"S4": 15, "S5": 35}, "status_led": 2,
//    "defaults": {"settings": {"name": "Lamp"}, "profile": {...}, "script": "...", "script_name": "Lamp show"}}
// or {"board_package": 1, "id": ..., "name": ..., "base": "proptroller", "defaults": {...}}: a built-in board's pins.
// A terminal or function left out does not exist on that board: never configured, driven or read.
#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "pins.h"
#include "storage.h"

namespace pt {

static const uint32_t BOARD_PACKAGE_FORMAT = 1;          // the "board_package" number this firmware reads
static const size_t BOARD_PACKAGE_MAX_BYTES = 16 * 1024; // a package as sent (its JSON)
// one record file (the package packed, lz.h) within one 4 KB LittleFS block with its 16-byte A/B header: a
// package with a 30-alias profile and a 2 KB script packs to about 2 KB
static const size_t BOARD_STORE_CAPACITY = 4 * 1024 - 48;
static const size_t BOARD_ID_MAX = 32;                   // a package id: a-z, 0-9, '-' and '_'
static const size_t BOARD_NAME_MAX = 60;
static const uint16_t BOARD_RECORD_VERSION = 1;          // the stored record's format (FLASH-MAP.md)
static const char BOARD_RECORD_MAGIC[4] = {'P', 'T', 'B', 'P'};
static const size_t BOARD_RECORD_HEADER = 12;
// the running board when the stored package cannot be used: no pins at all
static const char* const BOARD_NONE_ID = "none";

// The chip the image runs on: which GPIOs exist and which belong to something else.
struct ChipRules {
  Mcu mcu = Mcu::Esp32DevKit;
  bool psram = false;  // an ESP32 module with PSRAM (WROVER): GPIO16 / 17 are its
  bool radio = false;  // Pico W / Pico 2 W: GP23, 24, 25 and 29 are the radio's
};
// "esp32-devkit" or "pico": the socket a package is for (info.mcu starts with it: pico, pico-w, pico2-w)
const char* mcuFamily(Mcu m);

// What a board runs with.
struct BoardConfig {
  std::string id, name;
  PinMap map{};
  bool pixel_inverted = true;  // the PIXEL data is inverted by the board's level shifter (the Proptroller board)
};

// A board compiled into the image (selectable by id: set_board {"preset": id}).
struct BuiltinBoard {
  const char* id;
  const char* name;
  PinMap map;
  bool pixel_inverted;
};
// The image's built-in boards for this chip, its default first: the Proptroller board, or `image_default` (a board
// support package compiled in, proptroller_board.h), which then comes first and replaces a built-in of its id.
std::vector<BuiltinBoard> builtinBoards(const ChipRules& chip, const BuiltinBoard* image_default = nullptr);
const BuiltinBoard* findBuiltin(const std::vector<BuiltinBoard>& all, const std::string& id);
// The bare devkit of a chip family (firmware/BARE-DEVKIT.md, bsp/<id>/): esp32-devkit-bare (classic ESP32; built into
// the esp32dev_generic image, a package file for the stock one), esp32-s3-devkit, esp32-c3-supermini (the S3's and
// C3's only built-in board); null for the Pico.
const BuiltinBoard* bareDevkit(Mcu m);
// No pins at all: what the board runs on when its stored package cannot be used.
PinMap noPins(Mcu m);

// The pin rules for this chip (pins that exist, flash / PSRAM / radio / USB serial pins, input-only pins, analog
// pins, duplicates); `warnings` gets the boot-strapping pins. False and `why` in plain words.
bool checkPinMap(const PinMap& m, const ChipRules& chip, std::vector<std::string>& warnings, std::string& why);
// A package (parsed JSON) checked for this chip: the format, every field, then the pin rules and the defaults'
// shapes. False and `why` in plain words, naming the field.
bool checkBoardPackage(JsonVariantConst pkg, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins, BoardConfig& out,
                       std::vector<std::string>& warnings, std::string& why);
// The same from text (at most BOARD_PACKAGE_MAX_BYTES).
bool checkBoardPackageText(const std::string& json, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins,
                           BoardConfig& out, std::vector<std::string>& warnings, std::string& why);
// A built-in board as a package (get_board with "id").
void builtinToJson(const BuiltinBoard& b, const ChipRules& chip, JsonObject o);
// A package's defaults: the profile as JSON ("" when none), the settings as "key=value" lines, the script.
bool boardDefaults(const std::string& package_json, std::string& profile_json, std::string& settings_lines, std::string& script);
bool boardHasDefaults(JsonVariantConst pkg);
// What the package calls its default script (defaults.script_name; "" when it does not say).
std::string boardScriptName(const std::string& package_json);

// The stored record (FLASH-MAP.md "Stored records"), in its own A/B file pair (/board-a.bin, /board-b.bin):
//   0  4  "PTBP"
//   4  2  record format (BOARD_RECORD_VERSION)
//   6  2  flags: bit 0 = apply the package's defaults once at the next power-on (set_board "defaults": "apply")
//   8  4  the package's JSON length
//   12    the JSON packed (lz.h)
std::string boardRecordPack(const std::string& package_json, bool apply_defaults);
enum class BoardRecordRead : uint8_t { Ok, Damaged, Newer };
BoardRecordRead boardRecordUnpack(const std::string& rec, std::string& package_json, bool& apply_defaults, uint16_t& format);

// Power-on: what the board runs with, read before any pin is touched.
//   no record: the image's default built-in board (source BuiltIn);
//   a record that checks: that package (Stored);
//   a record that is damaged, from a newer firmware, or does not fit this chip: no pins at all (None), and `problem`
//   says why in plain words. Nothing is driven; the protocol still answers, so a good package can be sent.
enum class BoardSource : uint8_t { BuiltIn, Stored, None };
const char* boardSourceName(BoardSource s);  // "built-in", "stored", "none"
struct BoardBoot {
  BoardSource source = BoardSource::BuiltIn;
  BoardConfig cfg;
  std::string problem;
  bool apply_defaults = false;  // the record asks for its defaults once
  bool has_defaults = false;    // the stored package has defaults (a fresh board applies them)
};
BoardBoot boardAtBoot(Storage* store, const ChipRules& chip, const std::vector<BuiltinBoard>& builtins);

}  // namespace pt
