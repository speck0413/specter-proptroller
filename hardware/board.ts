// Spectre Proptroller, board rev B (rev A, the first ordered design, is described in hardware/README.md): all-in-one prop / show controller on a plug-in MCU board, 134 x 80 mm, 2 layers, JLC Economic.
// Edges (owner 2026-10-14 review): back = PIXEL, TRIG2, CONTACT1-2, RAILS daisy chain, power in, SPK (beside the DFPlayer); front = TRIG1 (far west), RLY1-2, OUT1-4;
// east = DFPlayer card slot and USER1-3; west = MCU antenna end; M3 holes in all four corners.
// MCU: EITHER a 30-pin ESP32 DevKit OR a Raspberry Pi Pico / Pico W / Pico 2 / Pico 2 W, nested in one socket field (antenna ends at the west edge).
// Build: bin/aieda build hardware/board.ts [--no-route]   Requirements: requirements.yaml (rev B)
// Coordinates: board-local mm, x from the west (left) edge, y from the back edge. Edges: back "N", front "S", left "W", right "E".
// xy() only for form-factor items: holes, the MCU socket nest and the DFPlayer sockets (module geometry). Everything else is placed by constraints.
import { align, apart, between, board, edge, near, region, req, xy, type Con, type PartDef } from "../aieda/src/dsl.ts";
import { FROZEN } from "./frozen.ts";
import { lockRoutes } from "./routes.ts";
import { B5819W, C0805P, LED0805G, MMBT3904, R0805, SW_TACT } from "./lib/parts.ts";
import {
  AO4485, FB_DF, C0805_47P, C0805_6N8, C1206_10U50, C1206_47U10, PICO_PIN_TO_END_MM, R_150K, R_8K2, SJ3_B12, TPS54360B, CP100U50_SMD, D1N4148W, DC005, DFPLAYER_ROW_SPACING_MM, HDR_1x15_F_DNP, HDR_1x20_F_DNP, HDR_1x8_F_DNP,
  HDR_1xN_M_DNP, JACK_TRS, L10U_SMD, LTV217, N2N7002, PICO_W_ANTENNA_KEEPOUT_MM, R_220, R_470, SMAJ24A, SRD05, SS56P, TB508_DNP,
} from "./lib/parts.ts";

const W = 134, H = 80;
const b = board("proptroller", { w: W, h: H, layers: 2 });   // MECH-1: 134 x 80 (owner 2026-10-14: loads on the front edge, power + daisy chain + SPK on the back, corner holes clear of both rows), 2 layers 1 oz (MFG-1)
b.stackup("2L");
// F GND pour everywhere except the socket nest: there it only formed floating islands between the pin rows (the B plane carries GND under it).
// Done by reshaping the pour (a keepout would also block tracks: tried 2026-10-14, it walled off the MCU).
{ const i = b.zones.findIndex(z => z.net === "GND" && z.layers.length === 1 && z.layers[0] === "F" && !z.poly); if (i >= 0) b.zones.splice(i, 1); }
b.pour("GND", ["F"], { poly: [[0, 0], [134, 0], [134, 80], [0, 80], [0, 50], [56, 50], [56, 20], [0, 20]] });
b.usage("enclosure", { bottomBlocked: true, req: "USE-1", note: "IP65 box, board on standoffs, lid open for service: field terminals, the MCU board (pull/replace, its USB and buttons), USER1-3, the DFPlayer card" });
// MECH-1: M3 in all four corners (owner); the mid-board hole was dropped in the routing pass (no clear spot after the front-edge reorder).
// SAF-2 (owner 2026-10-21, after the independent reviews): two earth bonding points, both plated MountingHole_3.2mm_M3_Pad (6.4 mm pad, both faces)
// on GND and bonded solidly into both GND pours; the AC case's earth wire lands on both with ring terminals under the screws.
//  - H3 (4, 76), front-west: the bond for the relay side (GND next to the relay contact islands, where an on-board insulation fault enters GND).
//  - H2 (130, 4), back-east: the bond for the main GND body (power entry, OUT returns, DFPlayer). The main body reaches H3 through no more than
//    about 3.1 mm of copper (the relay row and the MCU nest fence it off on both layers), so it gets its own hole (owner 2026-10-21).
// No jumper: the rev B SJ_E bond (0.6 mm bridge, 1.0 / 0.8 mm tracks, one 0.3 mm via) melted at about 28-127 A^2 s, near a 5 A fuse's I^2 t.
// H1 and H4 stay unplated.
const EARTH_HOLES = [[4, H - 4], [W - 4, 4]] as const;
const isEarth = (x: number, y: number) => EARTH_HOLES.some(([ex, ey]) => ex === x && ey === y);
for (const [x, y] of [[4, 4], [W - 4, 4], [4, H - 4], [W - 4, H - 4]] as const) b.hole(x, y, 3.2, isEarth(x, y) ? { plated: true, net: "GND" } : {});
// b.hole() keeps tracks, vias and zones out of the screw-head box (+/-2.88 mm). On H2 / H3 that box lies wholly inside the 6.4 mm GND pad, and a
// zone keepout there would only narrow the pour's join to the pad, so those entries are removed through the board's public keepout list (aieda's
// hole() has no per-hole option): every other net's copper is kept off the pad, and so out of the screw-head area, by its own clearance. The
// footprint's pad is zone_connect 2 (solid), so both GND pours join the whole pad edge, not through thermal spokes.
for (const h of ["H2 head", "H3 head"]) { const i = b.keepouts.findIndex(q => q.name === h); if (i < 0) throw new Error(`${h} keepout not found`); b.keepouts.splice(i, 1); }
// Stitching rings (SAF-2): five 0.4 mm GND vias on a 4.0 mm radius round each earth hole, on the sides away from the board edges, joining F and B
// besides the hole's own 3.2 mm plated barrel.
for (const [dx, dy] of [[4, 0], [2.83, -2.83], [0, -4], [-2.83, -2.83], [2.83, 2.83]] as const) b.via("GND", [4 + dx, H - 4 + dy], { d: 0.9, drill: 0.4 });
for (const [dx, dy] of [[-4, 0], [-2.83, 2.83], [0, 4], [2.83, 2.83], [-2.83, -2.83]] as const) b.via("GND", [W - 4 + dx, 4 + dy], { d: 0.9, drill: 0.4 });
// "EARTH" labels (owner 2026-10-20 / 2026-10-21), top and bottom, 1.0 mm, marking the earth screw points: H3's reads up the west edge north of it
// (x 1.0-2.0, clear of the pad at y 72.8, the stitching vias and the T1_RING via); H2's sits south of its stitching ring
b.text("EARTH", 1.5, 69.6, { size: 1.0, rot: 90 }).text("EARTH", 1.5, 69.6, { l: "B", size: 1.0, rot: 90 });
b.text("EARTH", W - 4, 10.0, { size: 1.0 }).text("EARTH", W - 4, 10.0, { l: "B", size: 1.0 });

// edge(..., { at }) places pin 1; these centre an n-pin header of pitch p on c. S edge runs +x, N edge -x, W edge +y, E edge -y.
const fwd = (c: number, n: number, p: number) => c - (n - 1) * p / 2, back = (c: number, n: number, p: number) => c + (n - 1) * p / 2;
// Channel copies (owner 2026-10-14): repeated circuits (OUT1-4, RLY1-2, CONTACT1-2, TRIG2/TRIG1) are carbon copies of each other. Channel 1 of each group
// is placed by the constraints below; its solved layout (origin offset from the channel connector's origin + rotation) is recorded in CHAN and every
// channel, channel 1 included, is then locked at that offset from its own connector (TRIG1 = TRIG2's layout turned 180 deg). aieda has no replicate()
// (toolchain limitation), so the copies are xy() locks generated from a measured build; regenerate CHAN after changing a channel's parts or constraints.
// +VIN power path (routing pass 2026-10-14, 20 C basis): a 5.9 mm F trunk from J_IN pin 1 down x = 97.5 to a 5.9 mm bus along y = 49.5 feeding the
// OUT1-4 FET sources (2.3 mm stubs). B under it stays free for signal crossings. Parts keep out of this corridor (apart below).
const TRUNK: [number, number, number, number] = [94.1, 7.5, 100.9, 49.5], BUS: [number, number, number, number] = [73.2, 46.1, 116, 52.6];
type Off = readonly [number, number, number];   // dx, dy, rot
const CHAN: Record<string, Record<string, Off>> = {   // measured from the seed-9 build, channel 1 / TRIG2, 2026-10-14
  out: { Q: [-0.50, -19.21, 0], RGS: [-4.9, -18.805, 270], RGD: [-2.20, -15.36, 0], QD: [-1.35, -12.41, 90], RPD: [-1.55, -9.51, 0], DF: [5.60, -14.76, 90], RL: [3.9, -9.30, 90], DL: [5.85, -9.30, 90] },   // from J_O1; hand-edited 2026-10-14 routing pass: R_GS into the gap west of the FET (pad 1 on the source stub), RL/DL east of the OUT track
  rly: { K: [5.08, -23.605, 270] },   // K fixed by hand (rot 270: NO/NC face the terminal, COM straight above pin 2); drivers re-solved from the constraints below
  con: { U: [-1.30, 20.97, 0], RA: [-4.50, 26.12, 90], RPD: [-5.40, 31.07, 180] },   // from J_C1
  trig: { RPU: [0.25, 23.07, 90], RS: [-1.15, 20.22, 0], DCL: [-2.75, 23.72, 90], CT: [-5.85, 21.87, 270], RR: [3.30, 21.12, 90] },   // from J_T2
};
const cp = (grp: string, role: string, ax: number, ay: number, turn: boolean, ...free: Con[]): Con[] => {
  const o = CHAN[grp]?.[role]; if (!o) return free;
  const s = turn ? -1 : 1; return [xy(ax + s * o[0], ay + s * o[1], (o[2] + (turn ? 180 : 0)) % 360)];
};
const YS_T = H - 5.895, YN_J = 8.775, YS_J = H - 8.775;   // origin y of a front-edge terminal, a back-edge jack, a front-edge jack

