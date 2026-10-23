#!/usr/bin/env python3
"""Will it print without supports? Two checks on STLs in their print pose (lowest z = 0).

1. Layer islands (the check that counts; exit status 1 if any part has one). The part is sliced every 0.2 mm, as the
   printer lays it, and each layer is rasterised on a 0.1 mm grid. A pixel is supported if the layer below has material
   within `--allow` (0.3 mm: a 45-degree wall moves 0.2 mm a layer) that is itself held up, all the way down to the
   bed: a layer printed on air holds nothing up, so the layers on it are islands too. What's left is grouped into regions, each one:
     ledge    every pixel within `--ledge` (0.5 mm) of supported material in the same layer: prints as a small overhang
     bridge   every pixel lies on a straight line through the layer's material that ends on supported material at both
              ends, at most `--span` (30 mm) long: the X1C bridges it. Reported with its longest span
     ISLAND   anything else: a layer region resting on air (a cantilever, or a strip whose support starts higher up).
              It needs supports, so the part fails.
   A face can be exactly 45 degrees and still float: what matters is whether each layer stands on the one below.

2. Down-facing faces steeper than 45 degrees (`--faces`), grouped by height, with area and extent: the old check, kept
   for reading the bridges' and ledges' surfaces.

    python3 tools/overhangs.py stl/*.stl          # islands; exit 1 if any
    python3 tools/overhangs.py --faces stl/tub.stl
Needs numpy only.
"""
import argparse, math, struct, sys
import numpy as np

LAYER, RES = 0.2, 0.1


def load(p):
    b = open(p, 'rb').read()
    if b[:5] == b'solid' and b'facet' in b[:300]:
        tok = b.split()
        idx = [i for i, t in enumerate(tok) if t == b'vertex']
        v = np.array([[float(tok[i + 1]), float(tok[i + 2]), float(tok[i + 3])] for i in idx])
        return v.reshape(-1, 3, 3)
    n = struct.unpack('<I', b[80:84])[0]
    a = np.frombuffer(b, dtype=np.uint8, count=n * 50, offset=84).reshape(n, 50)
    return a[:, 12:48].copy().view('<f4').reshape(n, 3, 3).astype(float)


def section(T, z, x0, y0, W, H):
    """even-odd raster of the part's cross-section at height z: H x W bools, pixel centres x0 + (i + 0.5) RES"""
    zs = T[:, :, 2]
    t = T[(zs.min(1) < z) & (zs.max(1) > z)]
    if not len(t): return np.zeros((H, W), bool)
    zt = t[:, :, 2]
    P = np.zeros((len(t), 2, 2)); k = np.zeros(len(t), int)
    for a, b in ((0, 1), (1, 2), (2, 0)):   # each edge that crosses z gives one end of the triangle's segment
        za, zb = zt[:, a], zt[:, b]
        c = (za < z) != (zb < z)
        f = np.where(c, (z - za) / np.where(c, zb - za, 1), 0)
        q = t[:, a, :2] + f[:, None] * (t[:, b, :2] - t[:, a, :2])
        sel = c & (k < 2)
        P[sel, k[sel]] = q[sel]; k[sel] += 1
    P = P[k == 2]
    lo, hi = np.minimum(P[:, 0, 1], P[:, 1, 1]), np.maximum(P[:, 0, 1], P[:, 1, 1])
    r0 = np.clip(np.ceil((lo - y0) / RES - 0.5).astype(int), 0, H)   # rows whose centre yc has lo <= yc < hi
    r1 = np.clip(np.ceil((hi - y0) / RES - 0.5).astype(int), 0, H)
    n = r1 - r0
    keep = n > 0
    P, r0, n = P[keep], r0[keep], n[keep]
    seg = np.repeat(np.arange(len(P)), n)
    rows = np.repeat(r0, n) + (np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n))
    yc = y0 + (rows + 0.5) * RES
    pa, pb = P[seg, 0], P[seg, 1]
    x = pa[:, 0] + (yc - pa[:, 1]) * (pb[:, 0] - pa[:, 0]) / (pb[:, 1] - pa[:, 1])
    cols = np.clip(np.floor((x - x0) / RES + 0.5).astype(int), 0, W)
    tog = np.zeros((H, W + 1), np.int32)
    np.add.at(tog, (rows, cols), 1)
    return (np.cumsum(tog, axis=1)[:, :W] & 1).astype(bool)


