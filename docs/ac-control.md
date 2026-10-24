# Switching AC (mains) loads: advanced

> **Advanced.** This page is about switching things that plug into a wall outlet: a 120 V lamp, a
> store-bought prop. Mains (120 V AC) can kill and start fires. If you are not sure, use the first method below and do
> not open anything that plugs into the wall.
>
> **Not for fog machines.** Switching a fog machine's wall power also switches its heater (minutes of warm-up), and
> its pump runs only while its remote is pressed, so it does not fog on cue. Never wire into a fog machine's wired
> remote: on many machines that cable carries mains. Use a machine with a **DMX IN** socket and the DMX add-on
> (`wiring-guides/06-fog-over-dmx.md`).

The Proptroller itself runs on low-voltage DC (9-24 V). Its ratings are for a 12-24 V supply (12 V is the primary use case); below
12 V (down to the 9 V minimum), derate: the OUT FETs see less gate drive and run hotter. The rules:

- **The recommended way to switch an AC load is off the board:** an outlet box that already has switchable relays in
  it (a listed relay-controlled outlet, or one you built), driven from the Proptroller with low voltage only. The
  Proptroller just turns a channel on and off; nothing on it touches mains. The three methods below show how to wire it.
- **Rev A relays: AC is experimental, for prove-out only, not considered safe, and recommended against until rev B.** On a rev A board (silk "RELAYS: <=30V DC ONLY") the relays are for low-voltage DC
  only. Use one of the methods below.
- **Mains never goes on a 3.5 mm jack**, nor on OUT1-4, CONTACT1-2 (CC1/CC2), TRIG, PIXEL, RAILS or the expansion header.
- Only a **rev B** relay may switch mains directly (120 V AC, 5 A resistive, 3 A inductive: "120VAC 5A (3A IND) / 28VDC 10A"), and only in the AC case variant
  described at the end of this page.

## What to buy: a relay-controlled outlet box

Look for an enclosed power strip or outlet box that switches its outlets from a **low-voltage control input**: two
screw terminals that accept a DC voltage (often "3-48 V DC" or similar) and turn the outlets on while that voltage is
present. Many have one outlet that is off until the input turns on and another that is on until the input turns on.

Search terms: "IoT relay outlet", "relay controlled power strip", "low voltage controlled outlet",
"DC control input power relay box", "3-48V DC trigger outlet".

Check before you buy:

- **It is listed** (a UL, ETL or CSA mark on the product, not only on the box it came in) and enclosed: you never open
  it or touch its mains side.
- **The outlet rating covers your load** (amps or watts, and "motor" or "inductive" if the load has a motor, a pump or
  a fan).
- **The control input range includes your Proptroller supply voltage** (9-24 V, usually 12 V), for methods 1 and 2.
- **The control input current** (usually on the label or in the manual, a few mA to a few tens of mA). Method 3
  needs it to be about 2 mA or less.
- **The polarity** of the control input (+ and −). Most inputs only work one way round.

Plug the box into a **GFCI-protected outlet**. A GFCI trips on a leak to earth; it does not protect against an
overload or a short between the wires, so the box's own breaker or fuse matters too.

## Method 1: from an output (OUT1-4)

OUT1-4 switch the board's own supply voltage (9-24 V; 12 V on a 12 V supply) onto the **+** screw. The box's input
current is tiny next to the output's 5 A rating.

```
  Proptroller OUT1 terminal                 relay-controlled outlet box
  ┌────────────────────────┐                ┌──────────────────────────────┐
  │ +  (switched supply) ──┼────────────────┼── control input (+)          │
  │ −  (GND)             ──┼────────────────┼── control input (−)          │
  └────────────────────────┘  low-voltage   │                              │
                              2-wire cable  │  [outlet] ─────── AC load    │
                                            │  [mains cord] ─── GFCI outlet│
                                            └──────────────────────────────┘
```

- Check the box accepts the supply voltage (a 3-48 V DC input covers 9-24 V) and get **+ and − the right way round**.
- Use only `on` and `off` on this output. A percentage (`out1 = 50%`) makes it pulse quickly (PWM), which the box's
  relay cannot follow.

```
effect porch lamp:
  wait for trig1
  out1 = on        # the box switches the lamp on
  wait 30
  out1 = off
```

## Method 2: from a relay (RLY1, RLY2) as a dry contact

