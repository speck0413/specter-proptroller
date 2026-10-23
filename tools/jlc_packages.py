#!/usr/bin/env python3
"""Three JLC order packages for the Spectre Proptroller board (rev A), all from the same gerbers:
  1-jlc-fits-everything        every SMD part, barrel jack, relays, screw terminals, DevKit/DFPlayer sockets, expansion headers
  2-you-fit-terminals-headers  JLC fits SMD + barrel jack + relays; you fit terminals and 0.1" headers/sockets
  3-you-fit-all-THT            JLC fits SMD only; you fit barrel jack, relays, terminals, headers
Base: aieda's fab bom.csv/cpl.csv (SMD + J_PWR + K1/K2). THT rows are re-centred on the pad bounding box (JLC places the part body at
Mid X/Y; KiCad's THT origin is pin 1). Added THT parts (DNP 'mech' in board.ts) take positions from the build IR.

JLC rotation corrections on top of aieda's CPL. Each one was checked against JLC's own footprint (EasyEDA pad data for the exact
LCSC part) and confirmed in JLC's 3D placement preview, 2026-10-15:
  U_BK  TI_SO-PowerPAD-8        +270  (takes the SOIC-8 correction; the toolchain has no rule for this footprint)
  C_BK1/C_BK2/C_DF  CP_Elec_8x10 C970687   +180  (aieda's 180 rule is wrong for this part: JLC pad 1 (+) is on -x like KiCad's)
  U_C1/U_C2  SOP-4 LTV-217-B-G C115450           +180  (JLC pad 1 at (+3.50,+0.64), KiCad pad 1 at (-3.19,-0.63))
  Q_D1-4, Q_PX, Q_R1, Q_R2  SOT-23 C8545/C20526  -90  (net: JLC rot = KiCad rot + 180; aieda's -90 rule is wrong)
  J_DKN/J_DKS/J_PIN/J_PIS/J_DFA/J_DFB/J_XS/J_XV/J_XG  PinSocket/PinHeader 1xN    0   (JLC's model lies along x at 0)
  J_IN/J_O1-4/J_SPK  2-pin terminal C8465   +180  (wire entry toward the board edge; checked with JLC's pink arrows)
Upload the CPLs in the package folders, never aieda's fab/cpl.csv (it lacks these corrections).
usage: tools/jlc_packages.py hardware fab/order   (after bin/aieda build and bin/aieda fab)"""
import csv, json, os, shutil, sys

bd, out = sys.argv[1], sys.argv[2]
ir = json.load(open(os.path.join(bd, "kicad/proptroller.ir.json")))
P = {p["ref"]: p for p in ir["parts"]}
OX, OY = 50.0, 50.0                      # IR board-local -> KiCad position file (x + 50, -(y + 50)), checked on U_BK and J_PWR
ROT_FIX = {"U_BK": 270,
           **{r: 180 for r in ("C_BK1", "C_BK2", "C_DF", "C_PX", "U_C1", "U_C2")},
           **{r: -90 for r in ("Q_D1", "Q_D2", "Q_D3", "Q_D4", "Q_PX", "Q_R1", "Q_R2")}}
ROT_ABS = {r: 0 for r in ("J_DKN", "J_DKS", "J_PIN", "J_PIS", "J_DFA", "J_DFB", "J_XS", "J_XV", "J_XG")}
# 2-pin terminals C8465 (checked in JLC's preview, 2026-10-15): JLC's model faced the wire entry the wrong way (pink arrows pointing at the board
# edge); +180 so the arrows point toward the board centre. Centre-symmetric 2-pad footprint, so the pads still land; each clamp keeps its
# position on the silk (+/- marks follow the pads, not the housing).
THT_ROT_FIX = {r: 180 for r in ("J_IN", "J_O1", "J_O2", "J_O3", "J_O4", "J_SPK")}

