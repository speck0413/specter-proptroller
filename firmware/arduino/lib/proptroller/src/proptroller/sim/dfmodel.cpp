#if !defined(ARDUINO) || defined(PROPTROLLER_SIM)
#include "dfmodel.h"
#include <stdio.h>

namespace pt {

bool DfModel::duration(uint8_t f, uint16_t t, Millis& d) const {
  if (card.empty()) { d = 3000; return true; }
  auto it = card.find({f, t});
  if (it == card.end()) return false;
  d = it->second;
  return true;
}

bool DfModel::byIndex(uint16_t i, Millis& d) const {
  if (card.empty()) { d = 3000; return true; }
  size_t k = i > 0 ? i - 1 : 0;
  if (k >= card.size()) return false;
  auto it = card.begin();
  std::advance(it, k);
  d = it->second;
  return true;
}

void DfModel::send(uint8_t c, uint16_t p) {
  uint8_t f[10];
  size_t n = df::frame(c, false, p, true, f);
  out_.insert(out_.end(), f, f + n);
}

void DfModel::receive(const uint8_t* b, size_t n, Millis now) {
  for (size_t i = 0; i < n; i++) {
    uint8_t x = b[i];
    if (rx_.empty() && x != 0x7E) continue;
    rx_.push_back(x);
    size_t k = rx_.size();
    if ((k == 2 && rx_[1] != 0xFF) || (k == 3 && rx_[2] != 0x06)) { bad_frames++; rx_.clear(); continue; }
    if (k == 8 && x == 0xEF) {
      uint8_t c = rx_[3];
      bool fb = rx_[4] != 0;
      uint16_t p = (uint16_t)(rx_[5] << 8) | rx_[6];
      rx_.clear();
      command(c, fb, p, now);
    } else if (k == 10) {
      uint8_t c = rx_[3];
      bool fb = rx_[4] != 0;
      uint16_t p = (uint16_t)(rx_[5] << 8) | rx_[6];
      uint16_t chk = (uint16_t)(rx_[7] << 8) | rx_[8];
      bool good = rx_[9] == 0xEF && chk == df::checksum(c, fb, p);
      rx_.clear();
      if (good) command(c, fb, p, now);
      else bad_frames++;
    }
  }
}

void DfModel::command(uint8_t c, bool fb, uint16_t p, Millis now) {
  using namespace df::cmd;
  log.push_back({now, c});
  frames.push_back({now, c, p});
  if (!online_sent_) return;  // a module that is still booting ignores commands
  bool ok = true;
  Millis d = 0;
  switch (c) {
    case PLAY_FOLDER: {
      uint8_t f = (uint8_t)(p >> 8);
      uint16_t t = p & 0xFF;
      if (duration(f, t, d)) { playing = true; play_folder = f; play_track = t; until = now + d; looping = false; }
      else { ok = false; send(df::msg::ERROR, 6); }
      break;
    }
    case PLAY_MP3:
      if (duration(MP3, p, d)) { playing = true; play_folder = MP3; play_track = p; until = now + d; looping = false; }
      else { ok = false; send(df::msg::ERROR, 6); }
      break;
    case PLAY_INDEX:
    case LOOP_TRACK:
      if (byIndex(p, d)) { playing = true; play_folder = ROOT; play_track = p; until = now + d; looping = c == LOOP_TRACK; }
      else { ok = false; send(df::msg::ERROR, 6); }
      break;
    case LOOP_FOLDER:
      if (!duration((uint8_t)p, 1, d)) d = 3000;
      playing = true; play_folder = (uint8_t)p; play_track = 1; until = now + d; looping = true;
      break;
    case VOLUME: volume = p > 30 ? 30 : (uint8_t)p; break;
    case STOP: case PAUSE: playing = false; break;
    case RESET: playing = false; online_sent_ = false; break;
    default: break;
  }
  if (ok && (c == PLAY_FOLDER || c == PLAY_MP3 || c == PLAY_INDEX || c == LOOP_TRACK || c == LOOP_FOLDER))
    plays.push_back({now, nowPlaying(), volume});
  if (ok && fb && behaviour.acks) send(df::msg::ACK, 0);
}

std::vector<uint8_t> DfModel::tick(Millis now) {
  if (!online_sent_ && now >= behaviour.boot_ms) {
    online_sent_ = true;
    send(df::msg::ONLINE, 2);
  }
  if (playing && now >= until) {
    if (looping) {
      Millis d = 0;
      if (!duration(play_folder, play_track, d)) d = 3000;
      until = now + d;
    } else {
      playing = false;
      send(df::msg::FINISHED_SD, play_track);
      if (behaviour.double_finish) send(df::msg::FINISHED_SD, play_track);
    }
  }
  std::vector<uint8_t> o;
  o.swap(out_);
  return o;
}

std::string DfModel::nowPlaying() const {
  if (!playing) return "";
  char b[32];
  if (play_folder == MP3) (void)snprintf(b, sizeof b, "/mp3/%04u", play_track);
  else if (play_folder == ROOT) (void)snprintf(b, sizeof b, "card #%u", play_track);
  else (void)snprintf(b, sizeof b, "/%02u/%03u", play_folder, play_track);
  return b;
}

size_t DfModel::count(uint8_t c) const {
  size_t n = 0;
  for (auto& x : log) n += x.second == c;
  return n;
}

}  // namespace pt
#endif
