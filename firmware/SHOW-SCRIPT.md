# Show script: the beginner language

Implemented by the firmware (`arduino/lib/proptroller/src/proptroller/script.cpp`).

A show script is a short text file that reads like a config file. Each effect waits for an input, then turns
outputs on and off, waits, and plays sounds. The board stores the text, checks it when it is saved, and runs it.
Nobody compiles anything. You can write it in the board's web page, in the desktop app as text, or in the desktop
app with drag-and-drop blocks.

Everything else (run limits, rest windows, phone triggers, recover, page lock, aliases) is the **advanced layer**:
see `STATE-MACHINE.md` and the firmware docs. The board's protections apply to a script without being written.

## 1. A first look

```
effect porch light:
  wait for trig1
  out1 = on
  audio_single = 1     # plays /01/001.mp3 from the card
  sleep 30
  fade out1 to 0% over 2

effect prop:
  wait for trig2
  rly1 = on        # presses a store-bought prop's button (§7)
  sleep 3
  rly1 = off
  sleep 60
```

Two effects, each with its own trigger. They run at the same time and never wait for each other.

## 2. Writing rules (SS-W)

- **SS-W1 Case does not matter.** `Effect1:`, `EFFECT1:` and `effect1:` are the same. So are `OUT1`/`out1`
  and `Sleep`/`sleep`. Only **file names** keep their case (`Shot.mp3` is not `shot.mp3`).
- **SS-W2 Spaces and indentation do not matter.** `out1=on`, `out1 = on` and `   out1 =on` are the same. Tabs
  count as spaces. Indenting the lines of an effect is optional and only for reading.
- **SS-W3 `=` or `:`** both work for a value: `out1 = on`, `out1: on`.
- **SS-W4 Phone typing is cleaned up first.** Smart quotes, non-breaking spaces, a trailing period and Windows
  line endings are turned into plain text before the script is checked.
- **SS-W5 Comments** start with `#` and run to the end of the line. Blank lines are ignored.
- **SS-W6 Times are in seconds** unless a unit follows: `sleep 0.4`, `sleep 200 ms`, `sleep 3 min`. Accepted
  units: `ms`, `s`, `sec`, `second(s)`, `min`, `minute(s)`. Decimals are fine.
- **SS-W7 A level is one of three things:** `on` (100 %), `off`, or a percentage such as `50%`. Example: `out1 = 50%`.

## 3. Lines (SS-L)

