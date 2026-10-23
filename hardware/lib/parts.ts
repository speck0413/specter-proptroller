// Spectre Proptroller part library: the parts this board uses, on top of the aieda toolchain library (re-exported below).
// Rules: KiCad library footprints only; every pad map checked against the manufacturer datasheet before use; LCSC numbers
// noted with stock checks at the time of selection. Board: JLCPCB Economic assembly (SMT by machine, THT by JLC or by hand).
import { def } from "../../aieda/src/dsl.ts";
export * from "../../aieda/src/lib/parts.ts";
// 0805 passives used on this board: UNI-ROYAL 0805W8F..T5E thick-film 1 % resistors; Yageo / Samsung MLCCs.
const R0805_LCSC: Record<string, string> = { "100": "C17408", "330": "C17630", "1k": "C17513", "1.3k": "C17384", "2.4k": "C17526", "3.6k": "C18359",
  "4.7k": "C17673", "5.1k": "C27834", "7.5k": "C17807", "10k": "C17414", "13k": "C17455", "18k": "C17506", "27k": "C17593", "43k": "C17695", "75k": "C17819" };
const C0805_LCSC: Record<string, { lcsc: string; v: number; diel: string }> = {
  "100n": { lcsc: "C49678", v: 50, diel: "X7R" },
  "10u": { lcsc: "C1713", v: 16, diel: "X5R" },   // X5R loses roughly half its value at 5 V bias: treat as ~5 uF on a 5 V rail
};

/** Chip resistor 0805. R0805("10k"); pass an LCSC number for values not tabulated (then check it yourself). */
export const R0805 = (val: string, lcsc?: string) => def("Resistor_SMD:R_0805_2012Metric", { 1: "1", 2: "2" },
  { val, lcsc: lcsc ?? R0805_LCSC[val], sym: "Device:R", h: 0.6, kind: "passive", tol: "1%", pwr: 0.125 });

/** Ceramic capacitor 0805. C0805P("100n") carries the voltage rating so the rail ratings check can see it. */
export const C0805P = (val: string, lcsc?: string) => { const t = C0805_LCSC[val]; return def("Capacitor_SMD:C_0805_2012Metric", { 1: "1", 2: "2" },
  { val, lcsc: lcsc ?? t?.lcsc, sym: "Device:C", h: 0.6, kind: "passive", v: t?.v, diel: t?.diel }); };

/** MMBT3904 NPN, SOT-23, 40 V 200 mA. Pin 1 B, 2 E, 3 C. verified: CJ MMBT3904 datasheet (1 BASE, 2 EMITTER, 3 COLLECTOR) against
 *  KiCad SOT-23, 2026-10-12. LCSC C20526 (JSCJ), JLC Basic, stock 230k, detail checked 2026-10-12. */
export const MMBT3904 = def("Package_TO_SOT_SMD:SOT-23", { B: "1", E: "2", C: "3" },
  { val: "MMBT3904", mpn: "MMBT3904", lcsc: "C20526", h: 1.1, kind: "ic" });

/** Green LED, 0805, K = pad 1, A = pad 2 (KiCad convention). KENTO KT-0805G, 2.6-3.1 V, 430 mcd. LCSC C2297, JLC Basic, stock 2.86M,
 *  detail checked 2026-10-12. verified: KiCad LED_0805 pad 1 = cathode (green mark side of the KENTO 0805 is the cathode: confirm in the
 *  orientation review, polarised part). */
export const LED0805G = def("LED_SMD:LED_0805_2012Metric", { K: "1", A: "2" },
  { val: "green", mpn: "KT-0805G", lcsc: "C2297", sym: "Device:LED", h: 0.8, kind: "led" });

/** AO4485 P-channel MOSFET, SOIC-8, -40 V, ID -10 A (TA 25 C, steady state; -12 A for 10 s), RDS(on) < 15 mOhm at VGS -10 V and < 20 mOhm at
 *  VGS -4.5 V (ID -8 A) (typ 12.5 / 16), VGS(th) -1.7 / -1.9 / -2.5 V, VGS +-20 V, Qg 42 nC typ (55 max) at -10 V / 18.6 nC at -4.5 V,
 *  Ciss 2500 pF, RthJA 59 / 75 C/W (steady), PD 1.7 W at 25 C. Pinout and footprint are the same as the AO4407A: pins 1-3 source, 4 gate, 5-8 drain.
 *  Power at 8 A: 8^2 x 15 mOhm = 0.96 W per FET (two in parallel on the VIN input: 0.5 W each); needs copper pour on the drain pins.
 *  verified: AOS AO4485 datasheet Rev 1.1 (aosmd.com/res/data_sheets/AO4485.pdf, SOIC-8 top view S S S G / D D D D, product summary, electrical table),
 *  against KiCad SOIC-8_3.9x4.9mm_P1.27mm, 2026-10-13. LCSC C51499 AO4485 (AOS), JLC Extended (none of the 40 V P-FETs is Basic or Preferred),
 *  stock 65,938.
 *  Cost-down alternative, same footprint and pinout, datasheet NOT read: UMW AO4485 C5125949 (stock 19,455; listing 12.5 mOhm at -10 V,
 *  16 mOhm at -4.5 V, 12 A, Qg 42 nC). Second source (original, same MPN): DigiKey 785-1551-2-ND (AO4485 reel; listing seen via search 2026-10-13).
 *  DPAK option (different footprint, would change the layout): AOD4185 (AOS C400894, stock 29,943; UMW C5346822, stock 37,088). */
export const AO4485 = def("Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", { S: ["1", "2", "3"], G: "4", D: ["5", "6", "7", "8"] },
  { val: "AO4485", mpn: "AO4485", lcsc: "C51499", h: 1.75, kind: "ic" });

