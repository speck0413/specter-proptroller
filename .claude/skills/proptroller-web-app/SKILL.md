---
name: proptroller-web-app
description: The phone web UI (served by every Proptroller firmware and the simulators) and the Tauri desktop app (discovery, New controller wizard, profiles) - layout, tests, and UI rules such as momentary buttons and device-supplied labels. Load before editing firmware/rust/web, app/, or their browser tests.
---

# Proptroller web UI and desktop app

## Where
- Phone UI: `firmware/rust/web/` (plain HTML/CSS/JavaScript, no build step). Embedded by the Rust firmware
  (`proptroller-web`) and by the C++ firmware (`tools/web_assets.py`). One UI for every firmware.
- Browser tests: `firmware/rust/web-test/` (`ui-test.mjs`, `settings-test.mjs`, playwright-core, `CHROMIUM=`).
- Desktop app: `app/` (Tauri 2). `app/src-tauri` (Rust commands, discovery: UDP 47800 + mDNS `_proptroller._tcp`),
  `app/ui` (list + New controller wizard), `app/ui-test/wizard.mjs`. Read `app/README.md` for commands.
- Profiles for the wizard: `firmware/rust/crates/proptroller-provision/profiles/`.

## Rules
- Labels, settings and roles come from the device (its parameter descriptions); the UI hard-codes no effect-specific
  words, so a private firmware can reuse it unchanged.
- Buttons that stand for inputs are momentary: held = closed, released = open (pointer events with capture), released
  on blur, page hide and WebSocket loss; the simulator also times out a stale hold.
- A remote trigger enable given from a page lasts only while that page's WebSocket session lives
  (`firmware/STATE-MACHINE.md` SM-R2); the page shows a queued run with Cancel and "cooling down, m:ss left".
- Page lock (`page_lock`) is off by default; when on, the password is checked before any save, enable or restart.
- Secrets are entered, never shown again or read back. Works at phone width (16 px gutter, no horizontal scroll).
- Screenshots for reports come from the browser tests, not hand-made images.
- A project's own words come from its profile's alias table (`firmware/STATE-MACHINE.md` §11): labels, units, key
  and topic names. The UI shows them; the public tree carries only generic names and the example table.
