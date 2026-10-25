# LED pattern golden vectors

The shared test set for the LED pattern interpreter. Specter Studio compiles a pattern to a small program of
numbers (its `src/patterns/compile.ts`) and runs it in its preview (`src/patterns/vm.ts`); a board runs the same
program bytes in its own interpreter (`firmware/arduino/lib/proptroller/src/proptroller/pattern_vm.cpp`). Both
must light every light the same, byte for byte. Studio writes these files from its interpreter
(`PATTERN_GOLDEN_OUT=<this folder> npx vitest run src/patterns/patterns.golden.test.ts`, the patterns are in its
`src/patterns/golden/cases.ts`); the firmware's `test/test_pattern_vm` and Studio's `patterns.golden.test.ts`
check them.

## Files

`limits.json`: the board's fixed limits for one pattern, the same in both (`code` words, `globals`, `functions`,
`controls`, `stack` values, call `depth`, `locals` of every running function, list `cells` (a list takes its
values and one more), `steps` one frame may take).

`cases/<name>.json`, one per case:

| Field | What |
|---|---|
| `name`, `source` | the case and the pattern's text (for reading; the board never sees source) |
| `program` | the program's bytes, base64 (the layout is in `firmware/PROTOCOL.md`, "LED patterns") |
| `pixels` | the strip's length |
| `seed` | the random source: xorshift32 from this seed (`x ^= x << 13; x ^= x >> 17; x ^= x << 5`), 16 bits a call (`x >> 16`) |
| `steps` | after the top-level code ran once: `{"frame": ms}` runs one frame of that many milliseconds; `{"control": name, "values": [...]}` calls a control's function with its values in 16.16 (an unknown name does nothing) |
| `frames` | the lights after every frame step, in order: red, green, blue per light, as hex |
| `error` | only when the pattern stops: the step that failed (`-1` the top-level code) and the plain message (no line number: a board is not sent the lines) |

The time limit plays no part here (only the step budget stops a pattern), so the vectors do not depend on how
fast a computer is.