/** LTV-217-B-G (Lite-On) phototransistor optocoupler, SOP-4 (half pitch, 1 channel, halogen free), 3.75 kVrms (3 kVrms test) isolation. Pins (datasheet outline page 2, internal diagram):
 *  1 anode, 2 cathode (LED), 3 emitter, 4 collector. Absolute maximum: IF 50 mA, VR 6 V (so a reversed LED drive is limited to 6 V), VCEO 70 V, VECO 7 V, IC 50 mA, Ptot 200 mW, -55..110 C.
 *  VF 1.2 V typ / 1.4 V max at IF 20 mA; VCE(sat) 0.4 V max at IF 8 mA, IC 2.4 mA; CTR 50-600 % at IF 5 mA, VCE 5 V over the whole series; RANK TABLE (page 5): A 80-160 %, B 130-260 %,
 *  C 200-400 %, D 300-600 %. This part is the "-B" rank, 130-260 % (TME listing; the LTV-217-G sheet lists ranks, not the ordering-code letter, so the B = rank B reading is NOT in the datasheet
 *  I read: ?; LCSC's parameter field shows the series range 50-600 %). ICEO 100 nA max at 50 V, tr 2 us typ / tf 3 us typ (18 max), BVCEO 80 V min.
 *  At IF 5 mA from a 3.3 V GPIO through 220 R (about 9 mA): worst case IC = 9 mA x 1.3 = 11 mA at CTR min.
 *  Footprint: Package_SO:SOP-4_4.4x2.6mm_P1.27mm: pads 1, 2 on one side and 3, 4 on the other, numbered counter-clockwise (1, 2, 3, 4) as the datasheet picture (rotated 90 degrees on the
 *  page); body 4.4 x 2.6, lead tip span 7.0, pitch 1.27, height 2.0 +-0.2.
 *  verified: Lite-On LTV-217-G series datasheet Rev A 2012 (LCSC copy of C115450; outline page, abs max, electrical, rank table), against KiCad pad coordinates (fpinfo), 2026-10-14.
 *  LCSC C115450 LTV-217-B-G, JLC BASIC, stock 310,905. Second source (US): Mouser / DigiKey LTV-217-B-G (Lite-On; Avnet carries it); a Mouser or DigiKey
 *  SKU was not looked up: ?. */
export const LTV217 = def("Package_SO:SOP-4_4.4x2.6mm_P1.27mm", { A: "1", K: "2", E: "3", C: "4" },
  { val: "LTV-217-B-G", mpn: "LTV-217-B-G", lcsc: "C115450", h: 2.0, kind: "ic" });

/** TPS54360BDDAR (TI) 60 V, 3.5 A step-down converter, 8-pin HSOIC PowerPAD (DDA), current mode, external compensation, 92 mOhm high-side FET, 0.8 V +-1 % reference,
 *  100 kHz - 2.5 MHz set by a resistor on RT/CLK, internal soft start, Eco-mode pulse skipping, thermal shutdown. SELECTED for the +5 V rail (2.5-3 A continuous, 5 V out, 9-24 V in).
 *  PINS (datasheet SNVSB93 section 5, top view): 1 BOOT, 2 VIN, 3 EN, 4 RT/CLK, 5 FB, 6 COMP, 7 GND, 8 SW, 9 thermal pad = GND (must be soldered and tied to GND: "proper operation").
 *  NOT pin-compatible with the TPS5430 (VIN is pin 2 here, pin 7 there; GND is pin 7 here, pin 6 there): board.ts must be re-wired, not swapped.
 *  ABSOLUTE MAXIMUM (section 6.1, verbatim table): VIN -0.3 to 65 V; SW -0.6 to 65 V (10 ns transient -2 to 65 V); BOOT 73 V; BOOT-SW 8 V; EN -0.3 to 8.4 V (NEVER tie EN to VIN: leave it
 *  floating, "Float to enable", internal pull-up current); FB, COMP -0.3 to 3 V; RT/CLK -0.3 to 3.6 V. Operating VIN 4.5-60 V. SMAJ24A clamps at 38.9 V: 26 V of margin (the TPS5430 had 1.1 V).
 *  Current limit 4.5 A minimum peak, 5.5 A nominal; RthJA 42 C/W is the JEDEC 4-layer figure (section 6.4): this board is 2 layers, assume 55 C/W (45-65). RthJC(bot) 3.6 C/W. Height 1.7 mm max.
 *  TI TYPICAL DESIGN (Figure 32, 12 V nominal / 8.5-60 V in, 5 V / 3.5 A out, fsw 600 kHz; section 8.2, VERBATIM values): C1 + C2 = 2 x 2.2 uF 100 V ceramic at VIN (at least 3 uF EFFECTIVE after DC bias),
 *  C4 0.1 uF BOOT-SW (10 V or more), L1 8.2 uH (Wurth 7447797820, Isat 5.8 A, Irms 5.05 A; datasheet: Isat at or above the 5.5 A switch limit is the conservative choice), D1 B560C-13-F (60 V 5 A SMC, 0.7 V at
 *  5 A, 300 pF; "60 V minimum reverse voltage is preferred"), C6 + C7 = 2 x 47 uF ceramic at VOUT (TI uses 58.3 uF DERATED in the loop maths), R6 10.2k (FB to GND) and R5 53.6k (VOUT to FB), R3 162k on RT/CLK
 *  (RT[kOhm] = 101756 / fsw[kHz]^1.008), compensation R4 13.0k + C5 6800 pF in series on COMP and C8 39 pF in parallel with that pair, UVLO divider R1 523k / R2 84.5k on EN (8 V start, 6.25 V stop; optional).
 *  TI compensation recipe: fp(mod) = Iout(max) / (2 pi Vout Cout) = 1912 Hz; fco = the lower of sqrt(fp x fz(mod)) and sqrt(fp x fsw / 2) = 23.9 kHz; R4 = (2 pi fco Cout / gmps) x (Vout / (Vref x gmea)) with gmps 12 A/V,
 *  gmea 350 uA/V = 13 kOhm; C5 = 1 / (2 pi R4 fp(mod)) = 6404 pF (6800 used); C8 = the larger of Cout x ESR / R4 and 1 / (pi R4 fsw) = 39 pF. Minimum inductor L = (Vin(max) - Vout) / (Iout x 0.3) x Vout / (Vin(max) x fsw);
 *  inductor ripple must stay above 150 mA for stable PWM.
 *  THIS BOARD'S ADAPTATION (mine, derived with TI's equations; the TI circuit values above are the ones that are datasheet-verified): fsw 644 kHz with RT 150k (R_150K, Basic; TI's 162k is Extended; 150k gives
 *  (101756/150)^(1/1.008) = 644 kHz, under the 710 kHz skip limit at 60 V and 1.09 MHz at the 38.9 V clamp); L1 = L10U_SMD (10 uH, Isat 8.8 A min; ripple 0.62 A p-p at 24 V in, 0.35 A p-p at 9 V in, peak 2.8 A at 2.5 A out);
 *  D1 = SS56P (60 V 5 A SMA; see the thermal note: add a second SS56P in parallel); CIN = 2 x C1206_10U50 (about 8 uF effective at 24 V bias); BOOT = C0805P("100n") (50 V); COUT = 2 x C1206_47U10 (the same 2 x 47 uF as TI, 10 V X5R 1206;
 *  its DC-bias derating at 5 V was NOT checked against the Samsung curve: ?, so TI's 58.3 uF effective is an assumption; if the real value is half, fp(mod) doubles and the phase margin drops: bench-check with a 0.9 to 2.6 A load step);
 *  FB divider R5 = 43k (R0805("43k"), Preferred) over R6 = 8.2k (R_8K2, Basic): 0.8 x (1 + 43 / 8.2) = 4.995 V nominal (TI's 53.6k C21398, 10.2k C17402 and 162k C17486 are all Extended); R4 = R0805("13k") (Preferred), C5 = C0805_6N8 (6.8 nF X7R, Preferred),
 *  C8 = C0805_47P (47 pF C0G, Basic; TI's 39 pF is Extended; the pole lands at 260 kHz instead of 314 kHz, harmless). Re-running TI's recipe at 644 kHz gives fco 24.8 kHz and R4 = 13.5 kOhm, so 13k / 6.8 nF stand. EN floating.
 *  THERMAL at 5 V / 2.5 A out, 50 C ambient, 2-layer board with GND pour, RthJA 55 C/W assumed: IC loss from TI's Figure 43 (5 V out, 600 kHz, 87.5 % at 24 V in, 91 % at 12 V in, read off the chart) minus the diode (0.5 V x 2.5 A x (1 - D))
 *  and the inductor DCR (0.16 W) is about 0.65 W at 24 V and 0.4 W at 12 V, so Tj = 86 C at 24 V and 72 C at 12 V (94 C / 78 C with 0.8 W / 0.5 W; 102 C / 83 C at 65 C/W). The CATCH DIODE is the hot part: SS56P at 2.5 A is 1.1 W at 24 V, 0.8 W at 12 V,
 *  RthJA 60 C/W gives about 115 C / 98 C: use two SS56P in parallel (about 83 C / 72 C as a BEST CASE with equal current sharing; Schottky Vf falls as it heats, so sharing is not guaranteed: put the two side by side on one copper patch so they stay thermally coupled).
 *  verified: TI TPS54360B datasheet SNVSB93 (December 2018; ti.com/lit/ds/symlink/tps54360b.pdf; pin table, section 6.1 abs max, 6.4 thermal, section 8.2 design example Figure 32 and equations 25-51, Figure 43 efficiency, package drawing DDA0008B),
 *  against KiCad Package_SO:TI_SO-PowerPAD-8: pads 1-4 west, 5-8 east, exposed pad = four sub-pads all named 9, EP 2.41 x 3.1 mm inside the drawing's 2.11-2.71 x 2.8-3.4 mm (same DDA package as the TPS5430), 2026-10-14.
 *  LCSC C524806 TPS54360BDDAR, JLC EXTENDED (one new type), stock 59,034. Use the B part: the older TPS54360DDAR (C44377, 2.5 MHz, no B) is a different datasheet and Mouser/DigiKey list it NRND.
 *  Second source (US): same MPN, DigiKey 296-TPS54360BDDARCT-ND (cut tape, seen via search 2026-10-14), Mouser 595-TPS54360BDDAR (seen via search, stock varied: 0 to 4,303); the tube part TPS54360BDDA is the same die. 5 A sibling, same pinout (datasheet NOT read: ?):
 *  TPS54560BDDAR C1850354 (stock 89,488). 42 V sibling TPS54340BDDAR (45 V abs max, same pinout) C1850353 (stock 1,833): less margin, dearer, thinner stock. */
