# Make it bigger

The starter kit is the whole pattern in small: **an input starts an effect, and the effect turns outputs on and off,
waits, and plays sounds.** Everything bigger is more of the same. Some ideas, each a small step from the kit:

## More with the same board

| You want | Do this | Guide |
|---|---|---|
| A second or third light | OUT3 and OUT4 work like OUT1: plus to +, minus to -. Give each its own effect, or add lines to "big moment" | [2](../02-lamp-on-out1.md) |
| A flickering haunted lamp | `flicker out1 for 5` in an effect | [2](../02-lamp-on-out1.md) |
| A jump-scare air valve | a DC 12 V air valve (solenoid valve) on an OUT, with a diode across its coil | [7](../07-air-valve-on-out.md) |
| A pump, a motor or a pop-up | a 12 V DC motor or pump on an OUT | [10](../10-pump-or-motor-on-out.md) |
| Fog on cue | a fog machine with a socket labelled **DMX IN**, through the DMX add-on (`dmx1 = on`, `sleep 4`, `dmx1 = off`). Never its wired remote: on many machines that cable carries mains | [6](../06-fog-over-dmx.md) |
| A longer pixel strip | over about 15 LEDs at full white, give the strip its own 5 V supply and join only DATA and GND to the board | [5](../05-pixel-strip.md) |
| A doorbell or step mat | wired like the button, on TRIG1 or TRIG2 | [1](../01-button-on-trig1.md) |
| A bigger power budget | a bigger 12 V supply on the screw terminal input (up to 10 A for the whole board) | [8](../08-powering-the-board.md) |
| A second board in the next room | CONTACT1 of the first board into TRIG1 of the second | [9](../09-chaining-two-boards.md) |

## More in the script

A board runs up to 8 effects at once, each with its own input. Some lines to try (the full language is
`firmware/SHOW-SCRIPT.md`):

```
effect creak:
  wait for trig2
  fade out3 to 60% over 4      # a slow glow
  flicker out3 for 3           # then a failing-bulb flicker
  out3 = off
  sleep 30                     # a rest: nothing restarts it for 30 s
```

- Each output belongs to one effect: if two effects need a light, give each its own OUT.
- A `sleep` at the end of an effect is a rest: presses during it are ignored.
- Times can be `sleep 200 ms`, `sleep 2.5`, `sleep 3 min`.

## Things that plug into the wall

Store-bought props and 120 V lamps plug into the wall. **These guides never show mains wiring.** The recommended way
is a listed relay-controlled outlet box, switched from an OUT or a relay with low voltage only: `docs/ac-control.md`.
Opening anything that plugs into the wall is for someone who knows mains wiring.

**Fog machines are different:** switching a fog machine's wall power also switches its heater, so it does not fog on
cue. Use a machine with a **DMX IN** socket (guide 6), never its wired remote.

## Always

- Power off while you wire; power on last.
- Stay inside the ratings: 5 A per output, 10 A for the whole board, relays per the silkscreen of your board's revision
  (every board built so far is **rev A**: its silk says "RELAYS: <=30V DC ONLY"), and through the case's 3.5 mm relay
  jacks only 0.3 A at 30 V DC, signals only (the plug shorts the contacts for a moment as it goes in).
- Use a listed power supply with over-current protection: the board has no fuse.
- Fit a diode across every solenoid, valve coil, relay coil or brushed motor on an OUT (a brushless fan has its own driver; the board's own diode on each OUT covers short leads).