// ================= MCU nest (IF-11): DevKit rows 25.4 mm apart, Pico rows 17.78 mm apart between them, antenna ends at x = XA =================
// DevKit (DOIT 30-pin ESP32 DevKit V1): north row = pads 16..30 (GPIO23 at the antenna end ... 3V3 at the USB end),
// south row = pads 15..1 (EN at the antenna end ... VIN at the USB end). Pico rotated USB-east: north row = pins 20..1, south row = pins 21..40.
// The Pico rows are 12.7 mm longer, toward the USB end; both antenna ends sit at the west edge (DevKit antenna overhangs it, Pico W antenna keep-out on board).
// Owner 2026-10-15: the nest moved 2.5 mm north (NEST_DY) into the space of the removed PIXEL bulk cap, so the DevKit board (south edge ~1.46 mm
// past its south row) clears relay K1 by ~2.8 mm instead of ~0.3 mm. Parts inside the nest (TRIG2, CONTACT, MCU diode) moved with it.
const NEST_DY = 2.5;
const XA = 3.5, YN = 22 - NEST_DY, YS = YN + 25.4, PN = YN + 3.81, PS = YS - 3.81;
const DK_N = ["23", "22", "TX0", "RX0", "21", "19", "18", "5", "17", "16", "4", "2", "15", "GND", "3V3"];
const DK_S = ["EN", "36", "39", "34", "35", "32", "33", "25", "26", "27", "14", "12", "13", "GND", "VIN"];
const PI_N = ["GP15", "GP14", "GND", "GP13", "GP12", "GP11", "GP10", "GND", "GP9", "GP8", "GP7", "GP6", "GND", "GP5", "GP4", "GP3", "GP2", "GND", "GP1", "GP0"];
const PI_S = ["GP16", "GP17", "GND", "GP18", "GP19", "GP20", "GP21", "GND", "GP22", "RUN", "GP26", "GP27", "GND", "GP28", "ADC_VREF", "3V3", "3V3_EN", "GND", "VSYS", "VBUS"];
// function net -> [DevKit GPIO, Pico GPIO]
const FN: Record<string, [string, string]> = {
  PWM1: ["23", "GP15"], PWM2: ["22", "GP14"], PWM3: ["21", "GP12"], PWM4: ["19", "GP11"], TRIG2: ["18", "GP10"], USER3: ["5", "GP9"], DF_TX: ["17", "GP8"],
  SDA: ["4", "GP4"], SCL: ["16", "GP5"], EXP15: ["15", "GP3"], USER1: ["13", "GP28"], USER2: ["14", "GP27"], CON1: ["27", "GP22"], RLY2: ["26", "GP21"],
  RLY1: ["25", "GP20"], PIXEL: ["33", "GP19"], CON2: ["32", "GP18"], EXP35: ["35", "GP17"], TRIG1: ["36", "GP16"], AN_IN: ["39", "GP26"], EXP34: ["34", "GP7"],
};
const PICO_ONLY = ["GP0", "GP1", "GP2", "GP6", "GP13"];
const netOf = (pin: string, side: "dk" | "pi"): string | null => {
  if (pin === "GND") return "GND";
  if (pin === "3V3") return "+3V3";
  if (pin === "VIN" || pin === "VSYS") return "+5V_MCU";
  const f = Object.entries(FN).find(([, v]) => v[side === "dk" ? 0 : 1] === pin);
  if (f) return f[0];
  if (side === "pi" && PICO_ONLY.includes(pin)) return `EXP_${pin}`;
  return null;   // EN, TX0/RX0, GPIO2 (DevKit LED), 12 (strap), RUN, VBUS, 3V3_EN, ADC_VREF: not used
};
const sock = (ref: string, d: PartDef<string>, x: number, y: number, names: string[], side: "dk" | "pi") => {
  const s = b.add(ref, d).at(req(xy(x, y, 90), "IF-11"));   // pin 1 at the antenna end, pins run east (checked with q part)
  names.forEach((nm, i) => { const n = netOf(nm, side); if (n) b.net(n, s.pin(String(i + 1))); else b.nc(s.pin(String(i + 1))); });
  return s;
};
const J_DKN = sock("J_DKN", HDR_1x15_F_DNP, XA, YN, DK_N, "dk"), J_DKS = sock("J_DKS", HDR_1x15_F_DNP, XA, YS, DK_S, "dk");
const J_PIN = sock("J_PIN", HDR_1x20_F_DNP, XA, PN, PI_N, "pi"), J_PIS = sock("J_PIS", HDR_1x20_F_DNP, XA, PS, PI_S, "pi");
// Pico W / 2 W antenna: no copper on either layer under it (datasheet keep-out), at the west end between the Pico rows
const AK = PICO_W_ANTENNA_KEEPOUT_MM, AY = (PN + PS) / 2;
const AX = XA - PICO_PIN_TO_END_MM + AK.l;   // module end (1.37 mm past the end pins) + 9 mm antenna region
b.keepout("Pico W antenna", [0, AY - AK.w / 2, AX, AY + AK.w / 2], ["F", "B"], ["track", "via", "zone"]);
// MCU 5 V: one B5819W from +5V into DevKit VIN / Pico VSYS, so the MCU's own USB never back-feeds +5V (A-10)
const D_MCU = b.add("D_MCU", B5819W).at(region([8, 36 - NEST_DY, 22, 42 - NEST_DY]));
const C_MCU = b.add("C_MCU", C0805P("100n")).at(near(D_MCU, 4));
b.net("+5V", D_MCU.A).net("+5V_MCU", D_MCU.K, C_MCU[1]).net("GND", C_MCU[2]);

// ================= power entry (IF-1): DC-005 barrel <= 5 A, parallel 5.08 terminal (DNP) <= 10 A; no fuses, no RP FETs =================
const J_PWR = b.add("J_PWR", DC005).at(req(edge("N", { at: 107.6 }), "IF-1"));
const J_IN = b.add("J_IN", TB508_DNP(2)).at(req(edge("N", { at: 97.5 }), "IF-1"));
b.nc(J_PWR.SW);
const pe = region([82, 16, 88.5, 36]);   // behind the input terminal, west of the +VIN trunk (centres; the 8 x 10 caps reach 5.3 mm further)
const D_TVS = b.add("D_TVS", SMAJ24A).at(pe, near(J_PWR, 8), apart(TRUNK, 0.3));
const C_BK1 = b.add("C_BK1", CP100U50_SMD).rot(180).at(pe, near(D_TVS, 6), apart(TRUNK, 0.3)), C_BK2 = b.add("C_BK2", CP100U50_SMD).at(pe, near(C_BK1, 4), apart(TRUNK, 0.3));
b.net("+VIN", J_PWR.TIP, J_IN.pin("1"), D_TVS.K, C_BK1.P, C_BK2.P).net("GND", J_PWR.SLEEVE, J_IN.pin("2"), D_TVS.A, C_BK1.N, C_BK2.N);
b.rail("+VIN", 24, { req: "PWR-1" });

// ================= +5V buck (PWR-2): TPS54360B, 65 V abs max, 3.5 A, PowerPAD, 644 kHz; TI design adapted in parts-prop.ts (RT 150k, FB 43k/8.2k, COMP 13k + 6.8n // 47p) =================
const bz = region([20, 50, 39, 78]);   // (2026-10-21: to y 78, for the output caps west of L_BK pad 2) front-west under nothing tall (the DevKit ends at y 46.4): about 75 mm from the DFPlayer, about 15 mm from the antenna end
const U_BK = b.add("U_BK", TPS54360B).at(bz).req("PWR-2");
const C_BI1 = b.add("C_BI1", C1206_10U50), C_BI2 = b.add("C_BI2", C1206_10U50), C_BST = b.add("C_BST", C0805P("100n")).at(bz, near(U_BK.SW, 3));   // BOOT-SW cap bridges both pins
const D_CT1 = b.add("D_CT1", SS56P).at(bz, near(U_BK.SW, 2.5)), D_CT2 = b.add("D_CT2", SS56P).at(bz, near(D_CT1, 1), near(U_BK.SW, 4));   // hot loop: catch diodes at the SW pin   // two in parallel on one copper patch: about 1.1 W at 24 V in
const L_BK = b.add("L_BK", L10U_SMD).at(bz, near(U_BK.SW, 4));
const C_BO1 = b.add("C_BO1", C1206_47U10).at(bz, near(L_BK, 4)), C_BO2 = b.add("C_BO2", C1206_47U10).at(bz, near(C_BO1, 2));
const R_RT = b.add("R_RT", R_150K()).at(bz, near(U_BK.RT, 3));
const R_FB1 = b.add("R_FB1", R0805("43k")).at(bz, near(U_BK.FB, 3)), R_FB2 = b.add("R_FB2", R_8K2()).at(bz, near(U_BK.FB, 3));
const R_CP = b.add("R_CP", R0805("13k")).at(bz, near(U_BK.COMP, 4)), C_CP = b.add("C_CP", C0805_6N8).at(bz, near(R_CP, 2)), C_CF = b.add("C_CF", C0805_47P).at(bz, near(U_BK.COMP, 3));
// buck input through its own bead (same MPZ2012S221A as FB_DF, no new part type): the long run to the buck is a 1.2 mm +VIN_BK track, and the
// bead keeps 644 kHz switching current off the +VIN trunk. Placed next to the trunk.
const FB_BK = b.add("FB_BK", FB_DF).rot(180).at(region([91.6, 40, 92, 42]), apart(TRUNK, 0.3), apart(BUS, 0.3));   // pad 1 faces the trunk
b.track("+VIN", "F", [FB_BK.pad("1"), [96, 41]], 1.2);
b.net("+VIN", FB_BK[1]).net("+VIN_BK", FB_BK[2], U_BK.VIN, C_BI1[1], C_BI2[1]).net("GND", U_BK.GND, C_BI1[2], C_BI2[2], D_CT1.A, D_CT2.A, C_BO1[2], C_BO2[2], R_RT[2], R_FB2[2], C_CP[2], C_CF[2]);
b.net("BK_SW", U_BK.SW, D_CT1.K, D_CT2.K, L_BK[1], C_BST[2]).net("BK_BOOT", U_BK.BOOT, C_BST[1]);
b.net("+5V", L_BK[2], C_BO1[1], C_BO2[1], R_FB1[1]).net("BK_FB", U_BK.FB, R_FB1[2], R_FB2[1]).net("BK_RT", U_BK.RT, R_RT[1]);
b.net("BK_COMP", U_BK.COMP, R_CP[1], C_CF[1]).net("BK_CZ", R_CP[2], C_CP[1]);
b.nc(U_BK.EN);   // EN floats = enabled (abs max 8.4 V: never tie it to VIN)
// buck pin field (seed 19 placement): COMP threads between the FB pin and R_FB1 to C_CF / R_CP; +VIN_BK leaves the VIN pin away from EN.
// Freerouting left clearance-rounding errors here; these few locks are measured from this placement (re-derive if the buck moves).
b.track("BK_COMP", "F", [U_BK.pad("6"), [25.4, 57.87], [25.4, 55.9], [27.74, 55.9], C_CF.pad("1"), R_CP.pad("1")], 0.25).track("+VIN_BK", "F", [U_BK.pad("2"), [33.3, 59.14], [34.3, 58.14]], 0.8).track("+VIN_BK", "F", [C_BI1.pad("1"), C_BI2.pad("1")], 0.8);   // 2026-10-21: input caps upright beside pin 2, VIN pads in a row (see the buck ground note)
b.decap(U_BK.VIN, 2.5, C_BI1).decap(U_BK.VIN, 4, C_BI2).decap(U_BK.BOOT, 3, C_BST);
b.rail("+5V", 5, { req: "PWR-2" });

