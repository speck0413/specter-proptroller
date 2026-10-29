---
name: proptroller-web-app
description: The phone web UI (firmware/web, served by the C++ firmware and its simulator, with its browser tests in firmware/web-test) and Specter Studio (studio/, Tauri 2 + React/MUI with a Rust core and Playwright e2e) - layout, test commands, and UI rules such as momentary buttons and device-supplied labels. Load before editing firmware/web, firmware/web-test, studio/, or their tests.
---

# Proptroller phone page and Specter Studio

## Where
- Phone UI: `firmware/web/` (plain HTML/CSS/JavaScript, no build step: `app.js`, `blocks.js`, `record.js`,
  `scriptview.js`, `take.js`, `words.js`). The C++ firmware embeds it (`firmware/arduino/tools/web_assets.py` writes
  the committed `web_assets.h`; never edit that by hand). One UI for every board and the simulator.
- Phone page tests: `firmware/web-test/` (`ui-test.mjs`, `settings-test.mjs`, `live-test.mjs`, `script-test.mjs`,
  `record-test.mjs`, ... playwright-core, `CHROMIUM=`). Its README lists each test and its arguments.
- Specter Studio: `studio/` (Tauri 2). `core/` the device layer in Rust (no Tauri; `src/bin/bridge.rs` serves it to a
  browser; discovery is UDP 47800 + mDNS `_proptroller._tcp` in `discover.rs`), `src-tauri/` the thin shell, `src/`
  the React + MUI app (`src/pages/`, `src/effects/` the editor: Monaco language, Blockly blocks, C++ preview),
  `plugins/` an example plugin, `tests/e2e/` Playwright. Read `studio/README.md` (commands, layout) and
  `studio/docs/BUILDING.md` (installers, CI, releases); `studio/docs/DESIGN.md` is the design.
- Studio builds against this repo's `firmware/`: its practice board is the firmware's own simulator
  (`studio/scripts/build-sim.sh`; run it again after a firmware change).

## Commands
```sh
# phone page: against a simulated board (firmware/web-test/README.md)
cd firmware/arduino && pio run -e sim && .pio/build/sim/program --port 18080 --discovery-port 0 &
cd firmware/web-test && npm install && CHROMIUM=/path/to/chromium node ui-test.mjs . http://127.0.0.1:18080/
node --test take-test.mjs blocks-test.mjs words-test.mjs   # no board, no packages
firmware/arduino/tools/check-host.sh                       # everything above, each test on a fresh board

# Studio, from studio/
scripts/setup.sh                 # once: tools, Rust, npm ci, the practice board
npm run tauri dev                # the desktop app; or `npm run bridge &` and `npx vite` for a browser
cargo test                       # device layer against the simulated board (incl. core/tests/checker_parity.rs)
npm test                         # vitest: blocks round trip on every golden case, language, C++ preview
npx playwright test              # the whole app in Chromium (CHROMIUM=/path/to/chrome)
npm run build                    # tsc --noEmit + vite build
```

## Rules
- Labels, settings and roles come from the device (its parameter descriptions); the UI hard-codes no effect-specific
  words, so a project's own firmware can reuse it unchanged.
- Buttons that stand for inputs are momentary: held = closed, released = open (pointer events with capture), released
  on blur, page hide and connection loss. Studio's "Hold to test" owes a let-go until the board takes it
  (`studio/src/held.ts`).
- Studio's script checker is the board's own (the simulator). The show-script golden set
  (`firmware/show-script-golden/`) must read the same in the firmware, `checker_parity.rs` and
  `studio/src/effects/parity.test.ts`; `studio/scripts/checker-parity.sh <ref>` shows what another commit's set
  changes. A firmware change and the Studio change that goes with it are one commit.
- `firmware/web/take.js`, `blocks.js` and `words.js` are shared with Studio: keep them free of page-only code.
- A remote trigger enable given from a page lasts only while that page's WebSocket session lives
  (`firmware/STATE-MACHINE.md` SM-R2); the page shows a queued run with Cancel and "cooling down, m:ss left".
- Page lock (`page_lock`) is off by default; when on, the password is checked before any save, enable or restart.
- Secrets are entered, never shown again or read back. The phone page works at phone width (16 px gutter, no
  horizontal scroll).
- Screenshots for reports come from the browser tests, not hand-made images.
- A project's own words come from its profile's alias table (`firmware/STATE-MACHINE.md` §11): labels, units, key
  and topic names. The UI shows them; the public tree carries only generic names and the example table.
