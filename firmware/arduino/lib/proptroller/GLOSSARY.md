# Glossary

**BOOTSEL.** The button on a Raspberry Pi Pico. Hold it while plugging in the USB cable and the Pico shows up as
a USB drive (`RPI-RP2`, Pico 2 W: `RP2350`): copy a `.uf2` file onto it to flash it.

**COM / NO / NC.** A relay's three terminals: common, normally open (connected to COM while the relay is on) and
normally closed (connected to COM while it is off). To press a button, wire it to COM and NO.

**Contact closure.** Two wires that the board connects together, like pressing a button. Nothing is
powered: the device on the other end decides what "closed" means. CC1 and CC2 are contact closures made
with an optocoupler (a light inside a chip), so the two boards stay electrically separate. Signals only: up to
30 V DC, and plan on about 2 mA; tip is +, sleeve is − (polarity matters). The silkscreen calls them CONTACT1
and CONTACT2.

**Cooldown.** Rest time after an effect has run before it can run again. Presses during the cooldown are
ignored. In a sketch: `prop.advanced().when(TRIG1)...cooldown(seconds(30)).allowRunsPerCooldown(3)` lets it run up
to three times before it rests (advanced layer, `#include <ProptrollerAdvanced.h>`). In an effect script, a
`wait` at the end of the effect is its rest.

**Cue.** A name that starts effects (advanced layer): `prop.advanced().when(TRIG1).sendCue("lights")` on one
board starts every effect made with `prop.advanced().onCue("lights")` on the boards around it.

**Debounce.** Buttons and switches bounce: for a few milliseconds a single press looks like many. The board
waits until an input has been steady for 100 ms (triggers; a tap shorter than that never counts) or 30 ms (buttons) before believing it.

**DFPlayer Mini.** The small MP3 player module that plugs into the board. It reads a microSD card and
drives a speaker directly.

**DMX.** The cable standard stage lights and fog machines use (3-pin XLR). The board sends it through the DMX
add-on on the expansion header; a fog machine is driven only through its socket labelled DMX IN (SHOW-SCRIPT.md
section 7).

**Dry contact.** A relay contact that only switches: you bring your own power through it. RLY1 and RLY2 are
dry contacts (COM, NO = closed when on, NC = closed when off): on a rev A board up to 30 V DC, 10 A, never
mains.

**Duty / level.** How much of the time an output is on while it dims: 25 % means on a quarter of the time,
a thousand times a second. `percent(25)`. Dimming is for smaller loads: at most 2 A on a 12 V supply, 1 A on
24 V (fully on, an OUT carries 5 A).

**Effect script (show script).** A short text file of effects that the board checks and runs, with no compiling:
typed on the board's page (Script tab) or in the desktop app (firmware/SHOW-SCRIPT.md).

**Effect.** One thing a prop does when it is triggered: wait for an input, then switch outputs, wait and play
sounds. A board runs several effects at once.

**Env (environment).** A PlatformIO build target: one board with its settings (`esp32dev`, `picow`, ...), chosen
in the status bar or with `pio run -e <env>`.

**Flash.** To write a firmware into the microcontroller's memory (over USB the first time, then over Wi-Fi from
the board's page). Also the name of that memory.

**FQBN.** The Arduino tools' name for a board, for example `esp32:esp32:esp32` or `rp2040:rp2040:rpipicow`
(written in a sketch's `sketch.yaml`).

**Load.** Whatever you connect to an output: a lamp, a fan, a solenoid. Choose it within the terminal's
rating (hardware/README.md); the board does not measure its current.

**.local.** A name such as `proptroller.local` that a device announces on your network (mDNS): the board's Name
setting, made into an address. When another board already uses that name, the board adds the end of its id
(`proptroller-ac2c.local`); Settings → Wi-Fi and the serial log show the one it uses. Some phones
(many Android ones) cannot look it up: use the board's IP address instead (the desktop app or your router shows it).

**MOSFET.** The electronic switch behind OUT1-OUT4. It can switch fast enough to dim (PWM).

**MQTT.** A messaging system used in home automation: devices publish messages on named topics to a
broker (a small server), and anyone subscribed to a topic gets them.

**Open-collector output.** A sensor output that connects its wire to ground when it triggers and leaves it
floating otherwise; it works on a TRIG jack like a button (signal to the tip, ground to the sleeve).

**Pixel strip (WS2812, NeoPixel).** A strip of LEDs where every LED can have its own colour. One data
wire carries the colours.

**PlatformIO.** The build tool (and VS Code extension) that compiles the firmware and flashes it.

**Priority.** When two effects want the same output at the same time, the higher priority wins.

**PWM (pulse-width modulation).** Dimming by switching fully on and off very fast (1000 times a second
here). Lamps and LED strips look dimmer; motors run slower. Up to 2 A per output on a 12 V supply, 1 A on 24 V.

**Relay.** A switch moved by an electromagnet: it clicks, it can switch things the board does not power,
and it cannot dim.

**Role.** A setting that gives an output a fixed job without a show (for example OUT3 repeats the trigger,
OUT4 pulses a sound box), changed in Settings on the board's page.

**Sensor high (`sensor_high`).** The trigger input type for a powered sensor whose signal goes high when it
triggers (a PIR motion sensor): set `trig1_input` / `trig2_input` to `sensor_high`. The default, `switch`, is for
anything that closes tip to sleeve.

**Setup hotspot.** The Wi-Fi network a board always makes, also while it is on yours: `Proptroller-XXXX`, password
`password` (a simple default: change it under Settings → Wi-Fi → Setup hotspot password), page at http://192.168.4.1/.

**Sketch.** An Arduino program: a folder with a `.ino` file holding `setup()` and `loop()`.

**Soft start.** Bringing a motor or fan up to speed over a fraction of a second instead of all at once.

**Trigger.** What starts an effect: a button, a pressure mat, a motion sensor, another board, a timer.
TRIG1 and TRIG2 are the 3.5 mm jacks for them: closing tip to sleeve is a press.

**TRS (tip, ring, sleeve).** The three contacts of a 3.5 mm stereo plug: the tip at the very end, the ring
behind it, the sleeve the long part nearest the cable. On TRIG jacks: tip = signal, ring = 3.3 V for a powered
sensor, sleeve = ground. On CONTACT jacks: tip +, sleeve −.

```
   tip  ring  sleeve
   [=]--[=]--[=========]==== cable
```

**UF2.** The file type a Pico takes on its BOOTSEL drive: copying one onto the drive flashes it.
