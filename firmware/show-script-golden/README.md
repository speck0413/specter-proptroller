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
| `case.json` | `spec` (rule ids), `sounds` (a name → number list the protocol still carries; always `{}` here and never read: a sound is its number), `settings` (board settings before the script is saved), and `check` and / or `trace` |

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
3. `#` to the end of the line is a comment. Trailing spaces are removed, then one trailing `.` (`wait 3.`
   is `wait 3`, `wait 0.4.` is `wait 0.4`; the `.` inside `0.4` stays).
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
| `wait <time>` / `wait for <time>` | SS-L: after `wait` (or `wait for`), a word that starts with a digit or `.`, or is a time setting (SS-G6), is a time: the step `sleep`, as for `sleep <time>` (the older spelling, still read the same). After `wait for`, an input or an input's name (SS-N) comes first: when it is also a time setting the line waits for the input, with W-waitname. A bare `wait` and an input or an input's name (`wait trig1`) is M-waitform; any other word after a bare `wait` is read as a time (`wait soon` is M-time). A first line `wait 2` is M-first (it is not a `wait for <input>`). |
| `fade <target> to <level> over <time>` | dimmable targets only; the level must be a percentage, `on` or `off` |
| `flicker <target> for <time>` | dimmable targets only |
| `audio_single = <sound>` / `audio_loop = <sound>` | `=` or `:`; surrounding `'` or `"` are removed from the sound |
| `audio_volume = <level>` | `=` or `:`; `N%` (0-100, decimals allowed, a space allowed before `%`), `on`, `off` or `0`; the step `{ "line", "op": "audio_volume", "volume" }` with the player's volume `round(N × 30 / 100)`, halves up, from the decimal text (`on` 30, `off` and `0` 0); a sound line (SS-R1); anything else is M-volume |
| `audio_stop` | |
| `limit <N> run(s) per <time>` | SS-G3: N is 1-20 or a count setting; the time is a time or a time setting, at most 60 min; no step (an effect field) |
| `pulse <target> for <time>` | SS-G5: targets `out1`-`out4`, `rly1`, `rly2`, `cc1`, `cc2`; the time a time or a time setting |
| `audio_loop = off` / `false` / `""` | SS-R7: the background loop off (the step `audio_loop_off`); a bare `audio_loop =` is still M-nosound |
| sound lines before the first `wait for` | SS-R1: `audio_single`, `audio_loop`, `audio_volume`, `audio_stop` may come first; M-first applies to the first other line |

- **Settings as times and counts** (SS-G6): where `wait`, `sleep`, `pulse` or `limit` take a time, a word that is not a time
  may be one of `out1_on_ms`, `out2_on_ms`, `out3_on_ms`, `recover_time_ms`, `cooldown_ms`, `trigger_out_pulse_ms`,
  `role_pulse_ms` (or a profile key alias of one, compared without case); where `limit` takes a number of runs,
  `runs_per_cooldown` (or its alias). The step table holds the setting's own name.

- **Level**: `on` (100 %), `off` (0 %), or `N%` with N from 0 to 100, decimals allowed, a space allowed before
  `%`. In the table an output level is permille (`50%` → 500, `12.5%` → 125, rounded to the nearest
  permille) and a DMX level is 0-255 (`round(N × 255 / 100)`, `on` = 255). Rounding is to the nearest, halves
  up (`50%` → DMX 128), computed exactly from the decimal text (no binary floating point surprises).
- **Time**: a number (whole or decimal) and an optional unit, a space allowed between: `ms`, `s`, `sec`, `secs`,
  `second`, `seconds`, `min`, `mins`, `minute`, `minutes`; no unit means seconds. Rounded to the nearest
  millisecond, halves up. 0 to 24 hours.
