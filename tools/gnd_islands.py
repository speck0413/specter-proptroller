#!/usr/bin/env python3
"""Find GND copper not connected to the main GND net body in a routed .kicad_pcb and propose locked stitching vias.
usage: tools/gnd_islands.py <board.kicad_pcb> [net] [origin_x origin_y]
Islands: filled zone polygons (fractured, even-odd). Items: vias, pads, tracks of the net. Union-find on geometric contact.
For each component other than the largest, proposes a via centre inside one of its islands AND inside a main-component island on the
other layer, with a 0.55 mm circle fully inside both fills (via r 0.35 + margin), in board-local mm (origin subtracted).
"""
import re, sys, math
import numpy as np

pcb = sys.argv[1]; NET = sys.argv[2] if len(sys.argv) > 2 else "GND"
OX, OY = (float(sys.argv[3]), float(sys.argv[4])) if len(sys.argv) > 4 else (50.0, 50.0)
s = open(pcb).read()

def blocks(tag, text, indent):
    # top-level-ish s-expr blocks starting with "\n<indent>(tag"
    out = []
    for m in re.finditer(r'\n' + indent + r'\(' + tag + r'\b', text):
        i = m.start() + 1; d = 0; j = i
        while True:
            c = text[j]
            if c == '(':
                d += 1
            elif c == ')':
                d -= 1
                if d == 0:
                    break
            elif c == '"':
                j += 1
                while text[j] != '"':
                    j += 2 if text[j] == '\\' else 1
            j += 1
        out.append(text[i:j + 1])
    return out

islands = []   # (layer, Nx2 array)
for z in blocks("zone", s, "\t"):
    if f'(net "{NET}")' not in z and f'(net_name "{NET}")' not in z:
        continue
    for f in re.finditer(r'\(filled_polygon\s*\(layer "([^"]+)"\)\s*\(pts(.*?)\)\s*\)', z, re.S):
        pts = np.array([(float(a), float(b)) for a, b in re.findall(r'\(xy ([-\d.]+) ([-\d.]+)\)', f.group(2))])
        islands.append((f.group(1)[0], pts))

def pip(poly, P):
    """even-odd point in polygon, P: Mx2 -> bool M"""
    x, y = P[:, 0][:, None], P[:, 1][:, None]
    x1, y1 = poly[:, 0][None, :], poly[:, 1][None, :]
    x2, y2 = np.roll(poly[:, 0], -1)[None, :], np.roll(poly[:, 1], -1)[None, :]
    c = ((y1 > y) != (y2 > y)) & (x < (x2 - x1) * (y - y1) / np.where(y2 - y1 == 0, 1e-12, y2 - y1) + x1)
    return (c.sum(axis=1) % 2) == 1

bbox = [(p[:, 0].min(), p[:, 0].max(), p[:, 1].min(), p[:, 1].max()) for _, p in islands]

def isl_hit(layer, P):
    """indices of islands on layer containing any of points P"""
    hits = []
    for k, (l, poly) in enumerate(islands):
        if l != layer:
            continue
        b = bbox[k]
        m = (P[:, 0] >= b[0]) & (P[:, 0] <= b[1]) & (P[:, 1] >= b[2]) & (P[:, 1] <= b[3])
        if m.any() and pip(poly, P[m]).any():
            hits.append(k)
    return hits

def ring(x, y, r, n=16):
    a = np.linspace(0, 2 * math.pi, n, endpoint=False)
    return np.c_[x + r * np.cos(a), y + r * np.sin(a)]

items = []   # (kind, layers, points-for-contact, desc, shape for item-item contact)
for v in blocks("via", s, "\t"):
    if f'(net "{NET}")' not in v:
        continue
    x, y = map(float, re.search(r'\(at ([-\d.]+) ([-\d.]+)', v).groups()); d = float(re.search(r'\(size ([-\d.]+)', v).group(1))
    items.append(dict(kind="via", layers="FB", x=x, y=y, r=d / 2, pts=np.vstack([[x, y], ring(x, y, d / 2 + 0.05)]), desc=f"via {x - OX:.2f},{y - OY:.2f}"))
for sgm in blocks("segment", s, "\t"):
    if f'(net "{NET}")' not in sgm:
        continue
    x1, y1 = map(float, re.search(r'\(start ([-\d.]+) ([-\d.]+)', sgm).groups()); x2, y2 = map(float, re.search(r'\(end ([-\d.]+) ([-\d.]+)', sgm).groups())
    l = re.search(r'\(layer "([FB])', sgm).group(1); t = np.linspace(0, 1, 9)[:, None]
    items.append(dict(kind="trk", layers=l, a=(x1, y1), b=(x2, y2), w=float(re.search(r'\(width ([-\d.]+)', sgm).group(1)),
                      pts=np.c_[x1 + (x2 - x1) * t[:, 0], y1 + (y2 - y1) * t[:, 0]], desc=f"trk {l} {x1 - OX:.2f},{y1 - OY:.2f}->{x2 - OX:.2f},{y2 - OY:.2f}"))
