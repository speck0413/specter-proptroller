# Wiring guides

Short, safe how-tos for the hookups people need first: a button, a lamp, a motion sensor, a speaker, and a starter
kit that wires them all at once. Each guide comes three ways, from one set of step data:

- **Interactive** (start here): a live 3D scene of the board and the case that you can turn, move and zoom, with
  Next / Back steps, the step in big plain words, and a voice that reads it. Open
  [`html/index.html`](html/index.html) in a browser (it works from disk, offline). The same folder is what the desktop
  app shows under Help > Wiring guides.
- **Written**: one page each (below), with a parts checklist, the steps, a test, "Not working?" and the words used.
- **Video**: an MP4 per guide, rendered from the same scene and narration (not stored in git; see below).

> **Low voltage only.** These guides never show mains (wall power) wiring. To switch something that plugs into the
> wall, see `../ac-control.md` (advanced). Fog machines only over their **DMX IN** socket (guide 6), never their wired
> remote.

| | Guide | Time · tools |
|---|---|---|
| **Start here** | [Meet your board and connect to it](00-meet-your-board.md) (how to open the board's page on your phone) | 10 min · no tools |
| **Start here** | [Powering the board safely](08-powering-the-board.md) | 10 min · no tools (a meter) |
| **Start here** | [The starter kit: your first hour](starter-kit/README.md) ([make it bigger](starter-kit/make-it-bigger.md)) | about an hour · screwdriver |
| 1 | [A push button on TRIG1](01-button-on-trig1.md) | 5 min · screwdriver |
| 2 | [A 12 V lamp or LED strip on OUT1](02-lamp-on-out1.md) | 10 min · screwdriver |
| 3 | [A motion sensor on TRIG2](03-motion-on-trig2.md) | 15 min · screwdriver |
| 4 | [A speaker and sound files](04-speaker-and-sounds.md) | 15 min · screwdriver |
| 5 | [A pixel strip on PIXEL](05-pixel-strip.md) | 20 min · screwdriver; soldering only for a strip without a plug lead |
| 6 | [Fog on cue: a fog machine with DMX IN](06-fog-over-dmx.md) (firmware v0.1.0-rc.1 or later, not yet tried on hardware) | 30 min · screwdriver; soldering if the expansion pins are not fitted |
| 7 | [A 12 V air valve on OUT1](07-air-valve-on-out.md) | 15 min · screwdriver; soldering only if the diode is not fitted |
| 9 | [Chaining two boards](09-chaining-two-boards.md) | 5 min · no tools |
| 10 | [A 12 V pump or motor on OUT1](10-pump-or-motor-on-out.md) | 10 min · screwdriver |

Each interactive page is `html/<guide>.html` (for example `html/01-button-on-trig1.html`); each video renders to
`video/out/<guide>.mp4`.

The guides follow the board as it is on `main` (rev B, the standard case). **Every board built so far is rev A**: its
silkscreen says "RELAYS: <=30V DC ONLY", and its relays are for low-voltage DC only. Ratings and terminal names come from
`hardware/README.md`, `hardware/board.ts` and `enclosure/README.md`; nothing here has been tried on a real board yet.

Every guide has the same shape: what you will build (time, tools, bare board or case), you need (a checklist), before
you start (the same safety box everywhere plus the guide's don'ts), find it, wire it (one connection per step), tug
test, tell the board (the exact phone-page path), test it (the exact tab, card and button, and what you should see),
use it in a show, not working?, next.

## In the interactive pages

- Drag to turn, right-drag (or two fingers) to move, wheel (or pinch) to zoom, double-click a part to look at it.
  **Reset view** goes back to the step's view; **Show case** / **Hide case** shows or hides the case.
- Keys: left / right arrow step, Space listens, A D turn, W S tilt, + and - zoom, R resets the view.
- **Listen** reads the step; **Play all** reads every step and moves on by itself (2.5 s after each step).
- On the first step, pick **Bare board** or **In the case**: the steps that differ follow your choice (remembered).
- Dotted words open a short meaning. Flat 2D cards show what the 3D scene is too small for: the plug's three screws,
  a diode's stripe, a meter reading, what the phone page should show.
- With "reduce motion" set in the system, the camera jumps instead of flying and wires appear at once. The step text
  is announced to screen readers.
- A step can be linked directly: `01-button-on-trig1.html#step=5` (the step number in the full list).

## Changes (2026-10-22)

From a beginner UX review and an electrical-safety review:

- **Guide 6 replaced.** "A fog machine remote on RLY1" is gone: on many fog machines the wired remote carries mains.
  The new guide 6 drives a fog machine through its **DMX IN** socket with a MAX485 module on expansion S4 (firmware
  v0.1.0-rc.1 or later, `dmx_port = s4`; not yet tried on hardware). Fog is no longer suggested on a relay-controlled outlet
  (`../ac-control.md`, `starter-kit/make-it-bigger.md`): switching its wall power also switches its heater.
- **Guide 7 reworked** around a DC 12 V air valve (coil must say DC 12V; diode pre-fitted or soldered; OUT1 max on
  watchdog for push solenoids). **New guide 10** for pumps, motors and pop-ups (start-up surge, soft start, direction).
- **New start page** "Meet your board and connect to it": which edge is the front, what each terminal is for, joining
  the board's `Proptroller-XXXX` Wi-Fi (password `password`, a simple default to change in Settings → Wi-Fi) and opening `192.168.4.1`, STOP and Auto. Every Test
  step links to it.
- **Every guide restructured** to the template above, with time and tools on the first step, a bare / case choice,
  a parts checklist, the safety box before any wiring, and "Not working?" and "Next" steps in the interactive page.
- **Exact phone-page paths**: `Settings > Inputs > TRIG2 input type > sensor_high > Save settings` (the setting needs
  firmware v0.1.0-rc.1 or later); outputs are tested with Live > Outputs > On, then **Auto**; sound with Live > Sound > Folder 1,
  Track 1, Play; pixels with Live > Pixels > Show.
- **Safety and real-world fixes**: screw plugs marked L / R / G; PIR needs a minute after power-on; a full 5 m LED reel is
  more than one OUT; single-colour strips only; 24 V PWM limit; the extra pixel resistor dropped (the board has one);
  both pixel supplies switch together; supply into PWR only in the case; centre-negative pedal adapters and supplies
  above 24 V warned off; the case's OUT and SPK jacks are checked with a meter or skipped for the terminals; chaining
  needs board two's TRIG1 on `switch`.
- **Soldering is said up front**: pre-fitted parts first (plug-lead pixel strips with a screw block for the capacitor,
  valves with the diode fitted, buttons with a plug), and an honest soldering path where it is needed.
- **3D**: labels no longer overlap or leave the view (moved apart, with leader lines), the camera backs off for narrow
  views and phones, darker case lettering, quicker wire drawing.
- **Controls and access**: Show / Hide case button, keyboard orbit, Space to listen, step dots with labels, a one-time
  touch hint on phones, reduced motion, screen-reader announcements.
- **Narration**: slower (about 135 words a minute) with longer pauses; WS2812, AM312, HC-SR501, 1N4007, COM, 5V, DMX
  read correctly.

## Building the guides

Everything builds offline once the tools are in place. In `video/`:

```
npm ci                 # three.js, esbuild, Remotion, gltf-transform (into node_modules)
npm run narrate        # Piper -> narration WAV (.cache/) and Opus (../html/audio/), step lengths in src/generated/
npm run html           # the offline pages and their one-file bundle in ../html/
build/posters.sh       # the poster thumbnails in ../html/posters/ (headless Chrome)
node build/markdown.mjs   # the written pages NN-*.md from the same step data
npm run render         # the MP4s into video/out/ (Remotion, headless Chrome, ffmpeg); one: npm run render -- 01
npm test               # narration format test: Opus, the AAC fallback, and Opus-fails-so-AAC (Chromium, WebKit)
```

- **Browser test**: `npm test` opens a page from disk in Chromium and, when it is installed, WebKit (the engine of Safari
  and of the desktop app on macOS and Linux), and checks the narration plays as Opus, as AAC when Opus is not offered,
  and as AAC when an Opus file fails to decode. Browsers: `npx playwright-core install chromium webkit`. On a Linux host
  missing one of WebKit's system libraries (it names it, e.g. `libmanette-0.2-0`), install that package, or unpack it
  without root (`apt-get download`, `dpkg-deb -x`) and run with `LD_PRELOAD=<its .so>
  PLAYWRIGHT_SKIP_VALIDATE_HOST_REQUIREMENTS=1`; `SKIP_WEBKIT=1` runs Chromium only.
- **Video size**: `npm run render` encodes with x264 `slow` at CRF 28 and 64 kbit/s AAC (slide-like scenes compress
  well); `CRF=`, `X264_PRESET=`, `AUDIO_BITRATE=` override it.

- **Narration** uses [Piper](https://github.com/rhasspy/piper) (offline neural text-to-speech) with the
  `en_US-ljspeech-high` voice (trained on the public-domain LJ Speech recordings). Put the `piper` release folder and the
  voice in `~/.cache/proptroller-guides/` (`piper-dl/piper/piper` and `voices/en_US-ljspeech-high.onnx` plus its `.onnx.json`), or point `PIPER` and
  `PIPER_VOICE` at them. The narration text is each step's `say` in `video/src/guides/*.js`; the Opus files (24 kbit/s
  mono) are committed so the pages work without building.
- **3D models**: `tools/guides/export-models.sh` rebuilds the board artwork (`video/assets/board-top.webp`, from the
  KiCad board), the board's connector positions (`video/src/scene/board-data.json`) and the case
  (`video/assets/case.glb`, from the OpenSCAD model, simplified and meshopt-compressed). Run it after the board or the
  case changes.
- **Where things live**: the shared scene is `video/src/scene/` (board, case, the parts you connect, wires), the guides
  are `video/src/guides/` (parts, wires, steps; `template.js` is the shared shape, `glossary.js` the words), the page
  player and the 2D inset cards are `video/src/player/`, the video compositions are `video/src/remotion/`.
- **Narration** removes the audio of steps that no longer exist and prints each guide's words per minute.
