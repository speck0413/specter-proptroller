# Orientation report

Pin 1 / cathode direction is measured from the footprint on the board (compass, +y down → S). JLC rotation is
the CPL value after the rotation corrections: `part.jlcRot(deg)`, then the project's table (aieda.json "rotations"),
then aieda's footprint table (`src/lib/jlc-rotations.csv`); `rule` names what matched, or none. A rule is a claim
about JLC's model, never a review: every part in this table, ruled or not, is confirmed in the JLC preview before ordering.

Pin-1 discipline (ICs and modules): majority SW; against it: K1 (centre), K2 (centre), Q_O1 (NW), Q_O2 (NW), Q_O3 (NW), Q_O4 (NW), Q_R1 (SE), Q_R2 (NW), U_BK (SE), U_C1 (W), U_C2 (W). Turn them unless a locked pattern needs the rotation (say which).

| Ref | Value | Footprint | Key pad | Physical mark to compare | Pad points | KiCad rot | JLC rot | Rule | 3D model |
|---|---|---|---|---|---|---|---|---|---|
| C_BK1 | 100u/50V | CP_Elec_8x10 | 1 | positive pad (+/1); check body + mark (tantalum) or negative stripe on the opposite pad (electrolytic) | E | 180 | 0 | ^CP_Elec_8x10 | library |
| C_BK2 | 100u/50V | CP_Elec_8x10 | 1 | positive pad (+/1); check body + mark (tantalum) or negative stripe on the opposite pad (electrolytic) | S | 90 | 270 | ^CP_Elec_8x10 | library |
| C_DF | 100u/50V | CP_Elec_8x10 | 1 | positive pad (+/1); check body + mark (tantalum) or negative stripe on the opposite pad (electrolytic) | S | 90 | 270 | ^CP_Elec_8x10 | library |
| D_C1 | B5819W | D_SOD-123 | 1 | cathode K: diode band / LED cathode mark | E | 180 | 180 | none | library |
| D_C2 | B5819W | D_SOD-123 | 1 | cathode K: diode band / LED cathode mark | W | 0 | 0 | none | library |
| D_CT1 | SS56 | D_SMA | 1 | cathode K: diode band / LED cathode mark | E | 180 | 180 | none | library |
| D_CT2 | SS56 | D_SMA | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_DF | B5819W | D_SOD-123 | 1 | cathode K: diode band / LED cathode mark | N | 270 | 270 | none | library |
| D_F1 | SS56 | D_SMA | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_F2 | SS56 | D_SMA | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_F3 | SS56 | D_SMA | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_F4 | SS56 | D_SMA | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_LO1 | green | LED_0805_2012Metric | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_LO2 | green | LED_0805_2012Metric | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_LO3 | green | LED_0805_2012Metric | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_LO4 | green | LED_0805_2012Metric | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_MCU | B5819W | D_SOD-123 | 1 | cathode K: diode band / LED cathode mark | E | 180 | 180 | none | library |
| D_RL1 | green | LED_0805_2012Metric | 1 | cathode K: diode band / LED cathode mark | N | 270 | 270 | none | library |
| D_RL2 | green | LED_0805_2012Metric | 1 | cathode K: diode band / LED cathode mark | W | 0 | 0 | none | library |
| D_TC1 | 1N4148W | D_SOD-123 | 1 | cathode K: diode band / LED cathode mark | N | 270 | 270 | none | library |
| D_TC2 | 1N4148W | D_SOD-123 | 1 | cathode K: diode band / LED cathode mark | S | 90 | 90 | none | library |
| D_TVS | SMAJ24A | D_SMA | 1 | cathode K: diode band / LED cathode mark | W | 0 | 0 | none | library |
| J_C1 | PJ-320D | Jack_3.5mm_PJ320D_Horizontal | ? | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | ? | 270 | 270 | none | none (not-fetched) |
| J_C2 | PJ-320D | Jack_3.5mm_PJ320D_Horizontal | ? | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | ? | 270 | 270 | none | none (not-fetched) |
| J_LO | PJ-320D | Jack_3.5mm_PJ320D_Horizontal | ? | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | ? | 180 | 180 | none | none (not-fetched) |
| J_PWR | DC-005-5A-2.0 | BarrelJack_Horizontal | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | centre | 270 | 270 | none | library |
| J_T1 | PJ-320D | Jack_3.5mm_PJ320D_Horizontal | ? | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | ? | 90 | 90 | none | none (not-fetched) |
| J_T2 | PJ-320D | Jack_3.5mm_PJ320D_Horizontal | ? | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | ? | 270 | 270 | none | none (not-fetched) |
| K1 | SRD-05VDC-SL-C | Relay_SPDT_SANYOU_SRD_Series_Form_C | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | centre | 270 | 270 | none | library |
| K2 | SRD-05VDC-SL-C | Relay_SPDT_SANYOU_SRD_Series_Form_C | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | centre | 270 | 270 | none | library |
| Q_D1 | 2N7002 | SOT-23 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 0 | ^SOT-23 | library |
| Q_D2 | 2N7002 | SOT-23 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 0 | ^SOT-23 | library |
| Q_D3 | 2N7002 | SOT-23 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 0 | ^SOT-23 | library |
| Q_D4 | 2N7002 | SOT-23 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 0 | ^SOT-23 | library |
| Q_O1 | AO4485 | SOIC-8_3.9x4.9mm_P1.27mm | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | NW | 0 | 270 | ^SOIC- | library |
| Q_O2 | AO4485 | SOIC-8_3.9x4.9mm_P1.27mm | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | NW | 0 | 270 | ^SOIC- | library |
| Q_O3 | AO4485 | SOIC-8_3.9x4.9mm_P1.27mm | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | NW | 0 | 270 | ^SOIC- | library |
| Q_O4 | AO4485 | SOIC-8_3.9x4.9mm_P1.27mm | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | NW | 0 | 270 | ^SOIC- | library |
| Q_PX | 2N7002 | SOT-23 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 0 | ^SOT-23 | library |
| Q_R1 | MMBT3904 | SOT-23 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SE | 180 | 90 | ^SOT-23 | library |
| Q_R2 | MMBT3904 | SOT-23 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | NW | 0 | 270 | ^SOT-23 | library |
| SW_U1 | TS-1187A | SW_Push_1P1T_XKB_TS-1187A | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 90 | none | none (not-fetched) |
| SW_U2 | TS-1187A | SW_Push_1P1T_XKB_TS-1187A | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 90 | none | none (not-fetched) |
| SW_U3 | TS-1187A | SW_Push_1P1T_XKB_TS-1187A | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SW | 90 | 90 | none | none (not-fetched) |
| U_BK | TPS54360BDDAR | TI_SO-PowerPAD-8 | 1 | pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet | SE | 180 | 180 | none | library |
| U_C1 | LTV-217-B-G | SOP-4_4.4x2.6mm_P1.27mm | 2 | cathode K: diode band / LED cathode mark | W | 0 | 0 | ^SOP-4_ | library |
| U_C2 | LTV-217-B-G | SOP-4_4.4x2.6mm_P1.27mm | 2 | cathode K: diode band / LED cathode mark | W | 0 | 0 | ^SOP-4_ | library |

