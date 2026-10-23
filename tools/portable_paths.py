#!/usr/bin/env python3
"""Make the generated KiCad project portable before it is committed: aieda writes resolved absolute paths (the 3D model library and
this checkout's directory) into the board, netlist and build records. Rewrites the model library prefix to ${KICAD10_3DMODEL_DIR}
and the checkout path to a relative one. Paths only; no design data changes. Run after `bin/aieda build` and `bin/aieda fab`.
usage: tools/portable_paths.py [kicad dir]   (default hardware/kicad)"""
import os, sys

root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
kdir = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "hardware", "kicad"))
lib3d = os.environ.get("KICAD10_3DMODEL_DIR") or os.environ.get("KICAD9_3DMODEL_DIR") or os.path.expanduser("~/kicad3d/3dmodels")
subs = [(os.path.abspath(lib3d).rstrip("/"), "${KICAD10_3DMODEL_DIR}"), (kdir, "."), (root, "../..")]
n = 0
for name in sorted(os.listdir(kdir)):
    if not name.endswith((".kicad_pcb", ".kicad_sch", ".kicad_pro", ".xml", ".json")):
        continue
    p = os.path.join(kdir, name)
    s = open(p, encoding="utf-8").read(); t = s
    for a, b in subs:
        t = t.replace(a, b)
    if t != s:
        open(p, "w", encoding="utf-8").write(t); n += 1
        print(f"portable_paths: {name}")
home = os.path.expanduser("~")
left = [f for f in os.listdir(kdir) if f.endswith((".kicad_pcb", ".kicad_sch", ".kicad_pro", ".xml", ".json")) and home in open(os.path.join(kdir, f), encoding="utf-8").read()]
if left:
    sys.exit(f"portable_paths: absolute home paths remain in {left}")
