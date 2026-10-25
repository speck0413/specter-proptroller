#if defined(ARDUINO) && (defined(ESP32) || defined(PICO_CYW43_SUPPORTED))
#include "net.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <atomic>
#include "../net/locked.h"
#include "../link.h"
#include <deque>
#if defined(ESP32)
#include <mutex>
#endif
#include "../scanrun.h"
#include "../wifijoin.h"
#if defined(ESP32)
#include <errno.h>
#include <mdns.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <espMqttClientAsync.h>
#include <lwip/sockets.h>
#include "../net/mqtt.h"
#if defined(PROPTROLLER_QEMU_ETH)
#include "qemu_eth.h"  // QEMU only: Ethernet instead of Wi-Fi
#define PT_LINK_UP() qemuEthUp()
#endif
#else
#include <LEAmDNS.h>
#include <PubSubClient.h>
#include <pico/cyw43_arch.h>
#endif

// The show runs whatever the network does. The step never waits on the network: it is its own task on the
// ESP32 and runs on core 1 on the RP2040, and the network code takes the controller lock only around calls into
// the firmware (LockedApp), never while it waits on a socket, a DNS answer or the broker. On top of that nothing
// here waits for long, so the loop stays responsive:
// * ESP32: Wi-Fi joins in the background (WiFi.begin), MQTT is espMqttClientAsync on AsyncTCP (DNS, connect,
//   send and receive happen in AsyncTCP's task, which only queues what arrives), sockets are written with
//   MSG_DONTWAIT, ESP-NOW cues are queued by the Wi-Fi task and handled in poll().
// * Pico W / Pico 2 W: arduino-pico has no maintained asynchronous MQTT client or Wi-Fi join, so the waits are
//   bounded: a join WIFI_JOIN_MS (arduino-pico's CYW43::begin waits on past that only while a join it is in the
//   middle of finishes: the bench measures the worst case), DNS never waited on (asked again each try until lwIP has the answer),
//   a broker connect MQTT_CONNECT_MS plus MQTT_ANSWER_S, a stalled publish ends the session. They run on core 0.
//   (A sketch that takes core 1 for itself, PROPTROLLER_NO_CORE1, runs the step in loop() too: there a join or
//   a broker connect delays the step by up to these bounds.)

// The network is there (MQTT runs only then); QEMU's Ethernet replaces it.
#ifndef PT_LINK_UP
#define PT_LINK_UP() (WiFi.status() == WL_CONNECTED)
#endif

namespace pt {

static const IPAddress AP_IP(192, 168, 4, 1);
static const char CUE_MAGIC[] = "PTCUE1:";
// Not yet tried on hardware: the mDNS name check below (QEMU has no radio and its Ethernet build never reaches it).
// Wi-Fi scan (op wifi_scan): asked by the controller, run here, never in the step (ScanRun, scanrun.h). The ESP32
// scans actively for at most SCAN_MS_PER_CHANNEL on each channel and goes back to the setup hotspot's channel in
// between, so a phone on the hotspot stays joined (the controller keeps scans WIFI_SCAN_MIN_GAP_MS apart; the log
// gives each scan's time). A scan is never started while a join is under way, and no join starts while a scan runs.
static const uint32_t SCAN_MS_PER_CHANNEL = 120;
static const char NAME_TAKEN_LOG[] = "[proptroller] another device already answers to %s.local: using %s.local\n";
// mDNS name: before claiming name.local the board asks whether another device answers for it (two boards both
// still named "Proptroller"), for MDNS_CHECK_MS; if one does it takes name-<short id>.local instead (mdnsLabel).
// MDNS_VERIFY_MS after claiming it asks once more, for two boards that started at the same moment. (The Pico W's
// LEAmDNS finds a conflict while it probes and reports it; the ESP32's mDNS would rename to "name-2" without
// telling, so the board checks for itself and always knows the name it answers to.)
#if defined(ESP32)
static const uint32_t MDNS_CHECK_MS = 1500;
static const Millis MDNS_VERIFY_MS = 5000;
#endif
#if !defined(ESP32)  // the ESP32's MQTT session is net/mqtt.h (shared with the simulator)
static const Millis MQTT_BACKOFF_MIN_MS = 5000, MQTT_BACKOFF_MAX_MS = 60000;
static const size_t MQTT_BUFFER = 16384;              // the largest message in or out (an rpc reply: get_params)
static const unsigned long WIFI_JOIN_MS = 5000;       // arduino-pico's WiFi.begin waits this long at most
static const unsigned long MQTT_CONNECT_MS = 1000;    // TCP connect and each write without progress
static const uint16_t MQTT_ANSWER_S = 2;              // CONNACK and the rest of a packet
#endif

static uint32_t randomU32() {
#if defined(ESP32)
  return esp_random();
#else
  return rp2040.hwrand32();
#endif
}

// Never blocks: WiFiClient::write would (ESP32: up to 10 x 1 s select; Pico W: until 5 s without progress), so
// a phone walking out of range would stall loop() into the watchdog. Only what the socket takes now is written;
// HttpServer keeps the rest queued.
class WifiConn : public Conn {
 public:
  explicit WifiConn(WiFiClient c) : c_(c) {
    c_.setNoDelay(true);
#if !defined(ESP32)
    c_.setTimeout(100);  // ms (arduino-pico reads < 100 as seconds): bounds a write that hits lwIP's ERR_MEM
#endif
  }
  int read(uint8_t* buf, size_t n) override {
    int a = c_.available();
    if (a <= 0) return c_.connected() ? 0 : -1;
    return c_.read(buf, n < (size_t)a ? n : (size_t)a);
  }
  int write(const uint8_t* buf, size_t n) override {
    if (!c_.connected()) return -1;
#if defined(ESP32)
    int r = ::send(c_.fd(), buf, n, MSG_DONTWAIT);
    if (r >= 0) return r;
    return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1;
#else
    size_t room = c_.availableForWrite();
    if (room == 0) return 0;
    return (int)c_.write(buf, n < room ? n : room);
#endif
  }
#if defined(ESP32)
  void close() override { c_.stop(); }
#else
  void close() override { (void)c_.stop(1); }  // stop() would wait up to 300 ms for a stalled peer's ack
#endif