export const TPS54360B = def("Package_SO:TI_SO-PowerPAD-8", { BOOT: "1", VIN: "2", EN: "3", RT: "4", FB: "5", COMP: "6", GND: ["7", "9"], SW: "8" },
  { val: "TPS54360BDDAR", mpn: "TPS54360BDDAR", lcsc: "C524806", h: 1.7, kind: "ic" });

/** SMAJ24A unidirectional TVS, SMA, 400 W (10/1000 us), 24 V stand-off, 26.7 V min / 29.5 V max breakdown, 38.9 V clamp at 10.3 A. K = pad 1 (band end), as D_SMA.
 *  RECONCILED: use the hongjiacheng part C19077541 (JLC PREFERRED, stock 107,791), not the Vishay SMAJ24A-E3/61 C89612 (Extended, stock 15,808) that
 *  parts-brain.ts uses. Same SMAJ24A class; the C19077541 listing shows 24 V, 29.5 V breakdown max, 38.9 V clamp, 10.3 A, 400 W.
 *  verified: LCSC/JLC listing C19077541 (datasheet PDF not opened beyond the listing), pad order as the SMAJ15A in parts.ts (cathode band, D_SMA pad 1), 2026-10-13. */
export const SMAJ24A = def("Diode_SMD:D_SMA", { K: "1", A: "2" },
  { val: "SMAJ24A", mpn: "SMAJ24A", lcsc: "C19077541", sym: "Device:D_TVS", h: 2.4, kind: "passive" });

/** SS56 Schottky 60 V 5 A, SMA, K = pad 1, Vf 0.7 V at 5 A. MDD SS56, LCSC C65009, JLC Extended, stock 189,865. Recommended
 *  for the buck catch diode and the OUT flyback diodes (60 V against a 38.9 V clamp). No Basic 60 V Schottky in SMA was found. */
export const SS56 = def("Diode_SMD:D_SMA", { K: "1", A: "2" },
  { val: "SS56", mpn: "SS56", lcsc: "C65009", sym: "Device:D_Schottky", h: 2.4, kind: "passive", v: 60 });