- **Sound**: a whole number, 1 to 3000 (DFPlayer datasheet: the folder "MP3" holds tracks 0001 to 3000). Any other
  word (a file name included) is M-sound; a number outside 1-3000 is M-soundrange.

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
| M-unknown | `line {n}: I do not understand "{text}". A line is effect, wait for, an output = on/off/N%, wait, fade, flicker, audio_single, audio_loop, audio_volume or audio_stop.` |
| M-unknown, hints | instead of the above for the commonest mistakes: a line starting `turn ` gets `line {n}: write it as three lines: "out1 = on", "wait 2", "out1 = off".`; `play {v}` gets `line {n}: to play a sound, write "audio_single = {v}" (a sound is its number: the card's mp3/0001-<name>.mp3 is 1).` (a leading `sound ` is dropped from `{v}`: `play sound 1` gets `"audio_single = 1"`; a `{v}` that is not a whole number becomes `1`); `delay {v}` or `pause {v}` gets `line {n}: to wait a time, write "wait {v}".`; an output name, a space and more (`out1 on`) gets `line {n}: write it as "{x} = {rest}".` |
| M-waitform | `line {n}: write it as "wait for trig1".` (`wait`, `wait for`, or `wait` and an input without the `for`) |
| M-fadeform | `line {n}: write a fade as "fade out1 to 50% over 2".` |
| M-flickerform | `line {n}: write a flicker as "flicker out1 for 3".` |
| M-nosound | `line {n}: say which sound by its number, for example "audio_single = 1" (the card's mp3/0001-<name>.mp3).` |
| M-input | `line {n}: there is no {x} on this board. The inputs are trig1, trig2 and user1 to user3.` |
| M-learn | `line {n}: {x} is a learn-mode button (buttons = learn). Set buttons to triggers to use it in a script.` |
| M-output | `line {n}: there is no {x} on this board. The outputs are out1 to out4, rly1, rly2, cc1 and cc2.`; for `pixel` (or `pixels`): `line {n}: the pixel strip is not in scripts yet: make a show for it on the Shows tab, or use a sketch.` |
| M-dmxch | `line {n}: there is no DMX channel {k}. DMX channels are 1 to 512.` |
| M-level | `line {n}: "{v}" is not a level. Use on, off or a percentage from 0% to 100%.` |
| M-onoff | `line {n}: {x} can only be on or off, not {v}. Dimming works on out1 to out4.` |
| M-nofade | `line {n}: {x} cannot fade. Fading works on out1 to out4.` |
| M-noflicker | `line {n}: {x} cannot flicker. Flickering works on out1 to out4.` |
| M-time | `line {n}: "{v}" is not a time. Write seconds (wait 2.5) or add a unit: ms, s or min.` |
| M-toolong | `line {n}: that is longer than 24 hours.` |
| M-sound | `line {n}: "{v}" is not a sound number. Sounds are numbered 1 to 3000: Studio's SD card tool puts them on the card for you (sound 5 is mp3/0005-<name>.mp3).` |
| M-soundrange | `line {n}: sound {k} is out of range. Sounds are numbered 1 to 3000: Studio's SD card tool puts them on the card for you (sound 5 is mp3/0005-<name>.mp3).` |
| M-volume | `line {n}: "{v}" is not a volume. Use a percentage from 0% to 100%, or off.` |
| M-dmxoff | `line {n}: {x} needs the DMX add-on, and it is not turned on. Turn it on with the setting dmx_port = s4.` |
| M-role | `line {n}: {x} is set to {role} ({x}_role). Set {x}_role to shows to use it in a script.` with `{role}` = `pulse on every trigger` (trigger_repeat), `pulse when a sound starts` (sound_trigger) or `not used` (none) |
| M-owned | `line {n}: {x} belongs to the built-in pulse-and-recover effect. Turn the effect off (setting effect = none) or use another terminal.` |
| M-lamp | `line {n}: {x} is the ready lamp (setting out4_ready_lamp). Turn it off to use {x} in a script.` (out4, on a board with `out4_ready_lamp` on and the built-in effect off) |
| (M-shared) | gone: two effects may set the same output; they take turns (SHOW-SCRIPT.md SS-R8) |

`limit` and `pulse` (SS-G) have their own messages, checked in this order on their line:

| Id | Message |
|---|---|
| M-limitform | `line {n}: write it as "limit 3 runs per 3 min".` |
| M-limitruns | `line {n}: "{v}" is not a number of runs. Use 1 to 20, or the setting runs_per_cooldown.` |
| M-time | as above, for the limit's time |
| M-notatime | `line {n}: {v} is a number of runs, not a time.` (a count setting where a time goes, in `wait`, `pulse` or `limit`) |
| M-limitlong | `line {n}: a limit's time is at most 60 minutes.` |
| M-twolimits | `line {n}: effect "{name}" already has a limit (line {m}). An effect has one.` |
| M-pulseform | `line {n}: write a pulse as "pulse cc1 for 200 ms".` |
| M-nopulse | `line {n}: {x} cannot pulse. Pulses work on out1 to out4, rly1, rly2, cc1 and cc2.` (a DMX channel; then M-time, M-notatime, M-toolong) |
| M-pulseshort | `line {n}: {x} is a relay or a contact: pulse it for at least 0.1 s.` (a literal time under 100 ms; a setting is held to 100 ms when it runs) |

A pulse line then has the target checks of any line that changes a target (M-dmxoff, M-role, M-owned, M-lamp). For M-relayfast a pulse leaves its relay or contact off with no time since its last change.

Then, per effect, on its header line: **M-empty** `line {n}: effect "{name}" has no lines.`, **M-first**
`line {n}: effect "{name}" must start with "wait for", for example "wait for trig1".`, **M-effectlines**
`line {n}: effect "{name}" has {k} lines. An effect can have at most 64.` (blank and comment lines do not
count). Then for the whole script (line 0): **M-effects** `the script has {k} effects. A board runs at most 8.`,
**M-lines** `the script has {k} lines. A board takes at most 256.` (every line counts, blank and comment
lines too), **M-bytes** `the script is {k} bytes. A board takes at most 8192.` (the raw text). After the per-effect checks, on the line that
switches it: **M-relayfast** `line {n}: {x} switches again after {k} ms. Relays and contacts switch at most every 100 ms: wait at least 0.1 between its changes.`
(rly1, rly2, cc1, cc2 set to another level less than 100 ms of wait, fade and flicker time after its previous
change; a `wait for` in between resets it, and two lines with no time between them do not count).

**Warnings** (never block a save), after the errors of the same line:

| Id | Message |
|---|---|
| W-stillon | `line {n}: effect "{name}" ends with {x} still on. It may stay on until the next trigger.` on the header line, one per target left above 0 % after the last line (in the order the targets first appear in the effect) |
| W-twosound | `line {n}: effects "{a}" and "{name}" both play sound. There is one sound player: a new sound replaces the one playing.` on the header line of every effect that plays sound after the first such effect `{a}` |

An empty script (no lines, or only blank and comment lines) is valid and removes the stored script.

### Cases that changed on purpose

- `e41-more-hints`: `wait 3` and `wait 0.5` wait a time (SS-L), so those lines get no hint.
- `e26-shared`, `e35-shared-dmx`: two effects may set the same output (SS-R8, they take turns): no M-shared error.
- `t07-release-counted-after-the-return`: `trig1_wait = ready` is the default (SS-R4): the press during the run,
  held past its end, starts the next run when the effect can (2250); the earlier answer (2700) is `t29` with
  `trig1_wait = return`.
- `t23-a-shared-output-waits-and-is-remembered`, `t25-pressed-together-the-first-goes-first`: the remembered
  input is now held until the outputs come free (memory with check, SS-R8); let go earlier it is dropped
  (`t31`, `t32`).
- `t29-release-after-the-return-with-trig1-wait-return`: the third press comes 0.25 s after a release (the
  `return` rule needs `trig_debounce_ms + trig_release_ms` open, 0.2 s here; at 0.2 s it would not count).

### Names given to terminals (SS-N)