 private:
  WiFiClient c_;
};

// The board link's frames that came by ESP-NOW (link.h): queued by the Wi-Fi task, handled in poll(); a queue of
// their own, so they are handled whether or not an MQTT session is up. Bounded (LINK_ESPNOW_QUEUE frames).
#if defined(ESP32)
static const size_t LINK_ESPNOW_QUEUE = 8;
struct LinkInbox {
  std::mutex m;
  std::deque<std::string> q;
  void push(const uint8_t* d, size_t n) {
    std::lock_guard<std::mutex> g(m);
    if (q.size() < LINK_ESPNOW_QUEUE) q.emplace_back((const char*)d, n);
  }
  bool pop(std::string& out) {
    std::lock_guard<std::mutex> g(m);
    if (q.empty()) return false;
    out.swap(q.front());
    q.pop_front();
    return true;
  }
};
static LinkInbox g_link_inbox;
#endif
// at most this many datagrams on the discovery port handled per pass of the loop (discovery and the board link)
static const int UDP_PER_POLL = 4;

#if defined(ESP32)
// ESP-NOW cues: the Wi-Fi task queues them in the MQTT session's inbox, poll() runs them behind the lock
static Inbox* g_inbox = nullptr;
static void onEspNow(const uint8_t*, const uint8_t* data, int len) {
  if (len >= (int)sizeof LINK_MAGIC && len <= (int)LINK_FRAME_MAX && memcmp(data, LINK_MAGIC, sizeof LINK_MAGIC) == 0) {
    g_link_inbox.push(data, (size_t)len);
    return;
  }
  if (len <= (int)sizeof(CUE_MAGIC) - 1 || memcmp(data, CUE_MAGIC, sizeof(CUE_MAGIC) - 1) != 0) return;
  if (g_inbox) g_inbox->push("", std::string((const char*)data + sizeof(CUE_MAGIC) - 1, (size_t)len - (sizeof(CUE_MAGIC) - 1)));
}
#endif

#if defined(ESP32)
// arduino-esp32 2.0.17 keeps these protected: its scan time limit (scanrun.h says why it is lifted) and the
// "scanning" flag a stopped scan leaves set.
struct EspScanLimit : WiFiScanClass {
  static void set(uint32_t ms) { _scanTimeout = ms; }
};
struct EspWifiBits : WiFiGenericClass {
  static void clear(int bits) { clearStatusBits(bits); }
};

class BoardScanRadio : public ScanRadio {
 public:
  bool start() override {
    if (WiFi.scanNetworks(true, false, false, SCAN_MS_PER_CHANNEL) != WIFI_SCAN_RUNNING) return false;
    EspScanLimit::set((uint32_t)WIFI_SCAN_TIMEOUT_MS * 2);  // ScanRun's own limit is the one that counts
    return true;
  }
  bool finished() override { return WiFi.scanComplete() != WIFI_SCAN_RUNNING; }
  bool collect(std::vector<WifiNetwork>& out) override {
    int16_t n = WiFi.scanComplete();
    for (uint8_t i = 0; n > 0 && i < (uint8_t)n; i++) out.emplace_back(WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    WiFi.scanDelete();
    return n >= 0;
  }
  void abort() override {
    esp_wifi_scan_stop();
    EspWifiBits::clear(WIFI_SCANNING_BIT);  // or the next scanNetworks would say "running" without starting one
    WiFi.scanDelete();
  }
};
#else
// Pico W / Pico 2 W: cyw43 reports each access point from its own context; they are kept here with no allocation
// (one per name, the strongest) and read only after the scan has ended.
static const size_t SCAN_RAW_MAX = 32;
struct ScanSeen {
  char ssid[33];
  int16_t rssi;
  bool secured;
};
static ScanSeen g_scan[SCAN_RAW_MAX];
static volatile size_t g_scan_n = 0;
static int onScanResult(void*, const cyw43_ev_scan_result_t* r) {
  if (!r || r->ssid_len == 0 || r->ssid_len > 32) return 0;
  char name[33];
  memcpy(name, r->ssid, r->ssid_len);
  name[r->ssid_len] = 0;
  size_t n = g_scan_n, weakest = 0;
  for (size_t i = 0; i < n; i++) {
    if (strcmp(g_scan[i].ssid, name) == 0) {
      if (r->rssi > g_scan[i].rssi) g_scan[i].rssi = r->rssi;
      return 0;
    }
    if (g_scan[i].rssi < g_scan[weakest].rssi) weakest = i;
  }
  size_t at = n < SCAN_RAW_MAX ? n : weakest;
  if (n >= SCAN_RAW_MAX && r->rssi <= g_scan[weakest].rssi) return 0;
  memcpy(g_scan[at].ssid, name, sizeof name);
  g_scan[at].rssi = r->rssi;
  g_scan[at].secured = r->auth_mode != 0;
  if (n < SCAN_RAW_MAX) g_scan_n = n + 1;
  return 0;
}

// cyw43 has no time limit of its own and says itself when the scan has ended (wifi_scan_state), nor a way to
// stop a scan: one that took too long is left to end on its own, and the next start drops what it found.
class BoardScanRadio : public ScanRadio {
 public:
  bool start() override {
    g_scan_n = 0;
    cyw43_wifi_scan_options_t o;
    memset(&o, 0, sizeof o);
    return cyw43_wifi_scan(&cyw43_state, &o, nullptr, onScanResult) == 0;
  }
  bool finished() override { return !cyw43_wifi_scan_active(&cyw43_state); }
  bool collect(std::vector<WifiNetwork>& out) override {
    for (size_t i = 0; i < g_scan_n; i++) out.emplace_back(g_scan[i].ssid, g_scan[i].rssi, g_scan[i].secured);
    return true;
  }
  void abort() override { g_scan_n = 0; }
};
#endif

// lwIP keeps an IPv4 address in network order: the four bytes as they print
static uint32_t ipU32(const IPAddress& a) { return (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24; }
static IPAddress ipOf(uint32_t v) { return IPAddress((uint8_t)(v & 0xFF), (uint8_t)((v >> 8) & 0xFF), (uint8_t)((v >> 16) & 0xFF), (uint8_t)(v >> 24)); }

struct ArduinoNet::Impl {
  NetApp& app;
  Lock& lock;
  NetConfig cfg;
  WiFiServer server{80};
  LockedApp<Lock, Guard> locked;
  HttpServer http;
  WiFiUDP udp;
  WifiJoin joiner;                    // when to join, and which tries failed (wifijoin.h)
  std::atomic<bool> stopping{false};  // the board is restarting: its Wi-Fi going down is no failed join
  bool ap_on = false, sta_wanted = false, mdns_on = false, espnow_on = false;
  BoardScanRadio radio;
  ScanRun scan;
  // the board link peer to peer (link.h): UDP on the discovery port on each network the board is on, and ESP-NOW
  struct Radio : LinkRadio {
    Impl& i;
    explicit Radio(Impl& impl) : i(impl) {}
    void udpTo(const std::string& f, const IPAddress& ip, uint16_t port) {
      if (!i.udp.beginPacket(ip, port)) return;
      i.udp.write((const uint8_t*)f.data(), f.size());
      i.udp.endPacket();
    }
    void broadcast(const std::string& f) override {
      // a subnet broadcast on the Wi-Fi network and on the board's own hotspot (another board may be joined to it)
      IPAddress sta;
      bool joined = i.sta_wanted && WiFi.status() == WL_CONNECTED;
      if (joined) {
        IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
        sta = IPAddress(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1], ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);
        udpTo(f, sta, DISCOVERY_PORT);
      }
      IPAddress ap(AP_IP[0], AP_IP[1], AP_IP[2], 255);
      if (i.ap_on && !(joined && sta == ap)) udpTo(f, ap, DISCOVERY_PORT);
#if defined(ESP32)
      static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      if (i.espnow_on && f.size() <= LINK_FRAME_MAX) esp_now_send(BCAST, (const uint8_t*)f.data(), f.size());
#endif
    }
    void reply(const std::string& f, const LinkAddr& to) override {
#if defined(ESP32)
      static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      if (to.espnow) {
        if (i.espnow_on) esp_now_send(BCAST, (const uint8_t*)f.data(), f.size());
        return;
      }
#endif
      udpTo(f, ipOf(to.ip), to.port);
    }
  };
  LinkPeer link{randomU32};
  Radio radio_link{*this};
  Millis joined_at = 0;
  IPAddress last_ip;
  std::string mdns_label;  // the mDNS label the board answers to, once it has one
#if defined(ESP32)
  enum class MdnsStep : uint8_t { Off, Checking, On, Verifying } mdns_step = MdnsStep::Off;
  mdns_search_once_t* mdns_search = nullptr;
  Millis mdns_verify_at = 0;
#endif
#if defined(ESP32)
  MqttSession<espMqttClientAsync> mqtt{locked, randomU32};
#else
  // MQTT: the broker's address and session strings live here (the client keeps pointers to them)
  std::string m_host, m_id, m_will_topic, m_will_payload, boot_id;
  uint16_t m_port = 0;
  uint32_t m_epoch = 0, m_seq = 0;
  Millis m_next_try = 0, m_next_poll = 0, m_backoff = MQTT_BACKOFF_MIN_MS;
  bool m_session = false;
  WiFiClient mqtt_sock;
  PubSubClient mqtt{mqtt_sock};
  IPAddress m_ip;
#endif
#if defined(ESP32)
  Impl(NetApp& a, Lock& l) : app(a), lock(l), locked(a, l), http(locked) {}
#else
  Impl(NetApp& a, Lock& l) : app(a), lock(l), locked(a, l), http(locked, HttpLimits::picoW()) {
    char b[9];
    (void)snprintf(b, sizeof b, "%08lx", (unsigned long)randomU32());
    boot_id = b;
  }
#endif

  // The board's own Wi-Fi (the hotspot) is always up, also while it is on a Wi-Fi network (AP and station at the
  // same time); the page and the API answer on both (server and discovery listen on every interface).
  // ESP32 and CYW43: the hotspot uses the station's channel, so when the station joins a network on another
  // channel the hotspot moves with it and a phone on the hotspot reconnects (the show step does not notice: it
  // runs in its own task / on core 1 and never waits on the network). The first join is right after power-on,
  // before a phone has joined the hotspot; later only a router that comes back on another channel moves it.
  void startAp() {
    // arduino-pico: in station mode softAPConfig would set the station's own address, and softAP without
    // WIFI_AP_STA tears the station down
    WiFi.mode(sta_wanted ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    bool secured = cfg.ap_pass.size() >= 8;  // WPA2; a shorter password cannot be used, the network is then open
    bool ok = secured ? WiFi.softAP(cfg.ap_ssid.c_str(), cfg.ap_pass.c_str()) : WiFi.softAP(cfg.ap_ssid.c_str());
    if (ok) {
      ap_on = true;
      // the password itself is never written to the log
      Serial.printf("[proptroller] the board's own Wi-Fi \"%s\" is up (%s) at http://192.168.4.1/\n", cfg.ap_ssid.c_str(),
                    secured ? "WPA2, password-protected" : "no password");
      locked.netChanged(apState());
      locked.wifiScanReady(true);  // the network list is ready before a phone gets to the setup page
    } else {
      Serial.println("[proptroller] could not open the board's own Wi-Fi");
    }
  }

  // Not on a Wi-Fi network (none set, or not joined yet): only the hotspot.
  NetState apState() {
    NetState n;
    n.mode = ap_on ? "ap" : "down";
    if (ap_on) {
      n.ip = "192.168.4.1";
      n.ssid = cfg.ap_ssid;
    }
    if (!mdns_label.empty()) n.host = mdns_label + ".local";
    return n;
  }
  NetState netNow() { return WiFi.status() == WL_CONNECTED && sta_wanted ? staState() : apState(); }

  // The board's state on the Wi-Fi network, with the mDNS name it answers to (once it has one) and the hotspot.
  NetState staState() {
    NetState n;
    n.mode = "sta";
    n.ip = WiFi.localIP().toString().c_str();
    n.ssid = cfg.sta_ssid;
    n.has_rssi = true;
    n.rssi = WiFi.RSSI();
    if (!mdns_label.empty()) n.host = mdns_label + ".local";
    if (ap_on) n.hotspot = cfg.ap_ssid;
    return n;
  }

  // The board answers to label.local from now on: said in the log and on the page (state.net.host).
  // One mDNS name on every interface: neither the ESP32's mDNS (ESP-IDF 4.4: one hostname; a delegated hostname is
  // answered on every interface too) nor the Pico W's LEAmDNS (one hostname on every netif) can give the hotspot a
  // name of its own, so label.local answers on both, each side with its own address (192.168.4.1 on the hotspot).
  void mdnsNamed(const std::string& label) {
    mdns_label = label;
    NetState n = netNow();
    Serial.printf("[proptroller] answers to http://%s.local/ (or http://%s/)\n", label.c_str(), n.ip.c_str());
    locked.netChanged(n);
  }

#if defined(ESP32)
  // Does a device other than this board answer for the name just asked about?
  bool answeredByOther(mdns_result_t* res) {
    uint32_t mine = (uint32_t)WiFi.localIP(), ap = (uint32_t)AP_IP;
    for (mdns_result_t* r = res; r; r = r->next)
      for (mdns_ip_addr_t* a = r->addr; a; a = a->next)
        if (a->addr.type == ESP_IPADDR_TYPE_V4 && a->addr.u_addr.ip4.addr != mine && a->addr.u_addr.ip4.addr != ap) return true;
    return false;
  }
  // The answer to the name check, once it is in: true = done (taken says whether someone else has the name).
  bool mdnsAnswer(bool& taken) {
    mdns_result_t* res = nullptr;
    if (!mdns_query_async_get_results(mdns_search, 0, &res)) return false;
    taken = answeredByOther(res);
    if (res) mdns_query_results_free(res);
    mdns_query_async_delete(mdns_search);
    mdns_search = nullptr;
    return true;
  }
  void mdnsClaim(const std::string& label) {
    mdns_hostname_set(label.c_str());
    mdns_instance_name_set(label.c_str());
    if (mdns_label.empty()) {
      mdns_service_add(nullptr, "_proptroller", "_tcp", 80, nullptr, 0);
      for (auto& kv : locked.txt()) mdns_service_txt_item_set("_proptroller", "_tcp", kv.first.c_str(), kv.second.c_str());
    }
    mdnsNamed(label);
  }
#endif

  void startMdns(IPAddress ip) {
    (void)ip;
    if (mdns_on) return;
#if defined(ESP32)
    if (mdns_init() != ESP_OK) return;
    mdns_on = true;
    mdns_search = mdns_query_async_new(cfg.hostname.c_str(), nullptr, nullptr, MDNS_TYPE_A, MDNS_CHECK_MS, 4, nullptr);
    if (mdns_search) mdns_step = MdnsStep::Checking;
    else {
      mdnsClaim(cfg.hostname);
      mdns_step = MdnsStep::On;
    }
#else
    if (!MDNS.begin(cfg.hostname.c_str())) return;
    // LEAmDNS probes the name first; when another device answers for it, it is name-<short id> instead
    MDNS.setHostProbeResultCallback([this](const char* name, bool ok) {
      if (ok) {
        mdnsNamed(name);
      } else if (cfg.hostname_taken != name) {
        Serial.printf(NAME_TAKEN_LOG, name, cfg.hostname_taken.c_str());
        MDNS.setHostname(cfg.hostname_taken.c_str());
      }
    });
    auto h = MDNS.addService(nullptr, "proptroller", "tcp", 80);
    static std::vector<std::pair<std::string, std::string>> keep;  // LEAmDNS keeps the pointers
    keep = locked.txt();
    for (auto& kv : keep) MDNS.addServiceTxt(h, kv.first.c_str(), kv.second.c_str());
    mdns_on = true;
#endif
  }

#if defined(ESP32)
  void mdnsPoll(Millis now) {
    bool taken = false;
    switch (mdns_step) {
      case MdnsStep::Checking:
        if (!mdnsAnswer(taken)) return;
        if (taken) Serial.printf(NAME_TAKEN_LOG, cfg.hostname.c_str(), cfg.hostname_taken.c_str());
        mdnsClaim(taken ? cfg.hostname_taken : cfg.hostname);
        mdns_step = MdnsStep::On;
        mdns_verify_at = taken ? 0 : now + MDNS_VERIFY_MS;
        return;
      case MdnsStep::On:
        if (mdns_verify_at == 0 || now < mdns_verify_at) return;
        mdns_verify_at = 0;
        mdns_search = mdns_query_async_new(cfg.hostname.c_str(), nullptr, nullptr, MDNS_TYPE_A, MDNS_CHECK_MS, 4, nullptr);
        if (mdns_search) mdns_step = MdnsStep::Verifying;
        return;
      case MdnsStep::Verifying:
        if (!mdnsAnswer(taken)) return;
        mdns_step = MdnsStep::On;
        if (!taken) return;
        Serial.printf(NAME_TAKEN_LOG, cfg.hostname.c_str(), cfg.hostname_taken.c_str());
        mdnsClaim(cfg.hostname_taken);
        return;
      case MdnsStep::Off: return;
    }
  }
#endif

  // ---------------------------------------------------------------- Wi-Fi scan

  void scanPoll(Millis now) {
    // never while a join is under way (the ESP32 refuses, the Pico W's join holds this loop anyway)
    bool join_busy = scanJoinBusy(now, sta_wanted, WiFi.status() == WL_CONNECTED, joined_at);
    // the log gives counts and times only, never the networks' names
    switch (scan.poll(now, join_busy, radio, locked)) {
      case ScanRun::End::None: return;
      case ScanRun::End::Found:
        Serial.printf("[proptroller] Wi-Fi scan: %u access points in %lu ms\n", (unsigned)scan.found(), (unsigned long)scan.tookMs());
        return;
      case ScanRun::End::Failed: Serial.printf("[proptroller] Wi-Fi scan failed after %lu ms\n", (unsigned long)scan.tookMs()); return;
      case ScanRun::End::Refused: Serial.println("[proptroller] Wi-Fi scan: the radio could not start one"); return;
      case ScanRun::End::TimedOut: Serial.printf("[proptroller] Wi-Fi scan: no answer in %lu ms, stopped\n", (unsigned long)scan.tookMs()); return;
    }
  }

  void begin() {
    cfg = locked.netConfig();
    sta_wanted = !cfg.sta_ssid.empty();
#if defined(PROPTROLLER_QEMU_ETH)
    sta_wanted = false;
    if (qemuEthBegin(10000)) {
      Serial.printf("[proptroller] QEMU Ethernet at http://%s/\n", qemuEthIp());
      NetState n;
      n.mode = "sta";
      n.ip = qemuEthIp();
      n.ssid = "qemu-eth";
      locked.netChanged(n);
    } else {
      Serial.println("[proptroller] QEMU Ethernet did not come up");
    }
#else
    WiFi.persistent(false);
    locked.wifiScanReady(false);  // op wifi_scan (the setup hotspot asks for a first list when it opens)
    WiFi.mode(sta_wanted ? WIFI_AP_STA : WIFI_AP);
    if (sta_wanted) {
      WiFi.setHostname(cfg.hostname.c_str());
#if defined(ESP32)
      WiFi.setAutoReconnect(false);  // the core's own reconnect rescans at once, over and over: poll() retries
#else
      WiFi.setTimeout(WIFI_JOIN_MS);
#endif
    }
    startAp();  // always, with or without a Wi-Fi network (the first join follows in poll())
    if (!sta_wanted && ap_on) startMdns(AP_IP);  // with a network, mDNS starts on its first join
#endif
    server.begin();
    udp.begin(DISCOVERY_PORT);
#if defined(ESP32)
#if defined(PROPTROLLER_QEMU_ETH)
    if (false) {  // ESP-NOW needs the Wi-Fi driver, which QEMU does not have
#else
    if (esp_now_init() == ESP_OK) {
#endif
      esp_now_peer_info_t peer = {};
      memset(peer.peer_addr, 0xFF, 6);
      peer.channel = 0;
      peer.ifidx = sta_wanted ? WIFI_IF_STA : WIFI_IF_AP;
      peer.encrypt = false;
      esp_now_add_peer(&peer);
      esp_now_register_recv_cb(onEspNow);
      espnow_on = true;
    }
    g_inbox = &mqtt.inbox;
    mqtt.client.onMessage([this](const espMqttClientTypes::MessageProperties&, const char* t, const uint8_t* p, size_t n, size_t i, size_t total) {
      mqtt.piece(t, p, n, i, total);
    });
#else
    g_impl = this;
    mqtt.setCallback(onMqtt);  // runs inside mqtt.loop(), in poll()
#endif
  }

#if !defined(ESP32)
  static Impl* g_impl;
  static void onMqtt(char* topic, uint8_t* payload, unsigned int len) {
    for (auto& m : g_impl->locked.mqttOnMessage(topic, std::string((const char*)payload, len)))
      if (!g_impl->publish(m)) break;
  }
#endif

  void join() {
#if defined(ESP32)
    WiFi.disconnect();
    WiFi.begin(cfg.sta_ssid.c_str(), cfg.sta_pass.c_str());  // returns at once; joins in the background
#else
    // arduino-pico: begin() outside WIFI_AP_STA (after disconnect(), or with only the AP up) opens an access
    // point under the network's name instead of joining it; it waits up to WIFI_JOIN_MS (longer only while a join it
    // is in the middle of finishes)
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(cfg.sta_ssid.c_str(), cfg.sta_pass.c_str());
#endif
  }

  void wifi(Millis now) {
    if (!sta_wanted) return;
    bool up = WiFi.status() == WL_CONNECTED;
    WifiJoin::Step step = joiner.poll(now, up, ap_on, stopping.load(), scan.running());
    if (up) {
      IPAddress ip = WiFi.localIP();
      if (ip != last_ip) {
        last_ip = ip;
        Serial.printf("[proptroller] joined \"%s\" at http://%s/\n", cfg.sta_ssid.c_str(), ip.toString().c_str());
        locked.netChanged(staState());
        startMdns(ip);
      }
      return;
    }
    if (last_ip != IPAddress()) {
      last_ip = IPAddress();
      locked.netChanged(apState());  // off the network: the page shows the hotspot only
    }
    switch (step) {
      case WifiJoin::Step::Wait: return;
      case WifiJoin::Step::Join: break;
      case WifiJoin::Step::Rejoin:
        Serial.printf("[proptroller] the connection to \"%s\" dropped: joining it again\n", cfg.sta_ssid.c_str());
        break;
      case WifiJoin::Step::Retry:
        Serial.printf("[proptroller] cannot join \"%s\" (attempt %d): check the name and password in Settings -> Wi-Fi\n", cfg.sta_ssid.c_str(), joiner.failures());
        break;
    }
    join();
    joined_at = now;  // the loop's clock (millis64() here is later than the now scanPoll is given)
  }

  // The discovery port: discovery requests, and the board link's frames (their own magic)
  void discovery(Millis now) {
    for (int pass = 0; pass < UDP_PER_POLL; pass++) {
      int n = udp.parsePacket();
      if (n <= 0) break;
      uint8_t buf[256];
      int k = udp.read(buf, sizeof buf);
      if (k <= 0) continue;
      if (isDiscover((const char*)buf, (size_t)k)) {
        std::string a = locked.announceJson(HTTP_PORT);
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write((const uint8_t*)a.data(), a.size());
        udp.endPacket();
        continue;
      }
      LinkAddr from;
      from.ip = ipU32(udp.remoteIP());
      from.port = udp.remotePort();
      link.received(buf, (size_t)k, from, now, locked, radio_link);
    }
#if defined(ESP32)
    std::string f;
    while (g_link_inbox.pop(f)) {  // by ESP-NOW
      LinkAddr from;
      from.espnow = true;
      link.received((const uint8_t*)f.data(), f.size(), from, now, locked, radio_link);
    }
#endif
    link.poll(now, locked, radio_link);
  }

  // ---------------------------------------------------------------- MQTT

#if defined(ESP32)
  bool publish(const MqttMsg& m, bool stamp = true) { return mqtt.publish(m, stamp); }
  void mqttPoll(Millis now) { mqtt.poll(now, PT_LINK_UP()); }
#else
  // End the session without a clean disconnect, so the broker publishes the will (online = false).
  void mqttDrop() {
    (void)mqtt_sock.stop(1);
    m_session = false;
  }

  // Publish on the session, if any; a publish the session cannot take ends it (the state is latest-wins and
  // retained, so the next session republishes it).
  bool publish(const MqttMsg& m, bool stamp = true) {
    if (!mqtt.connected()) return false;
    std::string p = stamp ? mqttStamp(m.topic, m.payload, boot_id, m_seq) : m.payload;
    if (mqtt.publish(m.topic.c_str(), (const uint8_t*)p.data(), p.size(), m.retain)) return true;
    Serial.println("[proptroller] MQTT: the broker is not taking messages; reconnecting");
    mqttDrop();
    return false;
  }

  bool mqttConnect() {
    MqttMsg will = locked.mqttWill();
    m_id = locked.mqttClientId();
    m_epoch = locked.mqttEpoch();
    m_will_topic = will.topic;
    m_will_payload = will.payload;
    // DNS without waiting: lwIP answers from its cache once the lookup this starts has finished
    if (!WiFi.hostByName(m_host.c_str(), m_ip, 1) || m_ip == IPAddress(0, 0, 0, 0) || m_ip == IPAddress(255, 255, 255, 255)) return false;
    mqtt.setBufferSize(MQTT_BUFFER);
    mqtt.setServer(m_ip, m_port);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(MQTT_ANSWER_S);
    mqtt_sock.setTimeout(MQTT_CONNECT_MS);
    return mqtt.connect(m_id.c_str(), nullptr, nullptr, m_will_topic.c_str(), 0, will.retain, m_will_payload.c_str(), true);
  }

  void mqttPoll(Millis now) {
    std::string host;
    uint16_t port = 0;
    bool want = locked.mqttTarget(host, port);
    if (!want || !PT_LINK_UP()) {
      if (mqtt.connected()) mqttDrop();
      if (!want && mqtt.getBufferSize() != 256) mqtt.setBufferSize(256);  // the 16 KB buffer only while a broker is set
      return;
    }
    if (mqtt.connected() && locked.mqttEpoch() != m_epoch) mqttDrop();  // renamed or moved: the will is sent
    if (!mqtt.connected()) {
      m_session = false;
      if (now < m_next_try) return;
      // 5 s, 10 s, ... 60 s apart, with jitter (a fleet does not reconnect in step after a broker restart)
      m_next_try = now + m_backoff + randomU32() % (m_backoff / 4 + 1);
      m_backoff = m_backoff * 2 > MQTT_BACKOFF_MAX_MS ? MQTT_BACKOFF_MAX_MS : m_backoff * 2;
      m_host = host;
      m_port = port;
      if (!mqttConnect()) return;
    }
    if (!m_session) {
      m_session = true;
      m_backoff = MQTT_BACKOFF_MIN_MS;
      for (auto& s : locked.mqttSubscriptions()) mqtt.subscribe(s.c_str(), 0);
      for (auto& m : locked.mqttOnConnect())
        if (!publish(m)) return;
    }
    mqtt.loop();
    if (now >= m_next_poll) {
      m_next_poll = now + 100;
      for (auto& m : locked.mqttPoll())
        if (!publish(m)) break;
    }
  }
#endif

  void cues() {
#if defined(ESP32)
    if (!espnow_on) return;
    // cues that arrived (mqttPoll handles the inbox while a session is up; without one, MQTT messages are
    // dropped)
    MqttMsg in;
    while (!mqtt.connected() && mqtt.inbox.pop(in))
      if (in.topic.empty()) locked.cueIn(in.payload);
    static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (auto& c : locked.takeCuesOut()) {
      std::string f = std::string(CUE_MAGIC) + c;
      esp_now_send(BCAST, (const uint8_t*)f.data(), f.size());
    }
#endif
  }
};

#if !defined(ESP32)
ArduinoNet::Impl* ArduinoNet::Impl::g_impl = nullptr;
#endif

ArduinoNet::ArduinoNet(NetApp& app, Lock& lock) : p_(new Impl(app, lock)) {}
ArduinoNet::~ArduinoNet() { delete p_; }

void ArduinoNet::begin() { p_->begin(); }
void ArduinoNet::stop() { p_->stopping = true; }

void ArduinoNet::poll() {
  Millis now = millis64();
  p_->wifi(now);
  if (p_->http.canAdd()) {  // when full, a new connection waits in the stack's backlog
#if defined(ESP32)
    WiFiClient c = p_->server.available();
#else
    WiFiClient c = p_->server.accept();
#endif
    if (c) p_->http.add(new WifiConn(c), now);
  }
  p_->http.poll(now);
  p_->discovery(now);
  p_->mqttPoll(now);
  p_->cues();
  p_->scanPoll(now);
#if defined(ESP32)
  p_->mdnsPoll(now);
#else
  if (p_->mdns_on) MDNS.update();
#endif
}

bool ArduinoNet::mqttConnected() const { return p_->mqtt.connected(); }

bool ArduinoNet::publish(const std::string& topic, const std::string& payload, bool retain) {
  return p_->publish(MqttMsg(topic, payload, retain), false);  // a sketch's own topics: as given
}

}  // namespace pt
#endif