| Line | Meaning |
|---|---|
| `effect <name>:` | Starts an effect. The name is anything after `effect` (`effect1`, `effect porch light`). Every line below belongs to it until the next `effect …:`. |
| `wait for <input>` | Waits until that input is pressed (rules SS-R3, SS-R4). Inputs: `trig1`, `trig2`, `user1`–`user3`. |
| `<output> = on` / `off` / `N%` | Sets an output. Outputs: `out1`–`out4` (the board's supply voltage, dimmable up to 2 A at 12 V), `rly1`, `rly2`, `cc1`, `cc2` (button presses, on or off only; §7). |
| `dmx<N> = on` / `off` / `N%` | Sets DMX channel N (1–512) through the DMX add-on (§7.4). |
| `sleep <time>` | Waits that long. |
| `fade <output> to N% over <time>` | Changes a dimmable output smoothly to N %. |
| `flicker <output> for <time>` | Flickers a dimmable output at random like a failing bulb, then returns it to its level before the flicker. |
| `audio_single = <file>` | Plays a sound once. |
| `audio_loop = <file>` | Plays a sound over and over (SS-R7). |
| `audio_stop` | Stops the sound. |

That is the whole beginner language: nine kinds of line.

**SS-N Names you gave the terminals** (owner approved 2026-10-22). Where a line takes an output or an
input, it also takes the name the board knows that terminal by: the wiring name (setting `out1_name` …
`user3_name`, "OUT1 is connected to", what the app and the phone page call the terminal's name) or the
profile's terminal alias. With OUT1 named `Lamp`, `lamp = on`, `fade lamp to 0% over 2` and `wait for bell`
work, in any case, and give the same table as `out1`. The messages show the terminal beside the name
(`fog (rly1) can only be on or off, not 50%.`). A name works in a script when it is one word of letters,
digits and `_` starting with a letter, is not a word of the language (`sleep`, `fade`, `on`, `to`, anything
starting with `effect` or `wait`, …) and belongs to one output (or one input) only; the terminal's own name
always wins (OUT4 named `trig1` gives a warning, and `trig1` still means TRIG1). Other names stay labels,
and a script that uses one is told why in plain words. The stored text is never rewritten (SS-C2): renaming,
removing or moving to another terminal a name the stored script uses is refused like any other setting that
would break it (`out1_name: the stored script uses lamp. Change the script first.`), and a script whose name went some other
way (a settings reset) is kept but does not run until the name is back or the script is changed.

## 4. How it runs (SS-R)

- **SS-R1 Each effect is its own loop.** At power-on, every effect sits at its first `wait for`. When it starts it
  runs its lines top to bottom, and after the last line it goes back to the top.
- **SS-R2 Busy until the last line finishes.** While an effect is running, presses of its trigger are ignored and
  never saved for later. A `sleep` at the end of an effect therefore works as a rest time: nothing restarts
  it until the sleep is over.
- **SS-R3 Pressed means pressed and held.** An input counts as pressed after it has been closed for the debounce
  time (100 ms, `trig_debounce_ms`) without opening, same as SM-T1a.
- **SS-R4 Release first.** To start again, the input must have been released **after the effect returned to its
  `wait for`**. Holding the button down never restarts an effect, and press–release–press during a run never
  starts a second run.
- **SS-R5 A `wait for` in the middle** of an effect follows the same rules (pressed and held, released since the
  last time this line was passed).
- **SS-R6 Outputs keep their level** until a line changes it. Everything is off at power-on (SM-B). The checker
  warns when an effect ends with an output still on, because the output stays on while it waits.
- **SS-R7 There is one sound player.** Every audio line replaces whatever is playing, whichever effect it came
  from. `audio_loop` keeps playing after its effect goes back to `wait for`, until another audio line or
  `audio_stop`. The checker warns when two effects both play sound.
- **SS-R8 Each output belongs to one effect.** The checker refuses a script where two effects set the same output
  or DMX channel. Two effects may wait for the same input; one press then starts both.
- **SS-R9 The protections still apply.** The safety rules at power-on (SM-B) still apply, and an output's
  maximum on-time still turns it off if you set one (Settings; off by default: set one for heaters, pumps and
  solenoids). The board never measures load current (SM-S3): keep each load within its terminal's rating.
- **SS-R10 No phone or remote start for scripts.** A script effect starts only from its input. Starting from the
  phone, MQTT or another board is advanced (SM-Q, SM-R), so the phone-enable rules cannot be bypassed.
- **SS-R11 Scripts and the advanced effects share terminals.** The checker refuses a script that uses a
  terminal an advanced effect owns (for example the pulse-and-recover effect's TRIG1, OUT1, OUT2).

## 5. Checking and storing (SS-C)

- **SS-C1 Checked when saved.** The board turns the text into a fixed table of steps once, when it is saved,
  and refuses a script with any error. The running script is never a half-checked one. An error names the line
  and says what to do in plain words, for example:
  `line 4: there is no out5 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.`
  `line 7: rly1 can only be on or off, not 50%. Dimming works on out1 to out4.`
- **SS-C2 The text is what is stored.** The board keeps the text exactly as written (plus the sound list,
  SS-C4), so the web page and the app always show what the person wrote.
- **SS-C3 Limits** (checked at save, sized for the smallest board, the Pico W): at most 8 effects, 64 lines
  per effect, 256 lines and 8 KB of text in all.
- **SS-C4 Sound files by name.** The sound player plays tracks by number and cannot read names off the card.
  The desktop app copies the sound files to the SD card as numbered tracks and saves the name → number list
  with the script, so a script says `chime.mp3`. Without the app, a name that starts with a number
  (`001_chime.mp3`) or a plain number (`audio_single = 1`) works too. The checker refuses a name it cannot
  find in the list.
- **SS-C5 Same everywhere.** The firmware and any other checker (an editor, an app) give the same answer for
  every script in the shared golden set (§8).

## 6. Where it is written (SS-E)

- **SS-E1 The board's web page** has a text box, a Check button and a Save button that shows the checker's
  messages, and the same three views as the desktop app (Script and blocks, Script only, Blocks only): its blocks
  are a small plain-JavaScript view (`web/blocks.js`, about 4 KB gzipped; Blockly would not fit the flash) with
  the desktop app's block words and colours, the same text round trip, ↑ ↓ / drag to move, ✕ to remove.
- **SS-E2 The desktop app** has the same text editor plus a **block editor**. Blocks snap together like
  building bricks, with one block for each kind of line: an effect block holds a stack of line blocks.
  Built on Blockly (Apache-2.0).
- **SS-E3 Blocks are a view of the text.** Changing either one changes the other. Turning text into blocks
  and back gives the identical text (a test checks this). There is nothing the blocks can do that the text
  cannot, and the app only ever saves text.
- **SS-E4 The app sends the script and sound files** to the board, and lets the person run it in the simulator
  first, using the same step table as the board.
- **SS-E5 Record effect** (both the web page and the desktop app): do the effect once on the live controls and it
  is saved as an effect script like any other (`docs/RECORD-EFFECT.md`).

## 7. Fog machines, store props and DMX (SS-D)

### 7.1 Which terminal does what
Every board made so far is **rev A** (silkscreen "RELAYS: <=30V DC ONLY"): everything on it is low-voltage DC,
and nothing on it may touch mains. The ratings are in `hardware/README.md` ("Ratings at a glance").
- **SS-D1 `out1`–`out4` switch the board's supply voltage**: a 12 V supply gives 12 V on them, a 24 V supply 24 V.
  Each carries 5 A continuous (7.5 A for 0.5 s), 10 A for the whole board. **Dimming** (`N%`, `fade`,
  `flicker`) is for smaller loads: at most 2 A on a 12 V supply, 1 A on a 24 V supply. Use them for things that run
  on the supply voltage: LED strips and lamps, solenoids, pumps, a 12 V animatronic (then power the board from
  12 V). They are **not** a button press: never wire them across another device's button or remote.
- **SS-D2 `rly1`, `rly2` are a dry contact**, like a hand pressing a button, either way round. **Rev A: up to
  30 V DC, 10 A, never mains.** Use them across a low-voltage button (a battery prop's "try me" button; never a
  fog machine's wired remote, §7.3) or to switch a low-voltage DC load directly within the rating. Rev B (not yet
  made) adds 120 V AC on the relays; even then a listed relay-controlled outlet box off the board is the
  recommended way to switch mains.
- **SS-D3 `cc1`, `cc2` are a small isolated closure** for signals only: up to 30 V DC, and plan on about 2 mA
  (tip +, sleeve −; **polarity matters**). Same use as the relays for small signals, and they never wear out.
  The silkscreen calls them CONTACT1 and CONTACT2.
- **SS-D4 Never mains on rev A.** Nothing on a rev A board (OUT, RLY, CC, TRIG, PIXEL, expansion) may touch mains.
  On a rev B board mains may go only on the relays and only at 120 V AC; nothing else on any revision may touch
  mains, and even on rev B a listed relay-controlled outlet box off the board is the recommended way.

### 7.2 Fog machine with a DMX input (preferred)
A fog machine with a socket labelled **DMX IN** is driven over the DMX add-on (§7.4): the script says `dmx1 = on`
(or a percentage), sleeps for the burst, then `dmx1 = off`. The machine is set to DMX mode, address 1. The guide
says to use the socket labelled DMX IN, never one labelled REMOTE, even when the two look alike (3-pin XLR).
- **SS-D5** Most fog machines only fog when their heater is ready. The board cannot see that, so a fog line does
  nothing on a cold machine. The guide says so.

### 7.3 Wired fog remotes: not supported
The board is never wired to a fog machine's wired remote, its remote socket or a timer remote. On many consumer
machines the remote line carries mains (the remote switches the pump at line voltage), and the manuals rarely say
which, so a beginner cannot tell a safe remote from a dangerous one. Switching the machine's power through a
relay-controlled outlet does not work either: that also switches the heater, which needs minutes to warm up.
Machines without DMX IN are not covered by the guides.

### 7.4 DMX add-on (for fog machines and lights with a DMX input)
Built from parts anyone can order, on the expansion header. The board sends DMX frames all the time once a script
uses a `dmx` line.
- **SS-D6 Parts:** one "MAX485 TTL to RS-485 module" (5 V, sold in multi-packs) and one 3-pin XLR **female**
  to bare-wire pigtail (or a DMX cable cut in half).
- **SS-D7 Wiring** (no soldering on the board):

| Module pin | Goes to |
|---|---|
| VCC | expansion +5V |
| GND | expansion GND and XLR pin 1 (shield) |
| DI | expansion S4 (ESP32 GPIO15, Pico GP3) |
| DE and RE (joined) | +5V (always sending) |
| A | XLR pin 3 (Data +) |
| B | XLR pin 2 (Data −) |

- **SS-D8** The DMX port is turned on with the setting `dmx_port = s4` (off by default, because S4 is a spare
  pin). The checker refuses `dmx` lines while it is off, saying the DMX add-on is not turned on.
- **SS-D9** This add-on is not isolated. That is fine for one fog machine or a few lights on a short cable. A
  large rig uses an isolated DMX interface. **First build: check** the S4 pin's boot behaviour on the ESP32
  (GPIO15 is a strapping pin) and the A/B order with a real fog machine.

### 7.5 Store-bought animatronics
- **SS-D10** If it runs from a 12 V DC adapter rated 5 A or less and the board runs from 12 V, power it from an
  `out` terminal and switch it with `out1 = on`. Otherwise keep its own adapter and press its "try me" or sensor input with `rly1` or `cc1`.

## 8. Tests (SS-T)

- **SS-T1 One shared golden set** in the public repo (`firmware/show-script-golden/`). Each case is a script plus
  either its expected checker messages or a timed trace of inputs, outputs, DMX values and sound commands.
  Every checker runs it, like the protocol golden session.
- **SS-T2** Required cases include: case and spacing variants giving the same table (SS-W1–W4); every error
  message; two effects at once; a trigger held down through the end of an effect (no restart, SS-R4);
  press–release–press during a run (one run, SS-R2); `audio_loop` surviving the return to `wait for` (SS-R7);
  two effects writing one output (refused, SS-R8); the limits (SS-C3); text → blocks → text (SS-E3).
- **SS-T3** A randomized test presses inputs at random against random valid scripts and checks SS-R2, SS-R4 and
  SS-R8 never break.