// ================= OUT1-4 (F-2): high-side AO4485, 2N7002 driver, 4.7k G-S / 4.7k, 4.7k pull-down (E9), SS56 freewheel, LED =================
// G-S / G-D both 4.7k (review 2026-10-15; was 2.2k): VGS = -VIN/2, so -19.5 V at the SMAJ24A 38.9 V clamp (AO4485 +/-20 V), -12 V at 24 V,
// -4.5 V at 9 V (RDS(on) is specified at -4.5 V).
// front edge, west to east: TRIG1, RLY1, RLY2, OUT1-4 (owner 2026-10-14 routing pass: OUT1-4 next to the power input keeps the 10 A +VIN trunk short).
// The relays (15.8 mm tall) sit east of the DevKit board (which overhangs the strip south of its socket row at ~10 mm); OUT pitch 12.35, corner holes clear
const T1X = 12.55, RP = [43.02, 61.12], OP = [79.9, 92.25, 104.6, 116.95];   // pin 1 x of each front connector; relays 18.1 mm apart (SAF-1 rev B: was 17.0 at 43.92 / 60.92; K1 NC to K2 NO pads 1.95 mm -> 3.05 mm), drivers in a strip north of them
OP.forEach((x, k) => {
  const n = k + 1, c = x + 2.54, col = region([c - 6, 51, c + 6, 67]);   // the driver parts behind each terminal
  const J = b.add(`J_O${n}`, TB508_DNP(2)).at(req(edge("S", { at: x }), "IF-5"));
  const o = (r: string, ...f: Con[]) => cp("out", r, x, YS_T, false, ...f);
  const Q = b.add(`Q_O${n}`, AO4485).at(...o("Q", col, near(J, 12)));
  const RGS = b.add(`R_GS${n}`, R0805("4.7k")).at(...o("RGS", col, near(Q.G, 7))), RGD = b.add(`R_GD${n}`, R0805("4.7k")).at(...o("RGD", col, near(Q.G, 8)));
  const QD = b.add(`Q_D${n}`, N2N7002).at(...o("QD", col, near(RGD, 5))), RPD = b.add(`R_PD${n}`, R0805("4.7k")).at(...o("RPD", col, near(QD.G, 5)));
  const DF = b.add(`D_F${n}`, SS56P).at(...o("DF", col, near(J, 8)));
  const RL = b.add(`R_LO${n}`, R0805("10k")).at(...o("RL", col, near(J, 12))), DL = b.add(`D_LO${n}`, LED0805G).at(...o("DL", col, near(RL, 3)));
  b.net("+VIN", Q.S, RGS[1]).net(`G${n}`, Q.G, RGS[2], RGD[1]).net(`GD${n}`, RGD[2], QD.D);
  b.net(`PWM${n}`, QD.G, RPD[1]).net("GND", QD.S, RPD[2]);
  b.track(`PWM${n}`, "F", [QD.pad("1"), RPD.pad("1")], 0.25);   // gate to its pull-down: the router left this 2 mm hop open (2026-10-14); same in every channel
  { const v: [number, number] = [x - 1.55 - 0.91, YS_T - 9.51 + 1.9]; b.track(`PWM${n}`, "F", [RPD.pad("1"), v], 0.25).via(`PWM${n}`, v).track(`PWM${n}`, "B", [v, [x - 2.3, YS_T - 11.475]], 0.25); }   // + B stub where the PWM line arrives (PWM1/PWM2 end on it; on 3-4 a 2 mm open stub, harmless at 1 kHz: removing it left PWM4 open, route 29)   // via just south of R_PD pad 1: the PWM line arrives on B
  b.net(`OUT${n}`, Q.D, J.pin("1"), DF.K, RL[1]).net(`LO${n}`, RL[2], DL.A).net("GND", J.pin("2"), DF.A, DL.K);
  // locked channel copper (same in every channel): 5 A OUT path drain column -> terminal pin 1, freewheel diode tap, gate pull-up links
  b.track(`OUT${n}`, "F", [Q.pad("8"), Q.pad("5")], 0.6).track(`OUT${n}`, "F", [[x + 1.5, YS_T - 19.7], [x + 1.5, 68.5], [x, 70], J.pad("1")], 2.3).track(`OUT${n}`, "F", [DF.pad("1"), [x + 1.5, YS_T - 12.755]], 2.3);
  if (n > 1) b.waive("courtyards_overlap", [[`D_F${n - 1}`, `R_GS${n}`]], "freewheel diode of the previous channel and this R_GS: courtyards touch, pads 0.25 mm apart");
  b.track(`OUT${n}`, "F", [RL.pad("1"), [x + 1.5, YS_T - 8.385]], 0.4);   // LED resistor onto the OUT track
  b.waive("courtyards_overlap", [[`Q_O${n}`, `R_GS${n}`]], "R_GS sits in the gap west of the FET: courtyards touch, pads are 0.3 mm or more apart (checked by DRC clearance)");
  b.track("+VIN", "F", [RGS.pad("1"), [x - 2.97, YS_T - 19.715]], 0.4).track(`G${n}`, "F", [RGS.pad("2"), Q.pad("4")], 0.3);
});

// ================= RLY1-2 (F-3): SRD-05VDC on +5V, JLC through-hole, dry COM/NO/NC, no flyback or snubber footprint (fit them at the load) =================
let K1: Parameters<typeof align>[0] | undefined;
RP.forEach((x, k) => {
  const n = k + 1, c = x + 5.08, rz = region([x + 9, 40.5, x + 26, 45]), kz = [apart(TRUNK, 0.3), apart(BUS, 0.3)];   // this relay's drivers, north-east of it (the DevKit and Pico sockets are north-west)
  const o = (r: string, ...f: Con[]) => cp("rly", r, x, YS_T, false, ...f);
  const J = b.add(`J_R${n}`, TB508_DNP(3)).at(req(edge("S", { at: x }), "IF-6"));
  // rot 270 (2026-10-14 routing pass): COM pad at the north end straight above terminal pin 2, NO / NC pads at the south end over pins 1 / 3, so every
  // contact path runs straight down without crossing; body 4 mm behind the terminal; both relays on one row (owner)
  const K = b.add(`K${n}`, SRD05).rot(270).at(...o("K", region([c - 1, 50, c + 1, 51]), ...(K1 ? [align(K1, "y")] : []))).req("F-3");
  if (!K1) K1 = K;
  b.waive("courtyards_overlap", [[`K${n}`, `J_R${n}`]], "relay body ends at y 68.6 (SRD-05 19.2 mm long from the COM pad), terminal housing back face at y 69.2: 0.6 mm clear; only the 0.5 mm courtyard margins overlap");
  if (n === 2) b.waive("courtyards_overlap", [["K2", "R_GS1"]], "rev B (SAF-1): K2 moved 0.2 mm east for the 3.0 mm island gap to K1; the courtyards overlap 0.19 mm, the relay body (15.5 mm wide) ends 0.43 mm short of R_GS1's body");
  // D_C1 (rev B, SAF-1): off the K1 COM pad (was 1.35 mm from it), between the relays north of their courtyards, anode toward K1 COIL_B
  const DC = b.add(`D_C${n}`, B5819W); if (n === 1) DC.rot(180);
  DC.at(...o("DC", n === 1 ? region([57.3, 47.2, 57.5, 47.4]) : rz, near(K.COIL_A, 18), ...kz)); const Q = b.add(`Q_R${n}`, MMBT3904).at(...o("Q", rz, near(K.COIL_B, 18), ...kz));
  const RB = b.add(`R_RB${n}`, R0805("1k")).at(...o("RB", rz, near(Q.B, 5), ...kz)), RPD = b.add(`R_RP${n}`, R0805("4.7k")).at(...o("RPD", rz, near(Q.B, 5), ...kz));
  const RL = b.add(`R_RL${n}`, R0805("1k")).at(...o("RL", rz, near(Q, 8), ...kz)), DL = b.add(`D_RL${n}`, LED0805G).at(...o("DL", rz, near(RL, 3), ...kz));
  // Rev B: no protection parts on the contacts. The rev A NO -> GND flyback footprint (D_FB) is gone: its GND pad sat inside the island and contradicted
  // "dry contact". The DNP RC snubber footprint (R_SN / C_SN, COM to NO on B) is gone too (owner 2026-10-20, independent review): the COM B track ran
  // 0.625 mm from its bare C_SN pads, under IPC-2221 B2 for 120 V AC, and a snubber fitted on an AC load is a hazard. F-3: flyback diode or snubber
  // at the load, DC loads only.
  b.net("+5V", K.COIL_A, DC.K, RL[1]).net(`COIL${n}`, K.COIL_B, DC.A, Q.C, DL.K).net(`RLED${n}`, RL[2], DL.A);
  b.net(`RLY${n}`, RB[1]).net(`RB${n}`, RB[2], Q.B, RPD[1]).net("GND", Q.E, RPD[2]);
  b.net(`R${n}_NO`, K.NO, J.pin("1")).net(`R${n}_COM`, K.COM, J.pin("2")).net(`R${n}_NC`, K.NC, J.pin("3"));   // terminal order NO COM NC (relay-module convention)
  // SAF-1 (rev B) keepouts. Zones: aieda fills the pours before it writes the custom rules, so the fill does not see the 3.0 mm island rule; these
  // zone-only rule areas pull both GND pours back from the island instead (zone-only, so the island's own copper may sit inside them):
  // terminal row, NO / NC pads and their tracks + 3.0 mm; the COM column (pad and track) + 3.0 mm (the B box is wider on the west: it kept the removed
  // snubber pads' margin, left as it was so the routed copper does not change).
  // (each box is island extent + 3.1 mm, 0.1 mm over the rule)
  b.keepout(`SAF-1 RLY${n} pour pullback`, [x - 5.57, 60.05, x + 15.68, 78.41], ["F", "B"], ["zone"]);
  b.keepout(`SAF-1 RLY${n} COM pour pullback F`, [c - 4.6, 45.9, c + 4.6, 60.15], ["F"], ["zone"]);
  b.keepout(`SAF-1 RLY${n} COM pour pullback B`, [c - 6.4, 45.9, c + 4.6, 60.15], ["B"], ["zone"]);
  // Routing guidance (Freerouting does not see the custom rule): no tracks or vias in the 3 mm moat north of the COM pad and between the COM pad and
  // each coil pad (these boxes keep >= 0.05 mm off the COM pad and the coil pads)
  b.keepout(`SAF-1 RLY${n} COM moat N`, [c - 4.5, 45.95, c + 4.5, 48.95], ["F", "B"], ["track", "via", "zone"]);
  b.keepout(`SAF-1 RLY${n} COM moat W`, [c - 4.5, 48.95, c - 1.55, 55.8], ["F", "B"], ["track", "via", "zone"]);
  b.keepout(`SAF-1 RLY${n} COM moat E`, [c + 1.55, 48.95, c + 4.5, 55.8], ["F", "B"], ["track", "via", "zone"]);
  // and inside the relay outline, between the coil pads and the NO / NC pads, either side of the COM track (both layers: the west box was F only while
  // the removed snubber sat on B there)
  b.keepout(`SAF-1 RLY${n} inside W`, [c - 5.5, 54.0, c - 1.25, 62.9], ["F", "B"], ["track", "via"]);
  b.keepout(`SAF-1 RLY${n} inside E`, [c + 1.25, 54.0, c + 7.2, 63.1], ["F", "B"], ["track", "via"]);
  // Air-gap slots (SAF-1): a non-plated routed slot between COM and each coil pad, centred in the 3.3 / 3.2 mm gap and running 1 mm past both
  // pads (y 48.0-54.7, the band rev B reserved for it). 1.2 mm wide: JLC mills slots to +/-0.2 mm, so the finished slot never drops under its
  // 1.0 mm minimum; that leaves about 1.0 mm of FR-4 to each pad. The COM moats above keep tracks and vias off it; aieda carries it to
  // Freerouting as a keepout grown by the edge clearance, so the reserved-band keepouts of the first rev B pass are gone.
  b.slot(`SAF-1 RLY${n} slot W`, between(K.pad("1"), K.pad("2")), 1.2, { req: "SAF-1" });   // COM to COIL_A
  b.slot(`SAF-1 RLY${n} slot E`, between(K.pad("1"), K.pad("5")), 1.2, { req: "SAF-1" });   // COM to COIL_B
  b.waive("silk_edge_clearance", [`K${n}`], "the relay's own silk (body outline and contact symbol) crosses its two air-gap slots under the relay body; the fab clips silk at a cutout, nothing else is cut");
  // 10 A contact paths, locked: 2.3 mm on F and again on B (5 A per layer at the 20 C basis), straight down to the terminal (no crossings)
  for (const L of ["F", "B"] as const) {
    b.track(`R${n}_NO`, L, [K.pad("3"), [c - 6.05, 68], [c - 5.08, 68.97], J.pad("1")], 2.3);
    b.track(`R${n}_COM`, L, [K.pad("1"), J.pad("2")], 2.3);
    b.track(`R${n}_NC`, L, [K.pad("4"), [c + 6, 68], [c + 5.08, 68.92], J.pad("3")], 2.3);
  }
});

