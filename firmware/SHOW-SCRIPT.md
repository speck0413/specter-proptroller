# Show script: the beginner language

Implemented by the firmware (`arduino/lib/proptroller/src/proptroller/script.cpp`).

A show script is a short text file that reads like a config file. Each effect waits for an input, then turns
outputs on and off, waits, and plays sounds. The board stores the text, checks it when it is saved, and runs it.
Nobody compiles anything. You can write it in the board's web page, in the desktop app as text, or in the desktop
app with drag-and-drop blocks.

Two more lines (§9: `limit` and `pulse`) and a few settings make a whole prop such as the pulse-and-recover
effect a script: every effect can be started from the phone by its name, and effects that use the same outputs
take turns by themselves. Page lock and aliases stay in the settings and the profile (`STATE-MACHINE.md`). The
board's protections apply to a script without being written.

## 1. A first look

```
effect porch light:
  wait for trig1
  out1 = on
  audio_single = 1     # plays sound 1: the file in the card's folder 01
  wait 30
  fade out1 to 0% over 2

effect prop:
  wait for trig2
  rly1 = on        # presses a store-bought prop's button (§7)
  wait 3
  rly1 = off
  wait 60
```

Two effects, each with its own trigger. They run at the same time and never wait for each other.

## 2. Writing rules (SS-W)

- **SS-W1 Case does not matter.** `Effect1:`, `EFFECT1:` and `effect1:` are the same. So are `OUT1`/`out1`
  and `Wait`/`wait`.
- **SS-W2 Spaces and indentation do not matter.** `out1=on`, `out1 = on` and `   out1 =on` are the same. Tabs
  count as spaces. Indenting the lines of an effect is optional and only for reading.
- **SS-W3 `=` or `:`** both work for a value: `out1 = on`, `out1: on`.
- **SS-W4 Phone typing is cleaned up first.** Smart quotes, non-breaking spaces, a trailing period and Windows
  line endings are turned into plain text before the script is checked.
- **SS-W5 Comments** start with `#` and run to the end of the line. Blank lines are ignored.
- **SS-W6 Times are in seconds** unless a unit follows: `wait 0.4`, `wait 200 ms`, `wait 3 min`. Accepted
  units: `ms`, `s`, `sec`, `second(s)`, `min`, `minute(s)`. Decimals are fine.
- **SS-W7 A level is one of three things:** `on` (100 %), `off`, or a percentage such as `50%`. Example: `out1 = 50%`.

## 3. Lines (SS-L)

