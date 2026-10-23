#!/usr/bin/env python3
"""Faceplate bending check: how far does a faceplate lift off its gasket between its screws?

A finite-element plate model (Mindlin, 4-node elements on a 1 mm grid, numpy + scipy) of each faceplate from
params.json: its outline (for mount = "screws": the taller plate, notched under the back wall's hinges) and jack holes,
the seal groove (thinner plate along the ring), the wall behind it (with its windows) and what holds it: the screws
print_params.py placed, or for comparison the same rows at other pitches and the drop-in layout the owner proposed
(bottom edge in a channel, both ends in vertical grooves, two screws at the top; evaluated, not built). The gasket sits
in the groove and stands proud of it (a gland): the plate is screwed down flat on the wall, so the gasket's squeeze is set
by the groove, as long as the plate stays on the wall. The gasket pushes the plate off the wall between the screws; the
check is how far it lifts (contact with the wall solved one-sided: the wall pushes, never pulls). Lift within the squeeze
keeps it sealed; under ~10 % keeps it evenly squeezed. (A wet or non-curing sealant isn't modelled: it fills the gap.)

    ~/.venvs/cpsat/bin/python tools/plate_fea.py              # the screws as built (params.json: design.toml's case)
    ~/.venvs/cpsat/bin/python tools/plate_fea.py --compare    # and the "pitch" layout at 25 / 50 / 75 mm, and the drop-in
    ~/.venvs/cpsat/bin/python tools/plate_fea.py --params stl/v/ac/params.json   # the AC case (after tools/print_build.sh)

    ~/.venvs/cpsat/bin/python tools/plate_fea.py --seals [--params ...]   # the screwed seal options, and --fill: how each fills

The screwed plate's seal (owner 2026-10-20): the groove sg_w x sg_d (3.5 x 1.6) along the screw rows, bending inward round
each screw (walls[face]['seal'], seal_sd); the glued plate's 2 x 1 ring is not modelled (bonded all over).

Results, --seals (2026-10-20, six screws as built; the share of the squeeze left, 100 % flat):
                  tubing 2.4 OD   TPU lip (gasket_h 2.4, 0.8 mm squeeze), E 9 / 25: sliding, friction 0.5, stuck (0.8)
  standard front  43 %            96 / 91 %, 94 / 84 %, 88 / 72 %          solid strip -25 / -52 %, hollow hexagon 86 / 67 %
  back            39 %            96 / 90 %, 93 / 82 %, 87 / 70 %          solid -32 / -60 %, hollow 84 / 64 %   (hinges at 1/3, 2/3)
  AC front        16 %            91 / 79 %, 85 / 66 %, 74 / 48 %          solid -50 / -81 %, hollow 70 / 41 %
Butyl tape and wet silicone fill the gap and push nothing back (no lift); --fill: a 2 x 3.2 mm butyl strip fills 114 % of
the groove, a 3 mm butyl rope 126 %, the TPU lip 40 %. Section push N/mm at 0.8 mm, E 9 / 25: lip 0.017 / 0.046 sliding,
0.030 / 0.084 at friction 0.5, 0.059 / 0.164 stuck; hollow hexagon 0.07 / 0.21; solid strip 8 / 22.

Before (the 2 x 1 ring inside the screws, 0.5 mm squeeze, lift mm for tubing 0.3 / cord 1 / cured bead 1.25-2.5 N/mm):
  standard front 0.25 / 0.43 / 0.46-0.52, back 0.27 / 0.45 / 0.48-0.55, AC front 0.38 / 0.50 / 0.52-0.58. A cured bead
lifts the plate by about its whole squeeze between six screws, a taller one too (--bead: 0.79-1.07 mm against 1 mm).

The section model (section_k): a plane-strain FE of each profile, TPU 95A at E 9 MPa (Bambu's TPU 95A data sheet, 9.2 MPa
ISO 527 XY, as search results quoted it: the PDF wasn't retrievable) and 25 MPa (other 95A filaments, and the stiffness
at small strain), the tip sliding freely on the wall, against friction 0.5, or 0.8 (stuck). Linear at 30-40 % strain.
Left out: the wall's own flex and flatness (0.2-0.4 mm bow as printed), creep, softening when warm.

Numbers: ASA as printed E = 2000 MPa (a 3 mm plate, 6 walls), nu 0.35. A 2 mm cord squeezed 0.5 mm (25 %) pushes
about 1 N per mm of length (Parker O-ring handbook, 70 Shore A); the stiff case doubles it. TPU nu 0.45.
"""
import json, math, os, sys
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spl

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# --params stl/v/ac/params.json: another case's params (tools/print_build.sh writes stl/v/<dc|ac>/params.json)
PJ = sys.argv[sys.argv.index('--params') + 1] if '--params' in sys.argv else os.path.join(ROOT, 'params.json')
D_ = json.load(open(PJ))
P, WALLS, OU = D_['P'], D_['walls'], D_['parts']['outlet']
E, NU, H = 2000.0, 0.35, 1.0   # ASA as printed (6 walls, 25 % grid): about 2000 MPa
if '--plate-t' in sys.argv: P['plate_t'] = float(sys.argv[sys.argv.index('--plate-t') + 1])   # what a thicker plate would do
HOLE = {'j35p': P['hole_j35p'], 'dcp': P['hole_dcp'], 'usb': P['hole_usb']}
def in_hole(c, x, y):   # c = [id, kind, x, y]: in the plate's cutout for that connector
    if c[1] != 'outlet': return math.hypot(x - c[2], y - c[3]) < HOLE[c[1]] / 2
    fw, fh = OU['face_h'] / 2 + OU['open_m'], OU['face_w'] / 2 + OU['open_m']
    return any(abs(x - c[2] - s_ * OU['face_pitch'] / 2) < fw and abs(y - c[3]) < fh for s_ in (-1, 1))