Parts without a rotation rule: 31. Verify them in JLC's assembly preview.
Before submitting, open `kicad/<board>-pin1-top.svg` (and `-pin1-bottom.svg` when present; `bin/aieda render` writes them):
every part in the table above carries a magenta dot on its pin-1 / cathode pad with its reference. Confirm each dot
against the JLC placement preview and the datasheet.

## Required visual inspection

The magenta pad marker measures the footprint; it does not prove the model or physical part is oriented correctly.
The agent must inspect the enlarged footprint and 3D image, compare the dot/notch/band/stripe with the datasheet, and record findings.
If the mark is hidden or the model is ambiguous, leave the review pending and ask the user to compare the exact part and JLC preview.
Record the review in `../kicad/orientation-review.json`; each part needs a reviewer, datasheet, notes and assembly-preview confirmation.

- C_BK1: positive pad (+/1); check body + mark (tantalum) or negative stripe on the opposite pad (electrolytic); [footprint close-up](../kicad/orientation/C_BK1-61734c-footprint.svg) / [model close-up](../kicad/orientation/C_BK1-61734c-model.png); visual review **pending**, JLC preview **pending**.
- C_BK2: positive pad (+/1); check body + mark (tantalum) or negative stripe on the opposite pad (electrolytic); [footprint close-up](../kicad/orientation/C_BK2-15a4da-footprint.svg) / [model close-up](../kicad/orientation/C_BK2-15a4da-model.png); visual review **pending**, JLC preview **pending**.
- C_DF: positive pad (+/1); check body + mark (tantalum) or negative stripe on the opposite pad (electrolytic); [footprint close-up](../kicad/orientation/C_DF-d32c5d-footprint.svg) / [model close-up](../kicad/orientation/C_DF-d32c5d-model.png); visual review **pending**, JLC preview **pending**.
- D_C1: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_C1-729a76-footprint.svg) / [model close-up](../kicad/orientation/D_C1-729a76-model.png); visual review **pending**, JLC preview **pending**.
- D_C2: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_C2-e65d8b-footprint.svg) / [model close-up](../kicad/orientation/D_C2-e65d8b-model.png); visual review **pending**, JLC preview **pending**.
- D_CT1: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_CT1-da0959-footprint.svg) / [model close-up](../kicad/orientation/D_CT1-da0959-model.png); visual review **pending**, JLC preview **pending**.
- D_CT2: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_CT2-a7e3c6-footprint.svg) / [model close-up](../kicad/orientation/D_CT2-a7e3c6-model.png); visual review **pending**, JLC preview **pending**.
- D_DF: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_DF-182143-footprint.svg) / [model close-up](../kicad/orientation/D_DF-182143-model.png); visual review **pending**, JLC preview **pending**.
- D_F1: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_F1-a4600f-footprint.svg) / [model close-up](../kicad/orientation/D_F1-a4600f-model.png); visual review **pending**, JLC preview **pending**.
- D_F2: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_F2-beb93b-footprint.svg) / [model close-up](../kicad/orientation/D_F2-beb93b-model.png); visual review **pending**, JLC preview **pending**.
- D_F3: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_F3-585457-footprint.svg) / [model close-up](../kicad/orientation/D_F3-585457-model.png); visual review **pending**, JLC preview **pending**.
- D_F4: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_F4-71aa16-footprint.svg) / [model close-up](../kicad/orientation/D_F4-71aa16-model.png); visual review **pending**, JLC preview **pending**.
- D_LO1: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_LO1-3d1d09-footprint.svg) / [model close-up](../kicad/orientation/D_LO1-3d1d09-model.png); visual review **pending**, JLC preview **pending**.
- D_LO2: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_LO2-02d0fd-footprint.svg) / [model close-up](../kicad/orientation/D_LO2-02d0fd-model.png); visual review **pending**, JLC preview **pending**.
- D_LO3: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_LO3-77edef-footprint.svg) / [model close-up](../kicad/orientation/D_LO3-77edef-model.png); visual review **pending**, JLC preview **pending**.
- D_LO4: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_LO4-e2dc43-footprint.svg) / [model close-up](../kicad/orientation/D_LO4-e2dc43-model.png); visual review **pending**, JLC preview **pending**.
- D_MCU: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_MCU-2de6a6-footprint.svg) / [model close-up](../kicad/orientation/D_MCU-2de6a6-model.png); visual review **pending**, JLC preview **pending**.
- D_RL1: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_RL1-88f29f-footprint.svg) / [model close-up](../kicad/orientation/D_RL1-88f29f-model.png); visual review **pending**, JLC preview **pending**.
- D_RL2: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_RL2-8246e4-footprint.svg) / [model close-up](../kicad/orientation/D_RL2-8246e4-model.png); visual review **pending**, JLC preview **pending**.
- D_TC1: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_TC1-c30e07-footprint.svg) / [model close-up](../kicad/orientation/D_TC1-c30e07-model.png); visual review **pending**, JLC preview **pending**.
- D_TC2: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_TC2-d19820-footprint.svg) / [model close-up](../kicad/orientation/D_TC2-d19820-model.png); visual review **pending**, JLC preview **pending**.
- D_TVS: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/D_TVS-ca348d-footprint.svg) / [model close-up](../kicad/orientation/D_TVS-ca348d-model.png); visual review **pending**, JLC preview **pending**.
- J_C1: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/J_C1-a59ca2-footprint.svg) / [model close-up](../kicad/orientation/J_C1-a59ca2-model.png); visual review **pending**, JLC preview **pending**.
- J_C2: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/J_C2-05f16f-footprint.svg) / [model close-up](../kicad/orientation/J_C2-05f16f-model.png); visual review **pending**, JLC preview **pending**.
- J_LO: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/J_LO-d33e95-footprint.svg) / [model close-up](../kicad/orientation/J_LO-d33e95-model.png); visual review **pending**, JLC preview **pending**.
- J_PWR: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/J_PWR-71c13d-footprint.svg) / [model close-up](../kicad/orientation/J_PWR-71c13d-model.png); visual review **pending**, JLC preview **pending**.
- J_T1: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/J_T1-fb0038-footprint.svg) / [model close-up](../kicad/orientation/J_T1-fb0038-model.png); visual review **pending**, JLC preview **pending**.
- J_T2: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/J_T2-10ffda-footprint.svg) / [model close-up](../kicad/orientation/J_T2-10ffda-model.png); visual review **pending**, JLC preview **pending**.
- K1: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/K1-badb72-footprint.svg) / [model close-up](../kicad/orientation/K1-badb72-model.png); visual review **pending**, JLC preview **pending**.
- K2: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/K2-6897ab-footprint.svg) / [model close-up](../kicad/orientation/K2-6897ab-model.png); visual review **pending**, JLC preview **pending**.
- Q_D1: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_D1-90069e-footprint.svg) / [model close-up](../kicad/orientation/Q_D1-90069e-model.png); visual review **pending**, JLC preview **pending**.
- Q_D2: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_D2-3b286c-footprint.svg) / [model close-up](../kicad/orientation/Q_D2-3b286c-model.png); visual review **pending**, JLC preview **pending**.
- Q_D3: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_D3-7277bd-footprint.svg) / [model close-up](../kicad/orientation/Q_D3-7277bd-model.png); visual review **pending**, JLC preview **pending**.
- Q_D4: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_D4-f061e9-footprint.svg) / [model close-up](../kicad/orientation/Q_D4-f061e9-model.png); visual review **pending**, JLC preview **pending**.
- Q_O1: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_O1-225905-footprint.svg) / [model close-up](../kicad/orientation/Q_O1-225905-model.png); visual review **pending**, JLC preview **pending**.
- Q_O2: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_O2-357811-footprint.svg) / [model close-up](../kicad/orientation/Q_O2-357811-model.png); visual review **pending**, JLC preview **pending**.
- Q_O3: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_O3-d4806e-footprint.svg) / [model close-up](../kicad/orientation/Q_O3-d4806e-model.png); visual review **pending**, JLC preview **pending**.
- Q_O4: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_O4-c9d40b-footprint.svg) / [model close-up](../kicad/orientation/Q_O4-c9d40b-model.png); visual review **pending**, JLC preview **pending**.
- Q_PX: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_PX-e2af50-footprint.svg) / [model close-up](../kicad/orientation/Q_PX-e2af50-model.png); visual review **pending**, JLC preview **pending**.
- Q_R1: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_R1-a154e7-footprint.svg) / [model close-up](../kicad/orientation/Q_R1-a154e7-model.png); visual review **pending**, JLC preview **pending**.
- Q_R2: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/Q_R2-f0fbba-footprint.svg) / [model close-up](../kicad/orientation/Q_R2-f0fbba-model.png); visual review **pending**, JLC preview **pending**.
- SW_U1: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/SW_U1-3cdda1-footprint.svg) / [model close-up](../kicad/orientation/SW_U1-3cdda1-model.png); visual review **pending**, JLC preview **pending**.
- SW_U2: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/SW_U2-08195a-footprint.svg) / [model close-up](../kicad/orientation/SW_U2-08195a-model.png); visual review **pending**, JLC preview **pending**.
- SW_U3: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/SW_U3-7d3313-footprint.svg) / [model close-up](../kicad/orientation/SW_U3-7d3313-model.png); visual review **pending**, JLC preview **pending**.
- U_BK: pin 1: package dot/notch/chamfer or keyed connector corner; compare exact datasheet; [footprint close-up](../kicad/orientation/U_BK-e69a43-footprint.svg) / [model close-up](../kicad/orientation/U_BK-e69a43-model.png); visual review **pending**, JLC preview **pending**.
- U_C1: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/U_C1-da1512-footprint.svg) / [model close-up](../kicad/orientation/U_C1-da1512-model.png); visual review **pending**, JLC preview **pending**.
- U_C2: cathode K: diode band / LED cathode mark; [footprint close-up](../kicad/orientation/U_C2-029a2e-footprint.svg) / [model close-up](../kicad/orientation/U_C2-029a2e-model.png); visual review **pending**, JLC preview **pending**.

Before ordering: upload the gerber ZIP, BOM and CPL; inspect every keyed part in JLC's placement preview against these close-ups.
Diode/LED bands identify the cathode; electrolytic stripes usually identify the negative terminal; tantalum + marks identify the positive terminal.
For ICs use pin 1, and for BGAs use the datasheet's reference corner (often A1). A CPL angle alone is not orientation proof.
