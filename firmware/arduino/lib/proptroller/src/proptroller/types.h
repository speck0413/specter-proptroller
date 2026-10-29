// Basic names of the Spectre Proptroller rev A board: outputs, inputs, levels and time.
//
// Names follow the silkscreen: OUT1-OUT4, RLY1, RLY2, CC1/CC2 (CONTACT1/2), TRIG1, TRIG2, USER1-USER3.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>

// The release (firmware/VERSION) and the source revision: the
// PlatformIO pre-build script (tools/embed_web.py) defines both; an Arduino IDE build gets these defaults.
// The firmware's name in info, announce and mDNS (`fw`): a workspace project defines its folder name.
#ifndef PROPTROLLER_FW_NAME
#define PROPTROLLER_FW_NAME "generic"
#endif
#ifndef PROPTROLLER_FW_VERSION
#define PROPTROLLER_FW_VERSION "0.1.0-rc.3"
#endif
#define PROPTROLLER_FW_VERSION_STR PROPTROLLER_FW_VERSION
#ifndef PROPTROLLER_FW_BUILD
#define PROPTROLLER_FW_BUILD "unknown"
#endif
// info.fw_impl of this implementation
#define PROPTROLLER_FW_IMPL "cpp"

// The code of the 1 ms sample and the 10 ms tick, ESP32: in IRAM (about 47 KB free), so the step does not wait on
// flash cache misses that Wi-Fi on the other core causes (a miss costs about 4 us at DIO 40 MHz).
#if defined(ESP32) && defined(ARDUINO)
#include <esp_attr.h>
#define PT_STEP_CODE IRAM_ATTR
#else
#define PT_STEP_CODE
#endif

namespace pt {

// An enumerator as a table index.
template <typename E>
constexpr size_t idx(E e) { return static_cast<size_t>(e); }
// A table with one entry per enumerator, checked at compile time (Last: the enum's last enumerator).
#define PT_ONE_PER(table, Last) \
  static_assert(sizeof(table) / sizeof((table)[0]) == ::pt::idx(Last) + 1, #table " needs one entry per enumerator up to " #Last)

// Milliseconds since power-on. 64 bits, so it never wraps.
typedef uint64_t Millis;
// Output level in permille: 0 = off, 1000 = fully on. Relays and contacts treat any non-zero level as on.
typedef uint16_t Level;
// A trigger closure shorter than this never counts (tap, bounce, noise); also how long it must be open to count
// as released. The default of trig_debounce_ms and trig_release_ms, for shows, scripts and the effect.
static const uint32_t TRIGGER_DEBOUNCE_MS = 100;

static const Level FULL = 1000;

// A switchable output terminal. OUT1-4 are switched supply with PWM; RLY1/2 dry relay contacts; CC1/2
// isolated optocoupler closures.
enum class Channel : uint8_t { Out1, Out2, Out3, Out4, Rly1, Rly2, Cc1, Cc2 };
static const int CHANNEL_COUNT = 8;
static const Channel ALL_CHANNELS[CHANNEL_COUNT] = {Channel::Out1, Channel::Out2, Channel::Out3, Channel::Out4,
                                                    Channel::Rly1, Channel::Rly2, Channel::Cc1,  Channel::Cc2};

inline int index(Channel c) { return (int)c; }
const char* name(Channel c);
inline const char* name_of(Channel c) { return name(c); }
// Case-insensitive; CONTACT1 / CONTACT2 are accepted for CC1 / CC2.
[[nodiscard]] bool parseChannel(const char* s, Channel& out);
inline bool isPwm(Channel c) { return (int)c < 4; }

// An input. TRIG1/TRIG2 are the 3.5 mm trigger jacks, USER1-3 the buttons. All are active low.
enum class Input : uint8_t { Trig1, Trig2, User1, User2, User3 };
static const int INPUT_COUNT = 5;
static const Input ALL_INPUTS[INPUT_COUNT] = {Input::Trig1, Input::Trig2, Input::User1, Input::User2, Input::User3};
inline int index(Input i) { return (int)i; }
const char* name(Input i);
[[nodiscard]] bool parseInput(const char* s, Input& out);

// What a show track drives: an output, the sound player, the pixel strip, or named network cues.
struct Target {
  enum class Kind : uint8_t { CHANNEL, SOUND, PIXEL, NET };
  Kind kind = Kind::CHANNEL;
  Channel channel = Channel::Out1;
  static Target of(Channel c) { Target t; t.kind = Kind::CHANNEL; t.channel = c; return t; }
  static Target sound() { Target t; t.kind = Kind::SOUND; return t; }
  static Target pixel() { Target t; t.kind = Kind::PIXEL; return t; }
  static Target net() { Target t; t.kind = Kind::NET; return t; }
  bool isChannel() const { return kind == Kind::CHANNEL; }
  bool operator==(const Target& o) const { return kind == o.kind && (kind != Kind::CHANNEL || channel == o.channel); }
  bool operator!=(const Target& o) const { return !(*this == o); }
};
const char* name(Target t);
bool parseTarget(const char* s, Target& out);
// Every target in display order: OUT1..CC2, PIXEL, SOUND, NET.
static const int TARGET_COUNT = 11;
Target targetAt(int i);

bool equalsIgnoreCase(const char* a, const char* b);

// Relays and contacts change at most this often: one that went off comes on again no sooner (Board::apply), and
// shows and scripts that would switch one faster are refused.
static const uint32_t RELAY_MIN_DWELL_MS = 100;

// The hardware watchdog watches the show step only (a stalled network or loop() never restarts the board): it is
// fed while the step ran less than STEP_ALIVE_MS ago (`step_at` may be a little after `now`: another core).
static const Millis STEP_ALIVE_MS = 100;
inline bool stepAlive(Millis now, Millis step_at) { return now <= step_at || now - step_at < STEP_ALIVE_MS; }

}  // namespace pt