def ke_q4(t, a, b):   # Mindlin Q4, dofs per node (w, bx, by); bending 2x2 Gauss, shear 1x1 (no locking)
    Db = E * t**3 / (12 * (1 - NU**2)) * np.array([[1, NU, 0], [NU, 1, 0], [0, 0, (1 - NU) / 2]])
    Ds = 5 / 6 * E / (2 * (1 + NU)) * t * np.eye(2)
    xi_n, eta_n = np.array([-1, 1, 1, -1]), np.array([-1, -1, 1, 1])
    K = np.zeros((12, 12))
    def grads(xi, eta):
        N = (1 + xi * xi_n) * (1 + eta * eta_n) / 4
        dx = xi_n * (1 + eta * eta_n) / 4 / (a / 2); dy = eta_n * (1 + xi * xi_n) / 4 / (b / 2)
        return N, dx, dy
    g = 1 / math.sqrt(3)
    for xi in (-g, g):
        for eta in (-g, g):
            N, dx, dy = grads(xi, eta); Bb = np.zeros((3, 12))
            Bb[0, 1::3] = dx; Bb[1, 2::3] = dy; Bb[2, 1::3] = dy; Bb[2, 2::3] = dx
            K += Bb.T @ Db @ Bb * (a * b / 4)
    N, dx, dy = grads(0, 0); Bs = np.zeros((2, 12))
    Bs[0, 0::3] = dx; Bs[0, 1::3] = -N; Bs[1, 0::3] = dy; Bs[1, 2::3] = -N
    return K + Bs.T @ Ds @ Bs * (a * b)