/** SS56P: the same SS56 (SMA / DO-214AC, 60 V, 5 A) from the JLC PREFERRED listing, so there is no extended-part fee. Zhuhai Hongjiacheng "SS52 THRU SS520" datasheet Rev 2.0 (LCSC copy of
 *  C18199169; LCSC brand "R+O"): SS56 VRRM 60 V, VRMS 42 V, IF(AV) 5.0 A, IFSM 120 A, Tj -55..125 C (series), RthJA 60, RthJL 22 C/W, cathode line marks the cathode, package outline A-H and
 *  pad layout as the SMA standard, so KiCad D_SMA (K = pad 1) applies as for the MDD part. Listing: Vf 0.7 V at 5 A, IR 200 uA at 60 V (the MDD C65009 listing says 1 mA at 60 V). The VF and
 *  IR table cells for SS56 were cut off in the PDF text, so those two figures are the LCSC listing's, not read from the sheet. JLC Preferred re-checked 2026-10-14 with jlc_code.py:
 *  "expand PREF", stock 1,193,211. RE-CHECK Preferred at order time. */
export const SS56P = def("Diode_SMD:D_SMA", { K: "1", A: "2" },
  { val: "SS56", mpn: "SS56", lcsc: "C18199169", sym: "Device:D_Schottky", h: 2.4, kind: "passive", v: 60 });

/** 1N4148W switching diode, SOD-123, 75 V 150 mA, K = pad 1 (band). LCSC C81598 (Semtech), JLC BASIC, stock 4,743,113. verified: listing (75 V, 150 mA, 1 V at 50 mA)
 *  and the cathode band on KiCad D_SOD-123 pad 1, 2026-10-13. */
export const D1N4148W = def("Diode_SMD:D_SOD-123", { K: "1", A: "2" },
  { val: "1N4148W", mpn: "1N4148W", lcsc: "C81598", sym: "Device:D", h: 1.2, kind: "passive" });

/** 2N7002 N-MOSFET, SOT-23, 60 V 115 mA, VGS +-20 V, RDS(on) 5 ohm at 10 V, VGS(th) 2.5 V max. Pin 1 G, 2 S, 3 D (as the AO3400A in parts.ts, same SOT-23 numbering).
 *  verified: listing C8545 (Changjiang, SOT-23) and the JEDEC SOT-23 MOSFET convention; datasheet pin picture NOT opened: ? (confirm in the orientation review).
 *  LCSC C8545, JLC BASIC, stock 1,880,748. VGS(th) up to 2.5 V is marginal for a 3.3 V gate drive (check the level-shift circuit). */
export const N2N7002 = def("Package_TO_SOT_SMD:SOT-23", { G: "1", S: "2", D: "3" },
  { val: "2N7002", mpn: "2N7002", lcsc: "C8545", sym: "Device:Q_NMOS_GSD", h: 1.1, kind: "ic" });

/** SMD replacement for CP100U50 (rev A, owner 2026-10-14): DMBJ RVT1H101M0810, 100 uF +-20 %, 50 V (the SMAJ24A clamps at 38.9 V, so 35 V is not enough), SMD aluminium electrolytic
 *  D8 x L10.2 mm, 105 C 2800 h, leakage <= 50 uA, ripple <= 146 mA rms at 120 Hz / 105 C (datasheet; no 100 kHz rating or ESR given: ?), body 8.3 x 8.3 x 10.2 max, tape 24 mm.
 *  The bulk sits behind the buck's 1206 10 u 50 V ceramics, so switching ripple lands on those first; 2 x 100 uF at 146 mA is a margin check for the first article, not a design number.
 *  Pad 1 = + (KiCad CP_Elec convention; the can stripe marks the NEGATIVE terminal, so pad 1 is the side away from the stripe; confirm against the silkscreen bar on the first article).
 *  FOOTPRINT: Capacitor_SMD:CP_Elec_8x10 (Nichicon 8x10): pads +-3.25 mm, 3.5 x 2.5 mm each, EXACTLY the DMBJ recommended land for 8 mm cans (X 2.5, Y 3.5, gap a 3.0).
 *  (CP_Elec_8x10.5 is the Vishay 0810 pattern with pads at +-3.7 mm, not this part's.)
 *  verified: DMBJ RVT series datasheet (wmsc.lcsc.com copy of C970687: table row 76, dimensions table, recommended land size), 2026-10-14. LCSC C970687, JLC Extended (no Basic or Preferred
 *  aluminium SMD electrolytic exists at any voltage), stock 20,356. Substitutes, same series/footprint: ROQANG C72480 (stock 93,708,
 *  2000 h, same 146 mA); stiffer ripple at 100 kHz, same D8 size but L10.5 (CP_Elec_8x10.5 pattern, NOT this footprint): jieerrui JVE50V100M8X10 C48615127 (300 mA at 100 kHz, 600 mOhm,
 *  stock 3,373). Extended-type count is unchanged against the THT C165524 it replaces. */
export const CP100U50_SMD = def("Capacitor_SMD:CP_Elec_8x10", { P: "1", N: "2" },
  { val: "100u/50V", mpn: "RVT1H101M0810", lcsc: "C970687", sym: "Device:C_Polarized", h: 10.2, kind: "passive", v: 50, diel: "Al-elec" });

export const C1206_10U50 = def("Capacitor_SMD:C_1206_3216Metric", { 1: "1", 2: "2" }, { val: "10u/50V", mpn: "CL31A106KBHNNNE", lcsc: "C13585", sym: "Device:C", h: 1.8, kind: "passive", v: 50, diel: "X5R" });

/** TPS54360B block parts (rev A +5 V buck), listings read with jlc_code.py 2026-10-14, 3D models fetched with bin/aieda lib model:
 *  47u 10 V X5R 1206 Samsung CL31A476MPHNNNE C96123 (Basic, stock 365,932; TI's output capacitance is 2 x 47 uF; DC-bias derating at 5 V not checked: ?);
 *  6.8n 50 V X7R 0805 Fenghua 0805B682K500NT C1755 (JLC PREFERRED, fee-free, stock 79,575; re-check Preferred at order time) is the COMP capacitor C5;
 *  47p 50 V C0G 0805 Samsung CL21C470JBANNNC C14857 (Basic, stock 42,731) is the COMP pole C8 (TI uses 39 pF, which is Extended only). */
export const C1206_47U10 = def("Capacitor_SMD:C_1206_3216Metric", { 1: "1", 2: "2" }, { val: "47u/10V", mpn: "CL31A476MPHNNNE", lcsc: "C96123", sym: "Device:C", h: 1.8, kind: "passive", v: 10, diel: "X5R" });

