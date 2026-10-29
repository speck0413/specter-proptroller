"""Compare two closed meshes (STL) by vertical ray casting on a grid.

    meshcmp.py A.stl B.stl [--h 0.2] [--shift dx dy dz] [--axis z|x|y] [--top 12]
Per grid column the solid's inside intervals along the axis come from the sorted crossings; the XOR length of the two
sets of intervals, summed over columns times h^2, is the volume where the meshes differ. Prints volumes, bboxes, the
XOR volume and the worst clusters of difference (centre, size, volume, which side has extra material).
"""
import sys, struct, argparse
import numpy as np
from numba import njit

def load(path):
    b = open(path, 'rb').read()
    if b[:5] == b'solid' and b'facet' in b[:400]:
        v = []
        for line in b.decode('ascii', 'replace').splitlines():
            s = line.split()
            if s and s[0] == 'vertex': v.append([float(s[1]), float(s[2]), float(s[3])])
        return np.array(v, dtype=np.float64).reshape(-1, 3, 3)
    n = struct.unpack('<I', b[80:84])[0]
    a = np.frombuffer(b[84:84 + n * 50], dtype=np.dtype([('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')]))
    return a['v'].astype(np.float64)

def volume(t):
    return float(np.einsum('ij,ij->i', t[:, 0], np.cross(t[:, 1], t[:, 2])).sum() / 6)

@njit(cache=True)
def crossings(t, x0, y0, h, nx, ny, maxc):
    cnt = np.zeros((nx, ny), np.int32); zs = np.zeros((nx, ny, maxc)); sg = np.zeros((nx, ny, maxc), np.int8)
    for k in range(t.shape[0]):
        ax, ay, az = t[k, 0]; bx, by, bz = t[k, 1]; cx, cy, cz = t[k, 2]
        d = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
        if abs(d) < 1e-14: continue
        i0 = max(0, int(np.ceil((min(ax, bx, cx) - x0) / h - 0.5))); i1 = min(nx - 1, int(np.floor((max(ax, bx, cx) - x0) / h - 0.5)))
        j0 = max(0, int(np.ceil((min(ay, by, cy) - y0) / h - 0.5))); j1 = min(ny - 1, int(np.floor((max(ay, by, cy) - y0) / h - 0.5)))
        for i in range(i0, i1 + 1):
            px = x0 + (i + 0.5) * h + 1.234e-7
            for j in range(j0, j1 + 1):
                py = y0 + (j + 0.5) * h + 2.345e-7
                w1 = ((bx - px) * (cy - py) - (cx - px) * (by - py)) / d
                w2 = ((cx - px) * (ay - py) - (ax - px) * (cy - py)) / d
                w3 = 1 - w1 - w2
                if w1 < 0 or w2 < 0 or w3 < 0: continue
                c = cnt[i, j]
                if c < maxc:
                    zs[i, j, c] = w1 * az + w2 * bz + w3 * cz; sg[i, j, c] = 1 if d > 0 else -1
                cnt[i, j] = c + 1
    return cnt, zs, sg