def seal_sd(face):
    """the screwed plate's seal (params walls[face]['seal'], README "Sealing a screwed plate"): a signed distance from the
    edge of the region E it runs round (negative inside E), on a 0.1 mm raster; the groove is |sd| <= sg_w / 2"""
    from scipy.ndimage import distance_transform_edt
    w = WALLS[face]; sl = w['seal']; r_ = 0.1
    xa, ya = w['x0'] - 2, w['pf_s'][0] - 2
    X, Y = np.meshgrid(xa + r_ * (np.arange(int((w['x1'] - w['x0'] + 4) / r_)) + 0.5), ya + r_ * (np.arange(int((w['pf_s'][1] - w['pf_s'][0] + 4) / r_)) + 0.5), indexing='ij')
    R = sl['rect']; E = (X >= R[0]) & (X <= R[1]) & (Y >= R[2]) & (Y <= R[3])
    for d in sl['discs']: E &= np.hypot(X - d[0], Y - d[1]) >= d[2]
    for c in sl['cuts']: E &= ~((X > c[0]) & (X < c[1]) & (Y > c[2]))
    sd = (distance_transform_edt(~E) - distance_transform_edt(E)) * r_
    def f(x, y):
        i = min(max(int((x - xa) / r_), 0), sd.shape[0] - 1); j = min(max(int((y - ya) / r_), 0), sd.shape[1] - 1)
        return sd[i, j]
    return f, float((np.abs(sd) <= P['sg_w'] / 2).sum() * r_ * r_ / P['sg_w'])   # and the loop's length