def dilate(A, r):
    out = A.copy(); R = int(math.ceil(r))
    for dy in range(-R, R + 1):
        for dx in range(-R, R + 1):
            if dx * dx + dy * dy > r * r or (dx == 0 and dy == 0): continue
            out[max(dy, 0):A.shape[0] + min(dy, 0), max(dx, 0):A.shape[1] + min(dx, 0)] |= \
                A[max(-dy, 0):A.shape[0] + min(-dy, 0), max(-dx, 0):A.shape[1] + min(-dx, 0)]
    return out


def components(mask):   # 8-connected regions of a bool image: list of (rows, cols) arrays
    ys, xs = np.nonzero(mask)
    if not len(ys): return []
    W = mask.shape[1]
    pos = {int(y) * W + int(x): k for k, (y, x) in enumerate(zip(ys, xs))}
    parent = list(range(len(ys)))
    def find(a):
        while parent[a] != a: parent[a] = parent[parent[a]]; a = parent[a]
        return a
    for k, (y, x) in enumerate(zip(ys, xs)):
        for dy, dx in ((0, 1), (1, -1), (1, 0), (1, 1)):
            if not 0 <= x + dx < W: continue
            j = pos.get(int(y + dy) * W + int(x + dx))
            if j is not None:
                a, b = find(k), find(j)
                if a != b: parent[a] = b
    groups = {}
    for k in range(len(ys)): groups.setdefault(find(k), []).append(k)
    return [(ys[g], xs[g]) for g in map(np.array, groups.values())]


DIRS = [(0, 1), (1, 0), (1, 1), (1, -1)]


def reach(cur, sup, ys, xs, d, nmax):
    """distance (pixels) along direction d from each pixel, through the layer's material, to the first supported pixel
    (inf if air or the grid edge comes first, or none within nmax steps)"""
    H, W = cur.shape
    out = np.full(len(ys), np.inf); live = np.ones(len(ys), bool)
    for k in range(1, nmax + 1):
        y, x = ys + k * d[0], xs + k * d[1]
        inside = (y >= 0) & (y < H) & (x >= 0) & (x < W)
        yy, xx = np.where(inside, y, 0), np.where(inside, x, 0)
        mat = inside & cur[yy, xx]
        hit = live & mat & sup[yy, xx]
        out[hit] = k * math.hypot(*d); live &= ~hit & mat
        if not live.any(): break
    return out


def islands(p, allow, ledge, span):
    T = load(p)
    T = T - [0, 0, T[:, :, 2].min()]
    lo, hi = T[:, :, :2].reshape(-1, 2).min(0) - 1, T[:, :, :2].reshape(-1, 2).max(0) + 1
    W, H = int((hi[0] - lo[0]) / RES) + 1, int((hi[1] - lo[1]) / RES) + 1
    nl = int(math.ceil(T[:, :, 2].max() / LAYER - 1e-6))
    prev, found = None, []   # found: (kind, z, area mm2, measure, bbox)
    nb = int(math.ceil(span / RES)) + 1
    for i in range(nl):
        cur = section(T, (i + 0.5) * LAYER + 0.0137, lo[0], lo[1], W, H)
        if prev is None: prev = cur; continue   # the first layer is on the bed
        cand = cur & ~prev
        held = cur & ~cand                         # this layer's material that stands on held material below
        if cand.any():
            ys, xs = np.nonzero(cand)
            R = int(math.ceil(max(allow, ledge, span) / RES)) + 2
            y0, y1 = max(ys.min() - R, 0), min(ys.max() + R + 1, H)
            x0, x1 = max(xs.min() - R, 0), min(xs.max() + R + 1, W)
            c, pv = cur[y0:y1, x0:x1], prev[y0:y1, x0:x1]
            sup = c & dilate(pv, allow / RES)
            for cy, cx in components(c & ~sup):
                area = len(cy) * RES * RES
                best = np.full(len(cy), np.inf); brg = np.full(len(cy), np.inf)
                for d in DIRS:
                    a = reach(c, sup, cy, cx, d, nb); b = reach(c, sup, cy, cx, (-d[0], -d[1]), nb)
                    best = np.minimum(best, np.minimum(a, b)); brg = np.minimum(brg, a + b)
                ok_l, ok_b = best * RES <= ledge, brg * RES <= span
                bb = (lo[0] + (x0 + cx.min()) * RES, lo[0] + (x0 + cx.max() + 1) * RES,
                      lo[1] + (y0 + cy.min()) * RES, lo[1] + (y0 + cy.max() + 1) * RES)
                if ok_l.all(): kind, meas = 'ledge', best.max() * RES
                elif (ok_l | ok_b).all(): kind, meas = 'bridge', brg[~ok_l].max() * RES
                else: kind, meas = 'ISLAND', (~(ok_l | ok_b)).sum() * RES * RES
                if kind != 'ISLAND': held[y0 + cy, x0 + cx] = True
                if kind == 'ledge' and area < 0.05: continue
                found.append((kind, round(i * LAYER, 2), area, meas, bb))
            held[y0:y1, x0:x1] |= sup
        prev = held   # only what is held up supports the next layer: a strip printed on air holds nothing up
    return found, nl