The relay is a switch with no voltage of its own, so take a low-voltage source from the board's **RAILS** terminal
(VIN = the board's supply voltage, and GND). The whole loop stays low voltage. This works on rev A and rev B, and is the
method to use if you want the box on until the relay turns it off (wire NC instead of NO).

The relay switches the box's **(−)** side to GND, so COM sits at GND and nothing on the relay side carries unswitched VIN.
Put a **0.5 A fuse or PTC in the VIN lead** to the box: RAILS VIN has no fuse of its own (the board has none), and that lead is
the one that can short to something.

```
  Proptroller RAILS terminal                                    outlet box
  ┌─────────────┐        0.5 A fuse or PTC                   ┌─────────────────────┐
  │ VIN ────────┼──────────────[ F ]─────────────────────────┼── control input (+) │
  │             │                                            │                     │
  │ GND ───┐    │      Proptroller RLY1 terminal             │                     │
  └────────┼────┘     ┌─────────────────┐                    │                     │
           │          │   NO ───────────┼────────────────────┼── control input (−) │
           └──────────┼── COM           │                    │  [outlet] ── AC load│
                      │   NC  (unused)  │                    └─────────────────────┘
                      └─────────────────┘
```

For "on until the relay turns it off", move the box's (−) wire from NO to NC.

If your board sits in the DC enclosure, RLY1 and RLY2 come out on 3.5 mm TRS jacks: **tip = NO, ring = NC,
sleeve = COM**. Use a TRS plug: the box's (+) to RAILS VIN through the fuse, the box's (−) to the tip wire (the ring
wire for on-until-off), and the sleeve wire to RAILS GND. The sleeve is the plug's exposed metal, and wired this way it
sits at GND. Low voltage only on these jacks, never mains.

This needs a box whose control input is simply a load between its (+) and (−) screws, which is what these boxes have. If
the box's manual says its (−) must go to ground, use method 1.

```
effect porch lamp:
  wait for trig2
  rly1 = on        # NO closes: the box input's (−) reaches GND and the box turns on
  wait 10
  rly1 = off
```

## Method 3: from a contact output (CC1, CC2 on the CONTACT jacks)

CONTACT1-2 are optocoupler switches on 3.5 mm jacks: **tip = +, sleeve = −**, polarity matters, **≤30 V DC and
50 mA absolute maximum**. Like the relay, they need a source: take it from RAILS. As in method 2, switch the box's (−)
side so the plug's sleeve sits at GND, and put a **0.5 A fuse or PTC in the VIN lead** to the box.

```
  Proptroller RAILS                                                outlet box
  ┌──────────┐        0.5 A fuse or PTC                          ┌─────────────────────┐
  │ VIN ─────┼───────────────[ F ]───────────────────────────────┼── control input (+) │
  │          │                                                   │                     │
  │ GND ───┐ │      CONTACT1 jack (3.5 mm plug)                  │                     │
  └────────┼─┘     ┌──────────────────────────┐                  │                     │
           │       │   tip (+) ───────────────┼──────────────────┼── control input (−) │
           └───────┼── sleeve (−)             │                  └─────────────────────┘
                   │   (ring unused)          │
                   └──────────────────────────┘
```

- **How much current it can pass: count on about 2 mA.** The board drives the optocoupler's LED with about 9 mA
  from an ESP32, and less from a Pico (about 5-6 mA: the RP2040 / RP2350 pins default to 4 mA drive and the firmware
  does not raise it). Switched fully on (saturated, about 0.4 V across it) the LTV-217 datasheet guarantees about
  **2.4 mA** at 8 mA LED current; the "about 4 mA" from the lowest current transfer ratio (50 %) holds only with about
  5 V left across the opto, not when it is switched fully on. The fitted part is ordered as
  LTV-217-B-G, most likely the B rank (130 % minimum), but the datasheet does not confirm that the "B" in the part number
  is the rank, so do not count on it. **If the box's control input draws more than about 2 mA, or the manual does not
  say, use method 1 or 2 instead.**
- Wired the wrong way round, it does not switch at all.

```
effect porch lamp:
  wait for trig2
  cc1 = on
  wait 10
  cc1 = off
```

## The AC case variant: rev B relays on outlets (alternative)

The enclosure has an AC variant, for **rev B boards only**, with two outlets switched independently by RLY1 and
RLY2. The board's relays then carry mains. It is the advanced alternative to the outlet box above. Its build is
described in the AC case section of [the enclosure README](../enclosure/README.md).

- **Wire it with the cord unplugged.** Mains wiring must be done by someone who knows mains wiring.
- Plug it into a GFCI outlet, and put a **5 A inline fuse in each relay's COM wire** (a discrete inline fuse holder you
  add; the board has no fuse).
- If either relay carries mains, treat **both** relays' wiring as mains: the 3 mm gap between them is the only barrier.
- Unplug the mains cord before plugging USB into the MCU board or opening the box.
- The full list of rules and first-board checks is in [hardware/README.md](../hardware/README.md), under "Relay contacts".