def model(face, screws, groove_d, mount='screws'):
    w = WALLS[face]; x0, x1 = w['x0'], w['x1']; y0, y1 = w['pf_s'] if mount == 'screws' else w['pf']
    top_n = w['pf'][1]                                  # under the hinge notches the plate stops at the glue plate's top
    nx, ny = int(round((x1 - x0) / H)), int(round((y1 - y0) / H))
    hx, hy = (x1 - x0) / nx, (y1 - y0) / ny
    xs, ys = x0 + hx * np.arange(nx + 1), y0 + hy * np.arange(ny + 1)
    pw = P['pg_w']; r = w['ring']
    rx0, rx1, ry0, ry1 = r[0] + pw / 2, r[1] - pw / 2, r[2] + pw / 2, r[3] - pw / 2   # the ring's centreline
    def on_ring(x, y, tol):
        inside = rx0 - tol <= x <= rx1 + tol and ry0 - tol <= y <= ry1 + tol
        return inside and min(abs(x - rx0), abs(x - rx1), abs(y - ry0), abs(y - ry1)) <= tol
    ringlen = 2 * ((rx1 - rx0) + (ry1 - ry0))
    if mount == 'screws' and 'seal' in w:   # the screwed plate's seal: the wider loop on the screw rows, round each screw
        sdf, ringlen = seal_sd(face); gw = P['sg_w']
        on_ring = lambda x, y, tol: abs(sdf(x, y)) <= (gw / 2 if tol >= gw / 2 - 1e-9 else tol)
        pw = gw
    notched = lambda x, y: mount == 'screws' and y > top_n and any(n[0] < x < n[1] for n in w['notches'])
    elems = []
    for i in range(nx):
        for j in range(ny):
            cx, cy = xs[i] + hx / 2, ys[j] + hy / 2
            if any(in_hole(c, cx, cy) for c in w['conns']) or notched(cx, cy): continue
            t = P['plate_t'] - (groove_d if on_ring(cx, cy, pw / 2) else 0)
            elems.append((i, j, t))
    kes = {t: ke_q4(t, hx, hy) for t in {e[2] for e in elems}}
    nid = lambda i, j: i * (ny + 1) + j
    rows, cols, vals = [], [], []
    used = set()
    for i, j, t in elems:
        ns = [nid(i, j), nid(i + 1, j), nid(i + 1, j + 1), nid(i, j + 1)]; used.update(ns)
        dofs = np.array([[3 * n, 3 * n + 1, 3 * n + 2] for n in ns]).ravel()
        r, c = np.meshgrid(dofs, dofs, indexing='ij'); rows += [r.ravel()]; cols += [c.ravel()]; vals += [kes[t].ravel()]
    ndof = 3 * (nx + 1) * (ny + 1)
    K = sp.csr_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))), shape=(ndof, ndof))
    used = sorted(used)
    XY = {n: (xs[n // (ny + 1)], ys[n % (ny + 1)]) for n in used}
    ring = [n for n in used if on_ring(*XY[n], 0.51 * max(hx, hy))]
    lpn = ringlen / len(ring)                          # ring length per ring node
    scr = [n for n in used if any(math.hypot(XY[n][0] - s[0], XY[n][1] - s[1]) <= 2.5 for s in screws if len(s) == 2)
           or any(abs(XY[n][0] - s[0]) <= 0.51 * hx and abs(XY[n][1] - s[1]) <= 0.51 * hy for s in screws if len(s) == 3)]
    wall = [n for n in used if not any(v[0] < XY[n][0] < v[1] and v[2] < XY[n][1] < v[3] for v in w['windows'])]
    free = sorted(set(range(ndof)) - {3 * n + k for n in set(range((nx + 1) * (ny + 1))) - set(used) for k in range(3)})
    return dict(K=K, ndof=ndof, ring=ring, lpn=lpn, scr=scr, wall=wall, free=free, XY=XY)

def solve(M, fixed, kdiag, f):
    """K u + diag(kdiag) u = f with w = 0 at `fixed` nodes; returns u"""
    K = M['K'] + sp.diags(kdiag)
    fx = {3 * n for n in fixed}
    act = [d for d in M['free'] if d not in fx]
    u = np.zeros(M['ndof'])
    u[act] = spl.spsolve(K[act][:, act].tocsc(), f[act])
    return u, K

def gland(face, screws, k_line, squeeze, mount='screws'):
    """cord in a groove: the plate is on the wall at the screws; the cord (a spring, k_line N per mm of length at
    `squeeze`) pushes it off, and lets go if the plate lifts by the whole squeeze. The wall only pushes."""
    M = model(face, screws, P['sg_d'] if mount == 'screws' and 'seal' in WALLS[face] else P['pg_d'], mount)
    k = k_line / squeeze * M['lpn']
    contact, cord = set(M['wall']), set(M['ring'])     # start with the plate flat on the wall, the cord squeezed
    for _ in range(80):
        kd, f = np.zeros(M['ndof']), np.zeros(M['ndof'])
        for n in cord: kd[3 * n] = k; f[3 * n] = k * squeeze
        u, K = solve(M, set(M['scr']) | contact, kd, f)
        r = K @ u - f                                  # reaction at each constrained node: the wall may only push (r > 0, outward)
        w = u[0::3]
        rel = {n for n in contact if n not in M['scr'] and r[3 * n] < -1e-9}
        add = {n for n in M['wall'] if n not in contact and w[n] < -1e-9}
        cord2 = {n for n in M['ring'] if w[n] < squeeze}
        if not rel and not add and cord2 == cord: break
        contact = (contact - rel) | add; cord = cord2
    lift = max(w[n] for n in M['ring'])
    return lift, M, w

# ---- the printed TPU 95A gasket (design.toml [gasket], README: Screws): its push from a plane-strain FE of its cross-section
E_TPU, NU_TPU = (9.0, 25.0), 0.45   # 95A as printed: Bambu's TPU 95A data sheet lists 9.2 MPa (ISO 527, XY); other 95A
                                    # filaments and the low-strain modulus run higher, so 25 MPa is the stiff case
MU_TPU = (0.0, 0.5, 0.8)            # friction at the lip's tip on the wall: sliding freely (or greased), 0.5, and dry TPU
                                    # on printed ASA, which grips (0.8: past it the as-built lip's tip sticks)

def section_k(inside, W, H, E, mu=0.0, h=0.025):
    """N per mm of length per mm of squeeze for a gasket cross-section (inside(x, y): material, x across the groove, y up
    off the groove's floor). Plane strain, Q4 on an h grid, volumetric term at one point (TPU is nearly incompressible).
    The base is held on the groove floor; the topmost material is pressed down 1 and slides sideways against a friction
    force mu x the push (Coulomb, against the way it slides). Linear: at 30-40 % strain an estimate; for a bending lip
    the true curve softens as the lip lies over, so it errs stiff."""
    nx, ny = int(round(W / h)), int(round(H / h))
    lam, G = E * NU_TPU / ((1 + NU_TPU) * (1 - 2 * NU_TPU)), E / (2 * (1 + NU_TPU))
    g, xn, yn = 1 / math.sqrt(3), np.array([-1, 1, 1, -1]), np.array([-1, -1, 1, 1])
    def Bm(xi, eta):
        dx = xn * (1 + eta * yn) / 2 / h; dy = yn * (1 + xi * xn) / 2 / h
        b = np.zeros((3, 8)); b[0, 0::2] = dx; b[1, 1::2] = dy; b[2, 0::2] = dy; b[2, 1::2] = dx; return b
    m = np.array([1.0, 1.0, 0.0])
    ke = sum(Bm(a, c).T @ (G * np.diag([2.0, 2.0, 1.0])) @ Bm(a, c) * h * h / 4 for a in (-g, g) for c in (-g, g))
    b0 = Bm(0, 0); ke = ke + lam * np.outer(b0.T @ m, b0.T @ m) * h * h
    nid = lambda i, j: i * (ny + 1) + j
    R, C, V, used, top = [], [], [], set(), -1
    for i in range(nx):
        for j in range(ny):
            if not inside((i + 0.5) * h, (j + 0.5) * h): continue
            ns = [nid(i, j), nid(i + 1, j), nid(i + 1, j + 1), nid(i, j + 1)]; used.update(ns); top = max(top, j + 1)
            d = np.array([[2 * n, 2 * n + 1] for n in ns]).ravel(); r, c = np.meshgrid(d, d, indexing='ij')
            R.append(r.ravel()); C.append(c.ravel()); V.append(ke.ravel())
    nd = 2 * (nx + 1) * (ny + 1)
    K = sp.csr_matrix((np.concatenate(V), (np.concatenate(R), np.concatenate(C))), shape=(nd, nd))
    fix = {2 * n + k for n in used if n % (ny + 1) == 0 for k in (0, 1)}
    pres = [2 * n + 1 for n in used if n % (ny + 1) == top]
    act = [d for d in sorted({2 * n + k for n in used for k in (0, 1)}) if d not in fix and d not in set(pres)]
    u = np.zeros(nd); u[pres] = -1.0
    Ka = K[act][:, act].tocsc(); u[act] = spl.spsolve(Ka, -(K @ u)[act])
    VA = -(K @ u)[pres].sum(); ux = float(np.mean([u[d - 1] for d in pres]))
    if not mu or abs(ux) < 1e-6: return VA
    fB = np.zeros(nd); fB[[d - 1 for d in pres]] = -np.sign(ux) / len(pres)   # a unit friction force, against the slide
    uB = np.zeros(nd); uB[act] = spl.spsolve(Ka, fB[act]); VB = -(K @ uB - fB)[pres].sum()
    # stuck: the tip held sideways too. It sticks if the side force that takes is within mu x the push
    st = [d for d in act if d + 1 not in set(pres)]; us = np.zeros(nd); us[pres] = -1.0
    us[st] = spl.spsolve(K[st][:, st].tocsc(), -(K @ us)[st]); r = -(K @ us)
    Vs, Hs = r[pres].sum(), abs(r[[d - 1 for d in pres]].sum())
    return Vs if mu * Vs >= Hs or mu * VB >= 1 else min(Vs, VA / (1 - mu * VB))

def tpu_profiles():
    """the cross-sections compared, each in the groove's width W less clearance, and gasket_h tall: [name, inside, area]"""
    W, H, tb = P['sg_w'] - 2 * P['gasket_clr'], P['gasket_h'], P['gasket_base']
    run = W - P['gasket_lip'] - P['gasket_tip']; t = P['gasket_lip']
    sw = 2.0                               # a solid strip that still fits the groove squeezed (TPU keeps its volume)
    def hexa(x, y):                        # a hollow hexagon on the base: 0.45 walls, 45-55 degree sides, a flat top
        if y < tb: return True
        hw = min(W / 2, 0.5 + (y - tb) * 1.4, t / 2 + (H - y) * 1.4); d = abs(x - W / 2)
        return hw - t <= d <= hw
    return [('solid strip %.1f x %.1f' % (sw, H), lambda x, y: abs(x - W / 2) <= sw / 2, sw * H),
            ('hollow hexagon, 0.45 walls', hexa, None),
            ('lip (as built: design.toml [gasket])', lambda x, y: y < tb or 0 <= x - (y - tb) * run / (H - tb) <= t,
             W * tb + t * (H - tb))]

def as_built(face):   # mount = "screws": the screws print_params.py placed
    return WALLS[face]['screws']

def pitch(face, p):   # the same rows at another screw pitch, for comparison
    import subprocess
    v = ['--variant', 'ac'] if P['ac'] else ['--variant', 'dc']
    out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'print_params.py'), '--screw-pitch', str(p), '--stdout'] + v,
                         capture_output=True, text=True, check=True).stdout
    return json.loads(out)['walls'][face]['screws']

