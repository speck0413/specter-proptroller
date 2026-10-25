# Record effect

Make an effect by doing it once: you work the board's outputs by hand, in time, and the board remembers it
as an **effect script** (the short text language of `../SHOW-SCRIPT.md`). There is one recorder, called
**Record effect**, and it works the same on the board's phone page and in Specter Studio. What it saves is an
ordinary effect script, so afterwards you can change any time, level or sound as text (the phone page's
**Script** tab, Studio's code view) or as blocks (Studio's blocks view).

## Record on the phone

1. Open the board's page and tap the **Record effect** tab.
2. **Starts when this is pressed**: pick the input that will start the effect later (TRIG1, TRIG2, or one of
   the USER1-USER3 buttons on the board). An input set to something else (TRIG2 set to pause the board) is
   listed but cannot be picked.
3. **Outputs to use**: tick the outputs this effect drives. OUT1-OUT4 are marked "dims": they get a level
   slider as well as a Hold button. RLY1, RLY2 (and CC1, CC2 when they are not used for something else) are on
   or off only. Outputs that have another job in Settings, or belong to the built-in effect, are not offered.
4. **Sounds to use** (boards with a sound player): tick the sounds you want buttons for. Sound N is the one file
   in the card's folder `NN` (`01/001-….mp3` is sound 1; Studio's SD card tool fills the card).
5. Leave **Count 3, 2, 1** on if you want a moment to get ready, and press **Start**.
6. Now do the effect. The board follows you live:
   - **Hold: on** turns the output on for as long as you hold it. Tap it for a short blip.
   - The **Level** slider (dimmable outputs) sets how bright or fast it runs, 0-100 %. Slide it smoothly for
     a fade.
   - **Play sound N** starts that sound.
   - Each control shows what the board reports (`board: 60 %`).
7. Press **Stop**. The board does the whole thing back once, so you can see and hear it.
8. Then choose:
   - **Play it again**: the board does it back once more.
   - **Try again**: throw it away and start over.
   - **Add more on top**: record another pass; what you already recorded plays on the board while you add to
     it (good for doing a light and a relay with one thumb each).
   - **Keep**: give it a name and save it on the board. From now on, pressing the input you picked runs it.

**Cancel** is on every screen (and Escape does the same): during the 3, 2, 1, while recording, while the board
does it back and on the review screen. It stops at once and lets go of every output the recording touched, so
each one goes back to what it did before (off, or what its show was doing). If something was recorded, the page
shows it and asks **Discard this recording?**: **Discard** throws it away and nothing is saved; **No, go back**
keeps it for review. With nothing recorded it just goes back to the start (from the first screen: to the Live tab).

**STOP** in the header always works. It, a pause, putting the phone away (the page hidden), and a dropped
connection end the recording: everything is let go and turned off, and what you recorded so far is kept for
you to review. An output never stays on longer than its **maximum on-time** (Settings): the recorder turns it
off at the limit even while you hold the button, and the saved effect says so.

**Keep never touches your other effects.** The new effect is added to the board's stored script. If an effect
with the same name is already there, the page asks before it replaces it. Each output belongs to one effect
(SHOW-SCRIPT.md SS-R8): if the stored script already uses one of your outputs in another effect, the page says
which, and nothing is saved; record without that output, or change the other effect first.

## Record in Specter Studio

On the board's page, choose **Make it do something… → Record effect**. The steps are the same: pick what starts
it, Start, work the big buttons (keys 1-8 for the outputs, Q W E R for sounds, Space to start and stop), Stop,
and the board does it back. **Cancel** (or Escape) works as on the phone. **Keep it** saves the effect in your library and sends it to the board. The
phone page turns a recording into a script with the shared module `firmware/web/take.js`, made for Studio to
import from this firmware so the same take gives the same script in both (until Studio switches to it, its own
copy of the rules has no level sliders).

## How a recording becomes a script

- Times are rounded to 0.1 s and written as `wait` lines between the moments something changes. A very
  short tap still lasts 0.1 s; two taps of one output that touch or overlap become one.
- A held button is `outN = on` then `outN = off`. A slider is `outN = 40%` lines, in 5 % steps and at most one
  every 0.1 s; a smooth movement becomes `fade outN to 100% over 0.7` when nothing else happens during it (a
  fade takes its time, like `wait`, so the other lines wait for it).