// SAF-1: K1's west moat (NO pad / track / terminal pin + 3 mm) next to the buck output caps and TP5: no tracks or vias, so their GND fanout stays out
b.keepout("SAF-1 RLY1 moat W", [37.55, 61.5, 40.45, 78.2], ["F", "B"], ["track", "via"]);
// ... between the two islands (K1 NC / J_R1 pin 3 to K2 NO / J_R2 pin 1), east of K2's NC pad (above the PWM3 / USER2 rise) and south of the terminal row
b.keepout("SAF-1 RLY1-RLY2 moat", [55.65, 54.0, 58.6, 78.2], ["F", "B"], ["track", "via"]);
b.keepout("SAF-1 RLY2 moat E", [73.75, 61.5, 76.7, 75.0], ["F", "B"], ["track", "via"]);
b.keepout("SAF-1 relay terminals moat S", [40.45, 75.35, 73.75, 78.2], ["F", "B"], ["track", "via"]);

// ================= PWM1 / PWM2 escape (locked, routing pass 2026-10-14) =================
// Freerouting left PWM1/PWM2 open in two runs: they start at the far-west corner of the socket nest. They leave along the 2.1 mm pad-free channel
// between the DevKit and Pico north rows (y 22.85-24.96) on B, to the east end of the nest; the router finishes from there. Pad map: J_DKN/J_PIN
// pin 1 = PWM1, pin 2 = PWM2 (FN table); re-derive if the nest moves.
b.track("PWM1", "F", [J_DKN.pad("1"), J_PIN.pad("1")], 0.25).track("PWM1", "B", [J_DKN.pad("1"), [XA + 1.3, YN + 1.3], [55, YN + 1.3]], 0.25);
b.track("PWM2", "F", [J_DKN.pad("2"), J_PIN.pad("2")], 0.25).track("PWM2", "B", [J_PIN.pad("2"), [XA + 2.54 + 1.26, PN - 1.26], [55, PN - 1.26]], 0.25);
// Continuations to the OUT1/OUT2 gate stubs (seed 19, route 19): PWM1 leaves the nest north of PWM2 but its gate is west of PWM2's, so the two
// must cross. PWM2 takes the 45-degree B diagonal Freerouting found; PWM1 hops under it on F (x 56.5) and runs a parallel B diagonal 2.3 mm
// south-west of it. The gate stub ends are (OP[k] - 2.3, YS_T - 11.475) on B.
{ const g1: [number, number] = [OP[0] - 2.3, 62.63], g2: [number, number] = [OP[1] - 2.3, 62.63];
  b.track("PWM2", "B", [[55, PN - 1.26], [58, PN - 1.26], [g2[0], PN - 1.26 + g2[0] - 58], g2], 0.25);
  // hop at x 57.5 (2026-10-15: U_C2 moved north with the nest onto x 56.5); the diagonal keeps its seed-19 line, parallel to PWM4's
  b.track("PWM1", "B", [[55, YN + 1.3], [57.5, YN + 1.3]], 0.25).via("PWM1", [57.5, YN + 1.3]).track("PWM1", "F", [[57.5, YN + 1.3], [57.5, 27.3]], 0.25)
   .via("PWM1", [57.5, 27.3]).track("PWM1", "B", [[57.5, 27.3], [g1[0], 27.3 + g1[0] - 57.5], g1], 0.25); }
// GND stitching vias and the route-24 RLY2 / EXP35 locks were measured on the seed-19 placement; removed when the nest moved (2026-10-15),
// re-derived below from the new routed build (tools/gnd_islands.py, tools/lock_route.py) where still needed.

// PWM3 (2026-10-15, nest moved): Freerouting left it open after the move. Locked along the path it found on the seed-19 placement: out of the
// nest on B between the socket pins (pin 5 column), round the west end, a B diagonal to the south edge, then F along the front edge to OUT3's gate stub.
b.track("PWM3", "B", [J_PIN.pad("5"), [XA + 4 * 2.54, PS - 3.93], [10.89, PS - 1.16], [2.51, PS - 1.16], [2.32, PS - 0.97], [2.32, 48.36], [28.89, 74.93], [31.7, 77.74]], 0.25)
 .via("PWM3", [31.7, 77.74])   // 2026-10-21: the via moved from (33.29, 74.93) to south of L_BK pad 2, out of the buck output's path
 // rev B (SAF-1): the F run went 0.2 mm under the relay terminal pins; it now follows the front edge (y 78.5, 3.07 mm from the pins) and rises at 45
 // degrees east of J_R2 (>= 3.2 mm from its pin 3) back onto its rev A line at y 75.63
 .track("PWM3", "F", [[31.7, 77.74], [32.46, 78.5], [74.0, 78.5], [76.87, 75.63], [93.41, 75.63], [OP[2] - 2.46, 66.9], [OP[2] - 2.46, YS_T - 9.51 + 1.9]], 0.25);
// PWM4 and USER2 (2026-10-15): routed in one post-move run and left open in the next, so locked along their seed-19 paths; the parts inside the
// nest moved north with it (NEST_DY), with a 45-degree join back onto the unchanged copper. USER2's last few mm to the moved button stay with the router.
b.track("PWM4", "B", [J_PIN.pad("6"), [25.81, 35.42 - NEST_DY], [51.0, 35.42 - NEST_DY]], 0.25).via("PWM4", [51.0, 35.42 - NEST_DY])
 .track("PWM4", "F", [[51.0, 35.42 - NEST_DY], [51.0 + NEST_DY, 35.42], [66.13, 35.42], [67.57, 36.86], [68.93, 36.86], [69.16, 36.63]], 0.25).via("PWM4", [69.16, 36.63])
 .track("PWM4", "B", [[69.16, 36.63], [88.35, 55.82], [88.35, 65.97], [89.54, 67.16], [113.82, 67.16], [OP[3] - 2.46, YS_T - 9.51 + 1.9]], 0.25);
// rev B (SAF-1): USER2's run under the relay terminals moved to B along the front edge (y 78.9), back onto F east of J_R2 (via >= 3.3 mm from its pin 3)
b.track("USER2", "B", [J_PIS.pad("12"), [XA + 11 * 2.54, YS - 2.54], J_DKS.pad("11")], 0.25)
 .track("USER2", "F", [J_DKS.pad("11"), [34.84, YS + 5.94], [34.84, 55.62], [36.45, 55.62]], 0.25).via("USER2", [36.45, 55.62])
 // 2026-10-21 (buck ground): down on B at x 36.45, east of the buck output trunk and next to the K1 moat, not through the buck ground at x 34.84
 .track("USER2", "B", [[36.45, 55.62], [36.45, 77.8], [37.55, 78.9], [75.0, 78.9], [77.62, 76.28]], 0.25).via("USER2", [77.62, 76.28])
 .track("USER2", "F", [[77.62, 76.28], [123.28, 76.28]], 0.25);
// GND stitching from the island analysis of the first routed build after the move (tools/gnd_islands.py): clear radius >= 0.75 mm in both fills
b.via("GND", [61.46, 32.84]).via("GND", [16.09, 17.45]).via("GND", [21.37, 51.12]);   // rev B: the fourth (under K2, between its coil and contact pads) is gone: that area is now the SAF-1 moat

// GND pins of the sockets that the B plane leaves isolated between the pin rows (routing pass 2026-10-14, seed 13): a 0.4 mm F stub threads
// between the neighbouring pins to the F pour just outside the nest (YN - 2.8 north, YS + 3.4 south). The modules also tie their GND pins internally.
const PX = (k: number) => XA + (k - 1) * 2.54;   // socket pin k x
for (const k of [3, 8, 13, 18]) b.track("GND", "F", [J_PIN.pad(String(k)), [PX(k) + 1.27, PN - 1.9], [PX(k) + 1.27, YN - 2.8]], 0.4);
b.track("GND", "F", [J_DKN.pad("14"), [PX(14), YN - 2.8]], 0.4).track("GND", "F", [J_DKS.pad("14"), [PX(14), YS + 3.4]], 0.4);
b.track("GND", "F", [J_PIS.pad("13"), [PX(13) + 1.27, PS + 1.9], [PX(13) + 1.27, YS + 3.4]], 0.4);
for (const k of [3, 8]) b.track("GND", "F", [J_PIS.pad(String(k)), [PX(k) + 1.27, PS + 1.9], [PX(k) + 1.27, YS + 3.4]], 0.4).via("GND", [PX(k) + 1.27, YS + 3.4]);   // the Pico GND pins get the same exit (routes 21-23); pin 18 would hit K1 pin 1, it is tied through R_CP2 instead
b.via("GND", [PX(14), YS + 3.4]).via("GND", [PX(13) + 1.27, YS + 3.4]);   // the south stubs end in a thin pour strip: stitch them to the B plane
b.track("PWM3", "F", [J_DKN.pad("5"), J_PIN.pad("5")], 0.25);

b.track("GND", "F", [C_MCU.pad("2"), [31.2, 38.35 - NEST_DY], [33.98, 41.99 - NEST_DY]], 0.4);   // C_MCU GND onto the R_CP1 -> J_PIS 13 link (seed 19)
// TRIG2 ring feed (T2_RING): jack ring pad -> pad-free column x = 32.71 through both socket rows -> R_TR2 (router left it open, 2026-10-14)
b.track("T2_RING", "F", [[34.25, 8.6], [32.71, 8.6], [32.71, 27.6 - NEST_DY], [34.3, 28.98 - NEST_DY]], 0.25);

// ================= +VIN power path (locked): trunk + bus + FET stubs on F, RAILS branch =================
const VX = 97.5, VY = 49.3;
b.track("+VIN", "F", [J_IN.pad("1"), [VX, VY]], 5.9).track("+VIN", "F", [[VX, VY], [76.5, VY]], 5.9).track("+VIN", "F", [[VX, VY], [114, VY]], 5.9);
OP.forEach((x, k) => b.track("+VIN", "F", [[x - 2.97, VY], [x - 2.97, 54.26]], 2.3));   // FET source pins 1-3 sit 2.97 mm west of the OUT terminal's pin 1 (template)