export const C0805_6N8 = def("Capacitor_SMD:C_0805_2012Metric", { 1: "1", 2: "2" }, { val: "6.8n/50V", mpn: "0805B682K500NT", lcsc: "C1755", sym: "Device:C", h: 0.9, kind: "passive", v: 50, diel: "X7R" });

export const C0805_47P = def("Capacitor_SMD:C_0805_2012Metric", { 1: "1", 2: "2" }, { val: "47p/50V", mpn: "CL21C470JBANNNC", lcsc: "C14857", sym: "Device:C", h: 0.9, kind: "passive", v: 50, diel: "C0G" });

export const R_220 = () => R0805("220", "C17557");

export const R_470 = () => R0805("470", "C17710");

/** TPS54360B block: 150k RT (C17470, Basic, stock 917,620) and 8.2k FB-bottom (C17828, Basic, stock 299,690), UNI-ROYAL 0805W8F, listings read 2026-10-14.
 *  43k (C17695) and 13k (C17455) are already in the R0805 table and are JLC Preferred (fee-free). */
export const R_150K = () => R0805("150k", "C17470");

export const R_8K2 = () => R0805("8.2k", "C17828");

/** FB_DF: ferrite bead in the DFPlayer Mini 5 V feed (its on-board amp pulls ~0.8 A peaks). TDK MPZ2012S221AT000, 0805, 220 ohm +-25 % at 100 MHz, 3 A rated,
 *  DCR 40 mOhm max, -55..125 C. Non-polarised: pins 1 and 2.
 *  NO BASIC PART QUALIFIES: the JLC Basic 0805 beads are Sunlord GZ2012D101TF C1015 (100R, 800 mA, 150 mOhm) and GZ2012D601TF C1017 (600R, 500 mA, 300 mOhm), both far under 2 A / 0.1 ohm;
 *  the Basic 120R/2A bead is 0603 (Murata BLM18PG121SN1D C14709). No Preferred bead found. So this is Extended (an extra per-order fee if it is a new Extended type).
 *  verified: JLC parts API listing (class, stock) 2026-10-14, DigiKey/HQ listings for the TDK numbers (220R, 3 A, 40 mOhm max); TDK datasheet PDF NOT opened: ?. Footprint
 *  Inductor_SMD:L_0805_2012Metric checked with fpinfo: pads 1 and 2 at x = -1.0625 / +1.0625 mm, 1.125 x 1.2 mm each; a bead has no polarity.
 *  LCSC C76818, JLC EXTENDED, stock 168,397. Second source (US): DigiKey MPZ2012S221AT000 (product 571875, active; DigiKey ordering SKU not read: ?).
 *  Same footprint alternatives (all Extended, JLC listings): Murata BLM21PG221SN1D C85840 (220R, 2 A, 45 mOhm, stock 173,455); Murata BLM21PG121SN1D C79382 (120R, 3 A, 30 mOhm,
 *  stock 153,945); Chilisin PBY201209T-121Y-N C142063 (120R, 3 A, 40 mOhm, stock 27,924); Sunlord UPZ2012U221-3R0TF C56066 (220R, 3 A, 40 mOhm, stock 371,596). */
export const FB_DF = def("Inductor_SMD:L_0805_2012Metric", { 1: "1", 2: "2" },
  { val: "220R@100M 3A", mpn: "MPZ2012S221AT000", lcsc: "C76818", sym: "Device:FerriteBead", h: 0.85, kind: "passive" });

/** SMD 10 uH shielded power inductor for the TPS5430 buck (rev A): RECOMMENDED Sunlord MWSA1005S-100MT, 10 uH +-20 % (100 kHz, 1 V), DCR 28 mOhm max,
 *  Isat (L drops 30 %) 8.8 A min / 10.0 A typ, Irms (dT 40 C) 7.0 A min / 8.0 A typ, 10.0 x 11.5 x 4.8 mm (4.8 +-0.2; h 5.0 used), molded. Isat 8.8 A against a 4-5 A switch limit;
 *  non-polar, pads 1 and 2 are interchangeable. KiCad Inductor_SMD:L_Sunlord_MWSA1005S: pads 4.1 x 4.1 mm at +-4.75 mm, 5.4 mm gap, EXACTLY the Sunlord recommended land (I 4.1, J 5.4, H 4.1).
 *  verified: Sunlord SMD Power Inductor catalogue rev 2023/09/13 (LCSC copy of C17700185, page 2 dimension/land table, page 8 MWSA1005S table, row MWSA1005S-100MT), 2026-10-14.
 *  LCSC C17700185, JLC Extended (no Basic or Preferred power inductor exists), stock 3,247. The listing shows the TYPICAL ratings (10 A / 8 A); design to the min column.
 *  Other candidates: L10U_SMDRI127 below (cheaper, taller, shielded, 12.3 square); SXN/Sunltech SLD10D50S100MTT C216167 (10.5 x 10.3 x 5.0, DCR 26 mOhm max, single 4.5 A IDC rating, tolerance
 *  +-20 %, stock 1,834): its datasheet's land pattern was not read and no KiCad footprint was matched, so it is NOT exported: ? ; Isat vs Irms not split on its sheet.
 *  Same-pad molded alternates by listing only (11.5 x 10 class, not checked against the MWSA land): FAUKU GSM1040-100-M C51933849 (30 mOhm, 8.6 A / 7.8 A, stock 30,711). */
export const L10U_SMD = def("Inductor_SMD:L_Sunlord_MWSA1005S", { 1: "1", 2: "2" },
  { val: "10uH 8.8A", mpn: "MWSA1005S-100MT", lcsc: "C17700185", sym: "Device:L", h: 5.0, kind: "passive" });

