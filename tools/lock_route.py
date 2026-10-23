#!/usr/bin/env python3
"""Emit board.ts lock statements (b.track / b.via) for the routed copper of the given nets in a .kicad_pcb.
usage: tools/lock_route.py <pcb> <origin_x> <origin_y> NET [NET...]
Segments of one net and layer are chained into polylines; coordinates are board-local mm, rounded to 0.001."""
import re, sys

pcb, OX, OY, nets = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4:]
s = open(pcb).read()
R = lambda v: round(v, 3)
for net in nets:
    segs, vias = {}, []
    for m in re.finditer(r'\t\((segment|via)\n(.*?)\n\t\)', s, re.S):
        b = m.group(2)
        if f'(net "{net}")' not in b:
            continue
        if m.group(1) == "via":
            x, y = map(float, re.search(r'\(at ([-\d.]+) ([-\d.]+)', b).groups()); vias.append((R(x - OX), R(y - OY)))
            continue
        if "(locked yes)" in b:
            continue   # already authored
        a = tuple(R(float(v) - o) for v, o in zip(re.search(r'\(start ([-\d.]+) ([-\d.]+)', b).groups(), (OX, OY)))
        e = tuple(R(float(v) - o) for v, o in zip(re.search(r'\(end ([-\d.]+) ([-\d.]+)', b).groups(), (OX, OY)))
        if a == e:
            continue
        l = re.search(r'\(layer "([FB])', b).group(1); w = float(re.search(r'\(width ([-\d.]+)', b).group(1))
        segs.setdefault((l, w), []).append([a, e])
    out = []
    for (l, w), ss in segs.items():
        chains = []
        while ss:
            c = ss.pop()
            grown = True
            while grown:
                grown = False
                for i, t in enumerate(ss):
                    if t[0] == c[-1]: c.append(t[1])
                    elif t[1] == c[-1]: c.append(t[0])
                    elif t[1] == c[0]: c.insert(0, t[0])
                    elif t[0] == c[0]: c.insert(0, t[1])
                    else: continue
                    ss.pop(i); grown = True; break
            chains.append(c)
        for c in chains:
            out.append(f'b.track("{net}", "{l}", [{", ".join(f"[{x}, {y}]" for x, y in c)}], {w});')
    for v in vias:
        out.append(f'b.via("{net}", [{v[0]}, {v[1]}]);')
    print("\n".join(out))