// ================= TRIG1 (front-west corner, owner) / TRIG2 (F-4) and CONTACT1-2 (F-5, isolated low-voltage contact-closure OUTPUTS) on the back edge; TRIG2/CONTACT parts sit under the MCU =================
// low SMD parts only under the MCU (its board is about 11 mm up); one window per channel behind its jack
([["TRIG1", "S", T1X], ["TRIG2", "N", 31]] as const).forEach(([net, side, x], k) => {
  const n = k + 1, tz = side === "N" ? region([25, 27 - NEST_DY, 37, 40 - NEST_DY]) : region([7, 52, 17, 64]);
  const o = (r: string, ...f: Con[]) => cp("trig", r, side === "N" ? x : x - 1.8, side === "N" ? YN_J - NEST_DY : YS_J + 9, side === "S", ...f);   // TRIG2 is the template, TRIG1 its 180 deg copy, 9 mm nearer its jack and 1.8 mm west (clear of OUT1) (TRIG2's parts clear the MCU socket row)
  const J = b.add(`J_T${n}`, JACK_TRS).at(req(edge(side, { at: x }), "IF-3"));
  const RPU = b.add(`R_TU${n}`, R0805("4.7k")).at(...o("RPU", tz)), RS = b.add(`R_TS${n}`, R0805("4.7k")).at(...o("RS", tz, near(RPU, 4)));   // 4.7k series (RP2350 E9)
  const DCL = b.add(`D_TC${n}`, D1N4148W).at(...o("DCL", tz, near(RS, 4))), CT = b.add(`C_T${n}`, C0805P("100n")).at(...o("CT", tz, near(RS, 4))), RR = b.add(`R_TR${n}`, R_220()).at(...o("RR", tz));
  b.net(`T${n}_TIP`, J.TIP, RPU[2], RS[1]).net("+3V3", RPU[1], DCL.K, RR[1]).net(`T${n}_RING`, J.RING, RR[2]);
  b.net(net, RS[2], DCL.A, CT[1]).net("GND", J.SLEEVE, CT[2]);
  if (side === "N") b.track("GND", "F", [CT.pad("2"), [22.28, 31.6 - NEST_DY], [21.28, 30.6 - NEST_DY], J_PIN.pad("8")], 0.4);   // TRIG2 filter cap GND sits in the nest, where the B plane is cut up: tie it to Pico GND pin 8 (route 22)
});
([["CON1", 42.6], ["CON2", 54.2]] as const).forEach(([net, x], k) => {
  const n = k + 1;
  const J = b.add(`J_C${n}`, JACK_TRS).at(req(edge("N", { at: x }), "IF-7"));
  const cz = region([x - 5.5, 27 - NEST_DY, x + 5.5, 40 - NEST_DY]), o = (r: string, ...f: Con[]) => cp("con", r, x, YN_J - NEST_DY, false, ...f);   // CON parts sit in the nest: anchor moves with it
  const U = b.add(`U_C${n}`, LTV217).at(...o("U", cz)), RA = b.add(`R_CA${n}`, R_220()).at(...o("RA", cz, near(U, 5))), RPD = b.add(`R_CP${n}`, R0805("4.7k")).at(...o("RPD", cz, near(RA, 4)));
  b.net(net, RA[1], RPD[1]).net(`CA${n}`, RA[2], U.A).net("GND", U.K, RPD[2]);
  b.net(`CT${n}_A`, U.C, J.TIP).net(`CT${n}_B`, U.E, J.SLEEVE);   // isolated: tip = +, sleeve = - (polarity-sensitive, owner accepts)
  b.nc(J.RING);
  b.track("GND", "F", [U.pad("2"), [x - 5.7, PN + 4.57], [x - 5.7, PN + 1.79], J_PIN.pad(n === 1 ? "13" : "18")], 0.4);   // opto GND to the nearest Pico GND pin (the B plane is cut up under the MCU)
  b.track("GND", "F", [[x - 5.7, PN + 4.57], [x - 5.7, 39.16 - NEST_DY], RPD.pad("2")], 0.4);   // and on to the pull-down GND: a DevKit-only board has no Pico to tie J_PIN and J_PIS GND (route 20)
  { const g = RPD.pad("2"), pk = n === 1 ? "13" : "18", px = XA + (+pk - 1) * 2.54; b.track("GND", "F", [g, [px, PS - 1.6], J_PIS.pad(pk)], 0.4); }   // pull-down GND to the Pico south-row GND pin
});

// ================= USER1-3 (F-8): straight to GPIOs, internal pull-ups =================
// one column at the east edge, south of the DFPlayer (owner: aligned, easy to press); 7.6 mm pitch
for (const n of [1, 2, 3]) { const y = 53.4 + (n - 1) * 7.6;   // USER1 13 mm clear of the DFPlayer row (owner: was too close)
  // xy (form-factor fixed, owner 2026-10-15): one straight column, x 128.5, to line up with the enclosure lid's button holes
  const S = b.add(`SW_U${n}`, SW_TACT).at(xy(128.5, y - 0.05, 90)); b.net(`USER${n}`, S.A).net("GND", S.B); }

// ================= PIXEL (F-6): 2N7002 inverting 5 V shifter, 4.7k gate pull-down, 330R out (no bulk cap: at the strip), 5.08 3P at the back-west corner (owner: swapped with TRIG1) =================
const J_PX = b.add("J_PX", TB508_DNP(3)).at(req(edge("N", { at: 21.27 }), "IF-8"));   // courtyard x 7.5-24.4, clear of the corner hole
const pz = region([6, 14.8, 24, 16.4]);   // one row under the PIXEL labels (y 12.3-13.6) and north of the MCU socket row (body from y 18.2); the bulk cap was removed (owner 2026-10-15)
const Q_PX = b.add("Q_PX", N2N7002).at(pz, near(J_PX, 10)), R_PXG = b.add("R_PXG", R0805("4.7k")).at(pz, near(Q_PX, 4)), R_PXU = b.add("R_PXU", R_470()).at(pz, near(Q_PX, 4));
const R_PXS = b.add("R_PXS", R0805("330")).at(pz, near(J_PX.pin("2"), 8));   // no bulk cap on board (owner 2026-10-15): the only +5V bulk is the buck's 2 x 47 uF (C_BO1/2, ~70 mm away; C_DF sits behind FB_DF + D_DF), so the strip needs its own 470-1000 uF at its input (README)
b.net("PIXEL", Q_PX.G, R_PXG[1]).net("GND", R_PXG[2], Q_PX.S).net("PIX_D5", Q_PX.D, R_PXU[2], R_PXS[1]).net("+5V", R_PXU[1]);
b.net("PIX_DATA", R_PXS[2], J_PX.pin("2")).net("+5V", J_PX.pin("1")).net("GND", J_PX.pin("3"));

// ================= DFPlayer Mini (F-7) at the east edge (owner: card slot reachable, clear of the relays), SPK terminal on the back edge beside it =================
// The card slot is between pins 8 (SPK2) and 9 (IO1) (owner 2026-10-14, DFRobot documentation), so that end faces the east board edge:
// row A (pins 1..8) is the south row, pin 1 at the west end, pins running east; row B (pins 16..9) north, socket pin k = DFPlayer pin 17 - k.
// Supply isolation: the module's amplifier is bridge-tied (SPK1/SPK2 never touch board ground), so board noise reaches it through +5V/GND and the
// serial line: ferrite bead + 100 uF + 100 nF at the module, Schottky against back-feed, 1k on DF_RX; about 25 mm from the buck and 10 mm from the relays.
const DFX = 131 - 7 * 2.54, DFY = 19;   // pins 8 / 9 at x 131, card slot past them at the edge
const J_DFA = b.add("J_DFA", HDR_1x8_F_DNP).at(xy(DFX, DFY + DFPLAYER_ROW_SPACING_MM, 90)).req("F-7");   // DFPlayer pins 1..8 running east from pin 1
const J_DFB = b.add("J_DFB", HDR_1x8_F_DNP).at(xy(DFX, DFY, 90)).req("F-7");                            // socket pin k = DFPlayer pin 17 - k
const dz = region([103.5, 38.5, 108, 44.5]);   // between the +VIN trunk and the DFPlayer
const FB = b.add("FB_DF", FB_DF).at(dz, apart(TRUNK, 0.3), apart(BUS, 0.3)), D_DF = b.add("D_DF", B5819W).at(dz, near(FB, 3), apart(TRUNK, 0.3), apart(BUS, 0.3));
const C_DF = b.add("C_DF", CP100U50_SMD).at(region([106, 29, 106.6, 36]), apart(TRUNK, 0.3)), C_DFH = b.add("C_DFH", C0805P("100n")).at(near(J_DFA.pin("1"), 4)), R_DFT = b.add("R_DFT", R0805("1k")).at(near(J_DFA.pin("2"), 6));
b.net("+5V", FB[1]).net("+5V_DFF", FB[2], D_DF.A).net("+5V_DF", D_DF.K, C_DF.P, C_DFH[1], J_DFA.pin("1")).net("GND", C_DF.N, C_DFH[2], J_DFA.pin("7"), J_DFB.pin("7"));
b.net("DF_TX", R_DFT[1]).net("DF_RX", R_DFT[2], J_DFA.pin("2"));
const J_SPK = b.add("J_SPK", TB508_DNP(2)).at(req(edge("N", { at: 121.3 }), "IF-9"));   // back edge beside the DFPlayer (owner); SPK1/SPK2 run under the module from its south row
b.net("SPK2", J_DFA.pin("6"), J_SPK.pin("1")).net("SPK1", J_DFA.pin("8"), J_SPK.pin("2"));
b.net("DF_BUSY", J_DFB.pin("1"));
// LINE OUT (owner 2026-10-14: left / right / ground): the DFPlayer's own amplifier is mono and bridge-tied (SPK1/SPK2, never grounded), so stereo
// comes from its DAC_L / DAC_R pins (5 / 4) as a line-level 3.5 mm jack for an external amplifier or powered speakers: tip L, ring R, sleeve GND,
// 1k series (short-circuit / ESD) and 10 uF coupling per channel. East edge between the DFPlayer and USER1.
const J_LO = b.add("J_LO", JACK_TRS).at(req(edge("E", { at: 44.0 }), "IF-9"));
const lz = region([108, 39.5, 118.5, 45]);
const R_LL = b.add("R_LL", R0805("1k")).at(lz, near(J_DFA.pin("5"), 8), apart(BUS, 0.4)), C_LL = b.add("C_LL", C0805P("10u")).at(lz, near(R_LL, 2), apart(BUS, 0.4));
const R_LR = b.add("R_LR", R0805("1k")).at(lz, near(J_DFA.pin("4"), 8), apart(BUS, 0.4)), C_LR = b.add("C_LR", C0805P("10u")).at(lz, near(R_LR, 2), apart(BUS, 0.4));
b.net("DAC_L", J_DFA.pin("5"), R_LL[1]).net("LL1", R_LL[2], C_LL[1]).net("LINE_L", C_LL[2], J_LO.TIP);
b.net("DAC_R", J_DFA.pin("4"), R_LR[1]).net("LR1", R_LR[2], C_LR[1]).net("LINE_R", C_LR[2], J_LO.RING).net("GND", J_LO.SLEEVE);
b.nc(J_DFB.pin("2"), J_DFB.pin("3"), J_DFB.pin("4"), J_DFB.pin("5"), J_DFB.pin("6"), J_DFB.pin("8"));