Only `w13-wait-for-a-name-that-is-also-a-setting` sets a name (TRIG2 named `out1_on_ms`, for W-waitname). A board may know its terminals by a wiring name
(settings `out1_name` … `user3_name`) and a profile terminal alias. Each is compared lower case with runs of
spaces made one. Where a target or an input is read and the word is not a terminal's own name (nor `dmx<N>`),
the names of the outputs (for a target) or of the inputs (for `wait for`) are looked up. A name is usable when
it is one word, `[a-z][a-z0-9_]*`, not a terminal's own name, and not `effect…`, `wait…`, `sleep` /
`sleep<digit>…`, `fade`, `flicker`, `pulse`, `limit`, `takes`, `audio_single`, `audio_loop`, `audio_stop`, `on`,
`off`, `to`, `over`, `for`, `turn` or `play`. The step table always holds the terminal. `{x}` in M-learn, M-onoff,
M-nofade, M-noflicker, M-nopulse, M-pulseshort, M-dmxoff, M-role, M-owned, M-lamp, M-relayfast and
W-stillon becomes `{name} ({terminal})`
when the line (for M-relayfast and W-stillon: the effect's first line that used one) gave a name; `{x}_role`
in M-role is always the terminal. Messages:

| Id | Where | Message |
|---|---|---|
| M-twonames | instead of M-input / M-output when two terminals of that kind have the name | `line {n}: "{w}" is the name of both {t1} and {t2}. Write {t1} or {t2}, or give them different names.` |
| M-badname | instead of M-input / M-output when one terminal has the name and it is not usable; also first on a line whose part before its first `=` (`=` before any `:`) is a name that starts as another line would (`wait…`, `sleep`, `fade`, `flicker`, `pulse`, `limit`, `takes`) | `line {n}: "{w}" is the name of {t}, but a script cannot use it: {why}. Write {t}, or give it another name.` with `{why}` = `a name in a script is one word`, `a name in a script starts with a letter and has only letters, digits and _`, `it is the name of a terminal` or `it is a word of the script language` |
| W-nameis | warning, the first line that uses a terminal's own name which another terminal was given | `line {n}: {x} is also the name given to {t}. Here {x} means the terminal {x}.` |
| W-waitname | warning, a `wait for <w>` where `<w>` is an input's name (SS-N) and also a time setting (or a setting's alias): the line waits for the input | `line {n}: "{w}" is the name of {t} and of the setting {s}. "wait for {w}" waits for {t}; to wait the setting's time, write "wait {s}".` |

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

The added lines add, only where an effect has them: after `"line"`,
`"limit": {"runs": N or "<setting>", "ms": T or "<setting>"}`; the step `{ "line", "op": "audio_loop_off" }`; the step `{ "line", "op": "pulse", "target", "ms" }`; and
the step `{ "line", "op": "audio_volume", "volume" }`; and
on a `sleep` or a `pulse` whose time is a setting, `"setting": "<name>"` instead of `"ms"`.

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