| Line | Meaning |
|---|---|
| `effect <name>:` | Starts an effect. The name is anything after `effect` (`effect1`, `effect porch light`). Every line below belongs to it until the next `effect …:`. |
| `wait for <input>` | Waits until that input is pressed (rules SS-R3, SS-R4). Inputs: `trig1`, `trig2`, `user1`–`user3`. |
| `<output> = on` / `off` / `N%` | Sets an output. Outputs: `out1`–`out4` (the board's supply voltage, dimmable up to 2 A at 12 V), `rly1`, `rly2`, `cc1`, `cc2` (button presses, on or off only; §7). |
| `dmx<N> = on` / `off` / `N%` | Sets DMX channel N (1–512) through the DMX add-on (§7.4). |
| `wait <time>` | Waits that long: `wait 2`, `wait 200 ms`, `wait 3 min`, `wait out1_on_ms` (SS-G6). `wait for 2 s` is the same line. A time starts with a digit or is a time setting; anything else after `wait for` is an input. A name that is both an input's name and a time setting means the input, with a warning. `sleep <time>` is an older spelling of the same line and still works. |
| `fade <output> to N% over <time>` | Changes a dimmable output smoothly to N %. |
| `flicker <output> for <time>` | Flickers a dimmable output at random like a failing bulb, then returns it to its level before the flicker. |
| `audio_single = <N>` | Plays sound N once (SS-C4). |
| `audio_loop = <N>` | Plays sound N over and over in the background (SS-R7); `audio_loop = off` (or `false`, or `""`) turns it off. |
| `audio_volume = <level>` | How loud the script's sounds play from this line on: `audio_volume = 60%` (0% to 100%, or `off`). |
| `audio_stop` | Stops the sound. |
| `pixels = <effect> <colour>` | Lights the pixel strip until another `pixels` line: `pixels = chase red`, `pixels = rainbow`, `pixels = warm white` (§10). |

That is the whole beginner language: eleven kinds of line.

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

- **SS-R1 Each effect is its own loop.** At power-on, every effect sits at its top: its sound lines (if it starts
  with any), then its first `wait for`. When it starts it runs its lines top to bottom, and after the last line it
  goes back to the top. Sound lines before the first `wait for` (`audio_loop`, `audio_single`, `audio_volume`, `audio_stop`) run
  at power-on and every time the effect comes back to its top, and so does a `pixels` line there (the strip's
  look while the prop waits); any other line before it is an error.
- **SS-R2 Busy until the last line finishes.** While an effect is running it never starts again; a press of its
  trigger is ignored (with `trig1_wait` / `trig2_wait` = `return`: remembered, and it starts the next run when
  this one ends only if the input is still held then, SS-R4). A `wait` at the end of an effect therefore works
  as a rest time: nothing restarts it until the wait is over.
- **SS-R3 Pressed means pressed and held.** An input counts as pressed after it has been closed for the debounce
  time (100 ms, `trig_debounce_ms`) without opening, same as SM-T1a.
- **SS-R4 Release first.** An effect starts again only after its input was released. How the first `wait for`
  counts on TRIG1 and TRIG2 is the setting `trig1_wait` / `trig2_wait`:
  - `ready` (the default): released since the effect's last run started, then held for the debounce while the
    effect could start (at its top, its outputs free, its limit not used up). A press held through a run starts
    the next run as soon as the effect can; taps while it cannot start are not saved; a press held since the last
    run never starts another (the pulse-and-recover effect's TRIG1, SM-T).
  - `return`: the input is read as the library's trigger reads it: released after it was open for the debounce
    plus `trig_release_ms` (about 0.2 s), then a press is the debounce closed. A press while the effect cannot
    start (another effect holds its outputs, or its own run) is remembered (one) and starts it when it can, if the
    input is still held then (SS-R8). A press in the 2 s boot lockout never starts: let go and press again.
  USER1-3 and a `wait for` in the middle of an effect count as released after the effect came back to that
  `wait for`, then held for the debounce; a press while the outputs are busy is remembered as above.
- **SS-R5 A `wait for` in the middle** of an effect follows the same rules (pressed and held, released since the
  last time this line was passed).
- **SS-R6 Outputs keep their level** until a line changes it. Everything is off at power-on (SM-B). The checker
  warns when an effect ends with an output still on, because the output stays on while it waits.
- **SS-R7 One sound player, one background loop.** `audio_loop = <N>` sets the background loop: it plays over
  and over, also after its effect goes back to its top. `audio_single = <N>` plays once over it, and the loop
  comes back by itself when that sound ends. `audio_loop = off` (or `false`, or `""`) turns the loop off; setting
  the same loop again while it plays changes nothing. `audio_stop` stops whatever plays and turns the loop off. The
  loop plays at `ambient_volume`, a sound once at `trigger_volume`, until a line `audio_volume = <level>` sets the
  volume for the script's sounds (its percentage on the player's 0-30, kept until the next `audio_volume` or a
  restart; the board's ambient and trigger volume settings stay as they are). The checker warns when two effects both play
  sound. While a script is stored the board's own ambient loop is off.
- **SS-R8 Effects take turns by their outputs.** An effect uses the outputs (and DMX channels) its lines set, fade,
  flicker or pulse. Two effects that use one in common never run at the same time: an effect whose outputs another
  effect holds waits. Effects with no output in common run at the same time; two effects waiting for the same
  input with no output in common both start on one press. When effects that share outputs would start on the same
  sample, the one on the lower input starts (TRIG1, TRIG2, USER1 ...; for the same input the first in the script)
  and the other is remembered (one deep). A remembered press runs when the outputs come free only if its input is
  still held at that moment; let go earlier, it is dropped (memory with check). Each effect starts only on its own
  trigger (its input or its name), and the same effect may run again and again.
- **SS-R9 The protections still apply.** The safety rules at power-on (SM-B) still apply, and an output's
  maximum on-time still turns it off if you set one (Settings; off by default: set one for heaters, pumps and
  solenoids). The board never measures load current (SM-S3): keep each load within its terminal's rating.
- **SS-R10 Starting from the phone, MQTT or serial.** Every effect can be started by its name (spaces as `_`):
  the web page's button, MQTT `<prefix>/<name>/control/<effect>`, the serial console's `<effect>`, op `action`
  with `key` the name. A remote start needs `remote_trigger_enable`, under the pulse-and-recover effect's rules
  (SM-Q, SM-R): it starts now when the effect can start, otherwise one start waits (a second is "already queued");
  `cancel` empties the waiting starts, and the enable going off (an enable from a phone page ends with that page)
  drops those that need it. The effects named in the setting `remote_free_effects` start without the enable. A
  name wins over a profile's control alias. When several starts wait for the same outputs, a waiting remote start
  goes before a remembered press.
- **SS-R11 Scripts and the advanced effects share terminals.** The checker refuses a script that uses a
  terminal an advanced effect owns (for example the pulse-and-recover effect's TRIG1, OUT1, OUT2).
- **SS-R12 Timing.** Every effect steps on the 1 ms input samples (its outputs reach the pins on the 10 ms tick).
- **SS-R13 Status.** With a script stored (and the built-in effect off), the board gives the pulse-and-recover
  effect's status values from it: `run_count` (every effect's runs), `ready`, `runs_left` and
  `cooldown_remaining_ms` (the first effect's; the last two with a limit), `pulsing` (OUT1 on, or a run that turns
  OUT2 on has not reached that line yet: a refill's wait before its OUT2 counts), `recovering` (OUT2 on), `queued`
  (a remote start waits), `remote_trigger_enable` and `remote_trigger_by`; a profile's status aliases name them
  (and their `label`s name them on the phone page; a time left shows as m:ss under any name). Over MQTT a status
  value goes out when it changes, at most every 0.1 s; a time left once a second. With `out4_ready_lamp` on, OUT4 is the first effect's ready lamp (on while a press would start
  it) and the checker refuses `out4` in the script.

## 5. Checking and storing (SS-C)

- **SS-C1 Checked when saved.** The board turns the text into a fixed table of steps once, when it is saved,
  and refuses a script with any error. The running script is never a half-checked one. An error names the line
  and says what to do in plain words, for example:
  `line 4: there is no out5 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.`
  `line 7: rly1 can only be on or off, not 50%. Dimming works on out1 to out4.`
- **SS-C2 The text is what is stored.** The board keeps the text exactly as written, so the web page and the
  app always show what the person wrote.
- **SS-C3 Limits** (checked at save, sized for the smallest board, the Pico W): at most 8 effects, 64 lines
  per effect, 256 lines and 8 KB of text in all.
- **SS-C4 Sounds are numbers.** The sound player plays files by number and cannot read names off the card, so a
  script says `audio_single = 3`: sound 3 is the one file in the card's folder `03`, named `001-….mp3` (1 to 99;
  the rest of the file name is for people). Studio's SD card tool fills the card for you; by hand, see
  `docs/SD-CARD.md` ("Setting up the SD card for the DFPlayer"). The checker refuses a name or a number outside
  1-99 and says how to put the sound on the card.
- **SS-C5 Same everywhere.** The firmware and any other checker (an editor, an app) give the same answer for
  every script in the shared golden set (§8).

## 6. Where it is written (SS-E)

- **SS-E1 The board's web page** has a text box, a Check button and a Save button that shows the checker's
  messages, and the same three views as the desktop app (Script and blocks, Script only, Blocks only): its blocks
  are a small plain-JavaScript view (`web/blocks.js`, about 4 KB gzipped; Blockly would not fit the flash) with
  the desktop app's block words and colours, the same text round trip, ↑ ↓ / drag to move, ✕ to remove. The text box colours the words as you type (keywords, outputs, inputs, numbers, pixel effects and colours,
  comments); the words come from one shared list, `web/words.js`, which the firmware's tests check against the checker
  and which the desktop app can use too.