// ================= expansion (IF-10): S/V/G = 3 x 11 servo-style 0.1" header (Signal / Voltage / Ground per column), 1k series per signal, V row 3V3 or 5V (solder jumper); RAILS = 5.08 5P terminal =================
const EXP = ["SDA", "SCL", "AN_IN", "EXP15", "EXP35", "EXP34", "EXP_GP0", "EXP_GP1", "EXP_GP2", "EXP_GP6", "EXP_GP13"];
// the S/V/G block is one 3 x 11 header made of three 1x11 rows at 2.54 mm pitch (KiCad has no 3-row header): its internal geometry is fixed, so xy()
const XX = 62, XY = 15.5;   // behind the RAILS terminal, east of the jacks: G row north, V, S row south (its 1k resistors below it, then the buck)
const J_XS = b.add("J_XS", HDR_1xN_M_DNP(11)).at(xy(XX, XY + 5.08, 90)), J_XV = b.add("J_XV", HDR_1xN_M_DNP(11)).at(xy(XX, XY + 2.54, 90)), J_XG = b.add("J_XG", HDR_1xN_M_DNP(11)).at(xy(XX, XY, 90));   // 2.54 mm row pitch: courtyards overlap 1.09 mm by design
b.waive("courtyards_overlap", [["J_XS", "J_XV"], ["J_XV", "J_XG"]], "three 1x11 headers form one 3 x 11 S/V/G block at 2.54 mm pitch (KiCad has no 3-row header); pads 0.84 mm apart");
EXP.forEach((n, i) => {
  const R = b.add(`R_X${i + 1}`, R0805("1k")).at(region([XX - 1, XY + 7.5, XX + 26.4, XY + 10.5]), near(J_XS.pin(String(i + 1)), 7));   // one row south of the S row
  b.net(n, R[1]).net(`XS${i + 1}`, R[2], J_XS.pin(String(i + 1))).net("V_EXP", J_XV.pin(String(i + 1))).net("GND", J_XG.pin(String(i + 1)));
});
const SJ_V = b.add("SJ_V", SJ3_B12).at(near(J_XV, 8), apart(TRUNK, 0.5));   // V row: bridged 1-2 = 3V3 (default); cut and bridge 2-3 for 5 V
b.net("+3V3", SJ_V.V33).net("V_EXP", SJ_V.COM).net("+5V", SJ_V.V5);
// DFPlayer serial feedback (owner 2026-10-15: UART TX + RX rather than BUSY, so every command can be acknowledged and status queried): the
// module's TX (pin 3) joins header column S6, so it reaches DevKit GPIO34 / Pico GP7 (input-only on the DevKit: fine for RX) through R_X6 (1k
// series). S6 is then the DF TX monitor pin, not a free input. Firmware: ESP32 UART2 RX on GPIO34 (GPIO matrix), Pico PIO UART RX on GP7.
b.net("XS6", J_DFA.pin("3"));
const J_RAIL = b.add("J_RAIL", TB508_DNP(5)).at(req(edge("N", { at: 84.7 }), "IF-2"));   // daisy chain out: 5.08 5P like every field terminal (IF-2), on the back edge beside the power entry
b.track("+VIN", "F", [[VX - 1.5, 10.8], [84.7, 10.8], J_RAIL.pad("1")], 2.3);   // RAILS +VIN (3 A)
b.track("+VIN", "F", [J_PWR.pad("1"), [107.6, 14.6], [99, 14.6]], 2.3);
b.track("+VIN", "F", [D_TVS.pad("1"), [88.05, 27.4], [95.5, 27.4]], 1.0);   // TVS cathode onto the trunk, in the gap between D_TVS and C_BK1 (seed 19; route 24 left it open)   // barrel tip onto the trunk, 0.65 mm clear of the unused SW pad
b.net("+VIN", J_RAIL.pin("1")).net("+5V", J_RAIL.pin("2")).net("+3V3", J_RAIL.pin("3")).net("GND", J_RAIL.pin("4"), J_RAIL.pin("5"));

// ================= silkscreen (owner 2026-10-14: label every connector, button and header row) =================
// front: each label sits just behind its terminal, pin names over the pins (pin 1 west)
b.text("TRIG1", T1X, 64.6, { size: 1.1 });
OP.forEach((x, k) => b.text(`+ OUT${k + 1} -`, x + 2.54, 67.7, { size: 1.0 }));
RP.forEach((x, k) => b.text(`RLY${k + 1}: NO COM NC`, x + 5.08, 70.3, { size: 1.0 }));
// back: pin 1 is the east pin on this edge
b.text("PIXEL: GND DATA 5V", 16.19, 12.4, { size: 0.9 });   // one line, so the PIXEL parts fit under it (2026-10-15)
b.text("TRIG2", 31, 15.4, { size: 1.0 }).text("CONTACT1", 42.6, 15.4, { size: 1.0 }).text("CONTACT2", 54.2, 15.4, { size: 1.0 });
["VIN", "5V", "3V3", "GND", "GND"].forEach((t, i) => b.text(t, 84.7 - i * 5.08, 12.2, { size: 0.9 }));
b.text("- 9-24V IN +", 94.96, 12.2, { size: 0.9 }).text("SPK", 118.76, 12.2, { size: 0.9 });
// S/V/G rows and the USER buttons
b.text("G", XX - 2.3, XY, { size: 1.0 }).text("V", XX - 2.3, XY + 2.54, { size: 1.0 }).text("S", XX - 2.3, XY + 5.08, { size: 1.0 });
[1, 2, 3].forEach((n) => b.text(`USER${n}`, 132.3, 53.35 + (n - 1) * 7.6, { size: 0.8, rot: 90 }));   // east of each button, reading along the column (the 0.6 mm gaps between buttons have pads)
b.text("LINE OUT L/R", 124, 36.6, { size: 0.8 });
// board name in the open area between the MCU nest and the bulk caps (visible with every module fitted; the old spot was under L_BK)
b.text("SPECTRE PROPTROLLER", 65, 31.6, { size: 1.3 }).text("rev B", 65, 34.0, { size: 1.3 });

// Safety marking at 1.5 mm (owner 2026-10-20), about 49.6 mm long: no longer fits between D_C1 and the K2 slots, so it runs along the masked
// +VIN bus strip from east of K2's COIL_B pad (x 73.4) to short of SW_U1's pads (x 126.25), above the OUT FET pads (y 52.7) and below the
// LINE OUT jack's pads (y 48.5). F-3 / SAF-1 (owner 2026-10-20): rev B relay contacts 120 V AC 5 A resistive, 3 A inductive (UL 3 A 125 VAC), or 28 V DC 10 A resistive
// (Songle SRD sheet: 28 VDC is the rated DC voltage, 30 VDC only the maximum switching voltage; rev A stays <= 30 V DC)
b.text("RELAYS: 120VAC 5A (3A IND) / 28VDC 10A", 99.6, 50.4, { size: 1.5 });

// module orientation (owner 2026-10-14): text marks, visible with the modules out (the DSL has no silk lines)
b.text("<< ANTENNA   ESP32 DEVKIT / PICO W   USB >>", 27, YN + 1.9, { size: 0.8 });
b.text("DEVKIT: EN = pin 1 (W)    PICO: GP16 = pin 21 (W)", 27, YS - 1.9, { size: 0.7 });
// Antenna box (owner 2026-10-15): outline the module antenna end at the west edge so the board orientation is obvious. The DSL has no silk lines,
// so each side is a run of underscores (top/bottom) or a rotated run (left/right) around the Pico W antenna keep-out; no copper or pads inside.
const ANT_H = 16, ANT_V = 21;   // underscore counts sized from the rendered silk (KiCad stroke font)
{ const x0 = 1.2, x1 = AX - 0.4, y0 = AY - AK.w / 2 + 0.4, y1 = AY + AK.w / 2 - 0.4, h = (n: number) => "_".repeat(n);
  b.text(h(ANT_H), (x0 + x1) / 2, y0 - 0.2, { size: 0.8 }).text(h(ANT_H), (x0 + x1) / 2, y1 - 0.45, { size: 0.8 });   // underscores sit ~0.3 mm below the text centre
  b.text(h(ANT_V), x0, AY, { size: 0.8, rot: 90 }).text(h(ANT_V), x1, AY, { size: 0.8, rot: 90 });
  b.text("ANTENNA", (x0 + x1) / 2, AY, { size: 1.0, rot: 90 }); }
b.text("1", XA - 2.2, YS, { size: 1.0 }).text("1", XA + 19 * 2.54, PN - 1.9, { size: 1.0 });   // DevKit EN pad; Pico pin 1 (GP0) at the USB end of the north row
b.text("DFPLAYER", DFX + 8.89, DFY + 6, { size: 1.0 }).text("SD CARD >>", DFX + 8.89, DFY + 9, { size: 1.0 }).text("1", DFX - 2.2, DFY + DFPLAYER_ROW_SPACING_MM, { size: 1.0 });
b.text("10A MAX (barrel 5A)", 97, 13.4, { size: 0.7 });
// ratings and the +5V budget on the bottom face (free of SMD; owner: high-level stats on the board, details in README). The relay line is 1.5 mm
// (owner 2026-10-20 safety marking). Positions (2026-10-21, second review: no via drill on silk text): each line placed where its glyphs (the
// mirrored outline KiCad plots) keep >= 0.15 mm from every via and >= 0.25 mm from every pad and slot on B, measured on the routed board;
// rows keep >= 1.92 mm pitch (2.4 mm beside the 1.5 mm line), between the S/V/G header pins (y 21.4) and the relay pads (y 49). Two pairs of lines
// swapped rows for it (OUT PWM / supply, TEST PADS / TRIG), and the x centres differ line to line. Checked with the review's silkall.py.
([
  ["SPECTRE PROPTROLLER rev B", 99.5, 22.3],
  ["IN 9-24V DC, 10A MAX TOTAL (barrel 5A, terminal 10A)", 103.2, 24.25],
  ["OUT1-4: 5A each, 7.5A 5s from idle (12-24V; derate <12V), sum <=10A", 90.9, 26.2],
  ["OUT PWM: <=1A at 24V, <=2A at 12V", 95.9, 28.5],
  ["supply MUST have OCP: no fuses on board, TVS SMAJ24A", 85.4, 30.45],
  ["RLY1-2 dry: 120VAC 5A (3A IND) / 28VDC 10A, GFCI", 92.6, 32.85, 1.5],
  ["+5V 2.5A: ESP32 0.5A pk, Pico W 0.3A, DFPlayer 0.8A pk,", 85.0, 35.25],
  ["relays 0.14A, PIXEL ~1A (inject 5V on long strips)", 90.5, 37.2],
  ["+3V3 from the MCU module: 0.1A board total", 81.9, 39.15],
  ["RAILS out: VIN 3A, 5V 0.1A, 3V3 0.05A", 85.0, 41.1],
  ["TEST PADS (pogo): TP1-TP5 = VIN 5V 3V3 GND DF_BUSY", 79.6, 43.05],
  ["TRIG1/2 3.3V in, ring 3V3/220R; CONTACT opto <=30V 50mA", 85.0, 46.9],
  ["EXP S6 = DFPlayer TX (monitor only, do not drive)", 89.9, 48.85],
] as [string, number, number, number?][]).forEach(([t, x, y, size = 1.0]) => b.text(t, x, y, { l: "B", size }));

// ================= test points =================
b.tp("+VIN", { near: C_BK1, max: 8 }).tp("+5V", { near: C_BO1 }).tp("+3V3", { near: SJ_V, max: 4 }).tp("DF_BUSY", { near: J_DFB.pin("1"), max: 10 }).tp("GND", { near: C_BI2 });   // TP5 (GND) beside the input caps since 2026-10-21