# added THT lines: (comment, footprint, LCSC, refs)
ADD = [
    ("WJ500V-5.08-2P screw terminal", "TerminalBlock_5.08mm_1x02", "C8465", ["J_IN", "J_O1", "J_O2", "J_O3", "J_O4", "J_SPK"]),
    ("WJ500V-5.08-3P screw terminal", "TerminalBlock_5.08mm_1x03", "C72334", ["J_R1", "J_R2", "J_PX"]),
    ("WJ500V-5.08-5P screw terminal", "TerminalBlock_5.08mm_1x05", "C42377750", ["J_RAIL"]),
    ("2541FV-15P-B 1x15 female 8.5mm", "PinSocket_1x15_P2.54mm_Vertical", "C25503121", ["J_DKN", "J_DKS"]),
    ("PM2.54-1X8PM-H85 1x8 female 8.5mm", "PinSocket_1x08_P2.54mm_Vertical", "C42431840", ["J_DFA", "J_DFB"]),
    ("PZ254V-11-11P 1x11 male", "PinHeader_1x11_P2.54mm_Vertical", "C2840011", ["J_XS", "J_XV", "J_XG"]),
    ("2541FV-20P-B 1x20 female 8.5mm", "PinSocket_1x20_P2.54mm_Vertical", "C25503124", ["J_PIN", "J_PIS"]),   # Pico / Pico W rows
]
for _, _, _, refs in ADD:
    for r in refs:
        assert r in P and P[r]["side"] == "F", r
THT_BASE = {"J_PWR", "K1", "K2"}
NOT_FITTED: set = set()   # refs to leave out of every package (aieda fab still lists handSolder() parts)


def centre(ref):
    pads = P[ref]["pads"]
    x0 = min(d["bb"][0] for d in pads); x1 = max(d["bb"][2] for d in pads)
    y0 = min(d["bb"][1] for d in pads); y1 = max(d["bb"][3] for d in pads)
    return (x0 + x1) / 2 + OX, -((y0 + y1) / 2 + OY)


def tht_row(ref):
    x, y = centre(ref)
    rot = (ROT_ABS.get(ref, P[ref]["rot"] % 360) + THT_ROT_FIX.get(ref, 0)) % 360
    return [ref, f"{x:.3f}", f"{y:.3f}", "Top", f"{rot:.0f}"]


fab = os.path.join(bd, "fab")
base_bom = list(csv.reader(open(os.path.join(fab, "bom.csv"), encoding="utf-8")))
base_cpl = list(csv.reader(open(os.path.join(fab, "cpl.csv"), encoding="utf-8")))
VARIANTS = {
    "1-jlc-fits-everything": dict(drop=NOT_FITTED, add=True),
    "2-you-fit-terminals-headers": dict(drop=NOT_FITTED, add=False),
    "3-you-fit-all-THT": dict(drop=THT_BASE | NOT_FITTED, add=False),
}
for name, v in VARIANTS.items():
    d = os.path.join(out, name); os.makedirs(d, exist_ok=True)
    shutil.copy2(os.path.join(fab, "proptroller-gerbers.zip"), d)
    with open(os.path.join(d, "bom.csv"), "w", newline="", encoding="utf-8") as g:
        w = csv.writer(g, lineterminator="\n"); w.writerow(base_bom[0])
        for row in base_bom[1:]:
            refs = [x for x in row[1].split(",") if x not in v["drop"]]
            if refs:
                w.writerow([row[0], ",".join(refs), row[2], row[3]])
        if v["add"]:
            for c, fp, l, refs in ADD:
                w.writerow([c, ",".join(refs), fp, l])
    with open(os.path.join(d, "cpl.csv"), "w", newline="", encoding="utf-8") as g:
        w = csv.writer(g, lineterminator="\n"); w.writerow(base_cpl[0])
        for row in (list(r) for r in base_cpl[1:]):
            ref = row[0]
            if ref in v["drop"]:
                continue
            if ref in THT_BASE:
                row = tht_row(ref)
            elif ref in ROT_FIX:
                row[4] = str(int((float(row[4]) + ROT_FIX[ref]) % 360))
            w.writerow(row)
        if v["add"]:
            for _, _, _, refs in ADD:
                for r in refs:
                    w.writerow(tht_row(r))
    print(name, sum(1 for _ in open(os.path.join(d, "bom.csv"))) - 1, "BOM lines", sum(1 for _ in open(os.path.join(d, "cpl.csv"))) - 1, "placements")