- Relays and contacts are on or off only (`rly1 = on`); they never switch faster than 0.1 s.
- A sound is `audio_single = N`, its number.
- The quiet bit before you first touched anything is left out, and everything is off at the end.
- A long take that does not fit one effect (64 lines) is split into one effect per output (plus one for the
  sounds), all waiting for the same input, so they still start together. A slider that moved very often is
  written in coarser steps, and a note says so. What is too big for a board (8 effects, 256 lines, 8 KB) is
  refused in plain words; nothing is saved.

## A worked example

A porch scare on TRIG1 (a doorbell button): OUT1 is the porch lamp (named `lamp` in Settings, Wiring), RLY1
presses a store-bought prop's "try me" button, and sound 2 is a scream.

1. Record effect: pick **TRIG1**; tick **lamp (OUT1)**, **prop (RLY1)** and **Sound 2**; Start.
2. Slide the lamp to about 30 %, wait, then push the slider up to full quickly; as it gets there, tap
   **Play sound 2**, then hold **prop** for half a second; wait a moment and press **Stop**.
3. The board does it back. Name it `porch scare` and press **Keep**. The Script tab now has:

```
# Recorded on the phone page (Record effect): when TRIG1 is pressed, the board does what you tapped.
# Change anything you like, as text or as blocks.
effect porch scare:
  wait for trig1
  out1 = 30%
  wait 1.6
  out1 = 40%
  fade out1 to 100% over 0.7
  wait 0.2
  audio_single = 2
  wait 0.1
  rly1 = on
  wait 0.5
  rly1 = off
  wait 1.6
  out1 = off
```

4. Tweak it on the **Script** tab (**Change it on the Script tab** after Keep opens it there). Pick the view at
   the top: **Script only** (the text), **Blocks only** (one coloured block per line: change a value in place,
   move with ↑ ↓, remove with ✕, add a line), or **Script and blocks** (both; each changes the other). The phone
   remembers your choice. In Studio, use its code or blocks view the same way:
   - the lamp should glow longer before the scare: change the first `wait 1.6` to `wait 4`;
   - it should start dimmer: change `out1 = 30%` to `out1 = 15%`;
   - add a rest so the prop is not set off twice in a row: put `wait 20` as the last line (nothing restarts
     an effect until its last line is done, SHOW-SCRIPT.md SS-R2).
5. Press **Check**, then **Save**. Press TRIG1: the board runs your changed effect.

The comments at the top are only notes; delete them if you like. Recording again with the same name replaces
this effect (the page asks first), so after you have tweaked it by hand, record under a new name instead.

## For developers

- `firmware/web/take.js`: one framework-free ES module of pure functions. `takeToScript(take, options)` →
  `{ text, error, notes, effects, steps, segments, sounds, lengthMs, timeline }`; `plan`, `suggestName`,
  `mergeTakes`, `mergeScript(stored, recorded, name, names)` → `{ text, replaced, clashes }`, `replacedBy`,
  `outputsByEffect`, `effectsIn`, `dimmable`, `seconds`, `LIMITS`. A take is
  `{ holds: [{ target, start, end, cut?, heldMs? }], sounds: [{ sound, at }], levels: [{ target, at, level }], end }`
  (ms from the start, levels in percent, lower-case targets). Unit tests: `node --test firmware/web-test/take-test.mjs`.
- `firmware/web/blocks.js`: text ⇄ blocks without Blockly (`textToBlocks`, `blocksToText`, `parse`, `lineOf`,
  `newBlock`, `KINDS`, `ADDABLE`), the same rules and words as Studio's Blockly blocks; `scriptview.js` draws it on
  the Script tab. Tests: `blocks-test.mjs`, `views-test.mjs`.
- `firmware/web/record.js`: the phone page's recorder (an ES module next to `app.js`); browser test
  `firmware/web-test/record-test.mjs`.
- The board has no recorder of its own: live control is `set_output` (a phone page's held outputs end with its
  connection), saving is `get_script` / `check_script` / `set_script` (PROTOCOL.md). The older board-side
  recorder (`record_start` … `record_abort`, the USER-button learn mode) is gone; a show it made stays a normal show.
