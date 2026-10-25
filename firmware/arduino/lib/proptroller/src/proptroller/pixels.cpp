#include "pixels.h"
#include <stdio.h>
#include <string.h>

namespace pt {

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseColor(const char* s, Rgb& out) {
  if (!s) return false;
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  int v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = hexv(s[i]);
    if (v[i] < 0) return false;
  }
  out = Rgb((uint8_t)(v[0] * 16 + v[1]), (uint8_t)(v[2] * 16 + v[3]), (uint8_t)(v[4] * 16 + v[5]));  // hex digits: 0-255
  return true;
}

std::string colorText(const Rgb& c) {
  char b[8];
  (void)snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
  return b;
}

bool Effect::operator==(const Effect& o) const {
  if (kind != o.kind || speed != o.speed) return false;
  switch (kind) {
    case Kind::Off: return true;
    case Kind::Fill: case Kind::Flicker: return color == o.color;
    case Kind::Chase: return color == o.color && step_ms == o.step_ms && width == o.width;
    case Kind::Rainbow: return period_ms == o.period_ms;
    case Kind::Flash: return color == o.color && period_ms == o.period_ms;
    case Kind::Fade: return color == o.color && ms == o.ms;
    case Kind::Comet: return color == o.color && step_ms == o.step_ms && width == o.width;
    case Kind::Sparkle: return color == o.color;
    case Kind::Breathe: return color == o.color && period_ms == o.period_ms;
    case Kind::Fire: return true;
    case Kind::Pattern: return pattern == o.pattern && pattern_gen == o.pattern_gen;
  }
  return true;
}

std::string Effect::describe() const {
  switch (kind) {
    case Kind::Off: return "off";
    case Kind::Fill: return "fill " + colorText(color);
    case Kind::Chase: return "chase " + colorText(color);
    case Kind::Rainbow: return "rainbow";
    case Kind::Flash: return "flash " + colorText(color);
    case Kind::Fade: return "fade " + colorText(color);
    case Kind::Flicker: return "flicker " + colorText(color);
    case Kind::Comet: return "comet " + colorText(color);
    case Kind::Sparkle: return "sparkle " + colorText(color);
    case Kind::Breathe: return "breathe " + colorText(color);
    case Kind::Fire: return "fire";
    case Kind::Pattern: return "pattern";
  }
  return "off";
}

Rgb wheel(uint8_t pos) {
  uint16_t p = pos;
  // p * 3 stays within 0-255 in each third
  if (p <= 84) return Rgb((uint8_t)(255 - p * 3), (uint8_t)(p * 3), 0);
  if (p <= 169) { p = (uint16_t)(p - 85); return Rgb(0, (uint8_t)(255 - p * 3), (uint8_t)(p * 3)); }
  p = (uint16_t)(p - 170);
  return Rgb((uint8_t)(p * 3), 0, (uint8_t)(255 - p * 3));
}

static uint32_t hash32(uint32_t x) {
  uint32_t h = x * 0x9E3779B9u;
  h ^= h >> 15;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  return h;
}