def dropin(face):   # drop-in from the top (evaluated, not built): the bottom edge in a channel, both ends in vertical
    # grooves (each holds a 2.5 mm strip of the plate on the wall, all along), two screws near the top ends
    w = WALLS[face]; x0, x1 = w['x0'], w['x1']; (f0, f1) = w['pf']
    line = [[x, f0 + 1.25, 0] for x in np.arange(x0 + 0.5, x1, 1.0)]
    line += [[x, y, 0] for x in (x0 + 1.25, x1 - 1.25) for y in np.arange(f0 + 0.5, f1, 1.0)]
    return line + [[x0 + 8, f1 - 4.2], [x1 - 8, f1 - 4.2]]

# the gasket in the groove, as N per mm of its length at the 0.5 mm squeeze: soft silicone tubing 0.3; a cured silicone
# bead (formed in place, README: Screws) 1.25 / 1.9 / 2.5 for cured RTV of E 1 / 1.5 / 2 MPa (a 2 mm wide, 1.5 mm tall bead
# squeezed a third, shape factor 0.67: about E x 1.9 x 0.33 x 2 mm); a solid O-ring cord 1, a stiff one 2
KS = [('tubing', 0.3), ('cord', 1.0), ('bead E1', 1.25), ('bead E1.5', 1.9), ('bead E2', 2.5)]
SQ = float(sys.argv[sys.argv.index('--squeeze') + 1]) if '--squeeze' in sys.argv else 0.5   # the gasket's squeeze, mm
if '--bead' in sys.argv:   # a taller formed-in-place bead: a 1.5 mm groove and a 1 mm shim (2.5 mm tall, 40 % strain at 1 mm)
    SQ, KS = 1.0, [('bead E1', 1.5), ('bead E1.5', 2.3), ('bead E2', 3.0)]