/** 3.5 mm TRS jack for TRIG1-2 and CONTACT1-2. THE BRIEF'S PART HAS NO MATCHING KICAD FOOTPRINT: XKB PJ-320A (C2884926, THT, 4 slot pins) drawing: three pins in a column at
 *  3.2 / 6.2 / 10.2 mm from the front edge and one pin 4.6 mm across at 11.4 mm, two 1.4 mm posts 7.0 mm apart at 1.6 / 8.6 mm; the closest KiCad footprint (Qingpu PJ320E,
 *  Jack_3.5mm_PJ320E_Horizontal) has the posts 1.3 mm beyond the pins instead, so the posts would be about 2.9 mm off. Not usable.
 *  CHOSEN INSTEAD: SMD PJ-320D class jack with a matching KiCad footprint. Geometry checked: Shou Han PJ-320D drawing (posts 7.00 apart, 1 mm, pads B/C/D at 10.30 / 6.30 / 3.30 mm
 *  from the pad-A line, 3.0 / 4.0 mm gaps) against KiCad Jack_3.5mm_PJ320D_Horizontal (R2, R1, T, S at -3.175, -0.175, 3.825, 4.925; posts at -4.775 and 2.225): matches within 0.05 mm.
 *  Mouth faces -x at rot 0 (barrel nub at x -8.4..-6.4 on F.Fab), so facing "W" (corrected 2026-10-13; was "S"). Pads: T = tip (deepest contact), R1 = ring, R2 and S = sleeve. A TRS plug's long sleeve shorts R2 and S together, so tie R2 and S to GND and take the ring from R1 (a TRRS socket used
 *  with a TRS plug); the datasheet schematic labels the contacts A (barrel), B, C, D but not tip/ring/sleeve: confirm the tip pad by continuity on the first article: ?.
 *  LCSC C431535 PJ-320D (SHOU HAN), SMD, JLC Extended, stock 55,516. Same footprint family, Korean Hroparts own PJ-320D-4A: C95562 (KiCad has its own
 *  footprint, stock 1,413). It is a surface-mount jack, so cable pulls load the solder joints (a THT part does not). Hooya PJ-320E C2939183 (THT, stock 2,508) is no match either
 *  (5.1 / 9.1 mm pin spacing against the KiCad PJ320E's 4.0 / 7.0). Body height 5 mm (datasheet 5.00). */
export const JACK_TRS = def("Connector_Audio:Jack_3.5mm_PJ320D_Horizontal", { TIP: "T", RING: "R1", SLEEVE: ["S", "R2"] },
  { val: "PJ-320D", mpn: "PJ-320D", lcsc: "C431535", facing: "W", h: 5, kind: "connector" });

/** Songle SRD-05VDC-SL-C relay, SPDT (1c), 5 V coil 70 ohm (71.4 mA, 0.36 W), 10 A 250 VAC class, pull-in 75 % max (3.75 V), 19.2 x 15.6 x 15.8 mm. KiCad
 *  Relay_THT:Relay_SPDT_SANYOU_SRD_Series_Form_C (pads 1..5, holes 1.3 mm, 12.2 mm between the coil column and the contact column, 12.0 mm across). Layout checked against the Sanyou SRD
 *  "1c" PCB layout (bottom view): the lone centre pin is common (pad 1), the two small pins 2.0 mm away are the coil (pads 2 and 5), the two pins 12.2 mm away are the contacts
 *  (pads 3 and 4). WHICH CONTACT IS NC AND WHICH NO is not printed on the datasheet's pin picture: NO = pad 3 and NC = pad 4 follows the KiCad Relay:SANYOU_SRD_Form_C symbol (arrow flag on
 *  pin 3); confirm by continuity with the coil off on the first article: ?. Coil pads have no polarity.
 *  verified: Songle SRD datasheet (LCSC copy of C35449) and Sanyou SRD.pdf page 3, against the KiCad footprint pads, 2026-10-13.
 *  LCSC C35449, JLC Extended THT, stock 102,274. Second source: Songle / Sanyou SRD-05VDC-SL-C on DigiKey/Mouser (not checked: ?). */
export const SRD05 = def("Relay_THT:Relay_SPDT_SANYOU_SRD_Series_Form_C",
  { COM: "1", COIL_A: "2", COIL_B: "5", NO: "3", NC: "4" },
  { val: "SRD-05VDC-SL-C", mpn: "SRD-05VDC-SL-C", lcsc: "C35449", sym: "Relay:SANYOU_SRD_Form_C", h: 15.8, kind: "ic" });

/** Owner measured 18.01 mm between the two DFPlayer header rows (2026-10-14); used as 18.0. */
export const DFPLAYER_ROW_SPACING_MM = 18.0;

/** XKB DC-005-5A-2.0 DC jack, female 2.0 mm centre pin / 5.5 mm (6.2 mm outer) plug, right angle THT, 14.0 x 9.0 mm, rated 57 V 5 A (drawing, spec 1-1; LCSC listing says 24 V 5 A,
 *  design to the lower 24 V), contact resistance 50 mOhm initial, 5,000 cycles, -20..70 C (listing). Terminals (drawing circuit diagram + PCB layout top view, slots 1.0 x 3.5 mm,
 *  tolerance 0.05): 1 = centre pin (tip), 2 = sleeve, 3 = sleeve switch (closed to 2 with no plug inserted; leave unconnected). Measured from the mouth: pin 2 at 7.70, pin 3 at 10.60,
 *  pin 1 at 13.60 (so 1-2 = 5.9 apart, pin 3 midway and 4.70 to the side).
 *  FOOTPRINT: Connector_BarrelJack:BarrelJack_Horizontal (the generic DC-005 pattern): pad 1 (0,0), pad 2 (-6.0,0), pad 3 (-3.0,+4.7), slots 1.0 x 3.0 mm (pads 1, 2) and 3.0 x 1.0 (pad 3),
 *  mouth toward -x so facing "W". Differences from the drawing: pad 2 and 3 are 0.1 mm off (5.9 / 6.0), slots are 3.0 long against 3.5 drawn: the tab WIDTH is not on the drawing,
 *  so test-fit a sample before the first order (?). Not matching: PJ-102AH (round 1.6 holes) and Wuerth 6941xx301002 (5.8 / 4.8 pattern, wrong). Same pattern as the
 *  Adafruit/CUI-class jacks in KiCad.
 *  verified: XKB drawing X-DC-005-5A-2.0 rev A1 (wmsc.lcsc.com copy of C381116), against fpinfo of BarrelJack_Horizontal, 2026-10-14. LCSC C381116, JLC Extended, stock 23,930
 *  (JLC API 2026-10-14). Height 11.0 (drawing, front view). Second source: any DC-005 2.0 mm jack with the 3-slot pattern (DigiKey/Mouser SKU not checked: ?). */
export const DC005 = def("Connector_BarrelJack:BarrelJack_Horizontal", { TIP: "1", SLEEVE: "2", SW: "3" },
  { val: "DC-005-5A-2.0", mpn: "DC-005-5A-2.0", lcsc: "C381116", facing: "W", h: 11, kind: "connector", v: 24 });

