# Show-script golden set

The shared test set for the beginner show-script language (`firmware/STATE-MACHINE.md` is the advanced
reference; the language is `firmware/SHOW-SCRIPT.md`). The firmware and any other
checker (Specter Studio's) run every case and must give the same answer (SS-C5). A tester
board (hardware in the loop) can replay the timed cases: it drives the inputs, timestamps the output edges,
reads the DMX frames and captures the DFPlayer UART.

This README is the contract. Where it is more precise than SHOW-SCRIPT.md, it settles the detail; where they
disagree, SHOW-SCRIPT.md wins and this file is fixed.

## Layout

One directory per case under `cases/`:

| File | What |
|---|---|
| `script.txt` | the script, byte for byte (UTF-8; some cases hold CRLF, tabs or smart quotes on purpose) |
| `case.json` | `spec` (rule ids), `sounds` (the name → number list stored with the script), `settings` (board settings before the script is saved), and `check` and / or `trace` |

## The check (`check`)

`check_script` / `set_script` answer:

```json
{ "ok": true,
  "errors":   [ { "line": 4, "message": "line 4: there is no out5 on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2." } ],
  "warnings": [ { "line": 2, "message": "line 2: effect \"porch\" ends with out1 still on. It may stay on until the next trigger." } ],
  "table": { "effects": [ ... ] } }
```

`ok` is false when there is any error; `table` is present only when `ok` is true. A line has at most one
error: the first that applies in the order the tables below list them (the line errors, then the
per-effect errors on a header line). Every line with an error is reported (one does not stop the others),
in line order; every warning is reported. The whole-script limits (line 0) come last, all that apply. A message about the whole script (a limit) has `"line": 0` and no `line N: ` prefix. A case's `check`
lists the expected `ok`, `errors`, `warnings` and, where given, `table`; an implementation must match them
exactly (byte for byte).

### Reading a line

1. Line endings: CRLF and CR become LF. Lines are numbered from 1. A final LF ends the last line; it does not
   start another one (`"a\nb\n"` has 2 lines).
2. Tab, U+00A0 (no-break space) and U+202F become a space; U+2018 / U+2019 become `'`, U+201C / U+201D
   become `"`.
3. `#` to the end of the line is a comment. Trailing spaces are removed, then one trailing `.` (`sleep 3.`
   is `sleep 3`, `sleep 0.4.` is `sleep 0.4`; the `.` inside `0.4` stays).
4. Blank lines (after the above) are ignored.
5. Everything is lower case except the value of an `audio_single` / `audio_loop` line (a file name keeps its
   case). Spaces between words do not matter: `out1=on`, `out1 = on`, `OUT1 =ON` are the same line.
6. A header with an error still starts an effect, so the lines under it are checked as its lines. A line with
   an error still counts as its kind (a `wait for trig7` first line gives M-input, not also M-first), but it
   adds no step: W-stillon and W-twosound look only at lines without errors.

### The lines

| Line | Rule |
|---|---|
| `effect <name>:` | A line that starts with `effect` is always a header. When the character after `effect` is a space, a `:` or the end of the line, the name is the text after `effect` up to the `:`, with the spaces around it trimmed (`effect:` has no name: M-noname); otherwise the whole word is the name (`effect1:` names `effect1`). 1-32 characters. In the table the name is lower case with runs of spaces made one. |
| `wait for <input>` | `trig1`, `trig2`, `user1`, `user2`, `user3`. `waitfor trig1` and `wait fortrig1` are the same. `user1`-`user3` only with `buttons = triggers` (the default). `buttons` is a setting of the checker's board: a firmware without learn mode (this C++ firmware since the Record effect) always has `triggers`; the `learn` case (e34) stays for checkers of boards that still have it. |
| `<target> = <level>` | `=` or `:` (the first one splits). Targets `out1`-`out4` (dimmable), `rly1`, `rly2`, `cc1`, `cc2` (on / off only), `dmx1`-`dmx512` (dimmable). |
| `sleep <time>` | |
| `fade <target> to <level> over <time>` | dimmable targets only; the level must be a percentage, `on` or `off` |
| `flicker <target> for <time>` | dimmable targets only |
| `audio_single = <sound>` / `audio_loop = <sound>` | `=` or `:`; surrounding `'` or `"` are removed from the sound |
| `audio_stop` | |

- **Level**: `on` (100 %), `off` (0 %), or `N%` with N from 0 to 100, decimals allowed, a space allowed before
  `%`. In the table an output level is permille (`50%` → 500, `12.5%` → 125, rounded to the nearest
  permille) and a DMX level is 0-255 (`round(N × 255 / 100)`, `on` = 255). Rounding is to the nearest, halves
  up (`50%` → DMX 128), computed exactly from the decimal text (no binary floating point surprises).
- **Time**: a number (whole or decimal) and an optional unit, a space allowed between: `ms`, `s`, `sec`, `secs`,
  `second`, `seconds`, `min`, `mins`, `minute`, `minutes`; no unit means seconds. Rounded to the nearest
  millisecond, halves up. 0 to 24 hours.
- **Sound**: looked up in the sound list (case kept). If it is not there and it starts with digits
  (`001_chime.mp3`, `3`), that number is the sound. Sounds are 1-99.

### Messages

`{n}` is the line number, `{text}` the line as written (comment and trailing spaces removed, original case),
`{x}` the target or input as written in lower case, `{v}` the value as written, `{name}` the effect's name.

**Errors**, in the order they are checked on a line:

| Id | Message |
|---|---|
| M-outside | `line {n}: this line is not inside an effect. Start one first with "effect <name>:".` |
| M-colon | `line {n}: put a colon after the effect name: "effect {name}:".` |
| M-noname | `line {n}: give the effect a name, for example "effect fog:".` |
| M-longname | `line {n}: the effect name is longer than 32 characters.` |
| M-dupname | `line {n}: there is already an effect called "{name}" (line {m}). Give each effect its own name.` |
| M-unknown | `line {n}: I do not understand "{text}". A line is effect, wait for, an output = on/off/N%, sleep, fade, flicker, audio_single, audio_loop or audio_stop.` |
| M-unknown, hints | instead of the above for the commonest mistakes: a line starting `turn ` gets `line {n}: write it as three lines: "out1 = on", "sleep 2", "out1 = off".`; `play {v}` gets `line {n}: to play a sound, write "audio_single = {v}".` (a leading `sound ` is dropped from `{v}`: `play sound 1` gets `"audio_single = 1"`); `delay {v}` or `pause {v}` gets `line {n}: to wait a time, write "sleep {v}".`; an output name, a space and more (`out1 on`) gets `line {n}: write it as "{x} = {rest}".` |
| M-waitform | `line {n}: write it as "wait for trig1".`; when what follows `wait` starts with a digit (`wait 3`): `line {n}: to wait a time, write "sleep {v}"; to wait for an input, write "wait for trig1".` |
| M-fadeform | `line {n}: write a fade as "fade out1 to 50% over 2".` |
| M-flickerform | `line {n}: write a flicker as "flicker out1 for 3".` |
| M-nosound | `line {n}: say which sound, for example "audio_single = 1" (plays /01/001.mp3).` |
| M-input | `line {n}: there is no {x} on this board. The inputs are trig1, trig2 and user1 to user3.` |
| M-learn | `line {n}: {x} is a learn-mode button (buttons = learn). Set buttons to triggers to use it in a script.` |
| M-output | `line {n}: there is no {x} on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.`; for `pixel` (or `pixels`): `line {n}: the pixel strip is not in scripts yet: make a show for it on the Shows tab, or use a sketch.` |
| M-dmxch | `line {n}: there is no DMX channel {k}. DMX channels are 1 to 512.` |
| M-level | `line {n}: "{v}" is not a level. Use on, off or a percentage from 0% to 100%.` |
| M-onoff | `line {n}: {x} can only be on or off, not {v}. Dimming works on out1 to out4.` |
| M-nofade | `line {n}: {x} cannot fade. Fading works on out1 to out4.` |
| M-noflicker | `line {n}: {x} cannot flicker. Flickering works on out1 to out4.` |
| M-time | `line {n}: "{v}" is not a time. Write seconds (sleep 2.5) or add a unit: ms, s or min.` |
| M-toolong | `line {n}: that is longer than 24 hours.` |
| M-sound | `line {n}: there is no sound called "{v}". Use its number: "audio_single = 1" plays /01/001.mp3 (the desktop app copies named sounds to the card and lists them).` |
| M-soundrange | `line {n}: sound {k} is out of range. Sounds are numbered 1 to 99.` |
| M-dmxoff | `line {n}: {x} needs the DMX add-on, and it is not turned on. Turn it on with the setting dmx_port = s4.` |
| M-role | `line {n}: {x} is set to {role} ({x}_role). Set {x}_role to shows to use it in a script.` with `{role}` = `pulse on every trigger` (trigger_repeat), `pulse when a sound starts` (sound_trigger) or `not used` (none) |
| M-owned | `line {n}: {x} belongs to the built-in pulse-and-recover effect. Turn the effect off (setting effect = none) or use another terminal.` |
| M-shared | `line {n}: {x} is already used by effect "{a}" (line {m}). Each output belongs to one effect.` (on every later line that sets it; for a DMX channel the last sentence is `Each DMX channel belongs to one effect.`) |

Then, per effect, on its header line: **M-empty** `line {n}: effect "{name}" has no lines.`, **M-first**
`line {n}: effect "{name}" must start with "wait for", for example "wait for trig1".`, **M-effectlines**
`line {n}: effect "{name}" has {k} lines. An effect can have at most 64.` (blank and comment lines do not
count). Then for the whole script (line 0): **M-effects** `the script has {k} effects. A board runs at most 8.`,
**M-lines** `the script has {k} lines. A board takes at most 256.` (every line counts, blank and comment
lines too), **M-bytes** `the script is {k} bytes. A board takes at most 8192.` (the raw text). After the per-effect checks, on the line that
switches it: **M-relayfast** `line {n}: {x} switches again after {k} ms. Relays and contacts switch at most every 100 ms: sleep at least 0.1 between its changes.`
(rly1, rly2, cc1, cc2 set to another level less than 100 ms of sleep, fade and flicker time after its previous
change; a `wait for` in between resets it, and two lines with no time between them do not count).

**Warnings** (never block a save), after the errors of the same line:

| Id | Message |
|---|---|
| W-stillon | `line {n}: effect "{name}" ends with {x} still on. It may stay on until the next trigger.` on the header line, one per target left above 0 % after the last line (in the order the targets first appear in the effect) |
| W-twosound | `line {n}: effects "{a}" and "{name}" both play sound. There is one sound player: a new sound replaces the one playing.` on the header line of every effect that plays sound after the first such effect `{a}` |

An empty script (no lines, or only blank and comment lines) is valid and removes the stored script.

### Names given to terminals (SS-N)

No case sets a name, so every case above is unchanged. A board may know its terminals by a wiring name
(settings `out1_name` … `user3_name`) and a profile terminal alias. Each is compared lower case with runs of
spaces made one. Where a target or an input is read and the word is not a terminal's own name (nor `dmx<N>`),
the names of the outputs (for a target) or of the inputs (for `wait for`) are looked up. A name is usable when
it is one word, `[a-z][a-z0-9_]*`, not a terminal's own name, and not `effect…`, `wait…`, `sleep` /
`sleep<digit>…`, `fade`, `flicker`, `audio_single`, `audio_loop`, `audio_stop`, `on`, `off`, `to`, `over`,
`for`, `turn` or `play`. The step table always holds the terminal. `{x}` in M-learn, M-onoff, M-nofade,
M-noflicker, M-dmxoff, M-role, M-owned, M-shared, M-relayfast and W-stillon becomes `{name} ({terminal})`
when the line (for M-relayfast and W-stillon: the effect's first line that used one) gave a name; `{x}_role`
in M-role is always the terminal. Messages:

| Id | Where | Message |
|---|---|---|
| M-twonames | instead of M-input / M-output when two terminals of that kind have the name | `line {n}: "{w}" is the name of both {t1} and {t2}. Write {t1} or {t2}, or give them different names.` |
| M-badname | instead of M-input / M-output when one terminal has the name and it is not usable; also first on a line whose part before its first `=` (`=` before any `:`) is a name that starts as another line would (`wait…`, `sleep`, `fade`, `flicker`) | `line {n}: "{w}" is the name of {t}, but a script cannot use it: {why}. Write {t}, or give it another name.` with `{why}` = `a name in a script is one word`, `a name in a script starts with a letter and has only letters, digits and _`, `it is the name of a terminal` or `it is a word of the script language` |
| W-nameis | warning, the first line that uses a terminal's own name which another terminal was given | `line {n}: {x} is also the name given to {t}. Here {x} means the terminal {x}.` |

The `out1 on` hint (M-unknown) takes a usable output name too (`lamp on` → `write it as "lamp = on".`).

### The step table

```json
{ "effects": [ { "name": "porch light", "line": 1, "steps": [
    { "line": 2, "op": "wait_for", "input": "TRIG1" },
    { "line": 3, "op": "set", "target": "OUT1", "level": 1000 },
    { "line": 4, "op": "audio_single", "sound": 3 },
    { "line": 5, "op": "sleep", "ms": 30000 },
    { "line": 6, "op": "fade", "target": "OUT1", "level": 0, "ms": 2000 },
    { "line": 7, "op": "flicker", "target": "OUT2", "ms": 3000 },
    { "line": 8, "op": "audio_loop", "sound": 2 },
    { "line": 9, "op": "audio_stop" },
    { "line": 10, "op": "set", "target": "DMX1", "level": 255 } ] } ] }
```

Keys in this order; targets `OUT1`-`OUT4`, `RLY1`, `RLY2`, `CC1`, `CC2`, `DMX1`-`DMX512`; inputs `TRIG1`,
`TRIG2`, `USER1`-`USER3`.

## Running (what the traces check)

- Each effect is its own loop (SS-R1): at power-on it is at its first step (a `wait for`); after the last step
  it goes back to the first.
- **`wait for`** (SS-R3-R5): arriving at a `wait for` (power-on counts as arriving) clears *released*.
  *Released* becomes true once the input has read open without a break for `trig_release_ms`, counted from
  the later of the arrival and the moment it opened. The line passes once the input has read closed without a
  break for `trig_debounce_ms`, counted from the later of the closure and the end of the 2 s boot lockout, and
  only after *released*. The input is sampled every 1 ms. Nothing passes in the boot lockout (SM-B2); an
  input held at power-on needs a release (SM-B3). Two effects waiting for one input both pass on one press.
- `sleep`, `fade` and `flicker` take their time: the next step starts when they are done. Every other step
  takes no time.
- `fade` goes in a straight line from the level the target has when the fade starts. `flicker` changes the
  level at random between 0 and the level it had when the flicker started, and gives that level back at the
  end. A DMX target's value in a frame is its current 0-255 level (no second rounding).

Recommended timing, so the simulator and the firmwares give the same picture (the traces allow for other
choices within their tolerances):

- Steps run on the engine's 10 ms tick; inputs are sampled every 1 ms and *released* updates on every
  sample. A `wait for` is tested on the tick: it passes once released and
  `now - max(closed_since, boot_end) >= trig_debounce_ms`.
- Each step runs at most once per tick. After the last step the effect is back at its `wait for`, which is
  first tested on the next tick. `sleep`, `fade` and `flicker` count from the tick their line is reached.
- Fade: `level = from + (to - from) × (now - start) / ms` with integer division toward zero, each tick; the
  exact target at the end.
- Flicker: xorshift32 (`x ^= x << 13; x ^= x >> 17; x ^= x << 5`), seed `0x2545F491`, re-seeded when a
  script loads; one generator shared by every flicker. At each change `level = base × (100 + r % 901) / 1000`
  (`base` = the level before the flicker), the next change `30 + r % 91` ms later.
- Outputs keep their level until a step changes it (SS-R6). Everything is off at power-on.
- **Sound** (SS-C4, SS-R7): sound N is the file `/NN/001.mp3` on the card (one file per folder, folders 01-99).
  `audio_single` sends DFPlayer command `0x0F` with parameter `(N << 8) | 1`; `audio_loop` sends `0x17` with
  parameter `N` (the player loops that folder: its one file); `audio_stop` sends `0x16`. While a script is
  stored the ambient loop is off, and script runs never play the default trigger sound or pulse the CC roles.
- **DMX** (SS-D6-D9): with `dmx_port = s4` and a script that has a `dmx` line, the board sends DMX frames on
  expansion S4 all the time: 250 kbaud, 8N2, a break of at least 92 µs, a mark after break of at least 12 µs,
  start code 0, 512 slots, a frame every 25 ms. Channels start at 0.
- A script output always wins over a show using the same output (a new board has no shows; the example show
  is added only on request). A script is refused when it uses a terminal the pulse-and-recover effect owns.

## The trace (`trace`)

```json
"trace": {
  "settings": { "ambient": false },
  "events": [ { "t": 1000, "in": "TRIG1", "closed": true }, { "t": 1150, "in": "TRIG1", "closed": false },
              { "t": 5000, "power_on": true } ],
  "until": 9000,
  "expect": {
    "edges":   { "OUT1": [ [1100, 1000], [1500, 0] ] },
    "samples": [ { "t": 2000, "out": "OUT2", "level": 500, "within": 30 } ],
    "flicker": [ { "out": "OUT3", "from": 1100, "to": 3100, "then": 1000 } ],
    "dmx":     { "1": [ [1100, 255], [1500, 0] ] },
    "sound":   [ [1100, "play", 3], [5000, "loop", 2], [6000, "stop"] ] } }
```

- **Time 0** is a sync point: the board has been up for at least 3 s with every input open, the script saved,
  `trig_debounce_ms = 100`, `trig_release_ms = 100` and the case's settings applied. `t` is in milliseconds
  from there. A tester board waits for the board under test to come up, then starts its clock.
- **`events`**: an input closing or opening at exactly `t`, or `power_on`: the board is switched off and on at
  `t` (it boots at `t`, so its 2 s boot lockout runs from `t`). Only a tester that switches the board's power
  can replay a `power_on` case.
- **`edges`**: for each listed output, every change of its level from 0 to `until`, as `[t, level]`. A level
  change must happen at a time in `[t, t + 20]`. An output listed with `[]` must not change.
- **`samples`**: the output's level at `t` is `level ± within` (for fades).
- **`flicker`**: between `from` and `to` the output takes at least three different levels, all at or below
  `then`; from `to + 20` on it is at `then` (until its next listed change).
- **`dmx`**: every change of a channel's value in the frames sent, as `[t, value]`; a change must be in the
  first frame sent at or after `t`, within `[t, t + 30]`.
- **`sound`**: the DFPlayer frames that play, loop or stop (commands `0x0F`, `0x17`, `0x16`), in order, each
  sent within `[t, t + 300]`. Other frames (volume, queries, ACKs, the start-up reset) are not compared.
- Every output not listed in `edges`, `samples` or `flicker` must stay off from 0 to `until`.
