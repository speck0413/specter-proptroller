// A DFPlayer Mini model for the simulator and the tests: "online" after power-up, ACKs when asked,
// "finished" twice at the end of a track, errors for files that are not on the card.
#pragma once
#include "../dfplayer.h"
#include <map>
#include <tuple>
#include <string>
#include <vector>

namespace pt {

struct DfBehaviour {
  Millis boot_ms = 1000;
  bool acks = true;
  bool double_finish = true;
};

class DfModel {
 public:
  static const uint8_t MP3 = 100;  // folder number used for /mp3/NNNN
  static const uint8_t ROOT = 0;   // play by card index
  DfBehaviour behaviour;
  void setCard(std::initializer_list<std::tuple<uint8_t, uint16_t, Millis>> files) {
    card.clear();
    for (auto& f : files) card[{std::get<0>(f), std::get<1>(f)}] = std::get<2>(f);
  }
  // Files on the card: (folder, track) -> duration ms. An empty card plays anything for 3 s.
  std::map<std::pair<uint8_t, uint16_t>, Millis> card;
  uint8_t volume = 30;
  bool playing = false;
  uint8_t play_folder = 0;
  uint16_t play_track = 0;
  Millis until = 0;
  bool looping = false;
  std::vector<std::pair<Millis, uint8_t>> log;  // (time, command)
  struct Frame { Millis t; uint8_t cmd; uint16_t param; };
  std::vector<Frame> frames;  // every command frame, with its parameter
  struct Play { Millis t; std::string what; uint8_t volume; };
  std::vector<Play> plays;  // every playback that started
  std::vector<std::pair<Millis, uint16_t>> finished;  // (time, track) of every track that played to its end
  uint32_t bad_frames = 0;

  void receive(const uint8_t* b, size_t n, Millis now);
  // Advance; returns bytes the module sends.
  std::vector<uint8_t> tick(Millis now);
  std::string nowPlaying() const;
  size_t count(uint8_t cmd) const;

 private:
  bool duration(uint8_t folder, uint16_t track, Millis& d) const;
  bool byIndex(uint16_t i, Millis& d) const;
  void send(uint8_t c, uint16_t p);
  void command(uint8_t c, bool fb, uint16_t p, Millis now);
  bool online_sent_ = false;
  std::vector<uint8_t> rx_, out_;
};

}  // namespace pt
