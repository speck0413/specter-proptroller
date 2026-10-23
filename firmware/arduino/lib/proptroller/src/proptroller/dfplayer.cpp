#include "dfplayer.h"

namespace pt {

namespace df {
uint16_t checksum(uint8_t c, bool fb, uint16_t p) {
  uint16_t sum = (uint16_t)(VERSION + LEN + c + (fb ? 1 : 0) + (p >> 8) + (p & 0xFF));
  return (uint16_t)(0 - sum);
}

size_t frame(uint8_t c, bool fb, uint16_t p, bool with_checksum, uint8_t f[FRAME_LEN]) {
  for (size_t i = 0; i < FRAME_LEN; i++) f[i] = 0;
  f[0] = START;
  f[1] = VERSION;
  f[2] = LEN;
  f[3] = c;
  f[4] = fb ? 1 : 0;
  f[5] = (uint8_t)(p >> 8);
  f[6] = (uint8_t)(p & 0xFF);
  if (with_checksum) {
    uint16_t k = checksum(c, fb, p);
    f[7] = (uint8_t)(k >> 8);
    f[8] = (uint8_t)(k & 0xFF);
    f[9] = END;
    return 10;
  }
  f[7] = END;
  return 8;
}
}  // namespace df

template <typename T>
static T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

void SoundCommand::encode(uint8_t& c, uint16_t& p) const {
  using namespace df::cmd;
  switch (kind) {
    case Kind::PlayFolder: c = PLAY_FOLDER; p = (uint16_t)((clampv<uint16_t>(a & 0xFF, 1, 99) << 8) | ((b & 0xFF) < 1 ? 1 : (b & 0xFF))); break;
    case Kind::PlayMp3: c = PLAY_MP3; p = clampv<uint16_t>(a, 1, 3000); break;
    case Kind::PlayIndex: c = PLAY_INDEX; p = clampv<uint16_t>(a, 1, 2999); break;
    case Kind::LoopIndex: c = LOOP_TRACK; p = clampv<uint16_t>(a, 1, 2999); break;
    case Kind::LoopFolder: c = LOOP_FOLDER; p = clampv<uint16_t>(a & 0xFF, 1, 99); break;
    case Kind::Volume: c = VOLUME; p = (uint16_t)((a & 0xFF) > 30 ? 30 : (a & 0xFF)); break;
    case Kind::Stop: c = STOP; p = 0; break;
    case Kind::Pause: c = PAUSE; p = 0; break;
    case Kind::Resume: c = RESUME; p = 0; break;
    case Kind::Next: c = NEXT; p = 0; break;
    case Kind::Reset: c = RESET; p = 0; break;
    case Kind::SelectSd: c = DEVICE; p = 2; break;
  }
}

bool SoundCommand::startsPlayback() const {
  return kind == Kind::PlayFolder || kind == Kind::PlayMp3 || kind == Kind::PlayIndex || kind == Kind::LoopIndex || kind == Kind::LoopFolder ||
         kind == Kind::Resume || kind == Kind::Next;
}

const char* SoundCommand::name() const {
  static const char* const n[] = {"play_folder", "play_mp3", "play_index", "loop_index", "loop_folder", "volume",
                                  "stop", "pause", "resume", "next", "reset", "select_sd"};
  PT_ONE_PER(n, Kind::SelectSd);
  return n[idx(kind)];
}

bool DfParser::push(uint8_t b, uint8_t& c, uint16_t& p) {
  if (n_ == 0 && b != df::START) return false;
  buf_[n_++] = b;
  if (n_ == 2 && b != df::VERSION) { errors++; n_ = 0; return false; }
  if (n_ == 3 && b != df::LEN) { errors++; n_ = 0; return false; }
  if (n_ == 8 && b == df::END) {
    n_ = 0;
    c = buf_[3];
    p = (uint16_t)((buf_[5] << 8) | buf_[6]);
    return true;
  }
  if (n_ == 10) {
    n_ = 0;
    if (b != df::END) { errors++; return false; }
    uint16_t k = (uint16_t)((buf_[7] << 8) | buf_[8]);
    uint16_t param = (uint16_t)((buf_[5] << 8) | buf_[6]);
    if (k != df::checksum(buf_[3], buf_[4] != 0, param)) { errors++; return false; }
    c = buf_[3];
    p = param;
    return true;
  }
  return false;
}

bool DfPlayer::current(SoundCommand& out) const {
  if (playing_ && has_current_) { out = current_; return true; }
  return false;
}

void DfPlayer::command(const SoundCommand& c) {
  if (c.kind == SoundCommand::Kind::Stop) len_ = 0;
  if (c.startsPlayback()) {
    playing_ = true;
    if (c.kind != SoundCommand::Kind::Resume && c.kind != SoundCommand::Kind::Next) { current_ = c; has_current_ = true; }
  }
  if (c.kind == SoundCommand::Kind::Stop || c.kind == SoundCommand::Kind::Pause || c.kind == SoundCommand::Kind::Reset) playing_ = false;
  uint8_t cmd = 0;
  uint16_t p = 0;
  c.encode(cmd, p);
  pushRaw(cmd, p);
}

void DfPlayer::pushRaw(uint8_t c, uint16_t p) {
  if (len_ == QUEUE) { dropped++; return; }
  size_t i = (head_ + len_) % QUEUE;
  qc_[i] = c;
  qp_[i] = p;
  len_++;
}

void DfPlayer::onMessage(uint8_t c, uint16_t p, Millis now, std::vector<DfEvent>& ev) {
  using namespace df::msg;
  DfEvent e;
  switch (c) {
    case ACK:
      pending_ = false;
      e.kind = DfEvent::Kind::Ack;
      ev.push_back(e);
      return;
    case FINISHED_SD:
    case FINISHED_USB: {
      // many modules send the finished message twice
      bool dup = has_finished_ && fin_track_ == p && (now > fin_at_ ? now - fin_at_ : 0) < 200;
      has_finished_ = true;
      fin_track_ = p;
      fin_at_ = now;
      if (!dup) {
        playing_ = false;
        e.kind = DfEvent::Kind::Finished;
        e.value = p;
        ev.push_back(e);
      }
      return;
    }
    case ONLINE:
      online_ = true;
      playing_ = false;
      e.kind = DfEvent::Kind::Online;
      e.value = p & 0xFF;
      ev.push_back(e);
      return;
    case ERROR:
      pending_ = false;
      playing_ = false;
      e.kind = DfEvent::Kind::Error;
      e.value = p & 0xFF;
      ev.push_back(e);
      return;
    case INSERTED:
      e.kind = DfEvent::Kind::CardInserted;
      ev.push_back(e);
      return;
    case REMOVED:
      playing_ = false;
      e.kind = DfEvent::Kind::CardRemoved;
      ev.push_back(e);
      return;
    default:
      e.kind = DfEvent::Kind::Reply;
      e.value = c;
      e.param = p;
      ev.push_back(e);
  }
}

void DfPlayer::feed(const uint8_t* b, size_t n, Millis now, std::vector<DfEvent>& ev) {
  for (size_t i = 0; i < n; i++) {
    uint8_t c = 0;
    uint16_t p = 0;
    if (parser_.push(b[i], c, p)) onMessage(c, p, now, ev);
  }
}

void DfPlayer::poll(ByteStream& uart, Millis now, std::vector<DfEvent>& ev) {
  while (uart.available() > 0) {
    int b = uart.read();
    if (b < 0) break;
    uint8_t c = 0;
    uint16_t p = 0;
    if (parser_.push((uint8_t)b, c, p)) onMessage(c, p, now, ev);
  }
  if (now < cfg_.init_delay_ms) return;
  if (has_tx_ && (now > last_tx_ ? now - last_tx_ : 0) < cfg_.gap_ms) return;
  // ACK handling: wait (without blocking) for the pending one, retry, then give up
  if (pending_) {
    if ((now > pend_at_ ? now - pend_at_ : 0) < cfg_.ack_timeout_ms) return;
    if (pend_tries_ < cfg_.retries) {
      send(uart, pend_c_, pend_p_, now, (uint8_t)(pend_tries_ + 1));
      return;
    }
    pending_ = false;
    DfEvent e;
    e.kind = DfEvent::Kind::Timeout;
    e.value = pend_c_;
    ev.push_back(e);
  }
  if (len_ > 0) {
    uint8_t c = qc_[head_];
    uint16_t p = qp_[head_];
    head_ = (head_ + 1) % QUEUE;
    len_--;
    send(uart, c, p, now, 0);
  }
}

void DfPlayer::send(ByteStream& uart, uint8_t c, uint16_t p, Millis now, uint8_t tries) {
  uint8_t f[df::FRAME_LEN];
  size_t n = df::frame(c, cfg_.use_ack, p, cfg_.checksum, f);
  uart.write(f, n);
  has_tx_ = true;
  last_tx_ = now;
  pending_ = cfg_.use_ack;
  if (pending_) { pend_c_ = c; pend_p_ = p; pend_at_ = now; pend_tries_ = tries; }
}

}  // namespace pt