/** DNP 5.08 mm screw terminal footprint, 2P, 3P or 5P (RAILS; -05 pads checked with fpinfo 2026-10-14: same 5.08 row, 1.6 mm drill), mechanical (no BOM or CPL line): the owner hand-solders a KF301-class blue terminal block or a 16-18 AWG pigtail.
 *  FOOTPRINT: TerminalBlock_CUI:TerminalBlock_CUI_TB007-508-02 / -03 (pitch 5.08, row at y 0, pads 1..n along +x): drill 1.60 mm, pad 2.40 mm round = 0.40 mm annular ring per side
 *  (JLC-legal; JLC minimum is 0.15 mm), pad edge gap 2.68 mm. The 1.6 mm hole takes a KF301-class pin (1.0 x 0.8 mm class; generic 5.08 mm block datasheet, Marco MC000034, says PCB hole 1.5 mm,
 *  wire 26-12 AWG) and a soldered 16 AWG stranded wire (about 1.5 mm with the strands pressed). Rejected: Phoenix MKDS-1,5 / MKDS-3 5.08 (drill 1.3, pad 2.6: the 1.3 mm hole is too small
 *  for a 16 AWG wire), MSTBA (1.4), MC 1,5 (1.2), RND 205-00232 (1.1) and 205-00287 (1.3). Connector_Wire has only SolderWire 0.5-4 mm pad families (1x02 at 4 mm pitch and below), none at 5.08.
 *  CUI/Same Sky TB007-508 drawing (cuidevices.com/product/resource/tb007-508.pdf, 09/12/2024): pitch 5.08, PCB hole 1.60, depth 10.25 (4.90 behind the pin row, 5.35 in front: wire entry
 *  faces +y = "S" at rot 0), pin 1.0 x 0.8, 16 A / 300 V, wire 24-12 AWG, body 14.0 high on the drawing (a generic KF301 reads about 10: use 14 for the access check, conservative).
 *  Drawing verified against KiCad pad positions and drill 2026-10-14. A real KF301-5.08 drawing was not found: the pin size of a typical KF301-class part is not verified: measure it. */
export const TB508_DNP = (n: 2 | 3 | 5) => def(`TerminalBlock_CUI:TerminalBlock_CUI_TB007-508-0${n}_1x0${n}_P5.08mm_Horizontal`,
  Object.fromEntries(Array.from({ length: n }, (_, i) => [String(i + 1), String(i + 1)])) as Record<string, string>,
  { val: `5.08-${n}P (DNP)`, mpn: `TB007-508-0${n} footprint`, facing: "S", h: 14, kind: "connector", mech: true });

/** 1x15 female 2.54 mm socket, 8.5 mm insulation, for the ESP32 DevKit V1 clone (two of them, row spacing see ESP32_DEVKIT_V1_ROW_SPACING_MM_UNVERIFIED). Hand-soldered by the owner:
 *  mech (no BOM line). Reference part if the board is ever assembled: HanElectricity 2541FV-15P-B LCSC C25503121, JLC Extended, stock 3,273 (8.5 mm, 3 mm tail, brass, gold);
 *  JXTCONN JXT-PM2.54-1X15P-A9 C54803365 (same family as C42431840, stock 192); hanxia HX PM2.54-1x15P ZC C41417327 (stock 7,691); Kinghelm KH-2.54FH-1X15P-H8.5 C2932676
 *  (stock 4,277). Generic equivalent: any 15-pin 2.54 mm female header strip, "tall" 8.5 mm. KiCad PinSocket_1x15_P2.54mm_Vertical, pins 1..15. LCSC data read 2026-10-14. */
export const HDR_1x15_F_DNP = def("Connector_PinSocket_2.54mm:PinSocket_1x15_P2.54mm_Vertical",
  Object.fromEntries(Array.from({ length: 15 }, (_, i) => [String(i + 1), String(i + 1)])) as Record<string, string>,
  { val: "1x15 socket (DNP)", mpn: "2541FV-15P-B", lcsc: "C25503121", h: 8.5, kind: "header", mech: true });

/** Hand-soldered variant of HDR_1x8_F for the ESP32-C3 SuperMini (two of them). Mech (no BOM line); the reel part is C42431840 (see HDR_1x8_F). */
export const HDR_1x8_F_DNP = def("Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical",
  { 1: "1", 2: "2", 3: "3", 4: "4", 5: "5", 6: "6", 7: "7", 8: "8" },
  { val: "1x8 socket (DNP)", mpn: "PM2.54-1X08PM-H85", lcsc: "C42431840", h: 8.5, kind: "header", mech: true });

/** 1x20 female 2.54 mm socket, 8.5 mm insulation, for the Raspberry Pi Pico / Pico W / Pico 2 / Pico 2 W (two of them, rows 17.78 mm apart: PICO_PINS below). Hand-soldered by the owner: mech (no BOM line).
 *  KiCad Connector_PinSocket_2.54mm:PinSocket_1x20_P2.54mm_Vertical: pads 1..20 on a 2.54 mm pitch down +y (pad 20 at y 48.26), courtyard x -1.845 .. +1.795, y -1.845 .. +50.045 (checked with fpinfo 2026-10-14).
 *  Reference part if the board is ever assembled: Kinghelm KH-2.54FH-1X20P-H8.5 LCSC C2905423 (JLC Extended THT, stock 3,291; listing: 20P 2.54 mm, 3 A, 8.5 mm, same family as the 1x15 C2932676).
 *  Alternates: HX PM2.54-1x20P ZC C41417332 (gold, 3.2 mm tail, 8.5 mm, stock 3,139); HanElectricity 2541FV-20P-B C25503124 (stock 79 only). 3D model fetched for C2905423 with bin/aieda lib model (lib/3d/C2905423.step).
 *  Second source (US): Sullins PPTC201LFBN-RC, DigiKey S7018-ND (20-position, 2.54 mm, insulation height 0.335 in = 8.51 mm, tin; seen via search 2026-10-14; a Mouser SKU was not found: ?). Generic: any "tall" 8.5 mm 1x20 female strip.
 *  verified: KiCad footprint pad coordinates (fpinfo) against the Pico datasheet pin pitch (2.54 mm, 1 mm holes: Pico datasheet section 2); the LCSC listing for the height (datasheet of the Kinghelm part not read: ?). */