- Every effect steps on the 1 ms input samples (the outputs reach the pins on the engine's 10 ms tick); *released*
  updates on every sample. A `wait for` passes once released and `now - max(closed_since, boot_end) >=
  trig_debounce_ms`.
- Each step runs at most once per sample pass. After its last step the effect is back at its top; its sound lines
  run and a start that waited may begin on the same sample. `sleep`, `fade` and `flicker` count from the sample
  their line is reached.
- Fade: `level = from + (to - from) × (now - start) / ms` with integer division toward zero, each tick; the
  exact target at the end.
- Flicker: xorshift32 (`x ^= x << 13; x ^= x >> 17; x ^= x << 5`), seed `0x2545F491`, re-seeded when a
  script loads; one generator shared by every flicker. At each change `level = base × (100 + r % 901) / 1000`
  (`base` = the level before the flicker), the next change `30 + r % 91` ms later.
- Outputs keep their level until a step changes it (SS-R6). Everything is off at power-on.
- **Sound** (SS-C4, SS-R7): sound N is the card's `/mp3/NNNN…` file (the player reads the four digits:
  `mp3/0003-scream.mp3` is sound 3). `audio_single` sends DFPlayer command `0x12` with parameter `N`; `audio_loop`
  sends `0x12` with parameter `N` and stays the background loop: the mp3 folder has no loop command, so each time
  the track ends the same `0x12` goes again (within 100 ms of the player's "finished"; a trace does not list
  these); after an `audio_single` ends (the sound's own length, plus 300 ms) the loop starts again (`0x12` once
  more); `audio_volume` sends `0x06` with its volume at once, and the script's sounds use it from then on
  (`audio_single` and the loop send it before they play) until another `audio_volume` or a restart; until a
  script sets one, `audio_single` plays at `trigger_volume` and the loop at `ambient_volume`. `audio_loop =
  off` sends `0x16` when the loop is playing; `audio_stop` sends `0x16` and ends the loop. Setting the loop that
  plays changes nothing. While a script is stored the ambient loop is off, and script runs never play the default
  trigger sound or pulse the CC roles. (`t30` assumes the simulator's 3 s for a sound the card list does not time.)
- **DMX** (SS-D6-D9): with `dmx_port = s4` and a script that has a `dmx` line, the board sends DMX frames on
  expansion S4 all the time: 250 kbaud, 8N2, a break of at least 92 µs, a mark after break of at least 12 µs,
  start code 0, 512 slots, a frame every 25 ms. Channels start at 0.
- **Start rules** (SHOW-SCRIPT.md SS-R4): the first `wait for` on TRIG1 / TRIG2 follows `trig1_wait` /
  `trig2_wait`. `ready` (the default) is the pulse-and-recover effect's TRIG1 gate (`STATE-MACHINE.md` SM-T:
  *released* is cleared when a run starts, set after `trig_release_ms` open outside a used-up window; the hold
  to confirm counts closed samples while the effect could start, from the later of the closure and the moment it
  could; a hold confirmed on a sample starts even if the input opens before the run's first step). `return` reads
  the input as the library's trigger does: *released* after `trig_debounce_ms + trig_release_ms` open (from the
  last change), cleared by the press; a press is `trig_debounce_ms` closed while released. A press is dropped in
  the boot lockout (the input must be let go and pressed again), while paused and in a used-up window; otherwise
  it is remembered (one) and starts the effect when it could start, its own run included, if the input is still
  held then. USER1-3 (and a runner given no rule) follow the rule above (released after arrival).
- **Turns** (SS-R8): an effect could start when it is at its top, the boot lockout is over, it is not paused, its
  limit is not used up and no running effect sets one of the outputs it sets. A press while it could not start
  only because of that is remembered (one). Starts on one sample go in the order of the inputs (TRIG1, TRIG2,
  USER1 ...; the script's order for the same input): an effect that shares outputs with one that started on that
  sample is remembered. A remembered press starts only if its input is still held when the effect could start,
  otherwise it is dropped (memory with check). When outputs come free, a waiting remote start (no hold needed)
  goes before a remembered press.
- **Remote starts** (SS-R10): a start by name (the trace's `control` event with the effect's name, spaces as `_`)
  begins on the next sample when the effect could start, otherwise one waits; `cancel` empties them.
- A pulse ends on the first tick at or after its time.
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
  can replay a `power_on` case. `{"t", "control": "<effect name>" | "cancel"}` is that control sent at `t`
  (web, MQTT or serial; a refused one changes nothing), and `{"t", "set": {"<key>": value}}` a setting changed at
  `t` (as on the serial console, so `remote_trigger_enable` is allowed).
- **`edges`**: for each listed output, every change of its level from 0 to `until`, as `[t, level]`. A level
  change must happen at a time in `[t, t + 20]`. An output listed with `[]` must not change.
- **`samples`**: the output's level at `t` is `level ± within` (for fades).
- **`flicker`**: between `from` and `to` the output takes at least three different levels, all at or below
  `then`; from `to + 20` on it is at `then` (until its next listed change).
- **`dmx`**: every change of a channel's value in the frames sent, as `[t, value]`; a change must be in the
  first frame sent at or after `t`, within `[t, t + 30]`.
- **`sound`**: the DFPlayer frames that play, loop or stop (`play` and `loop` are both command `0x12` with the
  sound's number, `stop` is `0x16`), in order, each sent within `[t, t + 300]`; a loop's `0x12` sent again when its
  track ends is not listed. Other frames (volume, queries, ACKs, the start-up reset) are not compared.
- Every output not listed in `edges`, `samples` or `flicker` must stay off from 0 to `until`.
