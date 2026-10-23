---
name: proptroller-hardware
description: Spectre Proptroller board facts (rev B, with the rev A differences) - terminals, ratings, GPIO map for the ESP32 DevKit and Pico, what is safe to connect, where the design files are. Load before writing firmware that touches pins, before answering wiring or rating questions, and before changing hardware/ or enclosure/.
---

# Spectre Proptroller hardware

Source of truth: `hardware/README.md` (ratings, GPIO map, power budget, test pads, first-board checks) and
`hardware/board.ts` (aieda design intent). Read the README section you need rather than relying on memory; this
skill is the summary and the rules.

## The board
- 134 x 80 mm, 2 layers. `main` is rev B (silk "rev B": SAF-1 relay insulation, 120 V AC relay rating; not yet ordered).
  Rev A (the first ordered design; this repo holds rev B) was ordered 2026-10-15 (20 boards; not yet built or tested). Ask which revision a board is
  before answering a relay question: the silk says "rev B" or, on rev A, "RELAYS: <=30V DC ONLY".
- Plug-in MCU: a **30-pin** ESP32 DevKit **or** a Raspberry Pi Pico / Pico W / Pico 2 / Pico 2 W, never both.
- Power 9-24 V DC (barrel ≤5 A, terminal ≤10 A board total). Ratings are for a 12-24 V supply (12 V is the primary use case);
  below 12 V (down to the 9 V minimum), derate: the OUT FETs see less gate drive and run hotter. TVS only: **no fuses, no reverse-polarity FET**.

| Terminal | What it is | Firmware name |
|---|---|---|
| OUT1-4 | high-side switched +VIN, at VIN 12-24 V: 5 A each continuous; 7.5 A for 5 s at ≤2 % duty, starting from idle (the FET cool) (pending a first-board heat test); below 12 V derate; 10 A only as a start-up surge under ~1 s; sum ≤10 A (barrel ≤5 A); PWM ≤1 A at 24 V / ≤2 A at 12 V | `OUT1`-`OUT4` |
| RLY1-2 | dry COM/NO/NC. **Rev B: 120 V AC 5 A resistive (3 A inductive) or 28 V DC 10 A resistive** ("120VAC 5A (3A IND) / 28VDC 10A") per relay, derate for DC motors and solenoids (no fuse on the board; for AC a GFCI outlet and a 5 A fuse in each COM wire). **Rev A: ≤30 V DC only, 10 A**. DC enclosure jacks: tip NO, ring NC, sleeve COM, never mains | `RLY1`, `RLY2` |
| CONTACT1-2 | isolated opto closure on 3.5 mm, ≤30 V DC, ≤50 mA absolute maximum but **count on about 2 mA** switched (LED drive and CTR), polarity matters | `CC1`, `CC2` |
| TRIG1 (front), TRIG2 (back) | 3.5 mm TRS: tip input active LOW, ring 3V3 via 220 Ω, sleeve GND | `TRIG1`, `TRIG2` |
| USER1-3 | on-board buttons | `USER1`-`USER3` |
| PIXEL | 5 V WS2812 data, **inverted** by the level shifter (firmware inverts), ≤1 A from the board | `PIXEL` |
| DFPlayer | two-way UART, SPK 3 W into 4 Ω bridge-tied (never ground a speaker wire), LINE OUT | `SOUND` |
| Expansion S1-S6 (+S7-S11 Pico) | I2C S1/S2, analog S3 (knob), S6 = DFPlayer TX (monitor only) | |

GPIO highlights (full table in `hardware/README.md`): ESP32 OUT1-4 = 23/22/21/19, TRIG1 = 36, TRIG2 = 18,
RLY1/2 = 25/26, CC1/2 = 27/32, PIXEL = 33, DFPlayer TX/RX = 17/34, status LED = GPIO2; GPIO34-39 input only.
Pico OUT1-4 = GP15/14/12/11, TRIG1 = GP16, TRIG2 = GP10, DFPlayer UART1 TX GP8 / RX GP7 (PIO UART).
The Pico W's LED is on the CYW43, not GP25.

## Rules
- Every output is low at reset and the firmware drives them low first; PIXEL idles high (inverted).
- The firmware does not measure or check load current (owner 2026-10-20): the ratings above are maximums the
  installer stays within. Firmware safety is outputs-off at reset and per-output maximum on-times.
- Mains is per revision. **Rev A**: relay and contact outputs are low-voltage DC only; AC on rev a relays is experimental, for prove-out only, not considered safe, and recommended against until rev b; AC loads go
  through a listed relay-controlled outlet off the board (`docs/ac-control.md`).
  **Rev B**: RLY1-2 may switch up to 120 V AC 5 A (3 A inductive). The rating rests first on the relay's own coil-to-contact
  insulation (1500 VAC, basic), then on SAF-1 (≥3.0 mm copper clearance around each relay's contact island, 1.2 mm air-gap slots
  between COM and the coil pads): a design margin, not a certification. No fuse, no relay terminal cover; the venue's GFCI
  outlets cover earth leakage only, so a 5 A fuse goes in each relay's COM wire; one relay on mains makes both relays' wiring mains;
  fog fluid residue, condensation and dust degrade the insulation over time (`hardware/README.md`). No snubber or flyback parts on
  the board: fit them at the load, DC only. The recommended way to switch AC is off the board (`docs/ac-control.md`).
  CONTACT1-2 stay ≤30 V DC on both revisions.
- Earth (rev B, SAF-2): two corner holes are plated on GND (silk EARTH), each bonded solidly into both GND pours with a ring of stitching vias:
  H3 (4, 76) for the relay side and H2 (130, 4) for the main GND body (the relay row and the MCU nest split GND, so one hole cannot serve
  both). In the AC case the earth wire lands on both with M3 ring terminals. No jumper: the board is earthed exactly when the rings are fitted,
  and they may be left off only when no relay carries mains. Bond capability in `hardware/requirements.yaml` SAF-2 (measured min-cuts).
  Hum with an earthed amplifier on LINE OUT: a 3.5 mm ground-loop isolator, no ground-lift part.
- Positions (rev B, board mm from the west / back edges): relay terminals J_R1 / J_R2 pin 1 at x 43.02 / 61.12 on the front
  edge (rev A 43.92 / 60.92), relays K1 / K2 directly behind them; the other connectors did not move from rev A.
- `hardware/kicad/` and `hardware/fab/` are generated by aieda (`make build`, `make fab order`); never hand-edit them.
  `aieda/` is a submodule and is never edited.
- Enclosure: `enclosure/` (OpenSCAD + `design.toml`); its README holds the print and check steps.
- The public repo carries no customer- or event-specific content, prices, store or location data, or personal paths.