export const HDR_1x20_F_DNP = def("Connector_PinSocket_2.54mm:PinSocket_1x20_P2.54mm_Vertical",
  Object.fromEntries(Array.from({ length: 20 }, (_, i) => [String(i + 1), String(i + 1)])) as Record<string, string>,
  { val: "1x20 socket (DNP)", mpn: "KH-2.54FH-1X20P-H8.5", lcsc: "C2905423", h: 8.5, kind: "header", mech: true });

/** n-pin male 2.54 mm pin header, vertical, owner-soldered (DNP, mech: no BOM line). Used as the S / V / G expansion rows (three of them, n = 11) and the RAILS strip (n = 5).
 *  KiCad has NO 3-row 2.54 mm header (Connector_PinHeader_2.54mm holds only 1xN and 2xN, plus SMD variants), so the 3 x 11 field is three PinHeader_1x11_P2.54mm_Vertical, rotated 90 degrees (rows along x), one per signal row:
 *  S row, V row, G row, pads 1..11 along the row. Placed 2.54 mm apart (a standard servo / Dupont pitch) their courtyards OVERLAP by 1.09 mm (courtyard is x -1.815 .. +1.815, 3.63 wide, fpinfo 2026-10-14):
 *  board.ts must either accept that overlap in the courtyard check or space the rows 3.63 mm apart (breaks the servo-plug pitch: use 2.54 mm). Pads of neighbouring rows are 2.54 mm apart (1.7 mm pads, 0.84 mm gap): fine to hand-solder.
 *  Pin n of S, V and G is one 3-pin servo plug position. Pins on the S row are 3.3 V logic only.
 *  Height above the board: 2.5 mm body + 6 mm pin = 8.5 mm (LCSC listing parameters of the PH2.54-1XnP-H25 family; datasheet not read: ?).
 *  Reference parts (JLC Extended THT, listings read 2026-10-14, each with its own 3D model fetched by bin/aieda lib model): n = 11: PZ254V-11-11P C2840011 (listing: 1x11P 2.54 mm, 2.5 mm body, 3 mm tail, 6 mm pin, 3 A; stock 2,819);
 *  n = 5: PH2.54-1X5P-H25 C42431835 (stock 18,658). Any other n: no exact-length part was looked up, so lcsc is left undefined (the KiCad PinHeader model is the fallback);
 *  the 1x20 strip PH2.54-1X20P-H25 C42431804 (stock 12,779) can be cut to length but its model is a 20-pin body, so it is NOT used for a shorter header. A 1x11 alternate: HX PZ2.54-1x11P ZZ C42372503 (stock 1,385). Second source (US): any 1x40 2.54 mm breakaway male header (Sullins PRPC040SAAN-RC, DigiKey S1011EC-40-ND; DigiKey SKU read from memory, not checked: ?).
 *  verified: KiCad PinHeader_1xNN_P2.54mm_Vertical pad coordinates (fpinfo, n = 11 and 5: pads 1..n on a 2.54 mm pitch), 2026-10-14. */
export const HDR_1xN_M_DNP = (n: number) => def(`Connector_PinHeader_2.54mm:PinHeader_1x${String(n).padStart(2, "0")}_P2.54mm_Vertical`,
  Object.fromEntries(Array.from({ length: n }, (_, i) => [String(i + 1), String(i + 1)])) as Record<string, string>,
  { val: `1x${n} pin header (DNP)`, mpn: n === 5 ? "PH2.54-1X5P-H25" : n === 11 ? "PZ254V-11-11P" : `1x${n} 2.54 mm pin header`, lcsc: n === 5 ? "C42431835" : n === 11 ? "C2840011" : undefined, h: 8.5, kind: "header", mech: true });

/** Same jumper delivered BRIDGED 1-2 (V row = 3V3 by default; cut the copper strip and bridge 2-3 for 5V). The footprint's one copper strip joins pads 1 and 2, so KiCad needs its (net_tie_pad_groups "1, 2")
 *  to allow the two nets on it: the KiCad 10 footprint declares it (grep'd 2026-10-14). Whether aieda's netlist / DRC honours net ties: NOT checked: ?; if not, use SJ3_OPEN plus a 0R as board.ts does today. */
export const SJ3_B12 = def("Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm_NumberLabels", { V33: "1", COM: "2", V5: "3" },
  { val: "SJ3 1-2", sym: "Jumper:SolderJumper_3_Bridged12", h: 0.1, kind: "passive", mech: true });

/** Pico W / Pico 2 W antenna keep-out, mm: "There is a cutout for the antenna (14 mm x 9 mm). If anything is placed close to the antenna (in any dimension) the effectiveness of the antenna is reduced. ...should be placed on the edge of a board
 *  and not enclosed in metal to avoid creating a Faraday cage. Adding ground to the sides of the antenna improves the performance slightly." (Pico W datasheet release 7 (build 03/07/2026), section 2.2.1, page 10, https://datasheets.raspberrypi.com/picow/pico-w-datasheet.pdf; Pico 2 W datasheet release 2 (build 03/07/2026), section 2.2.1, same words,
 *  https://datasheets.raspberrypi.com/picow/pico-2-w-datasheet.pdf; the plain Pico and Pico 2 have no antenna.) The text gives no orientation; Figure 7 (page 11, the carrier footprint: a cutout between the two pad rows at the end opposite the USB connector, no dimensions) shows the 14 mm side across the module
 *  (between the pin rows) and the 9 mm side along it, so w = 14 across and l = 9 deep from the antenna end of the module (my reading of the figure; scaling the figure against the 21 mm board width gives about 12.4 x 8.3 mm, so 14 x 9 is the larger, safe value).
 *  Use: no copper on any layer in this box, measured from the antenna end OF THE MODULE, not from the first pin: the module edge is PICO_PIN_TO_END_MM (1.37 mm) beyond pins 20 / 21, so with the pin centres at x = XA the box runs from
 *  x = XA - 1.37 to x = XA - 1.37 + l, centred between the Pico rows. (board.ts draws [0, AK.l] with XA = 3.5: that ends at 9.0, but the antenna region ends at 3.5 - 1.37 + 9 = 11.13, so it falls 2.13 mm short: use AK.l + XA - PICO_PIN_TO_END_MM.) */
export const PICO_W_ANTENNA_KEEPOUT_MM = { l: 9, w: 14 } as const;

export const PICO_PIN_TO_END_MM = 1.37;
