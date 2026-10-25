#include "link.h"
#include <string.h>
#include "password.h"

namespace pt {

// ---------------------------------------------------------------- HMAC-SHA256 (RFC 2104)

void hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t out[32]) {
  uint8_t k[64];
  memset(k, 0, sizeof k);
  if (key_len > sizeof k) sha256(key, key_len, k);
  else if (key_len) memcpy(k, key, key_len);
  uint8_t pad[64], inner[32];
  Sha256 h;
  for (size_t i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
  h.update(pad, 64);
  h.update(data, len);
  h.finish(inner);
  h.reset();
  for (size_t i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
  h.update(pad, 64);
  h.update(inner, 32);
  h.finish(out);
}

// ---------------------------------------------------------------- the frame

namespace {
void put32(std::string& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back((char)((v >> (8 * i)) & 0xFF));
}
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
bool putStr(std::string& b, const std::string& s, size_t max) {
  if (s.size() > max) return false;
  b.push_back((char)s.size());
  b += s;
  return true;
}
// a text cut to max bytes without splitting a UTF-8 character
std::string cut(const std::string& s, size_t max) {
  if (s.size() <= max) return s;
  size_t n = max;
  while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
  return s.substr(0, n);
}
const size_t FIELD_MAX[6] = {LINK_GROUP_MAX, LINK_ID_MAX, LINK_NAME_MAX, LINK_NAME_MAX, LINK_WHAT_MAX, LINK_VALUE_MAX};
}  // namespace

std::string linkEncode(const LinkFrame& f, const std::string& key) {
  std::string b(LINK_MAGIC, 4);
  b.push_back((char)f.kind);
  b.push_back((char)((key.empty() ? 0 : 1) | (f.ok ? 2 : 0)));
  put32(b, f.boot);
  put32(b, f.seq);
  put32(b, f.bound);
  const std::string* fields[6] = {&f.group, &f.id, &f.name, &f.to, &f.what, &f.value};
  for (int i = 0; i < 6; i++)
    if (!putStr(b, *fields[i], FIELD_MAX[i])) return "";
  if (!key.empty()) {
    uint8_t tag[32];
    hmacSha256((const uint8_t*)key.data(), key.size(), (const uint8_t*)b.data(), b.size(), tag);
    b.append((const char*)tag, LINK_TAG);
  }
  return b.size() <= LINK_FRAME_MAX ? b : "";
}

LinkRead linkDecode(const uint8_t* b, size_t n, const std::string& key, LinkFrame& out) {
  if (n < 4 || memcmp(b, LINK_MAGIC, 4) != 0) return LinkRead::NotLink;
  if (n < LINK_HEAD + 6 || n > LINK_FRAME_MAX) return LinkRead::Bad;
  uint8_t kind = b[4], flags = b[5];
  if (kind < 1 || kind > 4 || (flags & ~3u)) return LinkRead::Bad;
  bool tagged = flags & 1;
  size_t body = tagged ? n - LINK_TAG : n;
  if (tagged && n < LINK_HEAD + 6 + LINK_TAG) return LinkRead::Bad;
  LinkFrame f;
  f.kind = (LinkFrame::Kind)kind;
  f.ok = (flags & 2) != 0;
  f.boot = get32(b + 6);
  f.seq = get32(b + 10);
  f.bound = get32(b + 14);
  std::string* fields[6] = {&f.group, &f.id, &f.name, &f.to, &f.what, &f.value};
  size_t at = LINK_HEAD;
  for (int i = 0; i < 6; i++) {
    if (at >= body) return LinkRead::Bad;
    size_t len = b[at++];
    if (len > FIELD_MAX[i] || at + len > body) return LinkRead::Bad;
    fields[i]->assign((const char*)b + at, len);
    at += len;
  }
  if (at != body) return LinkRead::Bad;
  if (key.empty() && tagged) return LinkRead::Signed;
  if (!key.empty() && !tagged) return LinkRead::Unsigned;
  if (tagged) {
    uint8_t tag[32];
    hmacSha256((const uint8_t*)key.data(), key.size(), b, body, tag);
    uint8_t diff = 0;  // every byte compared (no early way out)
    for (size_t i = 0; i < LINK_TAG; i++) diff |= (uint8_t)(tag[i] ^ b[body + i]);
    if (diff) return LinkRead::BadTag;
  }
  out = f;
  return LinkRead::Ok;
}

// ---------------------------------------------------------------- numbers seen

int LinkSeen::find(const std::string& id, uint32_t boot) const {
  for (int i = 0; i < n_; i++)
    if (e_[i].boot == boot && e_[i].id == id) return i;
  return -1;
}

LinkSeen::Fresh LinkSeen::check(const std::string& id, uint32_t boot, uint32_t seq) const {
  int i = find(id, boot);
  if (i < 0) return Fresh::New;
  const Entry& e = e_[i];
  if (seq > e.top) return Fresh::New;
  uint32_t back = e.top - seq;
  if (back >= 32) return Fresh::Old;
  return (e.mask >> back) & 1 ? Fresh::Again : Fresh::New;
}

bool LinkSeen::take(const std::string& id, uint32_t boot, uint32_t seq, bool ok, const std::string& text, Millis now) {
  bool forgot = false;
  int i = find(id, boot);
  if (i < 0) {
    if (n_ < LINK_SENDERS) i = n_++;
    else {  // the one heard from least recently makes room
      i = 0;
      for (int k = 1; k < n_; k++)
        if (e_[k].used < e_[i].used) i = k;
      forgot = true;
    }
    e_[i] = Entry();
    e_[i].id = id;
    e_[i].boot = boot;
    e_[i].top = seq;
    e_[i].mask = 1;
  } else {
    Entry& e = e_[i];
    if (seq > e.top) {
      uint32_t shift = seq - e.top;
      e.mask = shift >= 32 ? 0 : e.mask << shift;
      e.top = seq;
      e.mask |= 1;
    } else {
      e.mask |= 1u << (e.top - seq);
    }
  }
  e_[i].last = seq;
  e_[i].last_ok = ok;
  e_[i].last_text = text;
  e_[i].used = now;
  return forgot;
}

bool LinkSeen::answerOf(const std::string& id, uint32_t boot, uint32_t seq, bool& ok, std::string& text) const {
  int i = find(id, boot);
  if (i < 0 || e_[i].last != seq) return false;
  ok = e_[i].last_ok;
  text = e_[i].last_text;
  return true;
}

// ---------------------------------------------------------------- the peer

LinkPeer::LinkPeer(uint32_t (*random)()) : random_(random) { newNonce(); }

void LinkPeer::newNonce() {
  uint32_t n = 0;
  while (n == 0 || n == nonce_) n = random_();  // 0 means "not bound"
  nonce_ = n;
  seen_.clear();
}

bool LinkPeer::refresh(NetApp& app) {
  uint32_t e = app.linkEpoch();
  if (e != epoch_) {
    epoch_ = e;
    LinkConfig c;
    have_cfg_ = app.linkConfig(c);
    if (c.key != cfg_.key) newNonce();  // a new key: nothing taken before counts
    cfg_ = c;
  }
  return have_cfg_ && (cfg_.listen || cfg_.send);
}

LinkFrame LinkPeer::request(const LinkOut& o, uint32_t bound) const {
  LinkFrame f;
  f.kind = o.set ? LinkFrame::Kind::Set : LinkFrame::Kind::Effect;
  f.boot = nonce_;
  f.seq = o.seq;
  f.bound = bound;
  f.group = cfg_.group;
  f.id = cfg_.id;
  f.name = cfg_.name;
  f.to = o.to;
  f.what = o.what;
  f.value = o.value;
  return f;
}

LinkPeer::Peer* LinkPeer::peerNamed(const std::string& name) {
  for (auto& p : peers_)
    if (!p.id.empty() && p.name == name) return &p;
  return nullptr;
}

void LinkPeer::learn(const LinkFrame& f, const LinkAddr& from, Millis now) {
  Peer* at = nullptr;
  for (auto& p : peers_)
    if (p.id == f.id) at = &p;
  if (!at)
    for (auto& p : peers_)
      if (!at && p.id.empty()) at = &p;
  if (!at) {
    at = &peers_[0];
    for (auto& p : peers_)
      if (p.used < at->used) at = &p;
  }
  at->id = f.id;
  at->name = f.name;
  at->nonce = f.boot;
  at->addr = from;
  at->used = now;
}

void LinkPeer::send(const LinkOut& o, Millis now, LinkRadio& radio) {
  if (o.seq <= last_seq_) newNonce();  // the board restarted under this network loop (the simulator): a new boot
  last_seq_ = o.seq;
  Sending* slot = nullptr;
  for (auto& s : sending_)
    if (!s.used && !slot) slot = &s;
  if (!slot) {
    slot = &sending_[0];
    for (auto& s : sending_)
      if (s.at < slot->at) slot = &s;
  }
  slot->used = true;
  slot->out = o;
  slot->at = now;
  slot->resends = 0;
  // to a board whose nonce is known (keyed): bound at once; otherwise unbound, and a keyed receiver challenges
  Peer* p = cfg_.key.empty() || o.to == "all" ? nullptr : peerNamed(o.to);
  std::string frame = linkEncode(request(o, p ? p->nonce : 0), cfg_.key);
  if (frame.empty()) { dropped++; slot->used = false; return; }
  radio.broadcast(frame);
  sent++;
}

void LinkPeer::poll(Millis now, NetApp& app, LinkRadio& radio) {
  bool on = refresh(app);
  // taken whatever the way out (the board logs tells over MQTT, with board_link off, and the answers that did
  // not come); only peer-to-peer ones come back here
  if (app.linkWork())
    for (auto& o : app.linkTake())
      if (on && cfg_.send) send(o, now, radio);
  if (!on) return;
  for (auto& s : sending_) {
    if (!s.used) continue;
    if (now - s.at >= LINK_KEEP_MS) { s.used = false; continue; }
    if (s.resends < 2 && now - s.at >= LINK_RESEND_MS[s.resends]) {
      s.resends++;
      Peer* p = cfg_.key.empty() || s.out.to == "all" ? nullptr : peerNamed(s.out.to);
      std::string frame = linkEncode(request(s.out, p ? p->nonce : 0), cfg_.key);
      if (!frame.empty()) radio.broadcast(frame);
    }
  }
}

void LinkPeer::received(const uint8_t* b, size_t n, const LinkAddr& from, Millis now, NetApp& app, LinkRadio& radio) {
  if (!refresh(app)) return;
  LinkFrame f;
  LinkRead r = linkDecode(b, n, cfg_.key, f);
  if (r == LinkRead::NotLink) return;
  if (r != LinkRead::Ok || f.group != cfg_.group || f.id == cfg_.id) { dropped++; return; }
  const bool keyed = !cfg_.key.empty();
  LinkFrame a;  // an answer or a challenge from this board
  a.boot = nonce_;
  a.seq = f.seq;
  a.bound = f.boot;
  a.group = cfg_.group;
  a.id = cfg_.id;
  a.name = cfg_.name;
  a.to = f.id;
  switch (f.kind) {
    case LinkFrame::Kind::Effect:
    case LinkFrame::Kind::Set: {
      if (!cfg_.listen || (f.to != "all" && f.to != cfg_.name)) return;
      if (keyed && f.bound != nonce_) {
        // not bound to this boot of this board: it may be a recording; the sender proves it is live
        a.kind = LinkFrame::Kind::Challenge;
        std::string frame = linkEncode(a, cfg_.key);
        if (!frame.empty()) radio.reply(frame, from);
        challenged++;
        return;
      }
      LinkSeen::Fresh fresh = seen_.check(f.id, f.boot, f.seq);
      if (fresh == LinkSeen::Fresh::Old) { dropped++; return; }
      if (fresh == LinkSeen::Fresh::Again) {  // came twice (UDP and ESP-NOW, a resend): answered again, not run again
        if (!seen_.answerOf(f.id, f.boot, f.seq, a.ok, a.value)) return;
        a.kind = LinkFrame::Kind::Answer;
        std::string frame = linkEncode(a, cfg_.key);
        if (!frame.empty()) radio.reply(frame, from);
        return;
      }
      LinkIn in;
      in.set = f.kind == LinkFrame::Kind::Set;
      in.id = f.id;
      in.from = f.name;
      in.what = f.what;
      in.value = f.value;
      in.via = from.espnow ? "ESP-NOW" : "Wi-Fi";
      LinkReply rep = app.linkIn(in);
      a.kind = LinkFrame::Kind::Answer;
      a.ok = rep.ok;
      a.value = cut(rep.text, LINK_VALUE_MAX);
      bool forgot = seen_.take(f.id, f.boot, f.seq, a.ok, a.value, now);
      taken++;
      std::string frame = linkEncode(a, cfg_.key);
      if (!frame.empty()) radio.reply(frame, from);
      // a sender forgotten to make room: with a key, a new nonce, so none of its earlier frames can come back
      if (forgot && keyed) newNonce();
      return;
    }
    case LinkFrame::Kind::Answer:
    case LinkFrame::Kind::Challenge: {
      if (f.to != cfg_.id || f.bound != nonce_) { dropped++; return; }
      Sending* s = nullptr;
      for (auto& x : sending_)
        if (x.used && x.out.seq == f.seq) s = &x;
      if (!s) return;  // answered already, or too late
      learn(f, from, now);
      if (f.kind == LinkFrame::Kind::Challenge) {
        std::string frame = linkEncode(request(s->out, f.boot), cfg_.key);
        if (!frame.empty()) radio.reply(frame, from);
        return;
      }
      app.linkAnswer(f.seq, f.name, f.ok, f.value);
      answered++;
      if (s->out.to != "all") s->used = false;  // a board's one answer: no more resends
      return;
    }
  }
}

}  // namespace pt