// ================= classes (IPC-2221 external, 1 oz) =================
// Design basis (owner 2026-10-14): 1 oz copper sized for a 20 C rise, not the 10 C the oracle's amps check assumes; widths include the
// -20 % etch tolerance (width / 0.8). 5 A on one layer = 2.3 mm; 10 A runs (+VIN trunk, relay contacts) are locked as parallel F + B tracks of
// 2.3 mm each (5 A per layer). The oracle cannot model a 20 C basis or parallel layers, so its amps / width rows on +VIN, OUTn and Rn_* fail
// by design: recorded exception, hand calculation in README.md (toolchain limitation: fixed 10 C rise, one layer at a time).
b.cls("vin", { w: 2.3, amps: 10, req: "PWR-1" }, "+VIN");
b.cls("out", { w: 2.3, amps: 5, req: "F-2" }, "OUT1", "OUT2", "OUT3", "OUT4");
// One class per relay (SAF-1): the island rule below names them. Same 2.3 mm / 10 A basis as rev A's single relay class.
b.cls("relay1", { w: 2.3, amps: 10, req: "F-3" }, "R1_COM", "R1_NO", "R1_NC");
b.cls("relay2", { w: 2.3, amps: 10, req: "F-3" }, "R2_COM", "R2_NO", "R2_NC");
// SAF-1 (owner 2026-10-20): each relay's contact island (COM / NO / NC) keeps >= 3.0 mm copper clearance to every net outside that island, on both
// layers and against the GND pours; copper inside one island keeps the normal clearance (COM-NO tracks 2.75 mm apart, terminal pins 5.08 mm pitch).
// A design margin, not a certification. KiCad DRC is the proof: Freerouting does not see custom rules, so the moats are also keepouts (above) and
// every island neighbour is locked.
for (const n of [1, 2]) {
  const isl = (X: string) => `(${X}.NetClass == 'relay${n}')`;
  b.rule(`# SAF-1 (requirements.yaml): RLY${n} contact island >= 3.0 mm to every other net`)
   .rule(`(rule "SAF-1 RLY${n} contact island 3.0 mm" (condition "${isl("A")} && !${isl("B")}") (constraint clearance (min 3.0mm)))`);
}
b.cls("p5v", { w: 0.9, amps: 2.5, req: "PWR-2" }, "+5V", "BK_SW");   // 2.5 A at the 20 C basis: 0.87 mm
b.cls("p5vs", { w: 0.5, amps: 0.8 }, "+5V_DF", "+5V_DFF", "+5V_MCU");   // 0.8 A at the 20 C basis: 0.3 mm
b.cls("vbk", { w: 0.8, amps: 1.6, req: "PWR-2" }, "+VIN_BK");   // buck input: 2.5 A x 5 V / 9 V / 0.9 = 1.55 A at the 9 V minimum
b.cls("p3v3", { w: 0.3, amps: 0.1, req: "PWR-3" }, "+3V3", "V_EXP");
b.cls("spk", { w: 0.3, amps: 0.7 }, "SPK1", "SPK2");   // 0.7 A at 20 C: 0.15 mm; 0.3 mm passes between the DFPlayer socket pins (0.84 mm gap)
b.cls("gnd", { w: 0.8, amps: 2 }, "GND");   // parallel: true was tried (2026-10-21): the combined F+B plane-current row finds GND whole, worst neck 0.2 mm (the C_MCU decoupler fanout stub in the MCU nest), so it only adds a failing row; the SAF-2 min-cuts are the measured basis (README)
const big = new Set(["+VIN_BK", "+5V_DFF", "+VIN", "OUT1", "OUT2", "OUT3", "OUT4", "R1_COM", "R1_NO", "R1_NC", "R2_COM", "R2_NO", "R2_NC", "+5V", "BK_SW", "+5V_DF", "+5V_MCU", "+3V3", "V_EXP", "SPK1", "SPK2", "GND"]);
b.cls("sig", { w: 0.25, amps: 0.05 }, ...[...b.nets.keys()].filter(n => !big.has(n)));

b.assume("Placement (owner 2026-10-14): 120 x 80, power + daisy chain on the back edge, loads on the front, everything reachable, room for silk; buck = TPS54360B (65 V, 3.5 A, PowerPAD; owner 2026-10-14: comfortable at 2.5-3 A).", { req: "MECH-1" })
 .assume("DevKit board outline past its pins and its underside are not measured (owner has none on hand): socket spacing and online drawings only; never both MCUs fitted.", { req: "IF-11" })
 .assume("DFPlayer card slot is at the pins 8 / 9 end (owner 2026-10-14, DFRobot documentation), facing the east edge; verify on a module.", { req: "F-7" });
b.routingStyle({ directions: "45", signalCorners: "any" });
b.autoroute({ threads: 1, neckdown: false });   // neckdown off: with it on, Freerouting narrowed +VIN and the relay contacts to 0.15 mm (2026-10-14)
// locks on parts created in loops above (looked up by reference)
const part = (ref: string) => { const p = b.parts.find(q => q.ref === ref); if (!p) throw new Error(ref); return p; };
b.track("USER2", "F", [[123.28, 76.28], [128.51, 71.05], [128.51, 65.84], part("SW_U2").pad("1")], 0.25);   // between SW_U3's pads, onto the moved SW_U2
// EXP_GP13 (2026-10-15): out of the nest between DevKit pins 4 and 5 (x 12.39, west of the PWM3 link), along under the row, up the gap between Q_PX and R_PXS, then its seed-19 path
b.track("EXP_GP13", "F", [J_PIN.pad("4"), [12.39, PN - 1.27], [12.39, 18.2], [14.93, 18.2], [14.93, 12.8], [20.03, 7.7], [59.04, 7.7], [62.96, 11.61]], 0.25).via("EXP_GP13", [62.96, 11.61])
 .track("EXP_GP13", "B", [[62.96, 11.61], [72.47, 11.61], [75.97, 15.11], [75.97, 15.87], [76.99, 16.89], [77.72, 16.89], [78.51, 17.68], [78.51, 18.46], [79.36, 19.31], [82.74, 19.31], [83.53, 20.1], [83.53, 22.7], [83.33, 22.89]], 0.25)
 .via("EXP_GP13", [83.33, 22.89]).track("EXP_GP13", "F", [[83.33, 22.89], [83.33, 23.55], part("R_X11").pad("1")], 0.25);

// PIXEL (2026-10-15): Pico south-row pin 5 up the x 17.47 column through the nest (its seed-19 line), under the DevKit north row on B, to Q_PX's gate
b.track("PIXEL", "F", [J_PIS.pad("5"), [17.47, PS - 3.81], [17.47, 18.3]], 0.25).via("PIXEL", [17.47, 18.3])
 .track("PIXEL", "B", [[17.47, 18.3], [12.3, 18.3], [11.6, 17.6]], 0.25).via("PIXEL", [11.6, 17.6]).track("PIXEL", "F", [[11.6, 17.6], [12.0, 17.2], part("Q_PX").pad("1")], 0.25);
// SCL (2026-10-15): as routed in the fifth post-move run (tools/lock_route.py), plus the Pico pin 14 link it left open (F between DevKit pins 14 / 15)
b.track("SCL", "F", [[62.987, 23.879], [63.487, 24.379], [65.959, 24.379], [66.838, 23.5]], 0.25);
b.track("SCL", "B", [[26.36, 19.5], [27.521, 18.339], [40.298, 18.339], [40.893, 18.933], [60.519, 18.933], [60.896, 19.31], [62.924, 19.31], [63.199, 19.585], [63.199, 23.667], [62.987, 23.879]], 0.25);
b.via("SCL", [62.987, 23.879]);
b.track("SCL", "F", [J_PIN.pad("14"), [37.79, PN - 1.27], [37.79, 17.9]], 0.25).via("SCL", [37.79, 17.9]).track("SCL", "B", [[37.79, 17.9], [37.79, 18.339]], 0.25);
// +5V trunk (review 2026-10-15, B1): the seed-19 0.9 mm B feed along y 18.4-19.0 sat where the DevKit north row is now, and Freerouting replaced
// it with 0.15 mm runs. Locked at the class width: J_RAIL.2 west on B (y 7.8, then y 16.7 under the DevKit's north edge) to J_PX.1 with a tap
// to SJ_V's 5 V pad, and J_RAIL.2 down the B corridor under the +VIN trunk to FB_DF and on to K2's coil (+5V from the buck via the relays).
b.track("+5V", "B", [part("J_RAIL").pad("2"), [77.22, 8.3], [63.77, 8.3], [55.37, 16.7], [21.27, 16.7], part("J_PX").pad("1")], 0.9)   // y 8.3: >= 0.7 mm from J_RAIL's +3V3 / GND pins (re-review)
 .via("+5V", [55.37, 16.7]).track("+5V", "F", [[55.37, 16.7], [55.37, 19.4], part("SJ_V").pad("3")], 0.9);
b.track("+5V", "B", [part("J_RAIL").pad("2"), [80.6, 5.9], [94.09, 19.39], [94.09, 43.49], [101.84, 43.49], [102.84, 43.49]], 0.9).via("+5V", [101.84, 43.49]).via("+5V", [102.84, 43.49])   // diagonal 0.68 mm off J_XG.11 (GND); two vias for the DFPlayer feed
 .track("+5V", "F", [[101.84, 43.49], [102.84, 43.49], [104.55, 43.49], part("FB_DF").pad("1")], 0.9);
b.track("+5V", "B", [[94.09, 43.49], [83.51, 43.49], [82.0, 45.0]], 0.9).via("+5V", [82.0, 45.0], { d: 0.9, drill: 0.4 })   // east of the PWM1/2/4 B diagonals; 0.4 mm drill for about 1.9 A (no room for a second via beside R_RL2)
 .track("+5V", "F", [[82.0, 45.0], [81.66, 45.34], [74.2, 45.34], [73.2, 45.34]], 0.9).via("+5V", [74.2, 45.34]).via("+5V", [73.2, 45.34])
 .track("+5V", "B", [[74.2, 45.34], [73.2, 45.34], [68.63, 45.34]], 0.9);   // rev B: the last run to K2 COIL_A crossed the K2 COM moat; left to the router (SAF-1 keepouts)
// +5V_MCU (2026-10-15): Freerouting fed the MCU's 5 V (0.8 A) with 0.15 mm after the move; locked at the class width along its own path:
// D_MCU -> C_MCU -> between Pico south pins 11 / 12 -> B under the DevKit south row -> DevKit VIN, and on to Pico VSYS on F
b.track("+5V_MCU", "F", [D_MCU.pad("1"), [25.0, 37.55], C_MCU.pad("1"), [29.5, 38.0], [30.17, 38.67], [30.17, 41.92], [30.75, 42.5], [32.71, 42.5], [32.71, 45.77], [33.34, 46.4]], 0.5).via("+5V_MCU", [33.34, 46.4])   // crosses the DevKit pin row on F, not the solder side
 .track("+5V_MCU", "B", [[33.34, 46.4], [37.56, 46.4], J_DKS.pad("15")], 0.5)
 .track("+5V_MCU", "F", [J_DKS.pad("15"), [41.59, 42.37], [47.94, 42.37], J_PIS.pad("19")], 0.5);