void render(const Effect& e, uint64_t t, Rgb* out, size_t n) {
  size_t n1 = n < 1 ? 1 : n;
  if (e.speed != 100) t = t * e.speed / 100;
  switch (e.kind) {
    case Effect::Kind::Off:
    case Effect::Kind::Pattern:  // played by the controller (pattern_vm.h); dark here
      for (size_t i = 0; i < n; i++) out[i] = Rgb();
      break;
    case Effect::Kind::Fill:
      for (size_t i = 0; i < n; i++) out[i] = e.color;
      break;
    case Effect::Kind::Chase: {
      size_t step = (size_t)(t / (e.step_ms < 1 ? 1 : e.step_ms));
      size_t w = e.width < 1 ? 1 : e.width;
      for (size_t i = 0; i < n; i++) {
        size_t d = (i + n - step % n1) % n1;
        out[i] = d < w ? e.color : Rgb();
      }
      break;
    }
    case Effect::Kind::Rainbow: {
      uint64_t per = e.period_ms < 1 ? 1 : e.period_ms;
      size_t base = (size_t)((t % per) * 256 / per);
      for (size_t i = 0; i < n; i++) out[i] = wheel((uint8_t)((base + i * 256 / n1) % 256));
      break;
    }
    case Effect::Kind::Flash: {
      uint64_t per = e.period_ms < 2 ? 2 : e.period_ms;
      Rgb c = t % per < per / 2 ? e.color : Rgb();
      for (size_t i = 0; i < n; i++) out[i] = c;
      break;
    }
    case Effect::Kind::Fade: {
      uint8_t k = (e.ms == 0 || t >= e.ms) ? (uint8_t)255 : (uint8_t)(t * 255 / e.ms);
      for (size_t i = 0; i < n; i++) out[i] = e.color.scale(k);
      break;
    }
    case Effect::Kind::Flicker: {
      uint32_t slot = (uint32_t)(t / 40);
      for (size_t i = 0; i < n; i++) {
        uint8_t k = (uint8_t)(120 + hash32(slot * 31u + (uint32_t)i) % 136);  // 120-255
        out[i] = e.color.scale(k);
      }
      break;
    }
    case Effect::Kind::Comet: {
      // a bright head moving along the strip with a tail that fades behind it
      size_t head = (size_t)(t / (e.step_ms < 1 ? 1 : e.step_ms)) % n1;
      size_t w = e.width < 1 ? 1 : e.width;
      for (size_t i = 0; i < n; i++) {
        size_t d = (head + n1 - i) % n1;  // pixels behind the head
        uint32_t k = d < w ? (uint32_t)(255 * (w - d) / w) : 0;
        out[i] = e.color.scale((uint8_t)(k * k / 255));
      }
      break;
    }
    case Effect::Kind::Sparkle: {
      // every pixel has its own turn (a random offset); on about one turn in six it flashes and fades
      for (size_t i = 0; i < n; i++) {
        uint64_t tt = t + hash32((uint32_t)i * 7919u + 1u) % PIXEL_SPARKLE_MS;
        uint32_t turn = (uint32_t)(tt / PIXEL_SPARKLE_MS), ph = (uint32_t)(tt % PIXEL_SPARKLE_MS);
        bool lit = hash32(turn * 2654435761u ^ (uint32_t)i) % 6 == 0;
        uint32_t k = lit && ph < PIXEL_SPARKLE_FADE_MS ? 255 - ph * 255 / PIXEL_SPARKLE_FADE_MS : 0;
        out[i] = e.color.scale((uint8_t)(k * k / 255));
      }
      break;
    }
    case Effect::Kind::Breathe: {
      // the whole strip glows up and down, never quite dark
      uint64_t per = e.period_ms < 2 ? 2 : e.period_ms;
      uint64_t ph = t % per;
      uint32_t x = (uint32_t)((ph < per / 2 ? ph : per - ph) * 510 / per);  // 0-255
      if (x > 255) x = 255;
      uint8_t k = (uint8_t)(8 + x * x * 247 / 65025);
      Rgb c = e.color.scale(k);
      for (size_t i = 0; i < n; i++) out[i] = c;
      break;
    }
    case Effect::Kind::Fire: {
      // each pixel a flame: a random level every PIXEL_FIRE_MS, blended in between, in red, orange and yellow
      uint32_t slot = (uint32_t)(t / PIXEL_FIRE_MS), frac = (uint32_t)(t % PIXEL_FIRE_MS) * 256 / PIXEL_FIRE_MS;
      for (size_t i = 0; i < n; i++) {
        uint32_t a = hash32(slot * 31u + (uint32_t)i) & 255, b = hash32((slot + 1) * 31u + (uint32_t)i) & 255;
        uint32_t heat = 90 + ((a * (256 - frac) + b * frac) >> 8) * 165 / 255;  // 90-255
        out[i] = Rgb((uint8_t)heat, (uint8_t)(heat * heat * 140 / 65025), (uint8_t)(heat > 240 ? (heat - 240) * 2 : 0));
      }
      break;
    }
  }
}

Effect pixelEffect(Effect::Kind kind, Rgb color, uint16_t speed) {
  Effect e;
  e.kind = kind;
  e.color = color;
  e.speed = speed < 1 ? 1 : speed;
  switch (kind) {
    case Effect::Kind::Chase: e.step_ms = PIXEL_CHASE_STEP_MS; e.width = PIXEL_CHASE_WIDTH; break;
    case Effect::Kind::Rainbow: e.period_ms = PIXEL_RAINBOW_MS; break;
    case Effect::Kind::Flash: e.period_ms = PIXEL_FLASH_MS; break;
    case Effect::Kind::Fade: e.ms = PIXEL_FADE_MS; break;
    case Effect::Kind::Comet: e.step_ms = PIXEL_COMET_STEP_MS; e.width = PIXEL_COMET_TAIL; break;
    case Effect::Kind::Breathe: e.period_ms = PIXEL_BREATHE_MS; break;
    case Effect::Kind::Off: case Effect::Kind::Fill: case Effect::Kind::Flicker: case Effect::Kind::Sparkle: case Effect::Kind::Fire:
    case Effect::Kind::Pattern:
    default: break;
  }
  return e;
}

}  // namespace pt
