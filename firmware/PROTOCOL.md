# Proptroller control protocol, version 1

Every Proptroller firmware, the simulator, the phone web UI and the desktop app speak this protocol.
This page is the wire description. The implementation is the C++ firmware (`firmware/arduino`: `proto.h`,
`device*.cpp`, `net/`); its host tests check every message byte for byte against the golden files in
`firmware/arduino/test/golden` (`tools/golden.sh` writes them).

## Transport

| | |
|---|---|
| HTTP port | 80 on the device (the simulator defaults to 8080) |
| `GET /` and `/<file>` | the phone web UI (with `X-Frame-Options: DENY` and `Content-Security-Policy: frame-ancestors 'none'`: no other site may show it in a frame; `Cache-Control: no-cache` with an `ETag`, so a browser asks again with `If-None-Match` and gets `304 Not Modified` while the files are the same; `HEAD` gives the real `Content-Length`) |
| `GET /api/v1/info` | [Info](#info) as plain JSON |
| `GET /api/v1/state` | [State](#state) as plain JSON |
| `POST /api/v1/rpc` | one request envelope in, one response out. `Content-Type: application/json` is required (so a web page on another site cannot send it as a simple form post) |
| `POST /api/v1/update` | a new firmware image ([Firmware updates](#firmware-updates)) |
| `GET /api/v1/ws` | WebSocket: request envelopes in, responses and [events](#events) out |
| CORS | allowed only for the desktop app's web view (`tauri://localhost`, `http(s)://tauri.localhost`) |
| Host | an address, `<name>.local` or `<name>` (any port); any other name is refused with 403 (a page using DNS rebinding) |
| Origin | a browser page may open the WebSocket or send anything but `GET` / `HEAD` only from the board's own page (`Origin: http://<Host>`) or the desktop app; another site's page gets 403. No `Origin` (not a browser page): allowed |
| Limits | bodies up to 42,952 B (the largest backup), one body over 4 KB at a time (a second gets 503: try again); WebSocket sessions: 4 (Pico W: 2), one more gets 503; connections: 8 (Pico W: 3), a new one waits until an idle keep-alive connection can be closed; a client that stops reading is closed (10 s without progress, or 16 KB / Pico W 8 KB waiting); an upload that stops sending for 10 s is aborted |

All bodies are UTF-8 JSON. Every message carries `"v": 1`. A request with a higher `v` is answered with
error `version`.

## Discovery

**mDNS / DNS-SD**: service `_proptroller._tcp.local`, port = the HTTP port, host `<name>.local`, TXT below. `<name>`
is the board's name made into an address (`hostLabel`: lower case letters and digits, every other run of
characters one `-`, at most 40 characters; no letters or digits at all: `proptroller-<short id>`): the default name
`Proptroller` is `proptroller.local`, `Front Door` is `front-door.local`. Renaming the board changes it after the
restart. Before it claims the name, a board that joined a network asks whether another device already answers
for it; if one does (two boards both still called `Proptroller`), it takes `<name>-<short id>.local` instead, where
the short id is the last four hex digits of its `id`, as in its setup hotspot `Proptroller-XXXX`
(`proptroller-ac2c.local`). The ESP32 asks again 5 s after claiming (two boards that joined at the same moment);
the Pico W's mDNS finds the conflict while it probes (the name choice is host-tested; the conflict handling on the
boards is not yet tried on hardware). The name the board really answers to is in `state.net.host`
and in the boot log (`[proptroller] answers to http://proptroller.local/ (or http://192.168.1.50/)`).
TXT:

| Key | Value |
|---|---|
| `v` | protocol version |
| `id` | unique id (MAC address hex on the ESP32) |
| `name` | user name |
| `fw` | firmware (`generic`, or a dedicated firmware's name) |
| `fwv` | firmware version |
| `fwi` | firmware implementation: `cpp` |
| `mcu` | `esp32-devkit`, `pico`, `sim` |
| `board` | the board package the board runs ([BOARD-PACKAGES.md](BOARD-PACKAGES.md)): the stored package's `id`, else the image's own board (`proptroller`, the Proptroller board's own pin map, or the `PROPTROLLER_BOARD_ID` of a board support package compiled in); `none` when the stored package cannot be used and no pins are driven |

A board that does not report `board` (older firmware) counts as `proptroller`.

**UDP broadcast fallback** (networks that block multicast, the setup access point): send
`{"proptroller":"discover","v":1}` to UDP port **47800** (broadcast or unicast). Each device answers to the
sender with

```json
{"proptroller":"announce","v":1,"id":"a0b1c2d3e4f5","name":"Front door","fw":"generic","fw_version":"0.1.0","fw_impl":"cpp","mcu":"esp32-devkit","board":"proptroller","port":80}
```

The HTTP address is the answer's source IP with `port`.

**Setup hotspot (the board's own Wi-Fi)**: every Wi-Fi board always makes its own network `Proptroller-XXXX`
(WPA2, password setting `ap_pass`, default `password`) and serves everything at `http://192.168.4.1/`, also while
it is joined to a Wi-Fi network (access point and station at the same time). The page, the API, the WebSocket and
UDP discovery answer on both: `192.168.4.1` on the hotspot, the address the router gives on the Wi-Fi network.
The default password is deliberately simple (anyone who knows it and is in range can join and use the page);
change it with `ap_pass` (Settings → Wi-Fi → Setup hotspot password, 8-63 characters, WPA2 needs at least 8) or
turn on the page lock. A factory reset brings back `password`.

The hotspot uses the radio's one channel: when the board joins a network on another channel (ESP32 and CYW43 alike),
the hotspot moves to it and a phone on the hotspot reconnects; the show step is not affected. The first join
happens right after power-on. While the hotspot is up, a failed join is retried every 60 s (a retry scans every
channel, which also drops a phone on the hotspot for a moment); a network that drops is joined again at once.

mDNS: the board answers to `<name>.local` on both interfaces, each with that interface's address (192.168.4.1 on
the hotspot). Neither mDNS stack can give one interface a name of its own (ESP32, ESP-IDF 4.4 mdns: one hostname,
and a delegated hostname is answered on every interface; Pico W, LEAmDNS: one hostname on every interface), so
there is no separate hotspot name. A board with a Wi-Fi network starts mDNS on its first join; a board without one
starts it with the hotspot. `state.net` says where the board is on the other side: `mode` `sta` with `ip`, `host`
and `hotspot` (the hotspot's name, at 192.168.4.1) while joined; `mode` `ap` while not joined. The phone page shows
it at the top of the Live tab ("On your Wi-Fi … this board is at …", or "not joined to a Wi-Fi network").
The hotspot side of mDNS (`<name>.local` from a phone on the hotspot) and the Pico W's hotspot with a joined
network are not yet tried on hardware.

## Requests

```json
{"v": 1, "id": 7, "pin": "1234", "op": "run", "show": 1}
```

`id` (optional, an unsigned integer) is echoed in the response, errors included: an unknown `op`, a missing or
mistyped field, the page lock, a request refused before it is parsed (too big for the memory left) and broken JSON
whose top-level `id` can still be read. Only an answer to a request without a readable `id` has none. `pin` is needed for requests that change something when the
device's page lock is on (capability `pin`): `pin` is then the page password. Stopping never needs it:
`stop_all`, `stop`, `pause` with `on: true` and `set_output` with `level` `null` or 0. After a
wrong password the next try (right or wrong) waits 1 s, doubling with every wrong one up to 30 s; a new page
password needs at least 6 characters. With the page lock off (the
default) no password is asked; joining the board's Wi-Fi is the gate. The page password is set-only and kept
as a salted hash. A WebSocket connection is a session: the server pings it every 2 s and closes it after 5 s
of silence, and what it was given ends with it: the remote trigger enable, manual outputs, a pixel test.

| `op` | Fields | Result |
|---|---|---|
| `info` | | `info` |
| `state` | | `state` |
| `list_shows` | | `shows` |
| `get_show` | `show` (id) | `show` |
| `put_show` | `data` ([Show](#shows)), `new` (optional, default `false`) | `done`, or error `invalid` with `show_error`. Adds the show, or replaces the show with that id. With `new: true` the board gives it the lowest free id (the `id` sent is ignored) and answers `show` with the stored show: two pages adding shows at once never pick the same id |
| `add_example` | | `show` (the added show). Adds the example show (TRIG1 pulses OUT1 for 2 s with a pixel flash) at id 1, or the lowest free id. A new board (first boot, factory reset) has no shows, so wiring an input starts nothing until a show is added. `invalid` when a terminal it uses is not free for shows (an output role, the built-in effect) |
| `delete_show` | `show` | `done`, or `not_found` |
| `run` | `show` | `done`, or `refused` (running, cooldown, disabled, paused) |
| `stop` | `show` | `done` |
| `stop_all` | | `done` (also releases manual outputs, stops sound, sends every script effect back to its first `wait for`, ends the effect's cycle and the role pulses) |
| `pause` | `on` | `done` (pause = show stop: outputs off, triggers ignored, as `stop_all` for scripts, the effect and roles; releasing it turns nothing back on) |
| `cue` | `name` | `done` (sends a named cue on this device) |
| `set_output` | `channel`, `level` (0-1000, or `null` to release), `ms` (optional: a pulse, at most 600000) | `done` (manual test; relays and contacts are on/off). From plain HTTP or MQTT a level above 0 needs `ms` (else `refused`); a level above 0 is `refused` while the board is paused (nothing set during a pause comes on at its release); what a phone page holds is released when its WebSocket closes |
| `pixel` | `action` (a pixel [action](#actions), or `null`) | `done`; with `{"target":"PIXEL","do":"pattern","name":"<pattern>"}` a stored LED pattern plays as the pixel test (`not_found` when the board has none of that name) |
| `sound` | `cmd` + fields: `play_folder` (`folder`, `track`: `/FF/TTT…`, command 0x0F; sound N is folder N, track 1), `loop_folder` (`folder`, 0x17), `play_mp3` (`track`: the card's `/mp3/NNNN…`, command 0x12), `loop_mp3` (`track`: the same, played again each time it ends; `stop` ends it), `play_index` (`index`: the N-th file written to the card's root, 0x03), `loop_index` (`index`, 0x08), `volume` (`volume` 0-30), `stop`, `pause`, `resume`, `next`, `reset`, `select_sd` | `done` |
| `get_params` | | `params` |
| `set_params` | `values`: `[{"key":..,"value":..}]` (all checked, none applied if one fails; an empty secret keeps the old one, except that a change of `wifi_ssid` without a non-empty `wifi_pass` in the same `set_params` clears the stored `wifi_pass`: the new network is joined as an open one. Set a network and its password together, or the network first) | `done`; `internal` (`storage full: ...`) when a text value (a name) would make the settings record too big for the board's 12 KB |
| `action` | `key` (from `info.actions`, or a control alias) | `done` |
| `get_profile` | | `profile`: `topic_prefix` (absent: `proptroller`), `aliases` |
| `set_profile` | `data`: a profile (see [Profiles](#profiles-a-projects-own-words)); an empty one removes it | `done`, or `bad_request` with the reason; `internal` (`storage full: ...`) when the settings record would no longer fit the board's 12 KB. Never changes a value |
| `get_script` | | `script`: `text` (exactly as written; empty when none), `sounds` (the list stored with it, as sent; never read: a sound is its number), `check` (its check with the current settings) |
| `check_script` | `text`, `sounds` (optional; accepted and not read: scripts name sounds by number) | `script_check`: `ok`, `errors`, `warnings` and, when `ok`, `effects` (how many effects; see [Show scripts](#show-scripts)) |
| `set_script` | `text`, `sounds` (optional; stored with the text, not read), `bytes` (optional: the text's length in UTF-8 bytes as sent; when it differs from what arrived, `bad_request` and nothing is checked or stored) | `script_check`, as `check_script`. Stored only when `ok`; a text with no effects removes the stored script. `refused` (`... factory reset first, then set the script`) while a stored script from a newer firmware, or one that could not be read, is kept, and while a new firmware waits for its self-check (up to 30 s after an update); `internal` when it could not be stored or removed. Needs the page password while the page lock is on |
| `list_patterns` | | `patterns`: the stored LED patterns, see [LED patterns](#led-patterns). A read: no page password. `unsupported` on a board without the player (no `patterns` capability) |
| `put_pattern` | `name`, `program` (the program's bytes, base64), `controls` (optional: `{"<control function>": [values]}`, each value 16.16, 65536 is 1) | `pattern_put`, see [LED patterns](#led-patterns); `invalid` (a name, program or control the board cannot take, in plain words), `refused` (8 stored, the store full, or while stored patterns from a newer firmware or that could not be read are kept, or a new image waits for its self-check). Needs the page password while the page lock is on |
| `delete_pattern` | `name` | `done`, or `not_found`. Needs the page password while the page lock is on |
| `get_backup` | | `backup` ([Backups](#backups)) |
| `wifi_scan` | `rescan` (optional, default `false`) | `wifi_scan`: the Wi-Fi networks the board can see, see [Wi-Fi scan](#wi-fi-scan). `unsupported` on a board without Wi-Fi (no `wifi_scan` capability: the plain Pico); `refused` during a firmware update. A read: no page password needed |
| `put_backup` | `data` (a backup), `keep_name` (optional, default `true`) | `restored` (`skipped`: setting keys this firmware does not have), or `invalid` with the first problem; nothing is changed when anything fails (also when the stored settings are kept, see `store` in `state`). `internal` when everything was applied but the script could not be stored. Needs the page password while the page lock is on |
| `reboot` | | `done` (the lifetime run count is written first) |
| `factory_reset` | `forget_board` (optional, default `false`), `keep_wifi` (optional, default `false`) | `done` (erases settings with the Wi-Fi, shows and the show script, restarts; a power cut part way never brings an older record back; the lifetime run count and the board package are kept, and the board starts with the package's defaults; `forget_board: true` also removes the board package; `keep_wifi: true` keeps the name (so the board's `name.local`), the Wi-Fi network and its password and the setup hotspot's password: everything else goes back to the defaults, and the board comes back on its network), or `internal` when the flash could not be cleared (nothing restarts) |
| `get_board` | `preset` (optional: a built-in board's id) | `board`: `mcu` (`esp32-devkit` or `pico`: the chip socket a package must name), `running` (`id`, `name`, `source`: `built-in`, `stored` or `none`, and `problem` in plain words when the stored package cannot be used), `stored` (only when a package is stored or one was just forgotten: its `id` and `name`, `pending`: a restart is still to apply the store), `presets` (the image's built-in boards, `id` and `name`, its default first); with `preset`, `package`: that built-in board as a board package. `not_found` for an unknown preset. A read: no page password. `unsupported` on a firmware that takes no board packages (no `board_package` capability). See [Board packages](BOARD-PACKAGES.md) |
| `set_board` | one of `package` (a [board package](BOARD-PACKAGES.md)), `preset` (a built-in board's id) or `forget: true`; `defaults` (optional: `apply` applies the package's defaults once at the next power-on, on top of the board's settings; `keep`, the default, leaves the settings) | `board_set`: `id`, `name`, `restart_needed` (the board runs on the board it started with until it restarts: send `reboot` once the person confirms), `warnings` (boot-strapping pins, in plain words). `invalid` with the first problem, naming the field (`board package: OUT3: GPIO 35 is input-only on the ESP32 and cannot drive an output`): nothing stored; `internal` when it is too big packed or could not be stored (the board keeps the one it had); `refused` over MQTT, during a firmware update and while a new image waits for its self-check. Needs the page password while the page lock is on (not on the USB serial port) |
| `get_defaults` | | `defaults`: the running board's defaults, what a fresh board starts with: `source` (`board_package`: the stored board package's; `firmware`: the image's own, compiled in; `none`), `board` (the running board's id), and when they have them `profile` (set_profile's data), `settings` (setting name to value), `script` (the text) and `script_name` (what it is called). `refused` while a new board package waits for its restart and the running one's defaults are no longer stored. A read: no page password |
| `apply_defaults` | `only` (optional: any of `"profile"`, `"settings"`, `"script"`; default all three) | `defaults_applied`: `source`, `applied` (the parts there were and that were applied, in the order profile, settings, script), `refused` (one plain line per value or script the board refused; the rest applied). Applied now, no restart: a firmware type change can take the new firmware's script (and the profile its words come from) and keep the board's other settings, Wi-Fi and name. `not_found` when the board has none of the parts asked for; `refused` while a new board package waits for its restart and while a new image waits for its self-check. Needs the page password while the page lock is on |
| `reset_runs` | | `done`: the lifetime run count goes back to 0 (`runs_since_power_on` stays), written to flash at once. Needs the page password while the page lock is on. See [Run counters](#run-counters) |
| `test_input` | `input` (TRIG1, TRIG2, USER1-3), `active`, `ms` (optional: let go after this, at most 30000) | `done`. A test press, "hold to test" for an input: held until `active: false`, its `ms`, 30 s at the most (`TEST_PRESS_MAX_MS`), or the WebSocket session that pressed it closing. While remote starts are allowed (`remote_trigger_enable`) it joins the pin's reading (OR) on the 1 ms samples, so shows, script effects and the pulse-and-recover effect react exactly as to a real press. With remote starts off it shows on the input (`test` in the state) and starts nothing, except the script effects named in `remote_free_effects`. From plain HTTP or MQTT a press needs `ms` (else `refused`). Letting go never needs the page password; a press does while the page lock is on. `refused` during a firmware update (a held press is ignored then). Capability `test_input` |
| `sim_input` | `input`, `active` | simulator only |
| `sim_pixels` | | simulator only: `sim_pixels` with `pixels`, the lights the strip shows now (red, green, blue per light, hex) |
| `custom` | `name`, `data` (any JSON) | `custom` with `data`: answered by the firmware's own code (`prop.advanced().onRequest(fn)`, GOING-FURTHER.md); `not_found` when none answers. Needs the page password while the page lock is on |

## Responses

```json
{"v":1,"id":7,"ok":true,"result":{"kind":"done"}}
{"v":1,"id":7,"ok":false,"error":{"code":"refused","message":"in cooldown"}}
{"v":1,"id":3,"ok":false,"error":{"code":"invalid","message":"PwmOverLimit in track 1 at cue 0","show_error":{"problem":"pwm_over_limit","track":1,"cue":0}}}
```

`result.kind`: `done`, `info`, `state`, `shows` (`shows`: id, name, enabled, length_ms, tracks),
`show` (`data`), `params` (`params`, `values`), `backup` (`data`), `restored` (`skipped`), `wifi_scan` (see
[Wi-Fi scan](#wi-fi-scan)).

Error codes: `bad_request`, `version`, `invalid`, `not_found`, `refused`, `unsupported`, `pin`, `internal`.

Show validation problems (`show_error.problem`): `bad_id`, `bad_name`, `too_big`, `duplicate_target`,
`too_long`, `wrong_target`, `pwm_over_limit`, `ramp_too_long`, `total_over_limit`, `out_of_range`,
`too_fast`, `bad_cue_name`.

## Info

```json
{"v":1,"id":"a0b1c2d3e4f5","name":"Front door","fw":"generic","fw_version":"0.1.0","fw_impl":"cpp",
 "fw_build":"cf0f971","mcu":"esp32-devkit","board":"proptroller",
 "update":{"slot":"app0","max_bytes":1966080,"state":"valid","rolled_back":false},
 "capabilities":["shows","outputs","sound","pixels","params"],
 "channels":["OUT1","OUT2","OUT3","OUT4","RLY1","RLY2","CC1","CC2"],
 "inputs":["TRIG1","TRIG2","USER1","USER2","USER3"],
 "actions":[{"key":"save","label":"Save now","confirm":false}]}
```

Capabilities: `shows`, `outputs` (manual output tests), `outputs_ro` (outputs shown, not
driven), `sound`, `pixels`, `params`, `pin` (the page lock is on), `sim`, `test_input` (test presses on the inputs), `update` (over-the-air updates, see `update`), `backup`,
`wifi_scan` (the board can list the Wi-Fi networks it sees: ESP32, Pico W, Pico 2 W and the simulator),
`board_package` (the board takes board packages: `get_board`, `set_board`, [BOARD-PACKAGES.md](BOARD-PACKAGES.md)),
`defaults` (`get_defaults`, `apply_defaults`), `patterns` (the board plays LED patterns: the ESP32, the ESP32-S3 and the Pico 2 W, not the Pico W, the plain
Pico or the single-core ESP32-C3; [LED patterns](#led-patterns)). A UI shows only what the device has.

`actions` are buttons for the firmware's own controls (`key`, `label`, `confirm`: ask first, `momentary`: a
press-and-hold button that sends `action` with `down: true`, repeated while held, and `down: false` on release;
the built-in effect's Recover is momentary while `recover_mode` is `hold`; `hold`, only when true: a show-script
start that the page sends once the button has been held for its hold time, about a second, with the setting
`start_buttons` = `hold`). A control alias of the profile names the button with the board's word beside it:
`go (Trigger)`; for a show script's effects, a control alias named like the effect with a `label` names its
button, and a labelled alias of `cancel` names Cancel (the label alone). `stop_button: false` (only then): the
setting `stop_button` is off and the page shows no STOP.

`fw` is the firmware's name (`generic` unless the build sets `PROPTROLLER_FW_NAME`); `fw_version` the release
version (`firmware/VERSION`); `fw_build` the
source revision; `fw_impl` `cpp`. `update` is present only with the `update` capability:
`slot` the running slot, `max_bytes` the largest image it takes, `state` `valid` or `pending` (a new image
that has not yet marked itself good), `rolled_back` true when the last update failed its self-check and the
board went back to the previous image (cleared by the next update), `install_max_s` (only on boards whose
bootloader installs an update at the restart, before the new image starts: 180 on the Pico W and Pico 2 W) the
longest that may take, in seconds; a client waits that long plus the restart before it gives up.

## State

```json
{"v":1,"uptime_ms":123400,
 "outputs":[{"channel":"OUT1","level":1000,"manual":false},{"channel":"OUT2","level":0,"manual":false,"fault":"pwm_over_limit"}],
 "inputs":[{"input":"TRIG1","active":false,"waiting":false}],
 "shows":[{"id":1,"name":"Door","state":"running","elapsed_ms":1200,"length_ms":4000},{"id":2,"name":"Fog","state":"cooldown","remaining_ms":800}],
 "paused":false,
 "runs_since_power_on":12,"runs_lifetime":4810,"last_run_uptime_ms":118300,
 "sound":{"playing":true,"track":"/02/001","source":"ambient","online":true,"ambient_missing":false},
 "pattern":"fast_blink",
 "status":[{"key":"run_count","value":3}],
 "net":{"mode":"sta","ip":"192.168.1.50","ssid":"Office","host":"proptroller.local","rssi":-58}}
```

`net.mode` is `sta` (on a network; `hotspot` names its own Wi-Fi, up at the same time at 192.168.4.1), `ap` (not on a network: only its own hotspot, `ssid` its name, at 192.168.4.1), `usb`, `sim` or
`down`; `host` is the mDNS name the board answers to (see [Discovery](#discovery)), present once it has one.

Levels are permille (0-1000). An input held by a test press (`test_input`) has `test: true` and `test_starts`
(whether it starts things: remote starts on, else it only shows). `waiting` = a trigger that is held and must be released before it
can start a show. `pattern` is the status LED pattern (`off`, `steady`, `fast_blink`, `slow_blink`, `double_blink`,
`triple_blink`, `flicker`, `heartbeat`, `{"count":n}`). `sound.track` and `sound.source` (`ambient`, `trigger`, `show`)
are there only while something plays. `status` carries firmware-specific values (`key`, `value`; `label` when a
profile's status alias gives one; `unit: "ms"` on a time left, whatever its name: shown as m:ss);
`run_labels` (only with a profile that labels them) the run counters' labels. Only when
something is wrong with the flash: `store` (plain text: the stored settings, or the stored show script, are from
a newer firmware or could not be read, and are kept unchanged until a factory reset; changes to them are not saved) and `store_errors` (failed
writes since power-on; a failed save is tried again, 2 s doubling to 60 s). `board_problem` (only then): the stored
board package cannot be used (damaged, from a newer firmware, or not for this chip), so no pins are driven and no
terminal exists until a good package is sent or the stored one forgotten ([BOARD-PACKAGES.md](BOARD-PACKAGES.md)). Settings and shows are written 2 s
after the last change once nothing runs, at most once every 30 s and at the latest 60 s after the first unsaved
change; a change that leaves the stored record as it was (the same value again, a retained MQTT `config/set`
redelivered, `remote_trigger_enable`) writes nothing, and the `save` action writes only when something changed.
`runs_since_power_on`, `runs_lifetime` and `last_run_uptime_ms` are the [run counters](#run-counters)
(`last_run_uptime_ms` only once a run started since power-on; the time since it is `uptime_ms - last_run_uptime_ms`).
While a show script is stored, `"script":{"effects":[{"name":"porch light","running":true,"line":5}]}`:
`line` is the script line each effect is at; when the stored script failed its check at power-on it does
not run and `"error"` holds the first message. An output a script sets has the driver `script`.
During a firmware update `"update":{"phase":"writing","done":204800,"total":1442560}` (then
`{"phase":"restarting","done":0,"total":0}` until the restart); absent otherwise.

## Wi-Fi scan

`wifi_scan` lists the Wi-Fi networks the board can see, for a network picker (the phone page's Settings → Wi-Fi).
It runs on the ESP32 (hardware), in the simulator and in the host tests; the Pico W's scan is not yet tried on
hardware:

```json
{"v":1,"id":91,"ok":true,"result":{"kind":"wifi_scan","scanning":false,"age_ms":90,
 "networks":[{"ssid":"Office","rssi":-48,"secured":true},{"ssid":"Guest Wi-Fi","rssi":-71,"secured":false}]}}
```

* `networks`: one entry per name (the strongest access point of that name), strongest first, at most 20; hidden
  networks (no name) are left out. `rssi` in dBm, `secured` false for an open network (no password).
* The answer never waits for the radio. It carries the last list the board has (`networks` empty before the first)
  and `scanning: true` while a new scan is asked for or running; ask again about every 1.5 s until it is false.
  `age_ms`: how old the list is (absent before the first). `error` (plain text) when the last scan failed or timed
  out (15 s); the list before it is kept.
* A plain `wifi_scan` starts a scan when there is no list, the last one failed, or it is 30 s old or more.
  `rescan: true` asks for a new one anyway. Scans are at least 10 s apart, whoever asks (every page together): a
  rescan sooner is not started and the answer has `retry_ms`, how long until one can be.
* The scan never touches the show step: it runs in the network loop (the ESP32's loop task, the RP2040's core 0)
  and the controller only keeps the list. The hotspot stays up while it scans (the ESP32 listens at most
  120 ms on each channel and goes back to the hotspot's channel in between; a scan takes about 1.6 s on an ESP32
  with the hotspot up); a phone on the hotspot may see a short pause. No scan starts while the board is joining a network, and no join starts
  during a scan. The board scans once right away when its hotspot comes up, so the list is ready when a phone
  gets there.
* A read like `get_params`: no page password needed while the page lock is on.

## Events

On the WebSocket the device pushes, besides responses:

```json
{"v":1,"seq":0,"event":"state","data":{ ...State... }}
{"v":1,"seq":12,"event":"notice","data":{"notice":"started","show":1,"cause":{"by":"input","detail":"TRIG1"}}}
{"v":1,"seq":13,"event":"notice","data":{"notice":"ended","show":1,"reason":"finished"}}
{"v":1,"seq":14,"event":"shows_changed"}
{"v":1,"seq":15,"event":"update","data":{"phase":"writing","done":524288,"total":1442560}}
{"v":1,"seq":15,"event":"update","data":{"phase":"failed","done":4096,"total":1442560,"message":"not an image for esp32-devkit"}}
{"v":1,"seq":15,"event":"params_changed"}
{"v":1,"seq":16,"event":"script_changed"}
{"v":1,"seq":16,"event":"patterns_changed"}
{"v":1,"seq":16,"event":"log","data":"sound player online"}
{"v":1,"seq":17,"event":"run","data":{"source":"input","kind":"show","show":1,"name":"Door","detail":"TRIG1","runs_since_power_on":12,"runs_lifetime":4810,"uptime_ms":118300}}
```

The state is pushed when it changes (at most every 200 ms; `uptime_ms` alone is no change) and at least every
5 s. While a client's socket is full, only the newest state waits for it. Notices: `started`
(`cause.by`: `input`, `release`, `timer`, `boot`, `cue`, `after`, `api`), `ended` (`reason`:
`finished`, `stopped`, `restarted`, `paused`, `replaced`, `released`: an `until_release` run whose input was let go), `ignored` (`why`: `running`, `cooldown`,
`disabled`, `paused`, `unknown`, `held` = a condition in the firmware's own code), `watchdog`, `interlock`, `paused`. A gap in `seq` means events were
dropped (the device keeps the last 64). Tell responses and events apart by `ok` vs `event`.
`run` events: a run started (see [Run counters](#run-counters)). `source` is what started it (`input`,
`release`, `timer`, `boot`, `cue`, `after` as for notices, `api` for the web page, MQTT, `POST /rpc` and the serial
port); `kind` is `show` (with `show`, its id), `script` (a show script's effect; `detail` the input its first
`wait for` waits for) or `effect` (the built-in pulse-and-recover effect, `name` `pulse_and_recover`); `name` the
show's or effect's name, `detail` the input or cue; the counts after this run and `uptime_ms` when it started.
`update` events: `writing` when the image starts (`done` 0) and at least every 128 KB (131072 B) after,
`restarting` when it is written and checked (`done` = `total`), `failed` with `message` (the same text as the
answer) when it stops. Each is preceded by a `log` event (`firmware update: receiving N bytes`,
`firmware update: the new image is written and checked; restarting into it`, `firmware update stopped: <message>`).

## Run counters

Every board counts its runs: `runs_since_power_on` (RAM, 0 at every power-on: a show night, when the boards are
powered up for the night and off afterwards) and `runs_lifetime` (kept in flash: across restarts, power cuts,
firmware updates, backup restores and factory resets; only `reset_runs` sets it back to 0). There is no count per
calendar day: a night's show runs past midnight.

One definition for every source: a run is one start of a show, of a show script's effect (it leaves its first
`wait for`) or of the built-in pulse-and-recover effect, whatever started it: a trigger input, the web page, MQTT,
the serial port, a cue, a timer, another show. Not runs: a trigger that is ignored (running, cooldown, paused),
the restart of a running show (`retrigger: restart`), a looping show going round again, and a recover cycle.
The effect's own `run_count` status keeps its meaning (since power-on, recover cycles included, SM-PR3), so it can
differ from `runs_since_power_on`.

They are in the [State](#state), on MQTT (`<p>/status/runs_since_power_on`, `…/runs_lifetime`,
`…/last_run_uptime_ms`, -1 before the first run; a profile may give them status aliases), in the serial
console's `status`, and each run is a `run` [event](#events) (on MQTT also `<p>/event/run`).

The lifetime count has its own small record (two files, `runs-a.bin` / `runs-b.bin`, power-safe like the
settings), written from the loop, never from the show step, and only when nothing runs: a flash write holds up
the step (estimated 1-3 ms, about 45 ms typical when the flash erases a sector), so a trigger pressed meanwhile is
seen that much later. It is written once 10 runs wait or 60 s after the first run not yet written, at most once
every 10 s, never while a show, the effect's cycle or a script effect runs (a board that never goes quiet writes
at its next quiet moment), and at once before a clean restart (`reboot`, the `restart` control, `factory_reset`,
a firmware update). A sudden power cut loses the runs since the last write: at most 10 runs or 60 s of runs while
the board has quiet moments between runs; every run since it was last quiet while it is busy without a break.

## Shows

```json
{"id": 1, "name": "Front door",
 "enabled": true, "priority": 1, "looping": false, "retrigger": "ignore", "cooldown_ms": 5000, "stop_sound": true,
 "triggers": [{"on": "input", "input": "TRIG1"}, {"on": "cue", "name": "door"}],
 "tracks": [
   {"target": "OUT1",  "cues": [{"at": 0, "do": "on"}, {"at": 3000, "do": "off"}]},
   {"target": "OUT2",  "cues": [{"at": 500, "do": "ramp", "to": 1000, "ms": 300}, {"at": 4000, "do": "off"}]},
   {"target": "RLY1",  "cues": [{"at": 1000, "do": "pulse", "ms": 3000}]},
   {"target": "CC1",   "cues": [{"at": 0, "do": "pulse", "ms": 200}]},
   {"target": "SOUND", "cues": [{"at": 0, "do": "play", "folder": 1, "track": 1}]},
   {"target": "PIXEL", "cues": [{"at": 0, "do": "flicker", "color": "#ff6000"}, {"at": 4000, "do": "off"}]},
   {"target": "NET",   "cues": [{"at": 100, "do": "emit", "cue": "lights"}]}
 ]}
```

Times are ms from the start of the show. A show ends at `length_ms` (default: the end of its last cue).

### Triggers

| `on` | Fields | |
|---|---|---|
| `input` | `input`, `until_release` (optional, left out when `false`) | a press; TRIG inputs must be released for 100 ms before they can start a show again, and a trigger held at power-on never starts one. With `until_release: true` a run this press started ends (reason `released`) as soon as the input is let go: "on while held". The show's end is the most it runs held (`length_ms`, or 10 minutes when it has none; a `looping` show loops for as long as the input is held, limited only by the per-output maximum on-times); a release after that turns nothing on, and a run started any other way (`run`, a cue) is not ended by a release. `until_release` on any other trigger is a `bad_request` |
| `release` | `input` | the release |
| `every` | `period_ms` (≥100) | periodic |
| `boot` | `delay_ms` | once after power-on (ambient loops, auto start) |
| `cue` | `name` | a named cue: another show's `emit`, another controller (ESP-NOW), or `op: cue` |
| `after` | `show` | when that show finishes |

A link ("on while held", or "on for N seconds when pressed") is a normal show: `{"id":5,"name":"Link: TRIG1 -> OUT1",
"triggers":[{"on":"input","input":"TRIG1","until_release":true}],"tracks":[{"target":"OUT1","cues":[{"at":0,"do":"on"}]}],
"length_ms":60000}`, or for N seconds a `pulse` cue with `ms` and no `until_release`. The per-output maximum on-times
still apply.

Retrigger while running: `ignore` (default), `restart`, `stop`. Triggers during the cooldown are
ignored, never queued. `runs_per_cooldown` (1-20, default 1, left out when 1) lets a show run several
times per cooldown: the cooldown starts when the first run ends, and once that many runs have started
inside it the show waits until it is over; the count is not stored, so a restart clears it. A restart
or loop of a running show is not a new run. The state then also carries `runs_left` and `window_ms`
(time until the window ends, 0 when none is open). With priority, a higher-priority show overrides a lower one on shared targets;
a target the show has not reached yet is left to the others.

### Actions

| Target | `do` | Fields |
|---|---|---|
| OUT1-4 | `on`, `off`, `pulse`, `blink`, `level`, `ramp` | `pulse`: `ms`; `blink`: `on_ms`, `off_ms`, `count` (0 = until the next cue); `level`: `level`; `ramp`: `to`, `ms` |
| RLY1-2, CC1-2 | `on`, `off`, `pulse`, `blink` | `blink`: `on_ms` and `off_ms` at least 100 (a relay or contact switches at most every 100 ms; one that went off comes on again no sooner) |
| SOUND | `play`, `loop_folder`, `volume`, `off` | `play`: `folder` 0 and `track` 1-99 is sound N (the card's `NN/001…`, as in a script), `folder` 1-99 and `track` 1-255 the file `/FF/TTT.mp3`; `loop_folder`: `folder` 1-99; `volume` 0-30 |
| PIXEL | `fill`, `flicker`, `chase`, `flash`, `fade`, `rainbow`, `off` | `color` `"#rrggbb"`, `step_ms`, `width`, `period_ms`, `ms` |
| NET | `emit` | `cue` (1-24 characters) |

### No load-current checks

The firmware neither measures nor checks load current (STATE-MACHINE.md SM-S3) and has no load-current
settings: each terminal's maximum rating is in `hardware/README.md`, and loads are chosen within it.
Shows are checked for targets (dimming and fades on OUT1-4 only; relays and contacts are on/off),
ranges and sizes. The per-output maximum on-times (`*_max_on_ms`) still apply.

## Settings (generic firmware)

`get_params` returns descriptions (`key`, `label`, `group`, `type` = `int` with `min`/`max`/`unit`, `bool`,
`text` with `max_len`/`secret`, `choice` with `options`; `help`; `reboot`) and values (a secret's `value` is
always empty, with `set`: whether one is stored). A dedicated firmware that names a generic setting its own way may
add `alias_of` (the generic key) to its description; the page then treats it as that setting (today: the remote
trigger switch beside the buttons, also found under a profile's alias of `remote_trigger_enable`). Keys: `name`, `pin`,
`wifi_ssid`, `wifi_pass`, `ap_pass`, `out1_soft_start_ms` …,
`out1_max_on_ms` … `cc2_max_on_ms`, `interlock_relays`, `trig2` (`trigger` / `pause`), `trig1_input` / `trig2_input` (`switch`: closes to ground, the default;
`sensor_high`: a powered sensor whose signal goes high, fed from the jack's ring; it must drive its signal low when
idle, and an unplugged cable counts as a trigger in this mode, so use `switch` for anything safety-related; a
change re-arms the trigger: it must be seen released first), `trig_debounce_ms`,
`trig_release_ms`, `pixel_count`, `pixel_brightness` (%), `pixel_speed` (%: how fast a show script's pixel effects run),
`volume`, `df_ack`, `df_checksum`, `status_led`, `board_link`, `link_group`, `link_key` ([Board link](#board-link)), and the pulse-and-recover effect's keys (`effect`,
`out1_on_ms` …, README "The pulse-and-recover effect"). Other firmwares publish their own keys the same way.

USER1-3 are always plain triggers. The board has no recorder of its own: the older learn mode (setting `buttons` =
`learn`, `record_show`, the REC / SELECT / TAP buttons) and the ops `record_start`, `record_tap`, `record_finish`,
`record_abort` are gone (an unknown op is a `bad_request`; the two keys are refused like any unknown key and ignored
in a stored settings record). A show made by the old recorder stays a normal show. Recording is done by the phone
page and Specter Studio ("Record effect", `docs/RECORD-EFFECT.md`): they drive the outputs with `set_output`
and save what was tapped as an effect script with `get_script` / `check_script` / `set_script`.

## Profiles: a project's own words

A profile gives the generic names a project's own words without renaming anything (reference:
`firmware/STATE-MACHINE.md` §11). It is one JSON block, loaded with `set_profile` (or by the desktop
app's New-controller wizard from a profile file), stored with the settings and read back with
`get_profile`:

```json
{ "topic_prefix": "myprop",
  "aliases": [
    { "alias": "valve_time",     "key": "out1_on_ms",          "label": "Valve open time", "unit": "ms" },
    { "alias": "refill_s",       "key": "out2_on_ms",          "label": "Refill time", "unit": "s", "scale": 1000 },
    { "alias": "skip_after_output", "key": "out3_enabled",     "invert": true },
    { "alias": "go",             "control": "trigger" },
    { "alias": "presses",        "status": "run_count" },
    { "alias": "VALVE",          "terminal": "OUT1",           "label": "Valve" },
    { "alias": "REFILL",         "terminal": "TRIG2",          "label": "Refill button" } ] }
```

* Each alias names exactly one `key` (a setting), `control` (`trigger`, `recover`, `cancel`, `restart`),
  `status` (a status line), `terminal` or `fixed` (a name an older tool still asks for: it always reads that
  text, and a value set under it is accepted and changes nothing). `label` and `unit` are what the page shows
  (and refusals say: `Valve open time is set to …`); `group` (a key alias) the Settings group it is listed in;
  `invert` (true / false settings) gives the opposite meaning; `scale` (number settings) is key = alias × scale;
  `also` (a key alias) lists 1-4 more keys of the same kind that a value set under the alias goes to as well.
* Settings: `get_params` lists an aliased setting under its alias, with its label, unit and range in the
  alias's units; `set_params`, MQTT and the serial console take the alias or the generic key.
* Status lines are reported under their aliases (State, MQTT `status/<alias>`, serial).
* Terminals: a request or show may use the alias wherever a terminal name goes (`"input":"REFILL"`,
  `"channel":"VALVE"`, any case); it is stored under the board name. `info.terminals`, `state.inputs` and
  `state.outputs` carry `alias`; displays read `REFILL (TRIG2)`, and pressing an aliased input logs
  `REFILL (TRIG2) pressed`.
* `topic_prefix` replaces `proptroller` in every MQTT topic, the broadcast topics included.
* Refused, with the reason (nothing changes): an alias used twice, an alias equal to a setting key, a
  control, a status name or a terminal name, an unknown key / control / status / terminal, an alias of a
  secret (`pin`, `wifi_pass`, `ap_pass`), an `also` that is not other keys of the same kind (or names a
  secret), more than 64 aliases. Key, control and status aliases: 1-32
  letters, digits or `_`; terminal aliases: 1-16 capital letters, digits or `_`, compared without case.

## Show scripts

The beginner show-script language is `firmware/SHOW-SCRIPT.md`; its exact grammar, every message text, the
step table and the run rules are the golden set's README (`firmware/show-script-golden/README.md`). The
text and its sound list are stored in a flash store of their own, compressed, with the checked step table;
power-on runs that table, and checks the text again only after a firmware update, a change to a setting the
checker reads, or when the stored table is damaged (FLASH-MAP.md "Stored records"). While a
script is stored the ambient loop is off, script runs never play the default trigger sound or pulse the
role outputs, a script output wins over a show using it, and a settings change that would break the script
is refused (`invalid`: `rly1_role: the stored script uses rly1. Change the script first.`). A script may
call a terminal by its wiring name (`out1_name`) or profile alias (SHOW-SCRIPT.md SS-N); renaming, removing or
moving to another terminal a name it uses is refused the same way (`out1_name: the stored script uses lamp. ...`, and from `set_profile`
`profile: the stored script uses fog. ...`).

Every effect of a stored script can be started by its name (SHOW-SCRIPT.md SS-R10; spaces as `_`): op `action`
with `key` the name, MQTT `…/control/<name>`, the serial console's `<name>`; `cancel` empties the waiting starts.
A name wins over a profile's control alias. A remote start needs `remote_trigger_enable` (refusals in the
pulse-and-recover effect's words; one start waits while the effect cannot start), except for the effects named
in `remote_free_effects`. `info.actions` lists every effect (`Start <name>`) and `cancel`. With the built-in
effect off, a stored script gives the effect's status lines (`run_count` of every effect; `ready`, `runs_left`
and `cooldown_remaining_ms` of the first; `pulsing`, `recovering` as OUT1 / OUT2 on; `queued`;
`remote_trigger_enable`, `remote_trigger_by`: SS-R13), so a profile's status aliases apply, and `out4_ready_lamp`
makes OUT4 the first effect's ready lamp. `trig1_wait` / `trig2_wait` (`ready`, the default, or `return`) choose
how a first `wait for` on that input starts its effect (SS-R4). Lines may name a setting for a time
(`wait out1_on_ms`), read when they run. The stored table's format is 4 (the tell lines' names and values, then the
names of the LED patterns and buttons the script names, after the steps); a table stored by an earlier firmware is
checked again at power-on. A script's `tell` lines reach other boards ([Board link](#board-link)).

The step table the checker makes stays on the board: a `script_check` (the answer of `check_script` and
`set_script`, `get_script`'s `check`, MQTT `script/result`) carries `ok`, `errors`, `warnings` and, when `ok`,
`effects` (the number of effects; 0 after a `set_script` that removed the script). Until 2026-10-20 it also
carried the whole table as `table`, which made the answer to the largest script about 20 KB of JSON; the
table's form is still the golden set's (`firmware/show-script-golden/README.md`), for checkers that want to
compare.

## LED patterns

Specter Studio compiles an LED pattern (its pattern language, `src/patterns/compile.ts`) to a small program of
numbers and sends the program's bytes; the board never sees source and never compiles. The board's interpreter
does exactly what Studio's preview does, bit for bit (`firmware/pattern-golden`, the shared vectors). A board
keeps at most 8 patterns by name (1-32 characters: letters, digits, spaces and `- _ ( )`; case and extra spaces do
not matter, so `Clue  Puzzle` replaces `clue puzzle`), all of them in one record of their own (FLASH-MAP.md), with
the control values each was sent with. A changed list is written to flash when nothing runs, at the latest a
minute after the change (a flash write holds up both cores for a moment); `reboot` writes it first, a factory
reset removes every pattern. Show scripts play them with `pixels = pattern <name>` and press their buttons with
`pixels press <button>` (SHOW-SCRIPT.md SS-X7, SS-X8); the pixel test plays one with the action `do: "pattern"`.

```json
{"v":1,"op":"put_pattern","name":"clue puzzle","program":"U1BBVAEA...","controls":{"sliderSpeed":[32768],"hsvPickerColor":[21845,65536,65536]}}
{"v":1,"ok":true,"result":{"kind":"pattern_put","name":"clue puzzle","bytes":1208,"patterns":2,"store_bytes":1530,"store_capacity":8192,"buttons":["clue1","clue2","clue3","clue4","clue5","clue6","reset"]}}
{"v":1,"op":"list_patterns"}
{"v":1,"ok":true,"result":{"kind":"patterns","patterns":[{"name":"clue puzzle","bytes":1208,"controls":[{"kind":"trigger","name":"triggerClue1","label":"Clue1"}],"buttons":["clue1"]}],"max":8,"store_capacity":8192,"saved":true,"playing":"clue puzzle"}}
```

`pattern_put`: the stored name, the program's size, how many patterns the board now keeps, the record's packed
size and its capacity, and the pattern's buttons (its `trigger` controls, by label, lower case: what `pixels press`
takes). `patterns`: each stored pattern with its controls (`kind`: `slider`, `hsvPicker`, `rgbPicker`, `toggle`,
`trigger`, `inputNumber`; `name` the function, `label` Studio's label for it) and buttons; `saved` false while a
change is not yet on flash; `playing` the pattern the strip shows, when it shows one. Sending a pattern under a
name already stored replaces it; whatever plays it starts the new one.

Limits, the same in Studio's preview (`BOARD_LIMITS`) and checked when the bytes arrive: 4096 code words, 256
variables, 128 functions, 16 controls, 64 values per function; while it runs, 256 values being worked on, 12
functions calling each other, 512 values of the running functions, 2048 list values (a list takes one more), and
98,304 steps in one frame (or one control); a frame that takes 250 ms stops the pattern too. The programs of every
stored pattern take at most 24 KB, the packed record 8 KB.

The pixel side plays the pattern (a task on core 0 on the ESP32 and ESP32-S3, `loop()` on the Pico 2 W), never the 10 ms
step, with up to 50 frames a second; a frame that took long is followed by as long without one. A pattern that
fails (an error, or more than its budget) stops, the strip goes dark, and the event log says why:
`pattern clue puzzle stopped: the pattern took too long on one frame and was stopped (a loop that never ends?).
The strip is dark until the pattern starts again`. `mem` adds the frames since the last report, their average and
longest time, the most steps one took and the player's memory.

The program's bytes (little endian; Studio's `src/patterns/encode.ts` writes them):

| Offset | Bytes | What |
|---|---|---|
| 0 | 4 | `SPAT` |
| 4 | 1 | format, 1 (a board refuses another: `the pattern was made by a newer Studio`) |
| 5 | 1 | 0 |
| 6 | 2 | code words |
| 8 | 2 | variables (globals; the first is `pixelCount`, which the board sets) |
| 10 | 2 | functions |
| 12 | 1 | controls |
| 13 | 1 | 0 |
| 14 | 8 | the functions `beforeRender`, `render`, `render2D`, `render3D` (0xFFFF: none; one of the render functions is needed) |
| 22 | 4 each | per function: its first code word (2), parameters (1), values (1, parameters included); function 0 is the top-level code |
| | | per control: kind (1, in the order above), function (2), name length (1) and name, label length (1) and label (printable ASCII, at most 63 each) |
| | 4 each | the code: opcodes and their arguments as 32-bit words (numbers 16.16) |

A program that does not read exactly so, or whose code jumps outside its function, uses an index out of range,
could take more values off its stack than it put on, or does not end each function with a return, is refused
(`the pattern's program is damaged: send it again from Studio`).

## Serial console

USB serial, 115200 baud (on the Pico, a line that starts with `{` is a JSON request instead). One command
per line: `key` answers `key=value`; `key=value` sets it and answers `key=value` or `error: …`; secrets
answer `(set)` / `(empty)`, never the value, and are echoed as `*` while typed; `trigger`, `recover`,
`cancel` answer `ok` or `error: …`; `restart`; `status` (every status line); `settings` (JSON); `help`.
Aliases work as their keys. The console is local access: the page lock does not apply to it.

JSON over USB serial: a line that starts with `{` is a request of at most 42,952 bytes, the same limit as an
HTTP body or a WebSocket message (`MAX_REQUEST_BODY`: a `put_backup` of everything a board stores, about 12 KB,
plus the largest show script with every character escaped, 16 KB, a full sound list and 4 KB of envelope; a full
board's backup is about 21 KB). A longer line is dropped as it arrives and answered once with `bad_request` ("the
line is longer than 42952 bytes"); a line the board has no memory for is dropped the same way and answered
`refused`. Neither is handled as a shorter request, and the line's memory is given back after every long line.
A long request still needs heap to be parsed and checked (about 4 times its size and 48 KB more): one the board
cannot hold at the moment is refused in plain words, as over HTTP. After the first one the board also writes every event and, every 250 ms at
most, the state when it changed other than in `uptime_ms` (and at least every 5 s), never while a request line
is coming in. A client that sends a long text over serial can add `bytes` to `set_script` so a line that lost
bytes on the way is refused instead of stored.

## Multi-unit

* **Contact cascade**: a CONTACT (CC1/CC2) or relay output of one board wired to TRIG1/TRIG2 of the next
  (3.5 mm cable). The receiving board sees an ordinary input press (20 ms debounce), so one controller
  drives up to four others with plain show tracks.
* **Radio cues**: an `emit` cue is broadcast over ESP-NOW as `PTCUE1:<name>`; every other Proptroller in
  range starts its shows that have a `cue` trigger with that name.
* **Board link**: a show script's `tell` lines start effects and change settings on other boards
  ([Board link](#board-link)).

## MQTT

Optional: set `mqtt_broker` (host or address) and `mqtt_port` (default 1883), then restart. MQTT 3.1.1,
QoS 0, no authentication or TLS (a broker on the show network). MQTT never holds up the show: the board
connects, resolves the broker's name and sends without waiting on the broker, reconnects 5 s after a lost
session and then 10, 20, 40 and at most 60 s apart (each with up to a quarter more at random, so a fleet does
not reconnect in step), and ends a session whose broker stops taking messages (the will then sets `online` to
`false`; the retained state is published again on the next session: the state is latest-wins).
Each firmware defines its own topic tree in one module; the generic firmware's (`device.cpp`, `mqttOnMessage`) uses `<p>` = `proptroller/<name>`, where `<name>` is
the mDNS label of the device name (`Front Door` → `front-door`). A profile's `topic_prefix` replaces
`proptroller` in every topic below, and setting, control and status names may be its aliases:

| Topic | Direction | Payload |
|---|---|---|
| `proptroller/broadcast/getName` | in | anything: every controller answers on `proptroller/broadcast/name` |
| `proptroller/broadcast/name` | out | the controller's `<name>` (for example `front-door`) |
| `<p>/online` | out, retained | `true` on connect; the last will sets `false` |
| `<p>/state` | out, retained | the [State](#state) JSON when it changes, at most once a second, with `boot_id` and `mqtt_seq` first (below) |
| `<p>/event` | out | the event JSON (notices, logs, runs, `params_changed`), with `boot_id` and `mqtt_seq` first (below) |
| `<p>/event/run` | out | each run as it starts: the `run` event's `data` alone (`{"source":"input","kind":"show","show":1,"name":"Door","detail":"TRIG1","runs_since_power_on":12,"runs_lifetime":4810,"uptime_ms":118300}`), without `boot_id` / `mqtt_seq`: the same run is also on `<p>/event` |
| `<p>/rpc` | in | a request envelope, exactly as for `POST /api/v1/rpc` (with `pin`, the page password, while the page lock is on) |
| `<p>/rpc/reply` | out | the response |
| `<p>/config/get/<key>` | in | anything: the value is published on `<p>/config/<key>` |
| `<p>/config/set/<key>` | in | the value: `true` / `false` (exact, lower case), a number, or text |
| `<p>/config/<key>` | out, retained | every setting on connect, then each one that changes; secrets are never published |
| `<p>/error` | out | why a `config/set` was refused (unknown key, out of range, bad value) |
| `<p>/cue` | in | a cue name: sends it, like a radio cue from another controller |
| `<p>/control/restart` | in | anything: restarts the board |
| `<p>/control/trigger`, `…/recover`, `…/cancel`, `…/<script effect>` | in | the pulse-and-recover effect, or a show-script effect by its name (a start needs `remote_trigger_enable` unless the effect is in `remote_free_effects`); refusals on `<p>/error`. The payload is not read, except another board's tell `{"from","id","seq","group"}`, which is answered on `<root>/<from>/link/answer` ([Board link](#board-link)) |
| `<p>/config/now/<key>` | in | the value (as for `config/set`), or another board's tell `{"value","from","id","seq","group"}`: the setting is used at once and **not saved** (it lasts until a restart or a change on the board); refused while page-locked and for the settings a board never takes from another ([Board link](#board-link)); the new value is published on `<p>/config/<key>`, refusals on `<p>/error` |
| `<p>/link/answer` | in | the answer to one of this board's tells: `{"from","id","seq","ok","message"}` |
| `<p>/status/<key>` | out, retained | the [run counters](#run-counters) (always, as soon as they change) and the effect's status lines (while it is on or a script is stored, when they change, at most every 0.1 s; a time left at most once a second) |
| `<p>/status/get/<key>` | in | anything: that status value is published again |
| `<p>/script/set` | in | `{"text": "..."}`: checks and stores the show script |
| `<p>/script/check` | in | the same: checks it only |
| `<p>/script/result` | out | the `script_check` result |

Every `<p>/state` and `<p>/event` message starts with `"boot_id":"1a2b3c4d","mqtt_seq":7`: `boot_id` is
random at each start of the board, and `mqtt_seq` counts the state and event messages of that boot together
(1, 2, 3, ...). A new `boot_id` means the board restarted; within one, a gap in `mqtt_seq` means messages were
lost (QoS 0, or a session that ended), so a fleet backend reads the retained state again. An event keeps its own
`seq` (the board's event counter, the WebSocket's).

The two `proptroller/broadcast/…` topics are the only ones every board listens to (a board whose script tells
`all` over MQTT also reads every board's retained `<root>/+/online`, to know which boards there are; it acts on
nothing there); everything else names one board, so a setting reaches only that board, and a setting or control sent to a broadcast or wildcard
topic is ignored. No board can be named `broadcast`. With the page lock on MQTT is read-only: `config/set`,
`script/set`, `cue` and every `control/…` but `trigger` (which needs `remote_trigger_enable`) are refused: use
`rpc`, which carries the page password.

## Board link

A show script's `tell` lines (SHOW-SCRIPT.md §11) reach other boards over MQTT or straight from board to board
(peer to peer). The setting `board_link` chooses how this board sends: `auto` (MQTT when `mqtt_broker` is set,
otherwise peer to peer), `mqtt`, `wifi` (peer to peer) or `off`. A board takes tells over MQTT whenever it has a
broker, and peer to peer unless `board_link` is `mqtt` or `off`. A board is addressed by its MQTT / mDNS label
(`hostLabel` of its name: `Front Door` is `front-door`); `all` is every other board (over MQTT: every board whose
retained `<root>/<label>/online` is `true`; peer to peer: every board of the group that hears the broadcast).

What the receiving board does is what a start by name or a `config/set` does, with the same rules:

* **Effect** (`tell porch blackout`): `remoteControl`, the code behind MQTT `<p>/control/<name>`: a control alias
  works, a start needs `remote_trigger_enable` unless the effect is in `remote_free_effects`, a page-locked board
  takes only starts that have that gate (as over MQTT), one start waits while the effect is busy. `restart` is
  never taken from another board.
* **Setting** (`tell porch pixel_speed = 150`): the value as `config/set` reads it (plus `on` / `off` for a switch
  and a trailing `%` for a percentage), key aliases included; it is applied at once and never written to flash:
  the value the flash keeps is remembered and written by any later save (and a backup carries it), until the
  setting is changed on the board, a backup is restored or the board restarts. Refused: every secret, every setting
  that needs a restart, `name`, `page_lock`, `pin`, `wifi_ssid`, `wifi_pass`, `ap_pass`, `mqtt_broker`,
  `mqtt_port`, `board_link`, `link_group`, `link_key`, `remote_trigger_enable`, `remote_free_effects`, `effect`,
  and everything while the board is page-locked.
* The sender keeps each tell 2 s for its answer (`LINK_ANSWER_MS`) and logs `sent`, a refusal with its reason,
  `no answer from <board>`, or for `all` how many boards answered; at most 20 tells a second (`LINK_MAX_PER_S`).
  The receiver logs who started what (`remote start by front-door: effect "blackout"`) and every setting changed.

**Over MQTT**: an effect is published to `<root>/<board>/control/<effect>`, a setting to
`<root>/<board>/config/now/<key>`, never retained, with the payload `{"from":"front-door","id":"<id>","seq":12,
"group":"<link_group>"}` (a setting adds `"value"`). The receiver answers on `<root>/<from>/link/answer` with
`{"from","id","seq","ok","message"}`; a payload whose `group` is not the receiver's own is ignored. Other tools
can publish the same messages; a plain payload works as before and gets no answer. MQTT has no key.

**Peer to peer**: one binary frame, sent as a UDP datagram to the discovery port (47800) as a subnet broadcast on
each network the board is on (its Wi-Fi network, and its own hotspot at 192.168.4.255), and on the ESP32 also as
an ESP-NOW broadcast (the radio's current channel, the cues' peer). Little endian, at most 250 bytes (ESP-NOW's
payload):

| Bytes | Field |
|---|---|
| 0-3 | `PTL1` |
| 4 | kind: 1 start an effect, 2 change a setting, 3 an answer, 4 a challenge |
| 5 | flags: bit 0 a tag follows (signed), bit 1 ok (an answer) |
| 6-9 | boot: the author's random nonce for this boot |
| 10-13 | seq: a request's number (from 1 at each boot); an answer's or challenge's: the request's |
| 14-17 | bound: a request: the receiver's nonce it is bound to (0: none); an answer or challenge: the request author's nonce |
| 18 … | six strings, each a length byte then its bytes: group (≤ 16), the author's id (≤ 16), the author's name (≤ 40), to (a board's label or `all`; an answer's or challenge's: the request author's id, ≤ 40), what (the effect or setting, ≤ 32), value (the setting's value, or an answer's text, ≤ 64) |
| last 16 | with a key: the first 16 bytes of HMAC-SHA256(`link_key`, every byte before them) |

A board takes a frame only from its own group (`link_group`, compared exactly), never its own (its id), and only a
request addressed to its label or `all`. Each request is answered back the way it came (UDP to its source
address and port, or an ESP-NOW broadcast). A request not answered is sent again after 250 and 700 ms; the
receiver keeps a window of 32 numbers per sender and boot, so a frame that arrives twice (UDP and ESP-NOW, or sent
again) runs once and is answered again, and one that arrives late still runs once.

**With `link_key`** (the same on every board of the group; set-only, never published or logged; sent empty from
the serial console or `set_params` it is removed): every frame is signed, unsigned or wrongly signed frames are
dropped, and a request is taken only when it is bound to the receiver's nonce for this boot. An unbound request
(the first one to a board, or any `tell all`) is answered with a signed challenge carrying the receiver's nonce;
the sender sends the request again bound to it and remembers the nonce for the next tells. A frame recorded on the
network and sent again is dropped: it is bound to another nonce (or none, and gets only a challenge nobody can
answer without the key), or its number was taken already. A receiver that has to forget a sender (more than 8)
takes a new nonce. **Without a key nothing is signed and anyone on the network can send tells**, as anyone on it
can use the board's page with the page lock off.

ESP-NOW reaches ESP32 boards on the same channel with no router; UDP reaches every Wi-Fi board (ESP32, Pico W,
Pico 2 W) joined to the same Wi-Fi network. **Not for boards joined to another board's hotspot**: every board's own
hotspot is 192.168.4.1/24, so a board joined to another's has two networks at 192.168.4.x and its answers go to
itself (ESP32 boards still reach each other by ESP-NOW; Pico W boards then cannot use the board link peer to
peer). Not yet tried on hardware: the UDP broadcasts (ESP32, Pico W) and ESP-NOW frames.

## Firmware updates

`POST /api/v1/update`, `Content-Type: application/octet-stream`, the image as the body:

| Header | |
|---|---|
| `Content-Length` | required; above `info.update.max_bytes` is error `invalid` before anything is written |
| `X-Proptroller-Sha256` | required: SHA-256 of the body, 64 hex digits |
| `X-Proptroller-Pin` | the page password, while the page lock is on |

The image is what the release lists for this board (`mcu`) and implementation: ESP32 an ESP-IDF app image
(magic `0xE9`, chip ESP32), RP2040 / RP2350 the raw binary of the app. The answer is one response envelope
(HTTP 200, `Connection: close`); a refusal is sent after the rest of the body has been read and dropped, so a
client that is still sending is not cut off. CORS allows the two `X-Proptroller-…` headers.

* Checked in this order before anything is written, with these exact messages:
  1. no update support: `unsupported`, `this board takes no over-the-air updates`;
  2. page lock on and no or a wrong `X-Proptroller-Pin`: `pin`, the usual page-lock message;
  3. `bad_request`: `Content-Type must be application/octet-stream`, `Content-Length is required`,
     `X-Proptroller-Sha256 must be the SHA-256 of the image as 64 hex digits`;
  4. `refused` while something runs (first that applies): `another update is running`,
     `the running firmware has not finished its self-check yet: try again in 30 s` (state `pending`),
     `a manual output test is on: release it first`
     (an output or the pixel test), `a show is running: stop it first`,
     `the pulse-and-recover effect is running: wait for it to finish` (a run in progress, not merely on),
     `the show script is running an effect: wait for it to finish` (an effect past its `wait for`, not
     merely stored);
  5. `invalid`: `the image is empty`, `the image is too big: 2000000 bytes, this board takes at most 1966080`.
* During the write the board is in update mode: outputs off, triggers and radio cues ignored, every request
  that changes something answered `refused`, `a firmware update is running`. `state.update` and the event
  `update` (see [Events](#events)) report progress.
* `invalid` when the image is not for this board (`not an image for esp32-devkit`: checked on its first
  bytes), or its SHA-256 does not match (`the image's SHA-256 does not match X-Proptroller-Sha256`). A
  connection that ends early stops the update (`the connection ended before the whole image arrived`, in
  the `failed` event). The running image is never touched; the board leaves update mode.
* `done`, then the board restarts about 1 s later into the new image. The new image boots in state `pending`
  and marks itself `valid` after its self-check: settings and shows loaded, the show script checked, and 30 s
  of normal running. A stored record that is there but cannot be read fails the self-check: the board then
  restarts into the previous image. A reset or crash before that boots the previous image again, with
  `rolled_back: true`.

`info.update.slot` is the running app slot (`app0` / `app1` on the ESP32 and the simulator; `active` on
the Pico W and Pico 2 W, which sit behind the Pico bootloader: an update is written to the dfu slot and the
bootloader swaps it in at the restart, and back when it does not pass its self-check). The Pico boards' image
check: a raw image linked for the active slot (its vector table at the start, the reset handler inside the
slot) carrying the markers `PROPTROLLER-MCU:<mcu>` and `PROPTROLLER-IMPL:cpp` (FLASH-MAP.md). On those boards
the swap runs before the new image starts: about 16 s, up to about 2.5 minutes with slow flash
(`info.update.install_max_s` 180), so a client waits up to 5 minutes for the board to come back (the phone page
says "the board is installing the update, this can take up to 3 minutes"). The simulator models it with
`--swap-ms N`.

**Simulator**: `firmware/arduino/tools/ptclient.py image OUT.bin --version 0.2.0 --build abc1234 [--fail-self-check]` writes a
simulator image (a `PROPTROLLER-SIM-IMAGE` header line, a JSON line, filler) and prints its SHA-256; after the
update the simulated board reports that version and build. With `--fail-self-check` (or
the simulator's own `--fail-self-check` for every image) the self-check fails 30 s after the restart and
the board rolls back. Any other image is `not an image for sim`.

Stored settings, shows, the script and the profile survive an update (FLASH-MAP.md); a newer firmware reads
the records of every older release. Boards without the `update` capability (the plain Pico) are updated
over USB by the desktop app.

## Releases

A release publishes its images and one manifest, `proptroller-firmware.json`:

```json
{"version": "0.2.0",
 "images": [{"fw": "generic", "impl": "cpp", "mcu": "esp32-devkit", "board": "proptroller",
             "file": "generic-esp32-devkit-0.2.0.bin", "bytes": 1119456, "sha256": "…64 hex digits…"}]}
```

`fw` is the firmware's name (`info.fw`: `generic` for the stock firmware, a workspace project's folder name for
one's own, set with `-DPROPTROLLER_FW_NAME`), `impl` its implementation (`cpp`), `mcu` the board's `info.mcu`,
`board` its `info.board` (an entry or a board without `board` counts as `proptroller`).
A client compares `fw_version` with `version` and offers the image whose `fw`, `impl`, `board` and `mcu` match the board;
the update image is `firmware.bin` (`POST /api/v1/update` with its `sha256`).

First-flash images (USB), one per board, listed in the same manifest with `"first_flash": true`:

| Board | File | Written |
|---|---|---|
| ESP32 DevKit | merged bin: bootloader, partition table, otadata and firmware (`esptool.py merge_bin` of the files in FLASH-MAP.md) | from 0x0 |
| Pico W, Pico 2 W | `flash.uf2`: bootloader and firmware (`tools/picoboot_image.py merge`) | BOOTSEL drive |
| Pico | `firmware.uf2` | BOOTSEL drive |

The update images come first in `images`, the first-flash entries after them, so a client that takes the first
entry whose `fw`, `impl` and `mcu` match gets the update image. The stock release
(`.github/workflows/release.yml`, on a tag `v<firmware/VERSION>`; packaged by `firmware/arduino/tools/release.py`)
names them `generic-<mcu>-<version>.bin` (update) and `first-flash-esp32-devkit-<version>.bin`,
`first-flash-pico-w-<version>.uf2`, `first-flash-pico2-w-<version>.uf2`, `first-flash-pico-<version>.uf2`, for the
ESP32 DevKit, the Pico W, the Pico 2 W and the plain Pico (which has a first-flash entry only: it has no update
capability and takes a newer `firmware.uf2` over USB). The images are not signed: `sha256` is a transfer check. The browser
flasher for the ESP32 (ESP Web Tools) is published to GitHub Pages with its own copy of the first-flash image,
because release downloads carry no CORS headers.

## Backups

`get_backup` returns everything a board needs to do what it does now, as one JSON document. The desktop app
saves it as a `.proptroller.json` file (local folder, GitHub, Google Drive) and restores it to the same or
another board with `put_backup`:

```json
{"format":"proptroller-backup","format_version":1,
 "from":{"id":"a0b1c2d3e4f5","name":"Front door","fw":"generic","fw_version":"0.1.0","fw_impl":"cpp","mcu":"esp32-devkit","board":"proptroller"},
 "name":"Front door",
 "settings":[{"key":"debounce_ms","value":100}],
 "shows":[{"id":1,"name":"Doorbell","enabled":true,"tracks":[]}],
 "script":{"text":"effect1: ...","sounds":{}},
 "profile":null}
```

* Secrets (Wi-Fi password, page password, `ap_pass`) are never in a backup. Restoring keeps the target
  board's Wi-Fi and page lock: `settings` also leaves out `wifi_ssid`, `page_lock`, `name` (it is the
  top-level `name`) and `remote_trigger_enable` (never stored). A backup that lists one of these anyway gets
  it back in `skipped`, unapplied. `settings` uses the generic keys (never profile aliases), in `get_params`
  order; `shows` are in id order; `script` and `profile` are `null` when the board has none.
* `settings` lists every non-secret setting with its value; `put_backup` checks all of them, the shows and
  the script with the target's rules first, and changes nothing when one fails. Keys the target does not have
  are left out and listed in `skipped`. The shows replace all of the target's shows. The first problem is the
  `invalid` message, in this order: `not a Proptroller backup (format must be "proptroller-backup")`,
  `the backup is format version 2; this firmware reads up to 1: update it first`, `settings: <the
  set_params message>`, `name: <reason>` (with `keep_name: false`), `show <id>: two shows have this number`,
  `show <id>: <the put_show message>`, `the backup has <n> shows; this board takes at most <m> shows` (16, less
  the sketch's own shows), `profile: <the set_profile message>`,
  `the shows do not fit this board's storage`, `script: <its first check error>`,
  `script: the script and its sound list do not fit the script store`.
* `put_backup` with `keep_name: true` (the default) keeps the target's own name.
* `get_backup` on a board that was just restored from a backup gives the same document again apart from
  `from` (and `name` when it was kept): the round trip is exact.