def merge(fs):   # one line per run of consecutive layers over the same place
    out = []
    for f in sorted(fs, key=lambda f: (round(f[4][0]), round(f[4][2]), f[1])):
        for o in out:
            if o[0] == f[0] and abs(o[5][0] - f[4][0]) < 3 and abs(o[5][2] - f[4][2]) < 3 and 0 < f[1] - o[2] <= 0.41:
                o[2] = f[1]; o[3] += f[2]; o[4] = max(o[4], f[3])
                o[5] = (min(o[5][0], f[4][0]), max(o[5][1], f[4][1]), min(o[5][2], f[4][2]), max(o[5][3], f[4][3]))
                break
        else: out.append([f[0], f[1], f[1], f[2], f[3], f[4]])
    return out


def report(p, found, nl, verbose):
    isl = [f for f in found if f[0] == 'ISLAND']
    br = [f for f in found if f[0] == 'bridge']; le = [f for f in found if f[0] == 'ledge']
    print(f'{p}: {nl} layers: {"FAIL" if isl else "ok"}, {len(isl)} unsupported island layer-regions, {len(br)} bridge'
          + (f' (longest span {max(f[3] for f in br):.1f} mm)' if br else '') + f', {len(le)} small ledges')
    for kind, fs in (('ISLAND', isl), ('bridge', br), ('ledge', le)):
        if kind == 'ledge' and not verbose: continue
        for k, z0, z1, a, m, bb in sorted(merge(fs), key=lambda o: o[1]):
            what = f'{m:.1f} mm2 on air' if k == 'ISLAND' else f'span {m:.1f} mm' if k == 'bridge' else f'reach {m:.2f} mm'
            print(f'   {k:6s} z {z0:5.1f}..{z1:5.1f}  {a:8.1f} mm2, {what:18s} x {bb[0]:7.1f}..{bb[1]:7.1f}  y {bb[2]:7.1f}..{bb[3]:7.1f}')
    return len(isl)


def faces(p):
    T = load(p); z0 = T[:, :, 2].min()
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); L = np.linalg.norm(n, axis=1); L[L == 0] = 1
    m = (n[:, 2] / L <= -math.cos(math.radians(45)) - 1e-6) & (T[:, :, 2].min(1) - z0 >= 0.05)
    print(f'{p}: down-facing faces steeper than 45 degrees', 'none' if not m.any() else '')
    zk = np.round(T[m][:, :, 2].min(1), 1); A = L[m] / 2; t = T[m]
    for k in np.unique(zk):
        s = zk == k
        if A[s].sum() < 0.5: continue
        v = t[s].reshape(-1, 3)
        print(f'   z {k:6.1f}: {A[s].sum():8.1f} mm2 over x {v[:, 0].min():.1f}..{v[:, 0].max():.1f} y {v[:, 1].min():.1f}..{v[:, 1].max():.1f}')


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('stl', nargs='+')
    ap.add_argument('--faces', action='store_true', help='list down-facing faces steeper than 45 degrees instead')
    ap.add_argument('--allow', type=float, default=0.3, help='mm a layer may stand past the one below (default 0.3)')
    ap.add_argument('--ledge', type=float, default=0.5, help='mm a small overhang may reach from support (default 0.5)')
    ap.add_argument('--span', type=float, default=30.0, help='longest bridge, mm (default 30)')
    ap.add_argument('-v', action='store_true', help='also list the small ledges')
    a = ap.parse_args()
    bad = 0
    for p in a.stl:
        if a.faces: faces(p); continue
        f, nl = islands(p, a.allow, a.ledge, a.span)
        bad += report(p, f, nl, a.v) > 0
    if bad: print(f'FAIL: {bad} part(s) with unsupported islands'); sys.exit(1)