# the screwed plate's seals in its groove (sg_w x sg_d, README "Sealing a screwed plate"): how much each fills it, and how far
# the plate lifts with each between its six screws. Butyl and wet silicone fill the gap and push nothing back: no lift.
BUTYL = [('butyl strip 2 x 3.2 (cut from 1/8 in tape)', 2.0 * 3.2), ('butyl strip 2.5 x 3.2', 2.5 * 3.2), ('butyl round cord 3 mm', math.pi * 1.5 ** 2)]
FOAM = [('EPDM foam 1/8 in, soft (30 kPa)', 0.03), ('EPDM foam, medium (60 kPa)', 0.06), ('EPDM foam, firm (100 kPa)', 0.10)]   # at 50 %, MPa
TUBE_OD, TUBE_RATE = 2.4, 0.6   # soft silicone tubing 0.8 x 2.4 mm (1/32 x 3/32 in): about 0.3 N/mm at 0.5 mm squeeze, 0.6 N/mm per mm
if __name__ == '__main__' and ('--tpu' in sys.argv or '--seals' in sys.argv or '--fill' in sys.argv):
    GA = P['sg_w'] * P['sg_d']; sq = P['gasket_h'] - P['sg_d']; W = P['sg_w'] - 2 * P['gasket_clr']; tsq = TUBE_OD - P['sg_d']
    print(PJ, 'case:', 'AC' if P['ac'] else 'standard', f'screwed seal groove {P["sg_w"]:g} x {P["sg_d"]:g} ({GA:.1f} mm2)')
    for n, a_ in BUTYL:
        print(f'  {n:44s} {a_:4.1f} mm2: {a_ / GA:4.0%} of the groove, stands {a_ / P["sg_w"] - P["sg_d"] if a_ > GA else 0:.2f} mm proud spread to its width'
              f' (wanted: 105-125 %, the excess squeezes out)')
    print(f'  {"soft silicone tubing OD %.1f" % TUBE_OD:44s} squeezed {tsq:.1f} mm ({tsq / TUBE_OD:.0%}); fills {math.pi * TUBE_OD ** 2 / 4 / GA:.0%} of the groove')
    rows = []
    for name, f, area in tpu_profiles():
        if area is None: area = sum(f((i + 0.5) * 0.025, (j + 0.5) * 0.025) for i in range(int(W / 0.025)) for j in range(int(P['gasket_h'] / 0.025))) * 0.025 ** 2
        if '--fill' in sys.argv: print(f'  TPU {name:40s} fills {area / GA:4.0%} of the groove squeezed'); continue
        ks = {(E_, mu): section_k(f, W, P['gasket_h'], E_, mu) * sq for E_ in E_TPU for mu in MU_TPU}
        rows.append((name, area, ks))
        if '--foam' in sys.argv: rows.pop()
        print(f'  TPU {name:40s} fills {area / GA:4.0%} of the groove squeezed; push N/mm at {sq:g}: '
              + '  '.join(f'E {E_:g} mu {mu:g}: {k:.3f}' for (E_, mu), k in ks.items()))
    if '--fill' in sys.argv: sys.exit()
    for face in ('front', 'back'):
        sc = as_built(face)
        print(f'\n{face} plate, {len(sc)} screws as built: lift mm (and the share of the squeeze left)')
        fsq = P['foam_t'] - P['sg_d']   # the foam strip, slit to the groove's width: squeezed to the groove's depth
        for n_, p_ in FOAM:
            lf = gland(face, sc, p_ * P['sg_w'], fsq)[0]
            print(f'  {n_:44s} {p_ * P["sg_w"]:.2f} N/mm, squeeze {fsq:.1f}: {lf:.3f} ({1 - lf / fsq:.0%})')
        if '--foam' in sys.argv: continue
        l = gland(face, sc, TUBE_RATE * tsq, tsq)[0]
        print(f'  {"tubing OD %.1f, squeeze %.1f" % (TUBE_OD, tsq):44s} {l:.3f} ({1 - l / tsq:.0%})')
        for name, area, ks in rows:
            print(f'  TPU {name:40s} squeeze {sq:g}: ' + '  '.join(f'E {E_:g} mu {mu:g}: {x:.3f} ({1 - x / sq:.0%})' for (E_, mu), k in ks.items() for x in [gland(face, sc, k, sq)[0]]))
    sys.exit()
