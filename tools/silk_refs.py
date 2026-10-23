#!/usr/bin/env python3
"""Put reference designators on the silkscreen of the generated board, where they stay readable with the parts fitted.

aieda v0.1.0 writes every reference to the fab layer and its DSL has no switch for it, so this runs as a step after
`bin/aieda build` (Makefile). It only moves the reference text of existing footprints; copper, parts and nets are untouched.
For each part, candidate spots around its courtyard (above, below, left, right; centred and at either end) are tried in order;
the first that clears every pad (+0.2 mm), every part body (fab outline: bodies hide silk in the 3D view and in real life),
board texts, labels already placed, the board edge and internal slots (0.5 mm) wins. Parts with no spot keep their reference on the fab
layer and are listed. Holes and fiducials are skipped.
usage: tools/silk_refs.py [board.kicad_pcb]"""
import os, sys
import pcbnew

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "hardware", "kicad", "proptroller.kicad_pcb")
SIZE, THICK, GAP, PAD_CLR, EDGE = 0.8, 0.15, 0.2, 0.2, 0.5
mm = pcbnew.FromMM
b = pcbnew.LoadBoard(path)
edge = b.GetBoardEdgesBoundingBox()
edge.Inflate(-mm(EDGE))


def box(bb, grow=0.0):
    bb = pcbnew.BOX2I(bb.GetOrigin(), bb.GetSize())
    if grow:
        bb.Inflate(mm(grow))
    return bb


def court(fp, layer):
    """The part's body: its fab-layer outline (what hides silk in real life and in 3D), else its courtyard."""
    fab = pcbnew.F_Fab if layer == pcbnew.F_CrtYd else pcbnew.B_Fab
    bb = None
    for g in fp.GraphicalItems():
        if g.GetLayer() == fab and not isinstance(g, pcbnew.PCB_TEXT):
            gb = g.GetBoundingBox()
            if bb is None:
                bb = box(gb)
            else:
                bb.Merge(gb)
    if bb is not None:
        return bb
    poly = fp.GetCourtyard(layer)
    if poly.OutlineCount():
        return box(poly.BBox())
    return box(fp.GetBoundingBox(False))


skip = lambda fp: fp.GetReference().startswith(("H", "FID")) or fp.GetReference() in ("REF**",)
obst = {pcbnew.F_SilkS: [], pcbnew.B_SilkS: []}
for fp in b.GetFootprints():
    if fp.IsFlipped():
        obst[pcbnew.B_SilkS].append(court(fp, pcbnew.B_CrtYd))
    else:
        obst[pcbnew.F_SilkS].append(court(fp, pcbnew.F_CrtYd))
    for p in fp.Pads():
        for side, cu in ((pcbnew.F_SilkS, pcbnew.F_Cu), (pcbnew.B_SilkS, pcbnew.B_Cu)):
            if p.IsOnLayer(cu):
                obst[side].append(box(p.GetBoundingBox(), PAD_CLR))
for d in b.GetDrawings():
    if d.GetLayer() in obst and isinstance(d, pcbnew.PCB_TEXT):
        obst[d.GetLayer()].append(box(d.GetBoundingBox(), 0.1))
# internal cutouts (b.slot air gaps): Edge.Cuts shapes inside the outline keep labels 0.5 mm off, like the board edge
outline = b.GetBoardEdgesBoundingBox()
for d in b.GetDrawings():
    if d.GetLayer() == pcbnew.Edge_Cuts:
        bb = d.GetBoundingBox()
        if bb.GetWidth() < outline.GetWidth() * 0.5 and bb.GetHeight() < outline.GetHeight() * 0.5:
            for side in obst:
                obst[side].append(box(bb, EDGE))

placed, missed = 0, []
for fp in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
    if skip(fp):
        continue
    front = not fp.IsFlipped()
    silk, cy = (pcbnew.F_SilkS, pcbnew.F_CrtYd) if front else (pcbnew.B_SilkS, pcbnew.B_CrtYd)
    ref = fp.Reference()
    ref.SetLayer(silk); ref.SetVisible(True); ref.SetKeepUpright(True)
    ref.SetTextSize(pcbnew.VECTOR2I(mm(SIZE), mm(SIZE))); ref.SetTextThickness(mm(THICK))
    ref.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER); ref.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    c = court(fp, cy)
    x0, y0, x1, y1 = c.GetX(), c.GetY(), c.GetRight(), c.GetBottom()
    cx, cy_ = (x0 + x1) // 2, (y0 + y1) // 2
    cands = []
    for gap, ang in [(mm(g), a) for g in (GAP, 0.6, 1.2, 2.0) for a in (0, 90)]:
        ref.SetTextAngleDegrees(ang); ref.SetPosition(pcbnew.VECTOR2I(cx, cy_))
        tb = ref.GetBoundingBox(); w, hh = tb.GetWidth(), tb.GetHeight()
        ox, oy = tb.GetCenter().x - cx, tb.GetCenter().y - cy_   # text box centre vs anchor
        at = lambda tx, ty: (int(tx - ox), int(ty - oy))
        if ang == 0:   # above / below, centred then towards either end
            for ty in (y0 - gap - hh // 2, y1 + gap + hh // 2):
                for tx in (cx, x0 + w // 2, x1 - w // 2):
                    cands.append((ang, at(tx, ty)))
        else:          # left / right
            for tx in (x0 - gap - w // 2, x1 + gap + w // 2):
                for ty in (cy_, y0 + hh // 2, y1 - hh // 2):
                    cands.append((ang, at(tx, ty)))
    ok = False
    for ang, (x, y) in cands:
        ref.SetTextAngleDegrees(ang)
        ref.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))
        tb = box(ref.GetBoundingBox())
        if not edge.Contains(tb.GetOrigin()) or not edge.Contains(tb.GetEnd()):
            continue
        if any(tb.Intersects(o) for o in obst[silk]):
            continue
        obst[silk].append(box(tb, 0.15)); ok = True; placed += 1
        break
    if not ok:
        ref.SetLayer(pcbnew.F_Fab if front else pcbnew.B_Fab); missed.append(fp.GetReference())
b.Save(path)
print(f"silk_refs: {placed} references on the silkscreen; {len(missed)} without a clear spot (left on the fab layer): {' '.join(missed)}")