@njit(cache=True)
def xor_len(ca, za, sa, cb, zb, sb, h, nx, ny, zmin, nz):
    """Per column: XOR length and A-only length; per z bin: exact A-only / B-only lengths."""
    out = np.zeros((nx, ny)); extra_a = np.zeros((nx, ny)); ha = np.zeros(nz); hb = np.zeros(nz)
    for i in range(nx):
        for j in range(ny):
            n = ca[i, j] + cb[i, j]
            if n == 0: continue
            ev = np.zeros(n); es = np.zeros(n, np.int8); ew = np.zeros(n, np.int8)
            m = 0
            for k in range(ca[i, j]):
                ev[m] = za[i, j, k]; es[m] = -sa[i, j, k]; ew[m] = 0; m += 1
            for k in range(cb[i, j]):
                ev[m] = zb[i, j, k]; es[m] = -sb[i, j, k]; ew[m] = 1; m += 1
            o = np.argsort(ev)
            wa = 0; wb = 0; last = 0.0
            for q in range(n):
                z = ev[o[q]]
                ia = wa > 0; ib = wb > 0
                if ia != ib and q > 0 and z > last:
                    out[i, j] += z - last
                    if ia: extra_a[i, j] += z - last
                    t = last
                    while t < z:
                        k = int((t - zmin) / h)
                        nxt = min(z, zmin + (k + 1) * h)
                        if nxt <= t: nxt = z
                        if 0 <= k < nz:
                            if ia: ha[k] += nxt - t
                            else: hb[k] += nxt - t
                        t = nxt
                if ew[o[q]] == 0: wa += es[o[q]]
                else: wb += es[o[q]]
                last = z
    return out, extra_a, ha, hb

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('a'); ap.add_argument('b'); ap.add_argument('--h', type=float, default=0.2)
    ap.add_argument('--shift', type=float, nargs=3, default=[0, 0, 0]); ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--ignore', type=float, default=0.0, help='skip clusters smaller than this volume')
    ap.add_argument('--axis', default='z')
    a = ap.parse_args()
    A = load(a.a); B = load(a.b) + np.array(a.shift)
    perm = {'z': [0, 1, 2], 'x': [1, 2, 0], 'y': [2, 0, 1]}[a.axis]
    A = A[:, :, perm]; B = B[:, :, perm]
    lo = np.minimum(A.reshape(-1, 3).min(0), B.reshape(-1, 3).min(0)) - 1; hi = np.maximum(A.reshape(-1, 3).max(0), B.reshape(-1, 3).max(0)) + 1
    h = a.h; nx, ny = int((hi[0] - lo[0]) / h) + 1, int((hi[1] - lo[1]) / h) + 1; nz = int((hi[2] - lo[2]) / h) + 1
    ca, za, sa = crossings(A, lo[0], lo[1], h, nx, ny, 64); cb, zb, sb = crossings(B, lo[0], lo[1], h, nx, ny, 64)
    out, ea, ha, hb = xor_len(ca, za, sa, cb, zb, sb, h, nx, ny, lo[2], nz)
    va, vb = volume(A), volume(B)
    inv = np.argsort(perm)
    print(f'A {a.a}: vol {abs(va):.2f}  bbox {np.round(A.reshape(-1,3).min(0)[inv],3)} .. {np.round(A.reshape(-1,3).max(0)[inv],3)}')
    print(f'B {a.b}: vol {abs(vb):.2f}  bbox {np.round(B.reshape(-1,3).min(0)[inv],3)} .. {np.round(B.reshape(-1,3).max(0)[inv],3)}')
    x = out.sum() * h * h; xa = ea.sum() * h * h
    print(f'XOR {x:.2f} mm3 ({100 * x / abs(va):.3f} % of A): A only {xa:.2f}, B only {x - xa:.2f}')
    import os
    thr = float(os.environ.get('ZHIST', '1e9'))
    zs = lo[2] + (np.arange(nz) + 0.5) * h
    for k in range(nz):
        if (ha[k] + hb[k]) * h * h > thr: print(f'   {"xyz"[perm[2]]} {zs[k]:7.2f}: A only {ha[k]*h*h:8.2f}  B only {hb[k]*h*h:8.2f}')
    from scipy import ndimage
    for arr, name in ((ea, 'A only'), (out - ea, 'B only')):
        lab, n = ndimage.label(arr > 1e-3)
        if not n: continue
        sizes = ndimage.sum(arr, lab, np.arange(1, n + 1)) * h * h
        objs = ndimage.find_objects(lab)
        for o in np.argsort(-sizes)[:a.top]:
            if sizes[o] < a.ignore: break
            sl = objs[o]
            p0 = (sl[0].start * h + lo[0], sl[1].start * h + lo[1]); p1 = (sl[0].stop * h + lo[0], sl[1].stop * h + lo[1])
            ax = 'xyz'
            print(f'  {name}: {sizes[o]:8.2f} mm3  {ax[perm[0]]} {p0[0]:.2f}..{p1[0]:.2f}  {ax[perm[1]]} {p0[1]:.2f}..{p1[1]:.2f}')

main()