- **SS-E2 The desktop app** has the same text editor plus a **block editor**. Blocks snap together like
  building bricks, with one block for each kind of line: an effect block holds a stack of line blocks.
  Built on Blockly (Apache-2.0).
- **SS-E3 Blocks are a view of the text.** Changing either one changes the other. Turning text into blocks
  and back gives the identical text (a test checks this). There is nothing the blocks can do that the text
  cannot, and the app only ever saves text.
- **SS-E4 The app sends the script** to the board, fills the SD card with its SD card tool, and lets the person run it in the simulator
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
  press–release–press during a run (one run, SS-R2); `audio_loop` surviving the return to `wait for` and coming
  back after a sound (SS-R7); two effects writing one output (they take turns, SS-R8); remote starts by name
  (SS-R10); the limits (SS-C3); text → blocks → text (SS-E3).
- **SS-T3** A randomized test presses inputs at random against random valid scripts and checks SS-R2 and SS-R4
  (the `return` rule) never break.

## 9. Two more lines (SS-G)

```
effect run:
  wait for trig1
  limit 3 runs per 3 min              # counted from the first run
  pulse cc1 for 200 ms                # on now, off after 200 ms; the next line does not wait
  out1 = on
  wait out1_on_ms                     # a setting: change it and the next run uses it
  out1 = off
  wait 10 ms
  out2 = on
  wait out2_on_ms
  out2 = off

effect refill:                        # shares out2 with run: they take turns (SS-R8)
  wait for trig2                      # with trig2_wait = return, a press during a run waits for it
  out2 = on
  wait out2_on_ms
  out2 = off
```

