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
  if (kind != o.kind) return false;
  switch (kind) {
    case Kind::Off: return true;
    case Kind::Fill: case Kind::Flicker: return color == o.color;
    case Kind::Chase: return color == o.color && step_ms == o.step_ms && width == o.width;
    case Kind::Rainbow: return period_ms == o.period_ms;
    case Kind::Flash: return color == o.color && period_ms == o.period_ms;
    case Kind::Fade: return color == o.color && ms == o.ms;
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
  switch (e.kind) {
    case Effect::Kind::Off:
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
  }
}

}  // namespace pt