for fp in blocks("footprint", s, "\t"):
    ref = re.search(r'\(property "Reference" "([^"]+)"', fp); ref = ref.group(1) if ref else "?"
    fx, fy, *fr = re.search(r'\(at ([-\d.]+) ([-\d.]+)( [-\d.]+)?\)', fp).groups(); fx, fy = float(fx), float(fy); fr = float(fr[0]) if fr and fr[0] else 0.0
    for pd in blocks("pad", fp, "\t\t"):
        if f'(net "{NET}")' not in pd:
            continue
        num = re.search(r'\(pad "([^"]*)" (\w+)', pd); pat = re.search(r'\(at ([-\d.]+) ([-\d.]+)( [-\d.]+)?\)', pd).groups(); px, py = float(pat[0]), float(pat[1]); pa = float(pat[2]) if pat[2] else 0.0
        w, h = map(float, re.search(r'\(size ([-\d.]+) ([-\d.]+)', pd).groups())
        a = math.radians(fr); ax = fx + px * math.cos(a) + py * math.sin(a); ay = fy - px * math.sin(a) + py * math.cos(a)
        lay = "FB" if num.group(2) in ("thru_hole",) else ("F" if '"F.Cu"' in pd else "B")
        r = max(w, h) / 2
        items.append(dict(kind="pad", layers=lay, x=ax, y=ay, r=r, rmin=min(w, h) / 2, w=w, h=h, pa=math.radians(pa), pts=np.vstack([[ax, ay], ring(ax, ay, min(w, h) / 2 + 0.1), ring(ax, ay, r + 0.1)]),
                          desc=f"{ref}.{num.group(1)} {ax - OX:.2f},{ay - OY:.2f}"))

N = len(islands) + len(items); par = list(range(N))
def find(i):
    while par[i] != i:
        par[i] = par[par[i]]; i = par[i]
    return i
def uni(i, j):
    par[find(i)] = find(j)

NI = len(islands)
for k, it in enumerate(items):
    for l in it["layers"]:
        for h in isl_hit(l, it["pts"]):
            uni(NI + k, h)
# item-item contacts: track endpoints on pads/vias/other track endpoints
def inpad(e, b):
    if b["kind"] == "via":
        return near(e, (b["x"], b["y"]), b["r"] + 0.01)
    dx, dy = e[0] - b["x"], e[1] - b["y"]; a = b["pa"]
    u = dx * math.cos(a) - dy * math.sin(a); v = dx * math.sin(a) + dy * math.cos(a)
    return abs(u) <= b["w"] / 2 + 0.01 and abs(v) <= b["h"] / 2 + 0.01
def near(p, q, d):
    return (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 <= d * d
for i, a in enumerate(items):
    if a["kind"] != "trk":
        continue
    for j, b in enumerate(items):
        if i == j:
            continue
        for e in (a["a"], a["b"]):
            if b["kind"] in ("via", "pad") and a["layers"] in b["layers"] and inpad(e, b):
                uni(NI + i, NI + j)
            elif b["kind"] == "trk" and b["layers"] == a["layers"] and (near(e, b["a"], 0.02) or near(e, b["b"], 0.02)):
                uni(NI + i, NI + j)

comps = {}
for i in range(N):
    comps.setdefault(find(i), []).append(i)
def area(p):
    return 0.5 * abs(np.dot(p[:, 0], np.roll(p[:, 1], 1)) - np.dot(p[:, 1], np.roll(p[:, 0], 1)))
main = max(comps.values(), key=lambda c: sum(area(islands[i][1]) for i in c if i < NI) + 0.001 * len(c))
mainset = set(main)
print(f"{len(islands)} islands, {len(items)} items, {len(comps)} components; main has {sum(1 for i in main if i < NI)} islands")
for root, c in comps.items():
    if c is main:
        continue
    isl = [i for i in c if i < NI]; its = [items[i - NI]["desc"] for i in c if i >= NI]
    print(f"-- component: islands {[(islands[i][0], round(bbox[i][0] - OX, 1), round(bbox[i][1] - OX, 1), round(bbox[i][2] - OY, 1), round(bbox[i][3] - OY, 1), round(area(islands[i][1]), 1)) for i in isl]} items {its[:6]}{'…' if len(its) > 6 else ''}")
    best = None
    for i in isl:
        l, poly = islands[i]; other = "B" if l == "F" else "F"
        b = bbox[i]
        xs = np.arange(b[0], b[1], 0.2); ys = np.arange(b[2], b[3], 0.2)
        if not len(xs) or not len(ys):
            continue
        G = np.array([(x, y) for x in xs for y in ys])
        G = G[pip(poly, G)]
        for x, y in G:
            P = np.vstack([[x, y], ring(x, y, 0.55)])
            if not pip(poly, P).all():
                continue
            for m in mainset:
                if m < NI and islands[m][0] == other:
                    mb = bbox[m]
                    if mb[0] <= x <= mb[1] and mb[2] <= y <= mb[3] and pip(islands[m][1], P).all():
                        # margin: grow until it leaves either fill
                        r = 0.55
                        while r < 2 and pip(poly, ring(x, y, r + 0.1)).all() and pip(islands[m][1], ring(x, y, r + 0.1)).all():
                            r += 0.1
                        if not best or r > best[0]:
                            best = (r, x, y)
                        break
    if best:
        print(f"   via at [{best[1] - OX:.2f}, {best[2] - OY:.2f}]  (clear radius {best[0]:.2f})")
    else:
        print("   no via spot: needs a track")
