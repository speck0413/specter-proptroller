# Spectre Proptroller board (rev B): ratings, pinout and power

134 x 80 mm, 2 layers. Silkscreen: "SPECTRE PROPTROLLER rev B" (rev A, the first ordered design, differs as this page says; this repo holds rev B). Plug-in MCU: a 30-pin ESP32 DevKit **or** a Raspberry Pi Pico family
board (never both). The bottom silkscreen carries a short version of this page.

Provided as-is, with no warranty and no certification; you build and use it at your own risk: read
[../SAFETY.md](../SAFETY.md) before wiring anything, and above all before mains on a rev B relay.

## Ratings at a glance

Ratings are for a **12-24 V supply** (12 V is the primary use case). Below 12 V (down to the 9 V minimum), derate: the OUT FETs see
less gate drive and run hotter.

| Item | Rating | Limit set by |
|---|---|---|
| Input (barrel DC-005) | 9-24 V DC, **5 A** | jack rating |
| Input (5.08 terminal, parallel) | 9-24 V DC, **10 A** board total | +VIN copper (3.5 mm class), supply OCP |
| Protection | SMAJ24A TVS only; **no fuses, no reverse-polarity FET** | design choice (accepted risk AR-1, rev B): use a listed supply with OCP, ≤10 A; it shuts itself off on overload |
| OUT1-4 (rev A and B) | switched +VIN (high side, AO4485), **5 A each continuous**; **7.5 A for 5 s at ≤2 % duty, starting from idle (the FET cool)**, both at **VIN 12-24 V** (e.g. once every 6 min; pending a first-board heat test; below 12 V derate); **10 A only as a start-up surge under ~1 s**; **sum of everything ≤10 A**; barrel input ≤5 A (use the terminal input above 5 A) | FET junction temperature (thermal note below), SS56 freewheel, copper |
| OUT1-4 PWM | ≤1 A at 24 V, ≤2 A at 12 V (slow 4.7k turn-off heats the FET) | FET switching loss |
| RLY1-2 (rev B) | dry COM / NO / NC, **120 V AC 5 A resistive (3 A inductive) or 28 V DC 10 A resistive** per relay ("120VAC 5A (3A IND) / 28VDC 10A"); derate for DC motors and solenoids | SRD-05 contacts (Songle datasheet, below) and the SAF-1 insulation margin (a design margin, not a certification); no fuse. Rev A: **≤30 V DC only** (table below) |
| +5V buck (TPS54360B) | **2.5 A** continuous | inductor, catch diodes, thermal |
| +3V3 | from the MCU module's regulator, **0.1 A** for board use | DevKit AMS1117 / Pico RT6150 headroom |
| RAILS out (5.08 5P) | VIN 3 A, 5V 0.1 A, 3V3 0.05 A | daisy-chain feed for a small accessory, not a second board |
| PIXEL | 5 V, ≤1 A from the board (≈16 WS2812 at full white); inject 5 V for longer strips. **No bulk capacitor on the board**: fit 470-1000 µF, ≥6.3 V across +5V / GND at the strip input, and plug or unplug the strip only with power off. The data output is inverted (2N7002 stage): the firmware inverts it | +5V budget |
| SPK | DFPlayer amplifier output, 3 W max into 4 Ω (bridge-tied: never ground either wire) | DFPlayer |
| TRIG1 / TRIG2 | 3.5 mm TRS: tip = input (3.3 V logic, closes to sleeve = active LOW), ring = 3V3 via 220 Ω (≤15 mA for a powered sensor), sleeve = GND | GPIO, 220 Ω |
| CONTACT1-2 | isolated optocoupler output (LTV-217) across tip (+) / sleeve (−): **≤30 V DC, ≤50 mA absolute maximum**, polarity matters; **count on about 2 mA** switched (note below) | LTV-217 (IC 50 mA, VCEO 70 V abs; CTR at the board's LED drive) |

**CONTACT1-2 current.** 50 mA is the LTV-217's absolute maximum, not what it is guaranteed to pass. The board drives the opto LED
with about 9 mA from an ESP32 and about 5-6 mA from a Pico (the RP2040 / RP2350 pads default to 4 mA drive and the firmware sets
none). Saturated (VCE(sat) 0.4 V max at IF 8 mA, IC 2.4 mA), the guaranteed current is about 2.4 mA; the higher figure of about
4 mA (50 % minimum CTR) holds only with about 5 V left across the opto. **Count on about 2 mA**; for anything that draws more, use an
OUT or a relay (`../docs/ac-control.md`, methods 1 and 2).

## Relay contacts: ratings and insulation (rev B, SAF-1)

| Board | Relay contact rating | AC |
|---|---|---|
| rev B (silk "rev B") | **120 V AC 5 A** resistive, **3 A** inductive, or **28 V DC 10 A** resistive per relay: "RELAYS: 120VAC 5A (3A IND) / 28VDC 10A" (owner 2026-10-20; on the silk at 1.5 mm, top and bottom); derate for DC motors and solenoids | yes, on the SAF-1 margin below, behind a GFCI outlet |
| rev A (the first ordered design, silk "RELAYS: <=30V DC ONLY") | **≤30 V DC only**, 10 A resistive; derate for DC motors and solenoids | **Not recommended.** AC on rev A relays is experimental, for prove-out only, not considered safe, and recommended against until rev B |

**Source:** Songle's own SRD (T73) datasheet, V1 (songlerelay.com, `srd-t73-relay-290486.pdf`, 2025), and LCSC's copy for C35449
(`datasheet.lcsc.com/datasheet/pdf/9ddfe54b7529126320ff1ddcabf6bab7.pdf`, 2016 Chinese edition; same figures). Resistive contact rating
7 / 10 / 15 A 250 VAC / 28 VDC and 10 A 125 VAC; maximum switching 277 VAC / 30 VDC, 15 A, 2770 VA / 300 W; the maximum-switching curve
holds Form C ("AC (C)", "DC (A,C)") at 10 A up to 277 VAC and 30 VDC. UL (E167996): 10 A 125 VAC, 3 A 125 VAC inductive, 15 A 125 VAC on
NO only. The relay body of C35449 is marked 10A 250VAC / 10A 125VAC / 10A 30VDC / 10A 28VDC. The sheet gives no DC inductive figure, so
none is quoted: derate for DC motors and solenoids (and fit the flyback diode at the load). Dielectric strength 1500 VAC for 1 min coil to
contacts, 1000 VAC for 1 min across open contacts; insulation resistance 100 MΩ at 500 VDC. The board's 5 A at 120 V AC is the owner's
derating of the 10 A figure (2026-10-20); 3 A inductive is the UL 3 A 125 VAC inductive figure, and 28 V DC is the sheet's rated DC voltage
(30 VDC is only its maximum switching voltage).

The SRD-05VDC-SL-C relay bodies say 250 VAC; the board's AC rating is 120 V and rests **first on the relay's own coil-to-contact
insulation** (1500 VAC for 1 min per the datasheet, which claims no reinforced insulation: treat it as basic, a single barrier), then on the
layout below. The layout is a design margin, **not** a creepage/clearance design to a safety standard and **not** a certification. There is
no fuse on the board (accepted risk AR-1). In the enclosure's AC case a screwed mains-bay cover with a hood over J_R1/J_R2 covers the
relay terminals (finger protection only, not a rated enclosure: `../enclosure/README.md`, "AC case").

- **GFCI.** Plug anything mains-powered that runs through a relay into a GFCI-protected outlet. The venue's GFCIs are the
  backstop for an earth-leakage fault only: a GFCI does **not** trip on a line-to-neutral overload or short. The board has no fuse and no
  other protection on the relay side.
- **Fuse each relay.** Put a **5 A inline fuse in each relay's COM wire** whenever a relay switches AC, as a discrete inline fuse
  holder you add, off the board (the board has no fuse by owner decision, AR-1). Preferred: switch AC with an outlet box that has
  its own relays (`../docs/ac-control.md`), so the board's relays stay low voltage.
- **One relay on mains means both.** If either relay carries mains, treat **both** relays' wiring and terminals as mains: the 3.05 mm gap
  between the two contact islands is the only barrier between them.
- **Disconnect first.** Unplug the relay's mains side before plugging in USB to the MCU board or servicing the board.
- **Terminals.** Fitted relay screw terminals (J_R1 / J_R2) must be rated **≥250 V, ≥10 A and UL-recognised**.
- **Mains never goes on a 3.5 mm jack.** The DC enclosure brings RLY1 / RLY2 out on 3.5 mm TRS jacks (**tip = NO, ring = NC, sleeve =
  COM**); through those jacks the relay rating is limited by the jack (the figure is in `enclosure/README.md`), low-voltage only. AC loads use
  the AC case variant (outlets, rev B only) or, preferred, an off-board relay-controlled outlet: [docs/ac-control.md](../docs/ac-control.md).
- **Keep it clean and dry.** Surface contamination (fog fluid residue, condensation, dust) degrades the insulation between
  the relay contacts and the low-voltage copper over time. Keep the board in its closed box away from fog output, let it
  dry out if it has been wet, and clean off any residue with the power off.
- Each relay's contact island (COM, NO, NC: their pads, terminal pins and tracks) keeps **≥3.0 mm**
  of copper clearance to every other net on both layers, including the GND pours, and the two islands keep ≥3.0 mm from
  each other. A KiCad custom DRC rule in `board.ts` checks it (DRC 0 errors), and a measurement of the routed board with the KiCad
  Python API gives the minimum quoted under "Latest build" below. 3.0 mm is as far as this footprint goes: its COM pad is only
  3.2-3.3 mm from the coil pads.
- **Air-gap slots.** A non-plated routed slot, **1.2 mm** wide, sits between COM and each coil pad of each relay and runs 1 mm
  past both pads (four slots, `b.slot(between(...))` in `board.ts`, listed by `bin/aieda q hardware/board.ts slots`). JLC
  mills slots to ±0.2 mm, so the finished slot never drops under its 1.0 mm minimum; about 1.0 mm of board is left to each pad.
- To get there, rev B moved the relays 18.1 mm apart (was 17.0) and moved the buck output capacitors C_BO1/C_BO2 and test pads TP2/TP5 west. (The 2026-10-21 buck-ground fix then moved C_BO1/C_BO2 west of L_BK and the input caps C_BI1/C_BI2 upright beside U_BK; see Layout and build.)
  PWM3 and USER2 now run along the front edge south of the relay terminals. The +5V and +VIN_BK runs over K1 moved north, and D_C1 moved
  between the relays (turned 180°).
- Rev B has no protection parts on the relay contacts. The rev A NO→GND flyback footprints (D_FB1/D_FB2) and the DNP RC snubber footprints
  (R_SN1/R_SN2, C_SN1/C_SN2) are gone: the independent review measured the COM track 0.625 mm from the bare snubber pads, below IPC-2221 B2
  (about 1.25 mm for 151-300 V), and a snubber fitted on an AC load is a hazard. Fit the flyback diode, or a snubber if needed, **at the load,
  DC loads only**.

## Earth bond (rev B, SAF-2)

- **Two bonding points.** The front-west hole **H3 (4, 76)** and the back-east hole **H2 (130, 4)**, both marked **EARTH** on both faces,
  are plated: a 6.4 mm copper pad on both faces, on **GND**. H1 and H4 are unplated. In the AC case, run the green earth wire (from the
  receptacle's green screw, or from a lever connector on the cord's earth) to **both** holes, each with an **M3 ring terminal** under the
  screw head, seated flat on the pad. At H3 point the tongue off the west or south edge (away from the TRIG1 jack's pads).
- **Why two.** The relay row (with its SAF-1 pour pullbacks) and the MCU nest split the board's GND into a west part and an east part on both
  layers. H3 bonds the west part, which holds the GND next to the relay contacts, where an on-board insulation fault (island tracking,
  relay coil-to-contact breakdown) enters GND. The main GND body in the east (power entry, OUT returns, DFPlayer) reaches H3 through no
  more than **3.1 mm** of copper even with every reroutable track taken out (measured), short of the 5.5 mm target, so it gets its own
  hole, H2.
- **Solid bond, no jumper** (owner 2026-10-21, after the independent reviews). Each pad joins both GND pours all round its edge (no thermal
  spokes) and the other layer through its own plated barrel plus a ring of five 0.4 mm GND vias. The rev B solder jumper SJ_E is gone: its
  0.6 mm bridge and thin tracks melt at about 28-127 A²s, near a 5 A fuse's I²t, so it could open before a fault cleared.
- **Measured** (the review's raster / max-flow min-cut on the routed board, 1 oz, vias counted at π × drill × 25/35): relay-adjacent GND
  to H3 **9.7 mm**, main GND body to H2 **25.6 mm** (target ≥5.5 mm, 0.19 mm²). With S = width × 0.035 mm that is about
  **9.7 kA²s** and **68 kA²s** before the narrowest copper melts (Onderdonk, adiabatic); the target is about 3 kA²s.
  Split by relay (both holes earthed, as the AC case has them): GND next to K1 to the earth holes 10.3 mm (9.7 mm to H3 alone), GND next
  to K2 6.9 mm, about 5 kA²s. K2's side reaches H3 alone through only 1.9 mm, part of it lying in the east GND, so **both earth leads are
  needed** whenever a relay carries mains; with only H3 earthed, K2's side is below the 5.5 mm target. The holes are 34.5 mm (H3) and
  74.3 mm (H2) from the nearest island copper.
- **Earthed or not** depends only on the ring terminals. With them fitted, an insulation fault from a mains-wired relay to the low-voltage
  side (GND, the jack sleeves, the MCU module) has a path to earth, and the 5 A COM fuse, the breaker or the GFCI clears it. Leaving them off
  (board GND not earthed) is allowed **only when no relay carries mains** (the DC enclosure, or relays switching low voltage).
- This is a design margin, not a protective-earth conductor sized to a safety standard. Both holes are more than 3.0 mm from both relay
  contact islands (the SAF-1 rule covers every net).
- **Audio hum.** With GND earthed and LINE OUT feeding an amplifier that is itself earthed, the two earths can make a ground loop and a hum.
  The fix is a **3.5 mm ground-loop isolator (transformer type) on LINE OUT**. More supply filtering does not fix a ground loop; the
  DFPlayer's supply filter (FB_DF, 100 µF + 100 nF, the Schottky) stays as it is. The SPK output is bridge-tied and never touches GND, so
  it is not part of the loop. There is no LINE OUT ground-lift resistor on the board (owner 2026-10-20): the plug-in isolator is the fix.

## Copper sizing basis

1 oz outer copper, sized for a **20 °C rise** (IPC-2221 external), each width divided by 0.8 for JLC's ±20 % etch tolerance:

| Path | Current | Copper | IPC-2221 width at 20 °C | Used |
|---|---|---|---|---|
| +VIN trunk + FET bus (J_IN → x 97.5 → y 49.3) | 10 A | F only | 4.72 mm → **5.9 mm** | 5.9 mm, locked |
| FET source stubs, OUT1-4 drain → terminal | 5 A each | F | 1.82 mm → **2.3 mm** | 2.3 mm |
| Relay NO / COM / NC (relay pad → terminal) | 10 A DC (5 A AC) | F **and** B in parallel, 5 A each | 2.3 mm per layer | 2.3 mm × 2, locked |
| RAILS +VIN | 3 A | F | 0.9 mm → 1.1 mm | 2.3 mm |
| Buck input +VIN_BK (through FB_BK) | 1.55 A at 9 V in | any | 0.5 mm | 0.8 mm (1.2 mm +VIN into FB_BK) |
| +5V, BK_SW | 2.5 A | any | 0.7 mm → 0.87 mm | 0.9 mm (J_RAIL / J_PX / SJ_V / FB_DF / K2 trunk locked) |
| +5V_MCU (DevKit VIN / Pico VSYS) | 0.8 A | any | 0.25 mm → 0.3 mm | 0.5 mm, locked |

The aieda oracle checks current at a fixed 10 °C rise and treats each layer's track on its own, so its `amps` / `width` rows for +VIN,
OUT1-4 and R1/R2_* fail by design. This table is the hand calculation that replaces them (a toolchain limitation). A 20 °C rise in a
50 °C box puts the hottest copper near 70 °C, well inside FR-4 (TG 135) and the 105 °C terminal ratings.

## OUT1-4 thermal basis (rev A and rev B)

The old "7.5 A for 0.5 s" was an inrush allowance carried from the original F-2 / IF-5 requirement, not a part limit. The rating
(owner 2026-10-20, restricted after the independent review) rests on this estimate of the AO4485 junction temperature (Tj) in a 50 °C box.
None of it is measured yet.

- **Datasheet (AO4485):** RDS(on) max 15 mΩ at VGS −10 V, 20 mΩ at −4.5 V, 23 mΩ at −10 V and Tj 125 °C (a 1.53× hot ratio).
  RθJA 40 °C/W for t ≤ 10 s and 75 °C/W steady, both on 1 in² of 2 oz copper; RθJL 24 °C/W.
- **This board:** about 0.1 in² of 1 oz drain copper per FET, far less than the datasheet's test board. The figures used here, Zθ(5 s)
  about **50-60 °C/W** and RθJA about **75-100 °C/W** steady, are **unverified estimates**, not datasheet or measured values.
- **Gate drive is −VIN/2**, so RDS(on) (max, cold / hot) depends on the supply:
  - 9 V in, VGS −4.5 V: about 20 mΩ cold, about 31 mΩ hot.
  - 12 V in, VGS −6 V: about 18 mΩ cold, about 27 mΩ hot.
  - 24 V in, VGS −12 V: about 15 mΩ cold, about 23 mΩ hot.
- **5 A steady** (hot RDS(on), 75-100 °C/W): 12 V 0.68 W, Tj 101-118 °C; 24 V 0.58 W, Tj 93-108 °C; **9 V 0.78 W, Tj 108-128 °C**.
  Under the 150 °C maximum at every input voltage, so 5 A continuous stands.
- **7.5 A for 5 s from idle** (FET at ambient, hot RDS(on) for margin, 50-60 °C/W): 12 V 1.52 W, Tj 126-141 °C; 24 V 1.29 W,
  Tj 115-128 °C; **9 V 1.74 W, Tj 137-155 °C**, which can pass 150 °C: hence the ratings are stated for **12-24 V**.
- **7.5 A for 5 s starting from 5 A steady** (12 V): Tj **147-164 °C**, over the maximum: hence **"starting from idle"**. The ≤2 % duty
  (e.g. once every 6 min) is what lets the FET cool back to idle between pulses.
- **10 A for 5 s:** 12 V 2.7 W, far too hot; 10 A is a start-up surge under ~1 s only. (The datasheet allows 12 A for 10 s on its own
  test board.)
- Copper: the 2.3 mm single-layer OUT stub at 7.5 A is about 34 °C rise steady (IPC-2221), less over 5 s.
- So the OUT ratings are stated for **VIN 12-24 V** (12 V is the primary use case). The 9 V figures above are the reason for the derating line:
  below 12 V (down to the 9 V minimum), derate: the OUT FETs see less gate drive and run hotter.
- The 7.5 A / 5 s figure is **pending the first-board heat test** (below); if the board passes only from idle, the rating stays "from idle".

## +5V budget (2.5 A buck)

| Load | Typical | Peak | Note |
|---|---|---|---|
| ESP32 DevKit (Wi-Fi on) | 0.15 A | 0.5 A | radio bursts; Pico W is about 0.1 / 0.3 A |
| DFPlayer Mini | 0.05 A | 0.8 A | audio peaks at full volume into 4 Ω |
| Relay coils SRD-05 | 0.072 A each | 0.144 A | both on |
| Relay + output LEDs | 0.02 A | 0.03 A | |
| PIXEL strip | — | ≈1.0 A | what is left: 2.5 − 0.5 − 0.8 − 0.15 ≈ 1 A |
| RAILS 5V | — | 0.1 A | |

Rule of thumb: with everything at its peak the 5 V rail is full. Longer pixel strips take their own 5 V supply
(join only DATA and GND to the board).

## Where the current goes on +VIN

+VIN = OUT1-4 loads + the buck input (2.5 A × 5 V / VIN / 0.9 ≈ 1.2 A at 12 V, 0.6 A at 24 V) + RAILS VIN.
Keep the total under the supply's rating and **under 10 A**. Example at 12 V: 4 × 2 A solenoids (8 A) + buck at
full load (1.2 A) = 9.2 A.

## Test pads (pogo)

| Pad | Net | Note |
|---|---|---|
| TP1 | +VIN | behind the input terminal |
| TP2 | +5V | at the buck output |
| TP3 | +3V3 | near the RAILS terminal |
| TP4 | DF_BUSY | under the DFPlayer: reachable with the module out |
| TP5 | GND | at the buck |

## GPIO map

| Function | ESP32 DevKit GPIO | Pico GPIO |
|---|---|---|
| OUT1 gate (PWM) | 23 | GP15 |
| OUT2 gate (PWM) | 22 | GP14 |
| OUT3 gate (PWM) | 21 | GP12 |
| OUT4 gate (PWM) | 19 | GP11 |
| TRIG2 input (back TRS) | 18 | GP10 |
| USER3 button | 5 | GP9 |
| DFPlayer RX (MCU TX) | 17 | GP8 |
| I2C SDA (expansion S1) | 4 | GP4 |
| I2C SCL (expansion S2) | 16 | GP5 |
| expansion S4 | 15 | GP3 |
| USER1 button | 13 | GP28 |
| USER2 button | 14 | GP27 |
| CONTACT1 opto | 27 | GP22 |
| RLY2 coil driver | 26 | GP21 |
| RLY1 coil driver | 25 | GP20 |
| PIXEL data (inverted by the level shifter) | 33 | GP19 |
| CONTACT2 opto | 32 | GP18 |
| expansion S5 (input only on ESP32) | 35 | GP17 |
| TRIG1 input (front TRS) | 36 | GP16 |
| analog in (expansion S3) | 39 | GP26 |
| expansion S6 = DFPlayer TX (MCU RX) | 34 | GP7 |

Pico-only expansion pins (S7-S11): GP0, GP1, GP2, GP6, GP13. The DevKit status LED is GPIO2. GPIO34-39 are input-only on the ESP32.
DFPlayer UART: ESP32 UART2 TX on GPIO17, RX on GPIO34 (GPIO matrix); Pico UART1 TX on GP8, RX on GP7 via a PIO UART. Header pin S6
carries the DFPlayer's TX: monitor it, never drive it.

## Orientation

- MCU nest: antennas at the **west** edge (DevKit antenna overhangs it), USB toward the board centre. DevKit EN (pin 1) is the
  west pin of the south socket row; Pico pin 1 (GP0) is the east pin of the north Pico row. Never fit both.
- DFPlayer: pin 1 (VCC) at the west end of the south row; the microSD slot (between pins 8 and 9) faces the **east** edge.

## Layout and build

- Source of truth: `board.ts` (intent: parts, nets, placement constraints, routing classes), with `frozen.ts` (reviewed part poses) and
  `routes.ts` (locked routed copper). The KiCad project, gerbers, BOM and pick-and-place are generated by `bin/aieda build` / `bin/aieda fab`
  and committed (`kicad/`, `fab/`). In KiCad the schematic and board are one linked project (every footprint carries its symbol's path);
  KiCad's own parity check reports field-only differences (LCSC fields, BOM-exclusion flags, deliberate no-connect pins), no net errors.
- 2 layers, 1 oz, 134 x 80 mm, JLCPCB Economic assembly. Every signal and power net is locked, so a rebuild reproduces the same copper
  (only the GND pour and its fanout are left to the router).
- Latest build (rev B, aieda cd1f38a, 2026-10-21: buck ground rejoined, earth bonds at H2 and H3, bottom silk off the vias): 0 DRC errors
  (including the SAF-1 island rule and the slots), 0 unconnected; `check slot` passes for all four slots (nearest copper 0.8 mm, the K1 coil
  feed). Measured contact-island clearance (KiCad Python API on the routed board): minimum **3.05 mm on F and on B** (K1 NC to K2 NO 3.054 mm;
  K1 COM to the +5V coil feed on B, 3.052 mm). Every bottom-silk text keeps at least 0.15 mm from via copper, pads and holes (the review's
  silkall.py). Of the failing checks:
  - the current checks (`amps` / `width`) assume a 10 C rise on one layer; this board is sized for 20 C, with 10 A paths on two layers (table
    above);
  - `plane-current GND B` and `GND F` report **SPLIT**, and that is real, not cosmetic: on its own neither layer joins every GND terminal
    (the MCU nest has no F pour, and the relay row's SAF-1 pullbacks and the nest's tracks cut both layers). Followed across the vias
    (tried with the class's `parallel` option) GND is whole, but its narrowest path to the nest decouplers is a 0.2 mm plane-fanout stub, so
    that row fails too and was left off. The fault-current paths that matter are measured as min-cuts instead (Earth bond, above): relay side
    to H3 9.7 mm, main body to H2 25.6 mm, main body to H3 only 1.9 mm, which is why there are two earth holes;
  - non-45-degree segments at pad entries are cosmetic.
- Buck ground (rev B, 2026-10-21 review): the input caps C_BI1/2 stand upright beside U_BK pin 2 (VIN pads in a row, GND pads south), the
  output caps C_BO1/2 sit west of L_BK pad 2 (GND pads west), and one solid-connect F ground area joins their grounds, the copper under
  L_BK between its pads, the catch-diode anodes and the PowerPAD. The rev B layout before this fix had them split: the +5 V output ran round
  the input caps on F (L_BK → x 34.4 → y 60.4 → x 42.05) and USER2 ran down B at x 34.84, and with the K1 pour pullback they fenced the cap
  grounds off (one 0.39 mm neck). The +5 V output now drops to B through four vias after C_BO and runs north at x 35.5, beside the K1 moat;
  USER2 runs down B at x 36.45. Measured min-cuts (review method): input-cap GND to the catch-diode anodes **4.1 mm on F** (was 0) and
  **4.8 mm on F + B** (was 0.4 mm); output caps to the diodes 7.8 mm; input caps to the PowerPAD on F only 0.9 mm (the package's pin rows
  leave the PowerPAD two narrow F exits; its vias join it to the B plane). The F return from the input caps to the diodes still runs
  under L_BK, about 30 mm, and only C_BI1's ground via reaches the B plane under the IC (C_BI2's lands in the strip between the +5 V
  trunk and USER2), so the B plane adds little to that loop: the bench check below (SW / VIN overshoot, ripple) is the real test.
- JLC orders: upload the gerbers plus the BOM / CPL from `tools/jlc_packages.py`, which applies the rotation corrections JLC's parts need
  (checked in JLC's 3D placement preview). Inspect the placement preview before ordering.
  In JLC's gerber viewer, check that **H2 and H3 are plated and H1 / H4 are not**: all four are 3.2 mm, in one mixed-plating drill file
  (`proptroller.drl`: H2 / H3 in a plated 3.2 mm tool, H1 / H4 in a non-plated one), so check the plating, not just the size.
- Reference designators: `tools/silk_refs.py` (run by `make build`) moves each one to the silkscreen beside its part, clear of pads,
  part bodies and the relay slots. aieda v0.2's own `b.silkRefs()` skips footprint-only (`mech`) parts such as the terminals and
  sockets, so the script stays. Parts in the densest clusters keep theirs on the fab layer (listed by the script);
  the KiCad file and the fab drawing still name them.

## First-board checks

- **Rev A boards only:** run the buck bench test of the Rev A errata (24 V in, 2.5 A load, SW / VIN at the IC pins and the +5 V ripple,
  before and after the bodge) and fit the C_BI2 → D_CT2 / C_BO2 → D_CT1 ground wires unless the test shows they are not needed.
- **Buck (rev B):** at 24 V in and 2.5 A out, scope SW and VIN at the IC pins and the +5 V ripple at C_BO1 (the input and output cap grounds
  now join the catch-diode and PowerPAD ground on F; see Layout and build).
- Continuity-check one PJ-320D jack (tip / sleeve pads) and the relay NO / NC pins against the silk. Check that no relay contact pin has continuity to GND (rev B has no NO→GND diode).
- After hand-soldering: no bridges between RAILS +5V / +3V3, the expansion header +5V / GND, and DFPlayer socket pins 3-5.
- Dry-fit an ESP32 DevKit: it should clear relay K1 by about 2.8 mm. Rev B moved K1 0.9 mm west, toward the DevKit's USB end, so measure from
  the DevKit's USB shell and board edge to the K1 pins and to the J_R1 screws, and check how far the DevKit reaches east over K1. Its outline is not measured.
- DFPlayer TX level on J_DFA pin 3 should be about 3.3 V (DFRobot); confirm on clones.
- Load +5V to about 2 A (pixels plus DFPlayer playing) and check the drop at the PIXEL and RAILS terminals.
- Probe the buck's switch node and +5V ripple; check a 10 A output load path for temperature.
- **OUT heat test** (closed box, lid on), run at **12 V** (the primary case) **and at 24 V**. 9 V is not part of the pass criterion; it is
  covered by the derating line (below 12 V, derate):
  - Calibrate first: on a hotplate, measure RDS(on) of a spare AO4485 (same part, same gate drive as on the board: −VIN/2) at a known
    small current over 25-150 °C, to get an RDS(on)-to-Tj curve.
  - Load the output under test with **5 A steady** and the neighbouring OUT with 5 A too, and let it settle. Then add the pulses: **7.5 A
    resistive for 5 s every 6 min for 30 min** on the output under test.
  - Log VDS across the FET's own source and drain pins (not at the terminals, whose copper drop would read as extra RDS(on)) during each
    pulse, convert it to RDS(on) (VDS / I) and then to Tj with the calibration curve. Log the OUT terminal temperatures too (thermocouple).
  - Extrapolate to a 50 °C box: Tj(50 °C) = measured Tj + (50 °C − measured ambient), and the same for the terminals.
  - **Pass:** Tj(50 °C) ≤ 125 °C and terminals ≤ 90 °C (their rating is 105 °C).
  - If the run from 5 A steady fails, repeat it with the pulses **starting from idle** (output off between pulses, neighbour still at 5 A).
    If only that passes, the rating stays "from idle". Until a run passes, the 7.5 A / 5 s rating stays "pending".

- **Earth bond (SAF-2):** H2 and H3 each read continuity to GND. In the AC case, with the earth leads on both: GND to the receptacle's earth
  screw under 1 Ω at each hole, and the GFCI test trips.

### Relay insulation and mains checks (rev B, before any relay carries mains)

- **Insulation screen:** 500 V DC insulation tester, **≥100 MΩ** each time, from each relay island (COM + NO + NC tied) to all low-voltage
  nets tied together (GND, +VIN, +5V, +3V3 and the MCU sockets), from island to island, and across each relay's open contacts. Repeat after a
  fog or humidity soak. This is a **screen, not a 1500 VAC withstand (hi-pot) test**: it finds contamination and gross faults, it does not
  prove the insulation withstands a mains surge.
- **Slots:** check all four air-gap slots are present and caliper each width: **≥1.0 mm**.
- **Heat run:** 1 h with **both relays energised and both loaded at the same time** (5 A resistive each, 120 V AC), in the closed box,
  logging the screw-terminal and relay-pin temperatures. Songle's temperature note assumes relays at least 10 mm apart; K1 and K2 bodies
  are only 2.6 mm apart (the 18.1 mm is their pitch), so each heats the other.
- **GFCI:** press the test button of the GFCI outlet the rig uses and confirm it trips and the load goes off.
- **Inductive switching:** switch a real fog machine remote (inductive) many times, on the GFCI outlet with a 5 A fuse in each relay's COM wire, and check
  the contacts and the remote.

## Rev A errata (the first ordered design)

- **Buck ground split.** On rev A the input-cap and output-cap grounds (C_BI1/2, C_BO1/2) reach the catch diodes (D_CT1/2) and U_BK only
  through a 0.39 mm pour neck at (32.5, 74.9) and one 0.3 mm GND via at (38.5, 74.45): about 1.1 mm of copper (min-cut), and the
  input-cap → catch-diode loop detours about 50 mm. Rev B fixes it in the layout (below). There is no rev A safety erratum: rev A has no earth
  bond, and its other GND necks are wide (≥6.7 mm).
- **Bodge** (top side): short heavy wires, 20-22 AWG or braid, from **C_BI2 pad 2 to D_CT2 pad 2** (about 18 mm) and from **C_BO2 pad 2 to
  D_CT1 pad 2**. Do not tie them to the R_RT or C_CF (quiet) grounds.
- **Bench test before deciding it is optional:** at 24 V in and a 2.5 A load, scope SW and VIN at the IC pins and measure the +5 V output
  ripple, before and after the bodge. If the SW / VIN overshoot stays well under the TPS54360B's 65 V absolute maximum and the ripple is
  acceptable without it, the bodge is optional; otherwise fit it on every rev A board.

## Where things are

- Back edge (north): PIXEL, TRIG2 jack, CONTACT1-2 jacks, RAILS, power in (barrel + terminal), SPK.
- Front edge (south): TRIG1 jack, RLY1-2, OUT1-4.
- East: DFPlayer (card slot at the edge), LINE OUT jack, USER1-3.
- West: MCU antenna end. M3 holes in all four corners; the front-west one (H3) and the back-east one (H2) are plated, on GND: the EARTH bonding points.
