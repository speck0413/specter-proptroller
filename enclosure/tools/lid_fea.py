#!/usr/bin/env python3
"""Lid seal check: how far does the lid lift off its rim bead between the four latches and the two hinges?

The lid as a Mindlin plate (tools/plate_fea.py's elements, 1 mm grid) over the tub's outline from params.json: the 2.4 mm
lid with its 5 mm rib grid smeared to an equivalent thickness (the ribs' bending stiffness per mm), the unribbed band over
the rim land, and the drip skirt on the front and both ends as an edge beam. It is held where it is held: at the four
latch keepers (on the end walls' skirt, latch_in from the corners) and at the two hinges on the back edge. The cured bead
in the rim groove, squeezed by the tongue, pushes it up all round (a spring per mm of ring, letting go where the lid lifts
by the whole squeeze). Lift within the squeeze keeps the seal closed.

    ~/.venvs/cpsat/bin/python tools/lid_fea.py                          # the standard case
    ~/.venvs/cpsat/bin/python tools/lid_fea.py --params stl/v/ac/params.json
    ~/.venvs/cpsat/bin/python tools/lid_fea.py --latches three   # one in the middle of each end wall and of the front (owner
                                                                  # 2026-10-21; the front one not built: README "Latches")
    ~/.venvs/cpsat/bin/python tools/lid_fea.py --latches two     # the end-wall centres only

A bead formed in place against the tongue (cling film on the tongue, the lid closed on the fresh bead: README "Seal") is
cured to the closed lid's shape and isn't squeezed, so it pushes nothing back: there is nothing to model, it fills the gap.
Numbers: ASA E 2000 MPa; the bead's push at the tongue's 1.1 mm squeeze into a flush bead of cured neutral silicone (E 1-2
MPa, a 1.6 mm tongue into a 3 x 2 mm bead, confined by the groove's walls): about 1-3 N per mm of ring, assumed.
"""
import math, sys
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spl
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import plate_fea as F

P = F.P
xo0, xo1, yo0, yo1 = P['ix0'] - P['wall'], P['ix1'] + P['wall'], P['iyf'] - P['wall'], P['iyb'] + P['wall']
SK = P['lid_gap'] + P['skirt_t']                       # the skirt, outside the tub's outline (front and ends)
X0, X1, Y0, Y1 = xo0 - SK, xo1 + SK, yo0 - SK, yo1
# the rib grid smeared: a 40 mm pitch T (2.4 mm flange, 1.6 x 5 web) per mm of width -> an equivalent plate thickness
p_, t_, b_, h_ = P['lid_rib_pitch'], P['lid_t'], 1.6, P['lid_rib_h']
yb = (p_ * t_ * t_ / 2 + b_ * h_ * (t_ + h_ / 2)) / (p_ * t_ + b_ * h_)
I_ = p_ * t_ ** 3 / 12 + p_ * t_ * (yb - t_ / 2) ** 2 + b_ * h_ ** 3 / 12 + b_ * h_ * (t_ + h_ / 2 - yb) ** 2
T_RIB = (12 * I_ / p_) ** (1 / 3)
T_SKIRT = P['drip'] + P['lid_t']                       # the skirt strip: as deep as the skirt and the lid
RC = P['land_w'] / 2                                   # the bead's centreline: the groove's middle, land_w / 2 in from the outside
SQ = P['tongue_h'] - P['seal_gap']                    # the tongue's reach into the flush bead (1.1 mm)
KS = [('soft foam cord', 0.1), ('soft tubing', 0.3), ('cured bead, soft', 1.0), ('cured bead', 2.0), ('cured bead, stiff', 3.0)]
# the default seal (owner 2026-10-21): 1/8 in closed-cell EPDM foam tape in the groove, pressed by the tongue: its compression
# foam_t - groove_d - seal_gap + tongue_h (1.5 mm, 47 %). Medium-density closed-cell EPDM at 40-50 % takes about 30-100 kPa
# (assumed: makers list 2-10 psi at 25 % for the soft to medium grades); under the 1.6 mm tongue that is 0.05-0.16 N/mm
SQF = P['foam_t'] - P['groove_d'] - P['seal_gap'] + P['tongue_h']
KF = [('EPDM foam, soft (30 kPa)', 0.03 * P['tongue_w']), ('EPDM foam, medium (60 kPa)', 0.06 * P['tongue_w']), ('EPDM foam, firm (100 kPa)', 0.10 * P['tongue_w'])]

