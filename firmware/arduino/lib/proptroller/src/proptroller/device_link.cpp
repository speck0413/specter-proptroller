// The board link (SHOW-SCRIPT.md §11, PROTOCOL.md "Board link"): a show script's `tell` lines start effects and
// change settings on other boards, over MQTT or peer to peer (link.h), and other boards' tells come in here.
// Everything here runs in the network loop or a request, holding the lock briefly; the step only hands over the
// tell lines that ran (ScriptRunner::takeTell) and raises link_work_.
#include "device.h"
#include <ArduinoJson.h>
#include <algorithm>

namespace pt {

Controller::LinkVia Controller::linkVia() const {
  switch (settings_.board_link) {
    case BoardLink::Off: return LinkVia::Off;
    case BoardLink::Mqtt: return LinkVia::Mqtt;
    case BoardLink::Wifi: return LinkVia::Wifi;
    case BoardLink::Auto:
    default: return settings_.mqtt_broker.empty() ? LinkVia::Wifi : LinkVia::Mqtt;
  }
}

bool Controller::linkConfig(LinkConfig& c) {
  // peer to peer: sends when that is the way out; takes tells unless the board is set to MQTT only or off
  c.send = linkVia() == LinkVia::Wifi;
  c.listen = settings_.board_link == BoardLink::Auto || settings_.board_link == BoardLink::Wifi;
  c.group = settings_.link_group;
  c.key = settings_.link_key;
  c.id = identity_.id.substr(0, 16);
  c.name = hostLabel(settings_.name, identity_.id);
  return true;
}

bool Controller::storedValue(const std::string& key, Value& out) const {
  int k = linkKeptAt(key);
  if (k >= 0) { out = link_kept_[k].second; return true; }
  return settings_.get(key, out);
}

// ---------------------------------------------------------------- shared with MQTT

bool Controller::remoteControl(const std::string& name, bool from_board, std::string& why) {
  // an alias works as its control; errors name the control
  std::string c = resolveControl(name);
  // locked: read-only, but for a trigger, which has its own gate (remote_trigger_enable); a script effect that
  // needs remote_trigger_enable has that gate too; one started without it does not
  bool gated = c == "trigger" || (c.compare(0, 7, "script:") == 0 && !((scriptFreeEffects() >> atoi(c.c_str() + 7)) & 1));
  if (locked() && !gated && !c.empty()) { why = "the page is locked: " + name + " on the web page with the page password"; return false; }
  if (c.empty()) { why = "unknown control"; return false; }
  if (from_board && c == "restart") { why = "restart: not taken from another board"; return false; }
  Request req;
  Response r = effectAction(req, c);
  if (!r.isOk() && c != "restart") {
    why = (c.compare(0, 7, "script:") == 0 ? name : c) + ": " + r.errorMessage();
    return false;
  }
  return true;
}

bool Controller::remoteSetting(const std::string& name, const std::string& value, bool save, std::string& why) {
  if (locked()) { why = "the page is locked: use rpc with the page password"; return false; }
  // a key alias works as its key (SM-AL1); a fixed name takes any value and changes nothing
  const AliasEntry* a = aliasNamed(name);
  if (a && a->kind == AliasEntry::Kind::Fixed) return true;
  if (a && a->kind != AliasEntry::Kind::Key) a = nullptr;
  std::string key = a ? a->target : name;
  const ParamDesc* d = Settings::find(key);
  if (!d) { why = "unknown setting"; return false; }
  std::string text = value;
  if (!save) {
    // as a script writes it: on / off for true / false, 50% for a percentage
    if (d->kind == ParamDesc::Kind::Bool && (text == "on" || text == "off")) text = text == "on" ? "true" : "false";
    if (d->kind == ParamDesc::Kind::Int && d->unit == "%" && !text.empty() && text.back() == '%') text.pop_back();
  }
  Value v;
  std::string e;
  if (!d->parsePlain(text, v, e)) { why = name + ": " + e; return false; }
  if (a) {
    if (a->invert && v.type == Value::Type::Bool) v.b = !v.b;
    if (a->scale && v.type == Value::Type::Int) {
      int64_t lo = aliasMin(d->min, a->scale), hi = aliasMax(d->max, a->scale);
      if (v.i < lo || v.i > hi) {
        why = name + ": allows " + std::to_string((long long)lo) + "-" + std::to_string((long long)hi) + (a->unit.empty() ? "" : " " + a->unit);
        return false;
      }
      v.i *= a->scale;
    }
  }
  Request req;
  Response resp = setParams(req, withAlso(a, key, v), save);
  if (!resp.isOk()) { why = name + ": " + resp.errorMessage(); return false; }
  return true;
}

// ---------------------------------------------------------------- this board's tells

void Controller::mqttLinkOut(const LinkOut& o) {
  // to <root>/<board>/control/<effect> or config/now/<setting>, never retained (a reconnect must not run it
  // again); tell all: to every board that answered broadcast/getName
  std::string root = topicRoot();
  JsonDocument d;
  d["from"] = hostLabel(settings_.name, identity_.id);
  d["id"] = identity_.id;
  d["seq"] = o.seq;
  d["group"] = settings_.link_group;
  if (o.set) d["value"] = o.value;
  std::string body;
  serializeJson(d, body);
  std::vector<std::string> to;
  if (o.to == "all") to = mqtt_names_;
  else to.push_back(o.to);
  for (auto& b : to) link_mqtt_out_.push_back(MqttMsg(root + "/" + b + (o.set ? "/config/now/" : "/control/") + o.what, body, false));
}

std::vector<LinkOut> Controller::linkTake() {
  std::vector<LinkOut> out;
  Millis now = now_;
  LinkVia via = linkVia();
  if (script_run_ && script_prog_) {
    uint16_t s = 0;
    while (script_run_->takeTell(s)) {
      ScriptTell t;
      if (s >= script_prog_->steps || !scriptTellOf(*script_prog_, script_prog_->step[s], t)) continue;
      std::string line = "tell " + t.board + " " + (t.set ? t.what + " = " + t.value : t.what);
      if (via == LinkVia::Off) { log(line + ": not sent (board_link is off)"); continue; }
      if (now - link_window_at_ >= 1000) { link_window_at_ = now; link_window_n_ = 0; }
      if (link_window_n_ >= LINK_MAX_PER_S) { link_dropped_++; continue; }
      link_window_n_++;
      LinkOut o;
      o.seq = ++link_seq_;
      o.set = t.set;
      o.to = t.board;
      o.what = t.what;
      o.value = t.value;
      if (link_pending_.size() >= LINK_PENDING_MAX) link_pending_.erase(link_pending_.begin());
      LinkPending p;
      p.seq = o.seq;
      p.all = t.board == "all";
      p.to = t.board;
      p.line = line;
      p.until = now + LINK_ANSWER_MS;
      link_pending_.push_back(p);
      if (via == LinkVia::Mqtt) {
        mqttLinkOut(o);
        log(line + ": sent (MQTT)");
      } else {
        out.push_back(o);
        log(line + ": sent (Wi-Fi)");
      }
    }
    uint32_t dropped = script_run_->tellsDropped() + link_dropped_;
    link_dropped_ = 0;
    if (dropped) log("tell: " + std::to_string(dropped) + " not sent (more than " + std::to_string(LINK_MAX_PER_S) + " a second, or " +
                     std::to_string(SCRIPT_TELL_RING) + " at once)");
  }
  // the answers that did not come
  while (!link_pending_.empty() && now >= link_pending_.front().until) {
    const LinkPending& p = link_pending_.front();
    if (!p.all) log(p.line + ": no answer from " + p.to);
    else if (p.answered.empty()) log(p.line + ": no board answered");
    else log(p.line + ": " + std::to_string(p.answered.size()) + (p.answered.size() == 1 ? " board answered" : " boards answered"));
    link_pending_.erase(link_pending_.begin());
  }
  link_work_.store(!link_pending_.empty() || (script_run_ && script_run_->tellsWaiting()), std::memory_order_relaxed);
  return out;
}

void Controller::linkAnswer(uint32_t seq, const std::string& from, bool ok, const std::string& text) {
  for (auto it = link_pending_.begin(); it != link_pending_.end(); ++it) {
    if (it->seq != seq) continue;
    if (std::find(it->answered.begin(), it->answered.end(), from) != it->answered.end()) return;  // said already
    if (it->answered.size() < LINK_NAMES_MAX) it->answered.push_back(from);
    if (!ok) log(it->line + ": " + from + " refused: " + text);
    if (!it->all) link_pending_.erase(it);
    return;
  }
}

// ---------------------------------------------------------------- other boards' tells

LinkReply Controller::linkIn(const LinkIn& in) {
  LinkReply r;
  std::string who = (in.from.empty() ? in.id : in.from) + " (board link, " + in.via + ")";
  if (updating()) {
    r.text = "the board is being updated";
    return r;
  }
  if (in.set) {
    r.ok = remoteSetting(in.what, in.value, false, r.text);
    if (r.ok) {
      r.text = "ok";
      log(who + " set " + in.what + " = " + in.value + " (until a restart or a change here; not saved)");
    } else {
      log(who + ": " + r.text);
    }
    return r;
  }
  remote_by_ = in.from.empty() ? in.id : in.from;
  r.ok = remoteControl(in.what, true, r.text);
  remote_by_.clear();
  if (r.ok) r.text = "ok";
  else log(who + ": " + r.text);
  return r;
}

}  // namespace pt