- **SS-G3 `limit <N> runs per <time>`** (or `limit 1 run per …`): up to N runs (1-20) in a window that opens at
  the first run and lasts the time (up to 60 minutes; 0: no window). While the window is used up the effect does
  not start: presses are ignored, a release does not count (a press held across the window's end starts nothing),
  and a remote start is refused with the time left (SM-L). One limit per effect.
- **SS-G5 `pulse <output> for <time>`**: the output (out1-out4, rly1, rly2, cc1, cc2) goes on now and off after the
  time, while the effect goes straight to its next line; a later line that sets the output ends the pulse. A
  relay or contact pulses for at least 0.1 s.
- **SS-G6 Times and counts kept as settings.** Where `wait`, `pulse` or `limit` take a time, and where `limit`
  takes a number of runs, a setting may stand: `out1_on_ms`, `out2_on_ms`, `out3_on_ms`, `recover_time_ms`,
  `cooldown_ms`, `trigger_out_pulse_ms`, `role_pulse_ms` (times in ms) and `runs_per_cooldown` (a count), or a
  profile's alias of one of them. The value is read when the line runs, so a change from the web page, MQTT or
  the serial console applies from the next time the line runs.

Written like the built-in effect (the example above, with `trig2_wait = return` and `remote_free_effects =
refill`), a script gives the effect's output edges to within one tick (`test_script_gates`), but for one thing:
the effect runs a recover pressed while it could not run even when TRIG2 was let go since, where a script runs a
remembered press only if its input is still held then (SS-R8).

## 10. The pixel strip (SS-X)

```
effect graveyard:
  pixels = breathe purple       # while it waits (SS-R1)
  wait for trig1
  pixels = fire
  out1 = on
  wait 10
  out1 = off
  pixels = comet 0xff2000       # an exact colour: 0x and six hex digits
  wait 3
```

- **SS-X1 One line:** `pixels = <effect> <colour>`, or `pixels = <colour>` to fill the strip with one colour. The
  strip shows it until another `pixels` line runs (any effect's), also after its effect goes back to its top, like
  `audio_loop`. `pixels = off` turns it dark.
- **SS-X2 Effects:** `off`, `fill`, `chase` (a few lit pixels running along), `rainbow`, `flash` (on and off),
  `fade` (in from dark, then stays), `flicker` (like a failing bulb), `comet` (a head with a fading tail),
  `sparkle` (random pixels flash and fade), `breathe` (slowly up and down), `fire` (red, orange and yellow
  flames). `rainbow` and `fire` have their own colours; the others are white when no colour is given.
- **SS-X3 Colours:** `red`, `orange`, `yellow`, `green`, `cyan`, `blue`, `purple`, `pink`, `white`, `warm white`,
  or an exact colour as `0xRRGGBB` (`0xff8000` is orange). `#ff8000` does not work: `#` starts a comment, and the
  checker says so.
- **SS-X4 Speed and brightness are settings:** `pixel_speed` (100 % is each effect's own speed, 200 % twice as
  fast, 10 % to 400 %) and `pixel_brightness` (a cap on every colour). How many pixels the strip has is
  `pixel_count`. A change applies at once.
- **SS-X5 Turn-taking:** the strip is an output: effects with a `pixels` line take turns like effects that share
  an output (SS-R8). A paused board (the stop button) turns the strip dark; after the pause every effect starts
  over at its top, so the lines before its first `wait for` set the strip again. A pixel test from the phone page
  wins while it is on.
- **SS-X6 Never waits:** the strip's frames go out in the background (50 a second at most), so a long strip never
  holds up the inputs or the outputs.

## 11. Telling other boards (SS-B)

One board's script can start an effect on another board and change its settings, over the Wi-Fi network: with an
MQTT broker, or straight from board to board without one.

```
# board "front door"                       # board "porch"
effect doorbell:                           effect blackout:
  wait for trig1                             wait for trig1
  tell porch blackout                        out1 = off
  audio_single = 1                           pixels = off
  wait 5                                     wait 1
  tell porch bright
  tell all pixel_speed = 150               effect bright:
  wait 30                                    wait for trig2
                                             out1 = on
                                             pixels = warm white
                                             wait 1
```

With `remote_free_effects = blackout, bright` on the porch board, the front door's doorbell turns the porch dark,
then bright again five seconds later, and makes every board of the group run its pixel effects one and a half
times as fast.

- **SS-B1 `tell <board> <effect>`** starts that effect on that board, exactly as a start by name from the phone,
  MQTT or the serial console does (SS-R10): the other board's `remote_trigger_enable` and `remote_free_effects`
  decide, its page lock applies, and its effects take turns as always (a start that finds the effect busy waits,
  one deep). The board is named by its network name, the one in `<name>.local` (`Front Door` is `front-door`);
  `all` is every other board of the group. An effect's name is written as in its own script; spaces become `_`
  (`tell porch porch light` starts `porch light`).
- **SS-B2 `tell <board> <setting> = <value>`** changes one of that board's settings, as `config/set` over MQTT
  does, with the same names, aliases and ranges (`on` / `off` for a switch, `50%` for a percentage). The board
  uses the new value at once and **does not save it**: it lasts until that board restarts or the setting is
  changed there (a save of the board's own settings writes the value it had before). A board never takes its name,
  passwords, Wi-Fi, MQTT, board link, page lock, remote trigger settings or built-in effect from another board
  (the checker refuses those lines), and a page-locked board takes no settings at all.
- **SS-B3 Never waits.** A tell line hands its message to the network and the next line runs at once; at most 20
  tells a second go out. The sending board's event log (the phone page's log) says `tell porch blackout: sent
  (Wi-Fi)`, then, if the other board refuses, why (`tell porch blackout: porch refused: blackout: ...`), or, after
  2 s without an answer, `tell porch blackout: no answer from porch` (`tell all` says how many boards answered).
  The receiving board logs who started what (`remote start by front-door: effect "blackout"`) and every setting
  another board changed.
- **SS-B4 Checked here, run there.** The checker knows the form of a tell line but not the other board's effects
  or settings: a name the other board does not have is refused there, and the sender logs it.
- **SS-B5 How it travels** is the setting `board_link`: `auto` (the default: MQTT when a broker is set, otherwise
  straight to the other boards), `mqtt`, `wifi` (straight to the boards on the same Wi-Fi network; an ESP32 also
  sends by ESP-NOW, which needs no router), or `off`. Boards joined to another board's own hotspot do not hear each
  other this way (every hotspot has the same address, 192.168.4.1), except ESP32 boards by ESP-NOW. A board hears tells
  over MQTT whenever it has a broker, and straight from other boards unless `board_link` is `mqtt` or `off`.
  PROTOCOL.md "Board link" has the wire formats.
- **SS-B6 Groups and the key.** Boards hear tells sent straight to them only from boards with the same
  `link_group` (empty by default). **Without a key, anyone on the network can tell a board what to do**, as anyone
  on it can open its page. With `link_key` set (the same on every board of the group; never shown or read back),
  messages are signed, and a message recorded on the network and sent again is dropped. MQTT has no key: a
  broker on the show network is as open as the boards' pages.