if __name__ == '__main__':
    print(PJ, 'case:', 'AC' if P['ac'] else 'standard')
    for face in ('front', 'back'):
        w = WALLS[face]
        print(f'\n{face} plate {w["x1"] - w["x0"]:.1f} mm long, t {P["plate_t"]}, groove {P["pg_w"]} x {P["pg_d"]}; lift against a {SQ:g} mm squeeze')
        print(f'  {"held by":44s}  gland lift mm (' + ' / '.join(f'{n} {k:g}' for n, k in KS) + ' N/mm)')
        lay = 'six' if P['screw_layout'] == 'six' else f'pitch <= {P["screw_pitch"]:g}'
        CAND = [(f'screws as built: {len(as_built(face))} ({lay})', as_built(face), 'screws')]
        if '--compare' in sys.argv and not P['ac']:   # the "pitch" layout doesn't fit the AC front plate
            for p in (25, 50, 75):
                s_ = pitch(face, p); CAND.append((f'screws: {len(s_)} (pitch <= {p})', s_, 'screws'))
            CAND.append(('drop-in: channel + end grooves + 2 screws', dropin(face), 'glue'))
        for name, s_, mt in CAND:
            l = [gland(face, s_, k, SQ, mt)[0] for n, k in KS]
            print(f'  {name:44s}  ' + ' / '.join(f'{x:6.3f}' for x in l))
