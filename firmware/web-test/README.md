Browser tests of the phone UI (`firmware/web`) against a running simulated board (the C++ host build):

```sh
cd firmware/arduino && pio run -e sim
.pio/build/sim/program --port 18080 --discovery-port 0 &          # a fresh simulated board
cd ../web-test && npm install && CHROMIUM=/path/to/chromium node ui-test.mjs . http://127.0.0.1:18080/
```

The other tests take the same arguments, each against a fresh board: `settings-test.mjs` (the Settings page),
`wifi-test.mjs` (Settings → Wi-Fi at 390 px: the board's address, the network combo box with the simulator's
network list, pick or type a name, Rescan and its 10 s limit),
`alias-test.mjs` (a profile's words), `script-test.mjs` (the Script tab: check, the messages, save, run, kept
after a restart), `shows-test.mjs` (at 390 px: a fresh board with no shows, Add an example show, Link on while
held and for a time, + New, replace with Save as, delete from the list and the editor asked in the page, two
phones: board-picked numbers, "changed on another phone"), `record-test.mjs` (Record effect at 390 px: TRIG1, a
slider on OUT1 and RLY1 held, the board following live, Stop and the board doing it back, Keep into the stored
script beside an effect already there and passing the board's check, TRIG1 running it, the same name asked before
it is replaced, an output another effect uses refused, STOP during the playback, focus lost and a dropped
connection while recording), `views-test.mjs` (the Script tab's Script and blocks / Script only / Blocks only:
remembered, stacked at 390 px and side by side when wide, block edits writing the text with comments and blank
lines kept, a recorded effect opening in the blocks, drag by ⠿), `live-test.mjs` (at 390 px: the framing headers, a dead
connection seen within 8 s with the age of the values, STOP confirmed or "STOP not confirmed", errors in the alert
bar, Resume on a paused board, too many pages and a page in the background, 48 px controls and 16 px fields on every
tab, contrast in both themes, no sideways scrolling at 360 px and 200 % zoom, tablist / names / keyboard holds, an
output with a role, Recover (hold) let go when another phone saves, the remote trigger switch and its loss with the
connection, the effect's status in plain words).

`update-test.mjs` (the Firmware tab: files checked in the page, a wrong SHA-256, a refusal with "stop all", an
update with its restart, pending and valid, a rollback with the page lock on) needs a board started with
`--speed 10` (the 30 s self-check then takes 3 s) and makes its images with `python3 ../arduino/tools/ptclient.py
image` (`PTCLIENT=` another path to it).

`update-swap-test.mjs` is the same against a board whose bootloader installs the update at the restart, as on
the Pico W / Pico 2 W (`--speed 10 --swap-ms 20000`): the page says "the board is installing the update, this can
take up to 3 minutes" while the board is away.

`take-test.mjs` and `blocks-test.mjs` are the unit tests of `firmware/web/take.js` (take → effect script) and
`firmware/web/blocks.js` (text ⇄ blocks, round trip on every golden case and example), the modules Specter Studio
can share: `node --test take-test.mjs blocks-test.mjs`, no packages.

`firmware/arduino/tools/check-host.sh` runs take-test, blocks-test and all ten browser tests, each on a fresh board.
