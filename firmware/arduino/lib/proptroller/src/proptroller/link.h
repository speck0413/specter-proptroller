// Board link, peer to peer (PROTOCOL.md "Board link"): one board's `tell` lines reach other boards on the same
// Wi-Fi without a broker, as UDP datagrams on the discovery port (and, on the ESP32, the same frame by ESP-NOW).
//
// The frame (binary, little endian; at most LINK_FRAME_MAX bytes, ESP-NOW's payload):
//   0  4  "PTL1"
//   4  1  kind: 1 start an effect, 2 change a setting, 3 an answer, 4 a challenge
//   5  1  flags: bit 0 a tag follows (the frame is signed), bit 1 ok (an answer)
//   6  4  boot: the author's random number for this boot (its nonce)
//   10 4  seq: a request's number (from 1 each boot); an answer's or a challenge's: the request's
//   14 4  bound: a request: the receiver's nonce it is bound to (0: none); an answer or a challenge: the
//          request author's nonce
//   18    six strings, each a length byte and its bytes: group, the author's id, the author's name, to (a board's
//          name or "all"; an answer's or challenge's: the request author's id), what (the effect or the setting),
//          value (the setting's value; an answer's text)
//   ..  16 the tag (flag bit 0): HMAC-SHA256(link_key, every byte before it), its first 16 bytes
//
// With a key (link_key), a receiver takes a request only when it is signed and bound to its own nonce: an unbound
// one is answered with a challenge (the receiver's nonce), and the sender sends it again bound. A recorded frame
// sent again is dropped: bound to an old nonce, or a number already seen (a window of 32 per sender and boot).
// Without a key, nothing is signed and anyone on the network can send.
#pragma once
#include "netapp.h"
#include <string>
#include <vector>

namespace pt {

static const char LINK_MAGIC[4] = {'P', 'T', 'L', '1'};
static const size_t LINK_HEAD = 18;
static const size_t LINK_TAG = 16;
static const size_t LINK_FRAME_MAX = 250;
static const size_t LINK_GROUP_MAX = 16, LINK_ID_MAX = 16, LINK_NAME_MAX = 40, LINK_WHAT_MAX = 32, LINK_VALUE_MAX = 64;
static const size_t LINK_KEY_MAX = 64;
static const uint8_t LINK_SENDERS = 8;      // senders (and boots) a receiver keeps the numbers of
static const uint8_t LINK_PEERS = 16;       // receivers whose nonce a sender knows
static const uint8_t LINK_SENDING = 16;     // requests a sender keeps for answers, resends and challenges
static const Millis LINK_RESEND_MS[2] = {250, 700};  // an unanswered request is sent again then (Wi-Fi broadcasts get lost)
static const Millis LINK_KEEP_MS = 2000;    // and kept this long for an answer (Controller: LINK_ANSWER_MS)

void hmacSha256(const uint8_t* key, size_t key_len, const uint8_t* data, size_t len, uint8_t out[32]);

struct LinkFrame {
  enum class Kind : uint8_t { Effect = 1, Set = 2, Answer = 3, Challenge = 4 };
  Kind kind = Kind::Effect;
  bool ok = false;
  uint32_t boot = 0, seq = 0, bound = 0;
  std::string group, id, name, to, what, value;
};

// The frame's bytes, signed when key is not empty; "" when a field is too long for its limit.
std::string linkEncode(const LinkFrame& f, const std::string& key);
enum class LinkRead : uint8_t {
  Ok,
  NotLink,   // not a link frame (another magic: discovery, a cue)
  Bad,       // too short, a field out of range
  Unsigned,  // no tag, and this board has a key
  Signed,    // a tag, and this board has no key: another group's key
  BadTag,    // a tag that does not match
};
LinkRead linkDecode(const uint8_t* b, size_t n, const std::string& key, LinkFrame& out);

// Where a frame came from (to answer the same way), and how frames go out.
struct LinkAddr {
  uint32_t ip = 0;   // IPv4, network order as lwIP / sockets keep it
  uint16_t port = 0;
  bool espnow = false;
};
class LinkRadio {
 public:
  virtual ~LinkRadio() {}
  // to every board that can hear: a UDP broadcast on each network the board is on, and ESP-NOW (ESP32)
  virtual void broadcast(const std::string& frame) = 0;
  // back the way a frame came: UDP to its address, or an ESP-NOW broadcast
  virtual void reply(const std::string& frame, const LinkAddr& to) = 0;
};

// The numbers a receiver has taken from each sender and boot: a window of 32 below the highest, so a frame that
// came twice (UDP and ESP-NOW, or sent again) runs once, and a late one still runs once.
class LinkSeen {
 public:
  enum class Fresh : uint8_t { New, Again, Old };
  Fresh check(const std::string& id, uint32_t boot, uint32_t seq) const;
  // true when a sender had to be forgotten to make room
  bool take(const std::string& id, uint32_t boot, uint32_t seq, bool ok, const std::string& text, Millis now);
  // the answer given to that number, when it was the last one taken from the sender (to answer a resend again)
  bool answerOf(const std::string& id, uint32_t boot, uint32_t seq, bool& ok, std::string& text) const;
  void clear() { n_ = 0; }

 private:
  struct Entry {
    std::string id;
    uint32_t boot = 0, top = 0, mask = 0;  // mask bit k: top - k taken
    uint32_t last = 0;
    bool last_ok = false;
    std::string last_text;
    Millis used = 0;
  };
  int find(const std::string& id, uint32_t boot) const;
  Entry e_[LINK_SENDERS];
  uint8_t n_ = 0;
};

// The peer-to-peer side of the board link, run by the network loop (ESP32, Pico W, simulator): takes the board's
// tell lines (NetApp::linkTake) and sends them, takes frames that arrive and hands requests to the board
// (NetApp::linkIn) and answers to it (NetApp::linkAnswer). Calls into the app take the lock (LockedApp); the
// frames, the tags and the waits are done without it.
class LinkPeer {
 public:
  explicit LinkPeer(uint32_t (*random)());
  void poll(Millis now, NetApp& app, LinkRadio& radio);
  void received(const uint8_t* b, size_t n, const LinkAddr& from, Millis now, NetApp& app, LinkRadio& radio);
  uint32_t nonce() const { return nonce_; }
  // counters (mem, tests)
  uint32_t sent = 0, taken = 0, answered = 0, challenged = 0, dropped = 0;

 private:
  struct Sending {
    bool used = false;
    LinkOut out;
    Millis at = 0;
    uint8_t resends = 0;
  };
  struct Peer {
    std::string id, name;
    uint32_t nonce = 0;
    LinkAddr addr;
    Millis used = 0;
  };
  bool refresh(NetApp& app);
  LinkFrame request(const LinkOut& o, uint32_t bound) const;
  void send(const LinkOut& o, Millis now, LinkRadio& radio);
  Peer* peerNamed(const std::string& name);
  void learn(const LinkFrame& f, const LinkAddr& from, Millis now);
  void newNonce();
  uint32_t (*random_)();
  uint32_t nonce_ = 0;
  uint32_t epoch_ = 0xFFFFFFFFu;
  uint32_t last_seq_ = 0;
  LinkConfig cfg_;
  bool have_cfg_ = false;
  LinkSeen seen_;
  Sending sending_[LINK_SENDING];
  Peer peers_[LINK_PEERS];
};

}  // namespace pt
