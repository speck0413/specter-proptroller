// Non-blocking DFPlayer Mini driver (9600 8N1). Every frame is 10 bytes:
//
//   0x7E 0xFF 0x06 CMD FEEDBACK PARAM_HI PARAM_LO CHK_HI CHK_LO 0xEF
//
// CHK = 0 - (0xFF + 0x06 + CMD + FEEDBACK + PARAM_HI + PARAM_LO), as in DFRobot's library. On the board the
// module's TX comes back to the MCU (ESP32 GPIO34 / Pico GP7), so the driver sees ACKs, "track finished"
// and errors.
//
// Clone tolerance: commands are queued and sent with a minimum gap, the driver never blocks waiting for an
// ACK (it retries once after a timeout), nothing is sent for 1.5 s after power-up, the checksum can be left
// out, and the duplicate "finished" many modules send is suppressed.
#pragma once
#include "types.h"
#include <vector>

namespace pt {

namespace df {
static const uint8_t START = 0x7E, VERSION = 0xFF, LEN = 0x06, END = 0xEF;
static const size_t FRAME_LEN = 10;
namespace cmd {
static const uint8_t NEXT = 0x01, PREV = 0x02, PLAY_INDEX = 0x03, VOLUME = 0x06, EQ = 0x07, LOOP_TRACK = 0x08,
                     DEVICE = 0x09, RESET = 0x0C, RESUME = 0x0D, PAUSE = 0x0E, PLAY_FOLDER = 0x0F, STOP = 0x16,
                     LOOP_FOLDER = 0x17, SINGLE_CYCLE = 0x19, PLAY_MP3 = 0x12, QUERY_STATUS = 0x42;
}
namespace msg {
static const uint8_t FINISHED_USB = 0x3C, FINISHED_SD = 0x3D, INSERTED = 0x3A, REMOVED = 0x3B, ONLINE = 0x3F,
                     ERROR = 0x40, ACK = 0x41;
}
uint16_t checksum(uint8_t cmd, bool feedback, uint16_t param);
// Build one frame into f; returns its length (10, or 8 without a checksum).
size_t frame(uint8_t cmd, bool feedback, uint16_t param, bool with_checksum, uint8_t f[FRAME_LEN]);
}  // namespace df

// A high-level player command.
struct SoundCommand {
  enum class Kind : uint8_t { PlayFolder, PlayMp3, PlayIndex, LoopIndex, LoopFolder, Volume, Stop, Pause, Resume, Next, Reset, SelectSd };
  Kind kind = Kind::Stop;
  uint16_t a = 0;  // folder / track / index / volume
  uint16_t b = 0;  // track for PlayFolder

  static SoundCommand playFolder(uint8_t folder, uint8_t track) { SoundCommand c; c.kind = Kind::PlayFolder; c.a = folder; c.b = track; return c; }
  static SoundCommand playMp3(uint16_t track) { SoundCommand c; c.kind = Kind::PlayMp3; c.a = track; return c; }
  static SoundCommand playIndex(uint16_t i) { SoundCommand c; c.kind = Kind::PlayIndex; c.a = i; return c; }
  static SoundCommand loopFolder(uint8_t f) { SoundCommand c; c.kind = Kind::LoopFolder; c.a = f; return c; }
  static SoundCommand volume(uint8_t v) { SoundCommand c; c.kind = Kind::Volume; c.a = v; return c; }
  static SoundCommand simple(Kind k) { SoundCommand c; c.kind = k; return c; }

  bool operator==(const SoundCommand& o) const { return kind == o.kind && a == o.a && b == o.b; }
  // Command byte and parameter (values clamped).
  void encode(uint8_t& cmd, uint16_t& param) const;
  bool startsPlayback() const;
  // Protocol name ("play_folder", ...).
  const char* name() const;
};

struct DfEvent {
  enum class Kind : uint8_t { Online, Finished, Error, Ack, CardInserted, CardRemoved, Reply, Timeout };
  Kind kind = Kind::Online;
  uint16_t value = 0;  // devices / track / error code / cmd
  uint16_t param = 0;  // Reply param
};

struct DfConfig {
  bool use_ack = true;
  bool checksum = true;
  uint32_t init_delay_ms = 1500;
  uint32_t gap_ms = 30;
  uint32_t ack_timeout_ms = 200;
  uint8_t retries = 1;
};

// Receive-side frame parser (accepts frames with or without a checksum; checks it when present).
class DfParser {
 public:
  // Returns true and fills cmd/param when a frame completes.
  bool push(uint8_t b, uint8_t& cmd, uint16_t& param);
  uint32_t errors = 0;

 private:
  uint8_t buf_[df::FRAME_LEN]{};
  size_t n_ = 0;
};

// The serial port the module is on.
class ByteStream {
 public:
  virtual ~ByteStream() {}
  virtual int available() = 0;
  virtual int read() = 0;  // -1 when nothing
  virtual size_t write(const uint8_t* b, size_t n) = 0;
};

class DfPlayer {
 public:
  explicit DfPlayer(const DfConfig& cfg = DfConfig()) : cfg_(cfg) {}
  const DfConfig& config() const { return cfg_; }
  bool isPlaying() const { return playing_; }
  // What is playing (false if nothing).
  bool current(SoundCommand& out) const;
  bool isOnline() const { return online_; }
  size_t queued() const { return len_; }
  // Queue a command. A Stop clears anything still queued first.
  void command(const SoundCommand& c);
  void pushRaw(uint8_t cmd, uint16_t param);
  // Feed received bytes directly.
  void feed(const uint8_t* b, size_t n, Millis now, std::vector<DfEvent>& events);
  // Read what the module sent, then send at most one queued frame. Never blocks.
  void poll(ByteStream& uart, Millis now, std::vector<DfEvent>& events);
  uint32_t dropped = 0;

 private:
  void onMessage(uint8_t c, uint16_t p, Millis now, std::vector<DfEvent>& ev);
  void send(ByteStream& uart, uint8_t c, uint16_t p, Millis now, uint8_t tries);
  static const size_t QUEUE = 16;
  DfConfig cfg_;
  DfParser parser_;
  uint8_t qc_[QUEUE]{};
  uint16_t qp_[QUEUE]{};
  size_t head_ = 0, len_ = 0;
  bool has_tx_ = false;
  Millis last_tx_ = 0;
  bool pending_ = false;
  uint8_t pend_c_ = 0;
  uint16_t pend_p_ = 0;
  Millis pend_at_ = 0;
  uint8_t pend_tries_ = 0;
  bool playing_ = false;
  bool has_current_ = false;
  SoundCommand current_;
  bool has_finished_ = false;
  uint16_t fin_track_ = 0;
  Millis fin_at_ = 0;
  bool online_ = false;
};

}  // namespace pt