// EXP35 (2026-10-15): routed in the seventh post-move run, left open in the eighth; locked as routed (tools/lock_route.py)
b.track("EXP35", "F", [[67.919, 29.24], [69.828, 31.15], [69.828, 36.948], [68.67, 38.107], [50.547, 38.107], [49.379, 39.275], [47.678, 39.275]], 0.25);
b.track("EXP35", "F", [[13.66, 44.9], [16.196, 47.436], [19.264, 47.436]], 0.25);
b.track("EXP35", "F", [[71.05, 26.013], [69.871, 27.191], [66.673, 27.191]], 0.25);
b.track("EXP35", "B", [[47.678, 39.275], [46.8, 39.275], [45.41, 40.664], [45.41, 41.492], [39.467, 47.436], [19.264, 47.436]], 0.25);
b.track("EXP35", "B", [[13.66, 44.9], [11.755, 42.995], [7.945, 42.995], [6.04, 41.09]], 0.25);
b.track("EXP35", "B", [[67.919, 29.24], [67.919, 28.437], [66.673, 27.191]], 0.25);
b.via("EXP35", [66.673, 27.191]);
b.via("EXP35", [47.678, 39.275]);
b.via("EXP35", [19.264, 47.436]);
b.via("EXP35", [67.919, 29.24]);
// rest of the +5V net as routed in the build reviewed on 2026-10-15: buck -> K1 -> K2 and K1 -> D_MCU at 0.9 mm, so a rebuild cannot neck it
// (one branch stays narrow by design: 0.15 mm to the 470 R PIXEL pull-up R_PXU, about 10 mA)
b.track("+5V", "F", [[68.65, 45.0], [65.635, 45.0], [64.088, 43.452], [64.088, 43.3]], 0.9);
// Buck output (2026-10-21, review B2): the rev B F run from L_BK round the input caps (x 34.4 / y 60.4 / x 42.05) to K1 COIL_A fenced the input and
// output cap grounds off from the IC and catch-diode ground; it is gone. C_BO1 / C_BO2 now sit west of L_BK pad 2 (their GND pads west, into the
// catch-diode ground), joined to the pad by short 1.8 mm F copper. After COUT the output drops to B through four 0.4 mm vias (outside the
// L -> COUT -> GND loop) and runs north on B at x 35.5, beside the K1 moat, to the +5V run at y 54.45 (K1 COIL_A, R_FB1 and the rest of the board).
b.track("+5V", "F", [C_BO1.pad("1"), [28.6, 73.6]], 1.8).track("+5V", "F", [C_BO2.pad("1"), [28.6, 76.4]], 1.8);
b.track("+5V", "F", [C_BO1.pad("1"), [23.775, 71.47], [23.605, 71.3]], 0.9);   // TP2
b.track("+5V", "F", [[31.5, 76.0], [35.5, 76.0]], 0.9).track("+5V", "F", [[35.5, 74.2], [35.5, 76.9]], 0.9);
for (const y of [74.2, 75.1, 76.0, 76.9]) b.via("+5V", [35.5, y], { d: 0.9, drill: 0.4 });
b.track("+5V", "B", [[35.5, 76.9], [35.5, 54.45]], 0.9);
// Buck ground (review B2): one F ground area joins the input caps' GND pads (south ends, at the top of the strip between U_BK and the K1 moat), the
// strip down to L_BK, the copper under L_BK between its pads, the catch diodes' anodes, the output caps' GND pads and the PowerPAD (through pin 7
// and its north end); the B plane under the IC is kept clear of other nets between the input-cap vias and the diode anodes, so the loop also closes
// on B directly under it. Stitching vias at the cap grounds, in the strip and at TP5.
b.via("GND", [34.4, 62.4]).via("GND", [37.35, 59.6]).via("GND", [34.3, 66.0]).via("GND", [34.3, 69.0]).via("GND", [34.3, 71.5]).via("GND", [37.15, 65.0]);
b.via("GND", [19.2, 73.6]).via("GND", [19.2, 76.4]);
// The buck's SMD ground pads (input / output caps, catch-diode anodes, PowerPAD, TP5) join that F area solidly, not through 0.3 mm thermal
// spokes (measured: the spokes were the narrowest copper between the cap and diode grounds): a solid-connect F GND pour over the buck, west of
// the K1 pour pullback (x 37.45) and south of the MCU nest (y 50), on top of the board-wide F pour.
b.pour("GND", ["F"], { poly: [[14, 50], [37.4, 50], [37.4, 79.7], [14, 79.7]], connect: "solid", prio: 1 });
b.track("+5V", "F", [[21.764, 53.082], [23.482, 54.8], [23.787, 54.8]], 0.9);
b.track("+5V", "F", [[26.846, 37.076], [30.077, 37.076], [32.684, 39.683]], 0.9);
b.track("+5V", "F", [[19.354, 37.55], [20.35, 37.55]], 0.9);
b.track("+5V", "F", [[84.412, 43.25], [84.412, 43.451], [82.863, 45.0], [82.0, 45.0]], 0.9);
b.track("+5V", "F", [[10.25, 13.938], [18.293, 5.895], [21.27, 5.895]], 0.15);
b.track("+5V", "B", [[32.684, 39.683], [33.123, 39.245], [38.337, 39.245], [39.301, 38.281], [51.082, 38.281], [53.283, 40.481]], 0.9);   // nest trunk; rev B: its run down to K1 / K2 is the router's
b.track("+5V", "B", [[42.05, 52.45], [37.709, 52.45], [35.709, 54.45], [23.132, 54.45], [21.764, 53.082]], 0.9);
b.track("+5V", "B", [[19.354, 37.55], [26.372, 37.55], [26.846, 37.076]], 0.9);
b.via("+5V", [19.354, 37.55]);
b.via("+5V", [26.846, 37.076]);
b.via("+5V", [21.764, 53.082]);
b.via("+5V", [32.684, 39.683]);
// GND stitching for the B pocket the +5V trunk fences off north of y 16.7 (re-review)
b.via("GND", [48.5, 15.6]).via("GND", [40.0, 15.2]);
// RLY2 (2026-10-15): routed in the ninth post-move run, left open in the tenth; locked as routed
// rev B (SAF-1): RLY2 now leaves the nest north of the corridor (routed in the first rev B run, locked as routed with tools/lock_route.py); the rev A run
// along y 49.4 / 45.3 north of K1 and its DevKit-side stub are gone
b.track("RLY2", "F", [[55.735, 39.59], [66.877, 39.59], [69.588, 42.3]], 0.25);
b.track("RLY2", "F", [[56.678, 40.247], [56.392, 40.247], [55.735, 39.59]], 0.25);
b.track("RLY2", "F", [[54.036, 33.235], [54.036, 32.057], [53.429, 31.45]], 0.25);
b.track("RLY2", "F", [[27.228, 35.252], [31.799, 35.252], [33.464, 33.587]], 0.25);
b.track("RLY2", "B", [[54.036, 33.235], [54.036, 37.605], [56.678, 40.247]], 0.25);
b.track("RLY2", "B", [[18.74, 41.09], [18.566, 40.915], [18.566, 37.192], [20.763, 34.994], [26.97, 34.994], [27.228, 35.252]], 0.25);
b.track("RLY2", "B", [[33.464, 33.587], [51.292, 33.587], [53.429, 31.45]], 0.25);
b.via("RLY2", [27.228, 35.252]).via("RLY2", [56.678, 40.247]).via("RLY2", [53.429, 31.45]).via("RLY2", [33.464, 33.587]).via("RLY2", [54.036, 33.235]);
// rev B (SAF-1): the coil-driver strip north of the relays. +5V (2.5 A from the buck to the rest of the board) leaves K1 COIL_A on B west of the K1 COM moat,
// runs east along y 45.4 (3.15 mm from the COM pad) to the nest trunk (x 53.283) and on to the RAILS-side feed (68.63, 45.34), with taps to K2 COIL_A
// and D_C1; +VIN_BK crosses it on F (x 43.1 / y 45.5) to its via at 55.423, 42.908. COIL1 as routed in the first rev B run.
b.track("+5V", "B", [part("K1").pad("2"), [43.1, 51.4], [43.1, 45.4], [53.283, 45.4], [53.283, 40.481]], 0.9)
 .track("+5V", "B", [[53.283, 45.4], [68.57, 45.4], [68.63, 45.34]], 0.9)
 .track("+5V", "B", [[59.05, 45.4], [59.05, 51.35], part("K2").pad("2")], 0.9)
 .via("+5V", [59.05, 48.6]).track("+5V", "F", [part("D_C1").pad("1"), [59.05, 48.6]], 0.9)   // D_C1 cathode
 .via("+5V", [67.0, 45.0]);   // D_C2 / R_RL1 feed (F, from rev A) onto the B run
b.track("+VIN_BK", "F", [[43.1, 47.775], [43.1, 45.5], [51.5, 45.5], [52.9, 44.1], [52.9, 42.9], [55.423, 42.908]], 0.8);
b.track("COIL1", "F", [[54.05, 52.45], [54.05, 49.15], [55.8, 47.4], [56.05, 47.15], [56.05, 45.405]], 0.25);
// RLY1 (rev B): routed in the second rev B run (through the nest, north of the corridor), locked as routed (tools/lock_route.py); widened from the 0.15 mm Freerouting left to the 0.25 mm sig class
b.track("RLY1", "F", [[29.998, 30.135], [28.447, 30.135], [25.455, 33.127], [23.614, 33.127]], 0.25);   // diagonal (x + y = 58.582) centred between the EXP34 via and D_TC2 pad 1: 0.158 mm to each
b.track("RLY1", "F", [[16.2, 41.09], [16.2, 39.162], [21.905, 33.457], [21.905, 33.127]], 0.25);   // diagonal 0.04 mm SW: >= 0.15 mm to EXP34
b.track("RLY1", "F", [[51.002, 31.489], [53.204, 33.689], [53.204, 33.694]], 0.25);
b.track("RLY1", "F", [[54.389, 41.001], [54.51, 41.001], [56.12, 42.611], [56.12, 43.171], [55.138, 44.153], [55.138, 44.8]], 0.25);   // x 56.12: >= 0.15 mm to the +VIN_BK track end
b.track("RLY1", "B", [[53.204, 33.694], [53.204, 38.967], [54.389, 40.153], [54.389, 41.001]], 0.25);
b.track("RLY1", "B", [[23.614, 33.127], [21.905, 33.127]], 0.25);
b.track("RLY1", "B", [[29.998, 30.135], [49.648, 30.135], [51.002, 31.489]], 0.25);
b.via("RLY1", [53.204, 33.694]).via("RLY1", [54.389, 41.001]).via("RLY1", [23.614, 33.127]).via("RLY1", [29.998, 30.135]).via("RLY1", [21.905, 33.127]).via("RLY1", [51.002, 31.489]);
// GND stitching (rev B, tools/gnd_islands.py on the second rev B run): the RLY1 / RLY2 / +5V / +VIN_BK runs fence off a B pocket around the relay drivers
b.via("GND", [63.0, 30.31]);
b.track("RLY2", "B", [[23.82, 44.9], [21.915, 42.995], [20.645, 42.995], [18.74, 41.09]], 0.25);
// reviewed placement kept when the nest moved (frozen.ts): every listed part is locked at its seed-19 pose; its constraints are still checked
for (const p of b.parts) { const f = FROZEN[p.ref]; if (f && !p.fixed) p.at(xy(f[0], f[1], f[2])); }
lockRoutes(b);   // every routed signal / power net locked (routes.ts): Freerouting is not repeatable on this board
b.seed(19);
export default b;
