#include "types.h"
#include <ctype.h>
#include <string.h>

namespace pt {

static const char* const CH_NAMES[] = {"OUT1", "OUT2", "OUT3", "OUT4", "RLY1", "RLY2", "CC1", "CC2"};
static const char* const IN_NAMES[] = {"TRIG1", "TRIG2", "USER1", "USER2", "USER3"};

bool equalsIgnoreCase(const char* a, const char* b) {
  while (*a && *b) {
    if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

const char* name(Channel c) { return CH_NAMES[(int)c]; }

bool parseChannel(const char* s, Channel& out) {
  if (!s) return false;
  for (int i = 0; i < CHANNEL_COUNT; i++)
    if (equalsIgnoreCase(s, CH_NAMES[i])) { out = ALL_CHANNELS[i]; return true; }
  if (equalsIgnoreCase(s, "CONTACT1")) { out = Channel::Cc1; return true; }
  if (equalsIgnoreCase(s, "CONTACT2")) { out = Channel::Cc2; return true; }
  return false;
}

const char* name(Input i) { return IN_NAMES[(int)i]; }

bool parseInput(const char* s, Input& out) {
  if (!s) return false;
  for (int i = 0; i < INPUT_COUNT; i++)
    if (equalsIgnoreCase(s, IN_NAMES[i])) { out = ALL_INPUTS[i]; return true; }
  return false;
}

const char* name(Target t) {
  switch (t.kind) {
    case Target::Kind::CHANNEL: return name(t.channel);
    case Target::Kind::SOUND: return "SOUND";
    case Target::Kind::PIXEL: return "PIXEL";
    case Target::Kind::NET:
    default: return "NET";
  }
}

bool parseTarget(const char* s, Target& out) {
  Channel c = Channel::Out1;
  if (parseChannel(s, c)) { out = Target::of(c); return true; }
  if (equalsIgnoreCase(s, "SOUND")) { out = Target::sound(); return true; }
  if (equalsIgnoreCase(s, "PIXEL")) { out = Target::pixel(); return true; }
  if (equalsIgnoreCase(s, "NET")) { out = Target::net(); return true; }
  return false;
}

Target targetAt(int i) {
  if (i < CHANNEL_COUNT) return Target::of(ALL_CHANNELS[i]);
  if (i == 8) return Target::pixel();
  if (i == 9) return Target::sound();
  return Target::net();
}

}  // namespace pt