def model():
    nx, ny = int(round(X1 - X0)), int(round(Y1 - Y0)); hx, hy = (X1 - X0) / nx, (Y1 - Y0) / ny
    xs, ys = X0 + hx * np.arange(nx + 1), Y0 + hy * np.arange(ny + 1)
    def thick(x, y):
        if x < xo0 or x > xo1 or y < yo0: return T_SKIRT                    # the skirt
        if x < xo0 + P['land_w'] + 1 or x > xo1 - P['land_w'] - 1 or y < yo0 + P['land_w'] + 1 or y > yo1 - P['land_w'] - 1: return t_
        return T_RIB
    kes = {}; rows, cols, vals = [], [], []
    nid = lambda i, j: i * (ny + 1) + j
    for i in range(nx):
        for j in range(ny):
            t = thick(xs[i] + hx / 2, ys[j] + hy / 2)
            if t not in kes: kes[t] = F.ke_q4(t, hx, hy)
            ns = [nid(i, j), nid(i + 1, j), nid(i + 1, j + 1), nid(i, j + 1)]
            d = np.array([[3 * n, 3 * n + 1, 3 * n + 2] for n in ns]).ravel(); r, c = np.meshgrid(d, d, indexing='ij')
            rows.append(r.ravel()); cols.append(c.ravel()); vals.append(kes[t].ravel())
    nd = 3 * (nx + 1) * (ny + 1)
    K = sp.csr_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))), shape=(nd, nd))
    XY = [(xs[n // (ny + 1)], ys[n % (ny + 1)]) for n in range((nx + 1) * (ny + 1))]
    rx0, rx1, ry0, ry1 = xo0 + RC, xo1 - RC, yo0 + RC, yo1 - RC
    ring = [n for n, (x, y) in enumerate(XY) if rx0 - 0.51 <= x <= rx1 + 0.51 and ry0 - 0.51 <= y <= ry1 + 0.51
            and min(abs(x - rx0), abs(x - rx1), abs(y - ry0), abs(y - ry1)) <= 0.51]
    lpn = 2 * ((rx1 - rx0) + (ry1 - ry0)) / len(ring)
    lay = sys.argv[sys.argv.index('--latches') + 1] if '--latches' in sys.argv else 'four'
    ym, xm = (P['iyf'] + P['iyb']) / 2, (P['ix0'] + P['ix1']) / 2
    if lay == 'four': lat = [(x, y) for x in (xo0 - SK / 2, xo1 + SK / 2) for y in (P['iyf'] + P['latch_in'], P['iyb'] - P['latch_in'])]   # as built
    else: lat = [(xo0 - SK / 2, ym), (xo1 + SK / 2, ym)] + ([(xm, Y0 + SK / 2)] if lay == 'three' else [])   # end-wall centres (+ front centre)
    xc = (P['ix0'] + P['ix1']) / 2
    hin = [(hx, Y1) for hx in P['hinge_xs']]
    nf = int(sys.argv[sys.argv.index('--front-latches') + 1]) if '--front-latches' in sys.argv else (1 if P.get('front_latch') else 0)   # as built: the front latch
    lat += [(xo0 + (xo1 - xo0) * (i + 1) / (nf + 1), Y0 + SK / 2) for i in range(nf)]
    held = [n for n, (x, y) in enumerate(XY) if any(math.hypot(x - a, y - b) <= 2.5 for a, b in lat + hin)]
    return dict(K=K, nd=nd, ring=ring, lpn=lpn, held=held, XY=XY)

def lift(M, k_line, sq):
    k = k_line / sq * M['lpn']; on = set(M['ring']); fix = {3 * n for n in M['held']}
    act = [d for d in range(M['nd']) if d not in fix]
    for _ in range(60):
        kd, f = np.zeros(M['nd']), np.zeros(M['nd'])
        for n in on: kd[3 * n] = k; f[3 * n] = k * sq
        Kt = (M['K'] + sp.diags(kd)).tocsr(); u = np.zeros(M['nd'])
        u[act] = spl.spsolve(Kt[act][:, act].tocsc(), f[act])
        on2 = {n for n in M['ring'] if u[3 * n] < sq}
        if on2 == on: break
        on = on2
    w = u[0::3]
    worst = max(M['ring'], key=lambda n: w[n])
    return w[worst], M['XY'][worst]

if __name__ == '__main__':
    M = model()
    print(F.PJ, 'case:', 'AC' if P['ac'] else 'standard', f'lid {X1 - X0:.0f} x {Y1 - Y0:.0f} mm, ribs smeared to {T_RIB:.2f} mm, '
          f'skirt {T_SKIRT:g} mm; latches {sys.argv[sys.argv.index("--latches") + 1] if "--latches" in sys.argv else "four"} (+{int(sys.argv[sys.argv.index("--front-latches") + 1]) if "--front-latches" in sys.argv else (1 if P.get("front_latch") else 0)} front), hinges at {P["hinge_xs"]}; the foam compressed {SQF:.1f} mm (a cured bead, --beads, {SQ:g} mm)')
    for name, k in KF:
        l, at = lift(M, k, SQF)
        print(f'  {name:26s} {k:.3f} N/mm at {SQF:.1f} mm: lift {l:.2f} mm ({1 - l / SQF:.0%} of the compression left) at x {at[0]:.0f}, y {at[1]:.0f}')
    if '--beads' not in sys.argv: sys.exit()
    for name, k in KS:
        l, at = lift(M, k, SQ)
        print(f'  {name:20s} {k:g} N/mm: lift {l:.2f} mm ({1 - l / SQ:.0%} of the squeeze left) at x {at[0]:.0f}, y {at[1]:.0f}')
