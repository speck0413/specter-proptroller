#!/usr/bin/env python3
"""Write the board facts the wiring-guide scene needs: connector pad positions (board mm, x from the left edge,
y from the back edge) from the KiCad board, and the part boxes from enclosure/board-parts.json.

Usage: board_data.py <proptroller.kicad_pcb> <board-parts.json> <out.json>   (needs KiCad's pcbnew module)
"""
import json, sys
import pcbnew

pcb, parts, out = sys.argv[1:4]
b = pcbnew.LoadBoard(pcb)
bb = b.GetBoardEdgesBoundingBox()
ox, oy = bb.GetX(), bb.GetY()
mm = lambda v: round(v / 1e6, 2)
pads = {}
for f in b.GetFootprints():
    r = f.GetReference()
    if r.startswith(('J_', 'SW_', 'K', 'H', 'D_LO', 'D_RL', 'TP')):
        pads[r] = {p.GetNumber() or 'M': [mm(p.GetPosition().x - ox), mm(p.GetPosition().y - oy)] for p in f.Pads()}
data = {"board": [mm(bb.GetWidth()), mm(bb.GetHeight()), 1.6], "pads": pads,
        "parts": json.load(open(parts))}
json.dump(data, open(out, 'w'), separators=(',', ':'))
print(out, len(pads), 'footprints', len(data["parts"]), 'boxes')
