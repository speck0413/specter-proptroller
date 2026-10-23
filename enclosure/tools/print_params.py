#!/usr/bin/env python3
"""Write params.json and params.scad: the one source of numbers for the 3D-printed enclosure, read by the OpenSCAD
model (proptroller-print.scad) and the checks in tools/.

Everything comes from design.toml (sizes, the connectors along each wall, the lid button) and the board
(../hardware/kicad/proptroller.ir.json: outline and mounting holes; board-parts.json: its parts as keep-out boxes, for
the assembled view only).

    python3 tools/print_params.py                       # params.json / params.scad here, from design.toml as it is
    python3 tools/print_params.py --variant usb --out D  # the same with the USB panel port flipped on (or off), into D
    python3 tools/print_params.py --variant ac --out D   # the AC case ([case] ac = true, the [ac] tables applied); dc: false
tools/print_build.sh writes each variant it builds into its own folder under stl/v/, next to a copy of the model, so the
parallel renders never share a params file.

World frame (right-handed, z up): X = board x, Y = -board y, so the front wall faces -Y and the back wall +Y.
Part sizes are the owner's caliper readings (2026-10-18 photos). Entries marked "assumed" are not measured yet.
"""
import json, math, os

import sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VARIANT = sys.argv[sys.argv.index('--variant') + 1] if '--variant' in sys.argv else None
OUT = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else ROOT
os.makedirs(OUT, exist_ok=True)
ir = json.load(open(os.path.join(ROOT, '..', 'hardware', 'kicad', 'proptroller.ir.json')))

board = ir['board']
holes = [[p['x'], -p['y']] for p in ir['parts'] if p['fp'].startswith('MountingHole')]

# every number to change lives in design.toml: read it, then derive what follows from it
import tomllib
DES = tomllib.load(open(os.path.join(ROOT, 'design.toml'), 'rb'))
# --variant: dc / ac force [case] ac; usb is the DC case with the USB panel port the other way round from design.toml
if VARIANT not in (None, 'dc', 'ac', 'usb'): sys.exit(f'--variant: dc, ac or usb, not {VARIANT}')
if VARIANT == 'usb': DES['connectors']['USB']['enabled'] = not DES['connectors']['USB'].get('enabled', True)
if VARIANT: DES['case']['ac'] = VARIANT == 'ac'
AC = DES['case']['ac']
ACP = {k: v for k, v in DES['ac'].items() if not isinstance(v, dict)}   # the AC case's own numbers
if AC:   # its tables override the sections of the same name, key by key (a connector's keys too)
    for sec, tab in DES['ac'].items():
        if not isinstance(tab, dict): continue
        for k, v in tab.items():
            if sec == 'connectors' and k in DES[sec]: DES[sec][k] = {**DES[sec][k], **v}
            else: DES[sec][k] = v
PARTS = DES['parts']
P = {}
for sec in ('box', 'feet', 'plates', 'gasket', 'seal', 'lid', 'hinge', 'latch', 'text'): P.update(DES[sec])
NOTES = P.pop('notes', [])   # extra plate text ([plates] notes), placed with the labels below
PER_FACE = {k: P.pop(k) for k in ('center_conns', 'plate_margin')}   # a value, or one per face ({ front = ..., back = ... })
def pf_(k, face): v = PER_FACE[k]; return v[face] if isinstance(v, dict) else v
PINS = P.pop('pins', {})     # guide pins set by hand, per face (the AC front); otherwise placed below
P['ac'] = AC
for k, v in ACP.items(): P['ac_' + k] = v
if '--screw-pitch' in sys.argv:   # tools/plate_fea.py --compare: the "pitch" layout at another pitch
    P['screw_pitch'] = float(sys.argv[sys.argv.index('--screw-pitch') + 1]); P['screw_layout'] = 'pitch'
F, HO = DES['fit'], DES['holes']
P['win_m'] = F['win_m']; P['pin_clr'] = F['pin_clr']
hf = F['hole_fit']
# holes: a part's hole is its size plus hole_fit all round, unless [holes] sets it outright
P['hole_usb'] = HO.get('usb', round(PARTS['usb']['hole_d'] + 2 * hf, 3))
P['hole_button'] = HO.get('button', round(PARTS['button']['thread_d'] + 2 * hf, 3))
P['hole_j35p'] = HO.get('j35_panel', PARTS['j35p']['hole_d'])   # panel-mount: the listing's hole, as is
P['hole_dcp'] = HO.get('dc_panel', PARTS['dcp']['hole_d'])
P.update({'screw_head': HO['screw_head'], 'screw_head_angle': HO['screw_head_angle'], 'screw_pan': HO['screw_head_type'] == 'pan'})
assert HO['screw_head_type'] in ('flat', 'pan'), '[holes] screw_head_type: "flat" or "pan"'
P.update({'screw_d': HO['screw'], 'insert_d': HO['insert'], 'insert_depth': HO['insert_depth'],
          'nut_af': {'M3': 5.5, '#4': 6.35}[HO['plate_nut']], 'nut_t': {'M3': 2.4, '#4': 2.4}[HO['plate_nut']], 'nut_clr': HO['nut_clr'],
          'standoff_hole': HO['standoff'], 'standoff_depth': HO['standoff_depth'], 'pin_d': HO['pin'], 'pin_pilot': min(HO['pin_pilot'], HO['pin']), 'plate_pilot': min(HO['plate_pilot'], HO['screw'])})
# the printed TPU gasket ([gasket]) in the screwed plate's groove (sg_w x sg_d): it prints (each lip layer within 0.25 mm of
# the one below, so a 0.45 mm line overlaps the last by 0.2), it is squeezed, it fits the groove squeezed (TPU doesn't change
# volume), and the lip's tip, swinging out as it bends flat, stays inside the groove
_gw = P['sg_w'] - 2 * P['gasket_clr']; _run = _gw - P['gasket_lip'] - P['gasket_tip']; _rise = P['gasket_h'] - P['gasket_base']
assert _run > 0 and _run / (round(_rise / 0.2) - 1) <= 0.25 + 1e-9, '[gasket]: the lip leans too far to print (over 0.25 mm a layer)'
assert P['gasket_h'] > P['sg_d'] > P['gasket_base'], '[gasket]: gasket_h over sg_d (the squeeze), the base under it'
assert _gw * P['gasket_base'] + P['gasket_lip'] * _rise <= 0.85 * P['sg_w'] * P['sg_d'], '[gasket]: too much TPU to fit the groove squeezed'
_sw = math.sqrt(_run ** 2 + _rise ** 2 - (P['sg_d'] - P['gasket_base']) ** 2) - _run   # the tip's swing, squeezed flat to sg_d
assert _sw <= P['gasket_tip'] + P['gasket_clr'] - 0.05, f"[gasket]: the lip's tip swings {_sw:.2f} mm out, past the groove"
P['gasket_swing'] = round(_sw, 2)
# the same section as the lid's TPU gasket in the rim's groove (groove_w x groove_d), pressed by the latched lid's tongue to
# groove_d + seal_gap - tongue_h over the floor
_gl = P['groove_d'] + P['seal_gap'] - P['tongue_h']; _lw = P['groove_w'] - 2 * P['gasket_clr']; _lrun = _lw - P['gasket_lip'] - P['gasket_tip']
assert _lrun > 0 and _lrun / (round(_rise / 0.2) - 1) <= 0.25 + 1e-9, '[gasket]: the lid gasket\'s lip leans too far to print (over 0.25 mm a layer)'
assert P['gasket_h'] > _gl > P['gasket_base'], '[gasket]: gasket_h over the latched tongue\'s height in the groove (the lid gasket\'s squeeze)'
assert _lw * P['gasket_base'] + P['gasket_lip'] * _rise <= 0.85 * P['groove_w'] * _gl, '[gasket]: too much TPU to fit the lid groove squeezed'
_lsw = math.sqrt(_lrun ** 2 + _rise ** 2 - (_gl - P['gasket_base']) ** 2) - _lrun
assert _lsw <= P['gasket_tip'] + P['gasket_clr'] - 0.05, f"[gasket]: the lid gasket's tip swings {_lsw:.2f} mm out, past the groove"
P['gasket_lid_squeeze'] = round(P['gasket_h'] - _gl, 2); P['gasket_lid_swing'] = round(_lsw, 2)
P['knuckle_l'] = round((P['pin_span'] - 2 * P['knuckle_gap']) / 3, 3)   # the hinge screw sets the hinge's length
B_ = PARTS['button']
C_LEFT, C_RIGHT = P.pop('clear_left'), P.pop('clear_right')
C_FRONT, C_BACK = P.pop('clear_front'), P.pop('clear_back')
P['board_t'] = board['thick']
ix0, ix1 = -C_LEFT, board['w'] + C_RIGHT
iyf, iyb = -(board['h'] + C_FRONT), C_BACK
P.update({'ix0': ix0, 'ix1': ix1, 'iyf': iyf, 'iyb': iyb})
_xa, _xb = ix0 - P['wall'], ix1 + P['wall']   # the back edge, outside: the hinges at hinge_at along it
P['hinge_xs'] = [round(_xa + f * (_xb - _xa), 2) for f in P.pop('hinge_at')]
zf1 = P['floor_t']   # the floor sits on the bed (rubber feet underneath)
P['z_floor'] = zf1
P['z_board'] = zf1 + P['standoff_h']
P['z_row'] = P['z_board'] + P['board_t'] + P['row_v']
P['z_top'] = zf1 + P['inner_h']
# latches: [wall, position along it]
P['latches'] = [[f, y] for f in ('left', 'right') for y in (iyf + P['latch_in'], iyb - P['latch_in'])]   # two on each end wall: the long walls are the faceplates'
assert P['latch_style'] in ('cam', 'draw'), '[latch] latch_style: "cam" or "draw"'
# the draw latch's points in the wall frame (y out of the wall's outside face, z up), closed: dl_p1 the lever's pin, dl_p2
# the hook's, dl_k where the hook's tip bears (the lid squeezed dl_squeeze); dl_open: the lever's opening (degrees) that
# frees the hook (the lid back up, 1 mm to spare)
def _dl():
    zlt = P['z_top'] + P['seal_gap'] + P['lid_t']; yl = P['lid_gap'] + P['skirt_t']
    p1 = (P['dl_out'], P['z_top'] - P['dl_drop']); k = (yl - P['dl_reach'] / 2, zlt - P['dl_squeeze']); ks = (k[0], zlt)
    D = math.dist(p1, k); u = ((k[0] - p1[0]) / D, (k[1] - p1[1]) / D)
    a0 = math.atan2(-u[1], -u[0]); r = P['dl_r']
    sg = min((1, -1), key=lambda g: math.cos(a0 + g * math.radians(P['dl_over'])))   # past centre towards the wall
    a = a0 + sg * math.radians(P['dl_over']); p2 = (p1[0] + r * math.cos(a), p1[1] + r * math.sin(a)); lh = math.dist(p2, k)
    def allow(phi):   # the lever's pin to the hook's tip, the lever opened phi degrees
        t = a - sg * math.radians(phi); b = r * math.cos(t) * u[0] + r * math.sin(t) * u[1]
        return b + math.sqrt(lh * lh - (r * r - b * b))
    snap = D - min(allow(q / 10) for q in range(0, 900))
    opn = next((q for q in range(0, 181) if allow(q) - math.dist(p1, ks) >= 1.0), None)
    assert snap >= 0.1, f'[latch] the draw latch barely goes over centre ({snap:.2f} mm): more dl_over'
    assert opn is not None and opn <= 100, '[latch] the draw latch never frees its hook: a longer dl_r or less dl_over'
    assert r - P['dl_ear_r'] - P['dl_pin'] / 2 >= 0.8, "[latch] the hook's pin swings into the ears: a longer dl_r or smaller dl_ear_r"
    assert P['dl_ear_r'] - (P['dl_pin'] + 0.15) / 2 >= 1.5 and P['dl_boss_r'] - (P['dl_pin'] + 0.45) / 2 >= 1.5, '[latch] under 1.5 mm round a pin'
    assert p2[0] - P['dl_boss_r'] >= 0.5, "[latch] the lever's lower boss runs into the wall"
    return [round(v, 3) for v in p1], [round(v, 3) for v in p2], [round(v, 3) for v in k], round(lh, 3), round(snap, 3), opn
P['dl_p1'], P['dl_p2'], P['dl_k'], P['dl_lh'], P['dl_snap'], P['dl_open'] = _dl()

# connectors: the panel jacks along each wall (design.toml [connectors]); kind j35p / dcp / usb names the part behind the hole
KIND = {'j35': 'j35p', 'dc': 'dcp', 'usb': 'usb', 'outlet': 'outlet'}
conns = sorted(({'id': k, 'label': c['label'], 'face': c['face'], 'X': c['x'], 'y': c.get('y', 0.0), 'kind': KIND[c['kind']]}
                for k, c in DES['connectors'].items() if c.get('enabled', True)), key=lambda c: (c['face'], c['X']))
assert all(c['face'] in ('front', 'back') for c in conns), 'a connector face is "front" or "back"'
assert AC or not any(c['kind'] == 'outlet' for c in conns), 'a receptacle (kind "outlet") is for the AC case only'
for face in ('front', 'back'):   # [plates] center_conns: each wall's row centred on the wall, x then sets only the spacing
    if pf_('center_conns', face):
        xs_ = [c['X'] for c in conns if c['face'] == face]
        dx = (ix0 + ix1) / 2 - (min(xs_) + max(xs_)) / 2
        for c in conns:
            if c['face'] == face: c['X'] = round(c['X'] + dx, 2)
P['button_xy'] = [B_['x'], -B_['y']]

# what sits behind each kind of hole, which its wall window clears
PT = PARTS
EXT = {'usb': [PT['usb']['window'] - 2 * P['win_m']] * 2, 'j35p': [PT['j35p']['behind_d']] * 2, 'dcp': [PT['dcp']['behind_d']] * 2}

# per wall, in the plate's own frame as seen from outside: x to the right (front: x = X; back: x = -X), y up from the row
OU = PARTS['outlet']
def wins_for(c, x):   # the wall windows behind a connector: [x0, x1, y0, y1, shape], shape 0 a rounded window, 1 a teardrop hole
    y, m = c['y'], P['win_m']
    if c['kind'] != 'outlet':
        w, h = EXT[c['kind']]
        return [[x - w / 2 - m, x + w / 2 + m, y - h / 2 - m, y + h / 2 + m, 0]]
    # a duplex receptacle on its side: a window per face, and a hole for each of its three #6-32s (the strap's two mounting
    # slots, and the faceplate screw tapped into the strap between the faces)
    fw, fh, r = OU['face_h'] / 2 + m, OU['face_w'] / 2 + m, OU['screw_d'] / 2
    out = [[x + s_ * OU['face_pitch'] / 2 - fw, x + s_ * OU['face_pitch'] / 2 + fw, y - fh, y + fh, 0] for s_ in (-1, 1)]
    return out + [[x + d - r, x + d + r, y - r, y + r, 1] for d in (-OU['screw_pitch'] / 2, 0, OU['screw_pitch'] / 2)]
def half_h(c): return (OU['face_w'] / 2 if c['kind'] == 'outlet' else EXT[c['kind']][1] / 2) + P['win_m']   # its windows' half height
xw0, xw1 = ix0 - P['wall'] + P['corner_r'], ix1 + P['wall'] - P['corner_r']   # the outside walls' straight stretch
walls = {}
for face in ('front', 'back'):
    cs = sorted([c for c in conns if c['face'] == face], key=lambda c: c['X'] * (1 if face == 'front' else -1))
    xs = [c['X'] * (1 if face == 'front' else -1) for c in cs]
    assert PT['dcp']['behind_d'] > 12.5 or PT['dcp']['thread_l'] >= P['plate_t'] + P['wall'] + 3.0, 'the DC jacks\' thread is too short for the nut on the wall\'s inside face: set [parts.dcp] behind_d = 17.5'
    assert 1.2 <= P['j35_seat'] <= P['plate_t'] - P['inlay'] - 0.4, '[plates] j35_seat: 1.2 mm or more, and under the labels\' inlay'
    dcx = sorted(x for c, x in zip(cs, xs) if c['kind'] == 'dcp')   # each DC jack's nut, inside, clear of its neighbour's
    assert all(b_ - a_ >= PT['dcp']['nut_c'] + 1.5 for a_, b_ in zip(dcx, dcx[1:])), f'{face}: two DC jacks closer than their nuts allow (nut_c + 1.5)'
    wins = []
    for w0 in sorted((w for c, x in zip(cs, xs) for w in wins_for(c, x)), key=lambda w: w[0]):
        a = wins[-1] if wins else None
        if a and a[4] == 0 and w0[4] == 0 and w0[0] - a[1] < 1.5:   # a rib under 1.5 mm is too weak to print: share the window (the plate backs the ribs)
            wins[-1] = [a[0], max(a[1], w0[1]), min(a[2], w0[2]), max(a[3], w0[3]), 0]
        else: wins.append(w0)
    for a_, b_ in zip(wins, wins[1:]):   # the ribs between windows: 1.5 mm or more (or the two share one window, above)
        assert b_[0] - a_[1] >= 1.5 - 1e-6 or b_[4] or a_[4] or b_[0] <= a_[1], f'{face}: a rib under 1.5 mm between windows'
    # guide pins (printed on the plate's back, into holes through the wall): one in the widest gap between two windows,
    # one past the last window at the plate's x1 end, so the pattern is lopsided and the plate fits one way round only;
    # or where [plates] pins puts them (board x, height above the row)
    ph = P['pin_d'] / 2 + P['pin_clr']                 # the hole's radius
    if face in PINS:
        pins = [[round(q[0] * (1 if face == 'front' else -1), 2), float(q[1])] for q in PINS[face]]
        for q in pins:
            assert all(math.hypot(max(w[0] - q[0], 0, q[0] - w[1]), max(w[2] - q[1], 0, q[1] - w[3])) >= ph + P['pin_wall'] for w in wins), \
                f'{face}: guide pin {q} too close to a window'
    else:
        gaps = [(wins[i + 1][0] - wins[i][1], (wins[i][1] + wins[i + 1][0]) / 2) for i in range(len(wins) - 1)]
        g, gx = max(gaps)
        assert g >= 2 * (ph + P['pin_wall']), f'{face}: no gap between windows wide enough for a guide pin'
        pins = [[round(gx, 2), 0.0], [round(wins[-1][1] + P['pin_wall'] + ph, 2), 0.0]]
    for q in pins:   # each guide pin's tip (1.6 mm into the box) clear of every DC jack's nut on the wall's inside face
        assert all(math.hypot(q[0] - x, q[1]) >= PT['dcp']['nut_c'] / 2 + P['pin_d'] / 2 + 0.5 for x in dcx), f'{face}: guide pin {q} runs into a DC jack\'s nut'
    # the seal ring (gasket groove, or the glue bead's groove) round the windows and the pin holes
    m, pw = P['ring_m'], P['pg_w']
    ring = [round(min([wins[0][0]] + [q[0] - ph for q in pins]) - m - pw, 2), round(max([w[1] for w in wins] + [q[0] + ph for q in pins]) + m + pw, 2),
            round(min([w[2] for w in wins] + [q[1] - ph for q in pins]) - m - pw, 2), round(max([w[3] for w in wins] + [q[1] + ph for q in pins]) + m + pw, 2)]
    # the plate: plate_margin past the outer connectors (room for their labels), and at least ring_edge past the ring
    # (a flat for the CA dots and the bond outside the bead, past the back-edge chamfer)
    pm = pf_('plate_margin', face)
    lo_m = max(pm, xs[0] - ring[0] + P['ring_edge']); hi_m = max(pm, ring[1] - xs[-1] + P['ring_edge'])
    sgx = P['sg_m'] + P['sg_w'] + P['sg_edge'] + 0.1   # and the screwed plate's wider seal groove, sg_m past the windows and pins
    lo_m = max(lo_m, xs[0] - (ring[0] + m + pw) + sgx); hi_m = max(hi_m, (ring[1] - m - pw) - xs[-1] + sgx)
    if pf_('center_conns', face): lo_m = hi_m = max(lo_m, hi_m)   # centred on the wall, like its connectors
    x0, x1 = xs[0] - lo_m, xs[-1] + hi_m
    assert (xw0 <= x0 and x1 <= xw1) if face == 'front' else (-xw1 <= x0 and x1 <= -xw0), f'{face}: the plate runs past the wall\'s straight stretch'
    # labels: row 1 above the connectors at label_base; row 2 (for crowded walls) below them, as far under the row.
    # Each label is fitted to the room
    # left by its neighbours in the same row (min 2.5 mm capitals); width ~ 0.9 x the text size per character (measured in
    # Liberation Sans Bold: CC1 10.5 mm, TRIG2 15.8 mm at size 4). A receptacle's two labels ("RLY1 RLY2": its left and right
    # faces) sit over its faces at label_h. Each label: [text, x, row, size, baseline y].
    WF, GAP = 0.9, 1.5
    import itertools
    li = [i for i, c in enumerate(cs) if c['kind'] != 'outlet']
    lc, lx = [cs[i] for i in li], [xs[i] for i in li]
    def caps_for(rows):   # each pair of neighbours in a row shares the gap between them by letter count: no overlaps
        caps = []
        for i in range(len(lc)):
            c = P['label_h']; n_i = len(lc[i]['label'])
            same = [j for j in range(len(lc)) if j != i and rows[j] == rows[i]]
            for j in same:
                if any(min(lx[i], lx[j]) < lx[k] < max(lx[i], lx[j]) for k in same): continue   # only nearest neighbours
                c = min(c, 2 * (abs(lx[i] - lx[j]) - GAP) / (WF * (n_i + len(lc[j]['label']))))
            c = min(c, 2 * (pm - 2) / (WF * n_i) if lc[i] in (cs[0], cs[-1]) else c)
            caps.append(c)
        return caps
    best = None
    for rows in itertools.product(*[[1, 2] if P['label_rows'] > 1 else [1]] * len(lc)):
        cp = caps_for(rows)
        score = (min(min(cp), 3.0), -sum(r == 2 for r in rows), min(cp), sum(cp))
        if best is None or score > best[0]: best = (score, list(rows), cp)
    rows, caps = best[1], best[2]
    def base(c, row, size):   # a label's baseline: over (row 1) or under (row 2) the connector, clear of its windows
        if c['kind'] != 'outlet': return P['label_base'] if row == 1 else -P['label_base'] - size
        return c['y'] + half_h(c) + 1.0 if row == 1 else c['y'] - half_h(c) - 1.0 - size
    labs = []
    for i, c in enumerate(cs):
        if c['kind'] == 'outlet':
            for t, s_ in zip(c['label'].split(), (-1, 1)):
                labs.append([t, round(xs[i] + s_ * OU['face_pitch'] / 2, 2), 1, P['label_h'], round(base(c, 1, P['label_h']), 2)])
        else:
            k = li.index(i); sz = round(max(2.5, caps[k]), 2)
            labs.append([c['label'], xs[i], rows[k], sz, round(base(c, rows[k], sz), 2)])
    # notes: centred on the connectors they name, in row 1 (over) or row 2 (under); no two texts may overlap
    ids = [c['id'] for c in cs]
    for n in NOTES:
        if n['face'] != face: continue
        assert all(i in ids for i in n['under']), f"{face}: a note names a connector not on this wall: {n['under']}"
        xn = sum(xs[ids.index(i)] for i in n['under']) / len(n['under'])
        r_ = n.get('row', 2)
        yn = min(base(cs[ids.index(i)], r_, n['size']) for i in n['under']) if r_ == 2 else max(base(cs[ids.index(i)], r_, n['size']) for i in n['under'])
        labs.append([n['text'], round(xn, 2), r_, n['size'], round(yn, 2)])
    for i, a in enumerate(labs):
        for b_ in labs[i + 1:]:
            if a[4] < b_[4] + b_[3] and b_[4] < a[4] + a[3]:   # overlapping heights
                assert abs(a[1] - b_[1]) >= WF * (a[3] * len(a[0]) + b_[3] * len(b_[0])) / 2 + 0.5, f'{face}: labels {a[0]!r} and {b_[0]!r} overlap'
    for l in labs:
        assert x0 + 1 <= l[1] - WF * l[3] * len(l[0]) / 2 and l[1] + WF * l[3] * len(l[0]) / 2 <= x1 - 1, f'{face}: label {l[0]!r} runs off the plate'
    # the show face (the plate is flat: no rails): from below the ring (or the row-2 labels) to just above the labels
    f1 = round(max([l[4] + l[3] + 1.2 for l in labs] + [ring[3] + 1.5]), 2)   # the labels set it: 2.3 mm or more of flat
    f0 = round(min([ring[2] - P['ring_edge']] + [l[4] - 1.2 for l in labs]), 2)
    assert P['z_row'] + f0 >= 0.3, f'{face}: the plate reaches below the floor'
    if face == 'front':   # the lid's drip skirt hangs over the front wall, 1 mm off it: the plate stays under it
        assert P['z_row'] + f1 <= P['z_top'] + P['seal_gap'] - P['drip'] - 0.5, 'front: the plate reaches up into the lid skirt'
    # mount = "screws": countersunk M3s into heat-set inserts in blind bosses behind the wall, a row below the ring and a
    # row above it, both outside the ring (a leak along a screw stays outside the seal). screw_layout "six" (the default):
    # left, middle and right along each row, each moved the least distance along its row off anything in the way (text on
    # the show face, the hinges over the back plate, a window under a top boss's wedge). "pitch": the fewest of each end
    # and midway between neighbouring connectors that keep every gap within screw_pitch (the earlier layout).
    csr = P['screw_head'] / 2; ed = csr + 1.5
    hw = max(P['pin_span'] / 2, (PARTS['hinge']['leaf_l'] + 2) / 2) + 0.5   # the pin hinge's supports or the Everbilt pad, and 0.5
    hxs = P['hinge_xs']
    notches = sorted([[round(-h - hw, 2), round(-h + hw, 2)] for h in hxs]) if face == 'back' else []
    def over_for(yt):   # the top row's bosses: their 45-degree wedge must not hang over a window it would reach (it would start on air)
        return [[w[0] - P['boss_w'] / 2 + csr + 1.4 - 0.5, w[1] + P['boss_w'] / 2 - csr - 1.4 + 0.5] for w in wins
                if w[3] + 0.4 > yt - P['boss_h'] / 2 - P['boss_len']]
    def text_at(y):   # the x stretches a screw head at height y may not take: labels it would cut into
        return [[l[1] - WF * l[3] * len(l[0]) / 2 - csr - 0.5, l[1] + WF * l[3] * len(l[0]) / 2 + csr + 0.5] for l in labs
                if l[4] - 0.3 < y + csr + 0.5 and y - csr - 0.5 < l[4] + 0.75 * l[3]]   # capitals: 0.75 of the size
    moved = []
    # mount = "screws": the seal (owner 2026-10-20): one continuous groove, sg_w wide and sg_d deep, round the windows and
    # the pins, its top and bottom runs on the screw rows and its ends at the end screws (or just outside the windows), so
    # it runs where the screws clamp; round each screw it bends inward past a solid land (sg_land round the screw, for the
    # countersunk head), so the screws stay outside the seal. Under the back plate's hinge notches it dips below the notch.
    # The region it runs round, E: the rectangle [vx0, vx1] x [yb, yt] less a disc round each screw and the notch cuts; the
    # groove is the band sg_w wide centred on E's edge (proptroller-print.scad rounds E's corners by sg_round)
    SW, SL, SE, SR = P['sg_w'], P['sg_land'], P['sg_edge'], P['sg_round']
    ext = [min([w[0] for w in wins] + [q[0] - ph for q in pins]), max([w[1] for w in wins] + [q[0] + ph for q in pins])]
    SM = P['sg_m']   # the groove's inner edge this far from the windows and the pin holes (squeeze-out stays off the jacks)
    def seal_for(scr, yb_, yt_):
        vx0 = min(min(q[0] for q in scr), ext[0] - SM - SW / 2 - 0.1); vx1 = max(max(q[0] for q in scr), ext[1] + SM + SW / 2 + 0.1)
        rd = SL + SW / 2
        cuts = [[n[0] - SW / 2 - SE, n[1] + SW / 2 + SE, wl_top - SE - SW / 2] for n in notches]
        sl = {'rect': [round(vx0, 2), round(vx1, 2), yb_, yt_], 'discs': [[q[0], q[1], round(rd, 2)] for q in scr],
              'cuts': [[round(c[0], 2), round(c[1], 2), round(c[2], 2)] for c in cuts]}
        def inE(x, y, g=0.0):   # inside E by g or more (approximately: each piece separately)
            if not (vx0 + g <= x <= vx1 - g and yb_ + g <= y <= yt_ - g): return False
            cx_, cy_ = min(max(x, vx0 + SR), vx1 - SR), min(max(y, yb_ + SR), yt_ - SR)   # the corners rounded by sg_round
            if math.hypot(x - cx_, y - cy_) > SR - g and (x != cx_ and y != cy_): return False
            if any(math.hypot(x - d[0], y - d[1]) < d[2] + g for d in sl['discs']): return False
            return not any(c[0] - g < x < c[1] + g and y > c[2] - g for c in cuts)
        g = m + SW / 2 + 0.05   # each window and pin hole at least ring_m inside the groove, a DC jack's sg_m
        bad = []
        for w in wins:
            g = (SM if any(w[0] < x < w[1] for x in dcx) else m) + SW / 2 + 0.05
            pts = [(w[0] + (w[1] - w[0]) * i / 20, w[2]) for i in range(21)] + [(w[0] + (w[1] - w[0]) * i / 20, w[3]) for i in range(21)]
            pts += [(w[0], w[2] + (w[3] - w[2]) * i / 20) for i in range(21)] + [(w[1], w[2] + (w[3] - w[2]) * i / 20) for i in range(21)]
            bad += [p_ for p_ in pts if not inE(p_[0], p_[1], g)][:1]
        g = m + SW / 2 + 0.05
        for q in pins:
            bad += [(q[0] + ph * math.cos(a / 8), q[1] + ph * math.sin(a / 8)) for a in range(51) if not inE(q[0] + ph * math.cos(a / 8), q[1] + ph * math.sin(a / 8), g)][:1]
        return sl, bad
    wl_top = f1   # the glued plate's top, where the back plate steps down under the hinges
    if P['screw_layout'] == 'six':
        # the top row above the connectors' labels and high enough that its bosses' 45-degree wedges clear the windows
        # (a wedge hanging over a window would start on air); the receptacle's labels are dodged along the row instead
        yb = round(ring[2] - 3.2, 2)
        yt = round(max(ring[3] + 3.2, P['label_base'] + 0.75 * P['label_h'] + csr + 0.8,
                       max(w[3] for w in wins) + 0.4 + P['boss_h'] / 2 + P['boss_len']), 2)
        def place(y, ban):   # ban: forbidden screw-centre stretches
            ban = sorted(ban); lo, hi = x0 + ed, x1 - ed; out = []
            def free(c): return lo - 1e-6 <= c <= hi + 1e-6 and not any(b[0] < c < b[1] for b in ban)
            for t in (lo, (x0 + x1) / 2, hi):
                c = t if free(t) else min((e for b in ban for e in (b[0], b[1]) if free(e)), key=lambda e: abs(e - t))
                if abs(c - t) > 1e-6: moved.append([face, round(t, 2), round(y, 2), round(c, 2)])
                out.append([round(c, 2), y])
            xs_ = [q[0] for q in out]
            assert all(b - a >= 2 * csr + 1 for a, b in zip(xs_, xs_[1:])), f'{face}: two of the six screws ended up on top of each other'
            return out
        nb = [[n[0] - csr - 1.4, n[1] + csr + 1.4] for n in notches + over_for(yt)]
        for _ in range(40):   # the seal's bends round the bottom screws must clear the windows above them: lower the row
            moved_ = len(moved); screws = place(yb, text_at(yb)) + place(yt, nb + text_at(yt))
            seal, bad = seal_for(screws, yb, yt)
            if not bad: break
            del moved[moved_:]
            assert all(q[1] < 0 for q in bad), f'{face}: the screwed seal runs into {bad} near the top row'
            yb = round(yb - 0.25, 2)
        assert not bad, f'{face}: the screwed seal does not clear the windows'
    else:
        yb = round(min([ring[2] - 3.2] + [l[4] - csr - 0.8 for l in labs if l[4] < 0]), 2)   # under any text below the row
        yt = round(max(ring[3] + 3.2, max(l[4] for l in labs) + 0.75 * P['label_h'] + csr + 0.8), 2)
        def row(y, avoid):
            cand = [x0 + ed] + [(xs[i] + xs[i + 1]) / 2 for i in range(len(xs) - 1)] + [x1 - ed]
            cand += [n[0] - csr - 1.5 for n in avoid] + [n[1] + csr + 1.5 for n in avoid]
            cand = sorted(c for c in cand if x0 + ed - 1e-6 <= c <= x1 - ed + 1e-6 and not any(n[0] - csr - 1.4 < c < n[1] + csr + 1.4 for n in avoid))
            cand = [c for i, c in enumerate(cand) if i == 0 or c - cand[i - 1] > 2 * csr + 1]   # two spots closer than a head: one
            out = [cand[0]]
            for i, c in enumerate(cand[1:], 1):   # greedy: keep a spot when skipping it would leave a gap over screw_pitch
                nxt = cand[i + 1] if i + 1 < len(cand) else None
                if nxt is None or nxt - out[-1] > P['screw_pitch'] or any(out[-1] < n[0] and nxt > n[1] for n in avoid): out.append(c)
            return [[round(c, 2), y] for c in out]
        screws = row(yb, []) + row(yt, sorted(notches + over_for(yt)))
        seal, bad = seal_for(screws, yb, yt)
        assert not bad, f'{face}: the screwed seal runs into {bad[0]}'
    pf_s = [round(min(f0, yb - ed), 2), round(max(f1, yt + ed), 2)]
    r_ = seal['rect']   # the groove stays sg_edge inside the screwed plate's edge (its back chamfer and a flat)
    assert pf_s[0] <= r_[2] - SW / 2 - SE + 1e-6 and r_[3] + SW / 2 + SE <= pf_s[1] + 1e-6, f'{face}: the seal groove runs off the plate (y)'
    assert x0 <= r_[0] - SW / 2 - SE + 1e-6 and r_[1] + SW / 2 + SE <= x1 + 1e-6, f'{face}: the seal groove runs off the plate (x): {r_[:2]} in {x0, x1}'
    assert P['plate_t'] - P['sg_d'] - P['inlay'] >= 0.8, 'the seal groove leaves under 0.8 mm under the inlaid labels'
    assert P['z_row'] + pf_s[0] >= 0.3, f'{face}: the screwed plate reaches below the floor'
    if face == 'front':
        assert P['z_row'] + pf_s[1] <= P['z_top'] + P['seal_gap'] - P['drip'] - 0.5, 'front: the screwed plate reaches up into the lid skirt'
    for q in pins:   # a guide pin's tip (into the box) clear of every insert boss behind the wall
        for sc in screws:
            assert abs(q[0] - sc[0]) >= P['boss_w'] / 2 + ph + 0.3 or not (sc[1] - P['boss_h'] / 2 - P['boss_len'] - ph - 0.3 < q[1] < sc[1] + P['boss_h'] / 2 + ph + 0.3), \
                f'{face}: guide pin {q} runs into the insert boss of screw {sc}'
    assert P['nut_t'] + 0.4 + 1.5 <= P['boss_len'], 'the boss is too short for the nut pocket and 1.5 mm in front of it'
    assert P['nut_af'] + 2 * P['nut_clr'] + 2 * 1.2 <= P['boss_h'] + 1e-6 and (P['nut_af'] + 2 * P['nut_clr']) * 2 / math.sqrt(3) + 2 * 1.5 <= P['boss_w'] + 1e-6, \
        'the boss is too small round the nut pocket (a flat up: 1.2 mm over and under it, 1.5 beside it)'
    # one way round only: turned end for end (180 degrees about the plate's centre), at least one pin lands on solid wall
    def blocked(cy):
        def clear(px, py):
            dw = min(math.hypot(max(w[0] - px, 0, px - w[1]), max(w[2] - py, 0, py - w[3])) for w in wins)
            dp = min(math.hypot(px - q[0], py - q[1]) for q in pins)
            return min(dw, dp - ph) >= ph + 0.5
        return any(clear(x0 + x1 - q[0], 2 * cy - q[1]) for q in pins)
    assert blocked((f0 + f1) / 2) and blocked(sum(pf_s) / 2), f'{face}: the plate would also fit turned round'
    walls[face] = {'moved': moved, 'x0': round(x0, 2), 'x1': round(x1, 2), 'pf': [f0, f1], 'pf_s': pf_s, 'notches': notches, 'ring': ring,
                   'pins': pins, 'screws': screws, 'boss_lens': [P['boss_len']] * len(screws), 'nuts': [1] * len(screws), 'seal': seal, 'windows': [[round(v, 2) for v in w[:4]] + [w[4]] for w in wins],
                   'labels': labs, 'conns': [[c['id'], c['kind'], round(x, 2), c['y']] for c, x in zip(cs, xs)]}

# the AC case's barrier, from where the board's parts are (board-parts.json, rev B): world boxes [x0, y0, x1, y1, z0, z1].
# The mains bay is the front left: in front of the board from the left wall to just right of J_R2, round a low-voltage pocket
# in front of the TRIG1 jack (J_T1). Along the board's front edge a wall closes it off, except for a window in front of the
# relay terminals (J_R1, J_R2): a sill below the board, a curtain above the terminal blocks. A wall from there to the front
# wall, between J_R2 and J_O1 (OUT1's terminal), closes its right side. Every barrier stops 1 mm under the lid's ribs; the
# lid has a rib each side of each, overlapping it, so a wire can't cross over the top. A screwed cover closes the bay.
P['ac_barrier'], P['ac_curtain'], P['ac_gland'] = [], [], []
P.update({'ac_earth': [], 'ac_ties': [], 'ac_cover': [], 'ac_cover_bosses': [], 'ac_chimney': [], 'ac_seat': 0.0, 'ac_cover_z': 0.0, 'ac_ribs': [],
          'ac_shelf': [], 'ac_lip': [], 'ac_stops': [], 'ac_niche_out': [], 'ac_earth2': []})
# rubber feet: the screw's way into the boss is blind; the cap over the pilot must stay closed
FCAP = P['z_floor'] + P['feet_boss_h'] - P['feet_depth']
assert FCAP >= 1.0, 'the feet bosses are too short to close over their pilots'
P['feet_cap'] = round(FCAP, 2)
if AC:
    KO = {r[0]: r for r in json.load(open(os.path.join(ROOT, 'board-parts.json')))}
    def bx(n): r = KO[n]; return r[1], r[4], r[2], r[5], r[6]   # board x0, x1, y0 (back), y1 (front), z1 above the board's bottom
    z_lu = P['z_top'] + P['seal_gap']
    bt, z0, zb = ACP['barrier_t'], P['z_floor'] - 0.01, z_lu - P['lid_rib_h'] + ACP['overlap']   # up beside the lid's ribs
    assert zb <= P['z_top'] - 0.5, 'the barrier would reach the rim: make overlap smaller'
    # the mains-bay cover: a plate under the rim land's underside, over the receptacle's terminal screws and wiring
    ct = 2.4
    zc1 = P['z_top'] - P['land_h'] - (P['land_w'] - P['wall']) - 0.5; zc0 = zc1 - ct
    P['ac_cover_z'] = round(zc0, 2)
    ya = -(board['h'] + ACP['barrier_gap']); yb_ = ya - bt                   # the edge wall: its back and front faces
    t0, t1 = bx('J_T1')[:2]
    px0, px1 = t0 - ACP['pocket_m'] - bt, t1 + ACP['pocket_m'] + bt         # the pocket's outside faces
    yp = ya - ACP['pocket_d'] - bt
    wx0 = bx('J_R1')[0] - 1.0                                                # the relay window: J_R1's left end, less 1
    wx1 = (bx('J_R2')[1] + bx('J_O1')[0]) / 2 - bt / 2                         # the right wall: midway J_R2 / J_O1
    # the barrier's right wall stands in front of the board's edge (from the edge wall to the front wall), not between the
    # terminal blocks: it only has to sit between J_R2's and J_O1's wire entries, so 0.4 mm each side of them is enough
    assert bx('J_R2')[1] + 0.4 <= wx1 and wx1 + bt + 0.4 <= bx('J_O1')[0], 'no room for the barrier between J_R2 and J_O1'
    assert px1 + 0.5 <= wx0, 'the TRIG1 pocket runs into the relay window'
    zt_ = P['z_board'] + max(bx('J_R1')[4], bx('J_R2')[4]) + ACP['window_top']   # the window's top at its ends
    # the earth wire's niche: the edge wall steps forward round a low-voltage niche between it and the TRIG1 pocket, so the
    # wire to H3 (its ring terminal flat on H3, the tongue west, the crimp earth_west from H3) turns from west to south on
    # an earth_bend radius before it passes the niche's front wall; the niche stays clear of the cover's lip and the gland
    ex0 = ACP['earth_point'][0]; hx = ex0 - ACP['earth_west'] - ACP['earth_bend']
    nx0 = round(hx - ACP['earth_hole'] / 2 - 1.0 - bt, 2)                       # the niche's west wall, outside face
    yn = ya - ACP['earth_niche'] - bt                                             # its front wall's front face
    assert -ACP['earth_point'][1] - ACP['earth_bend'] - 1 >= yn + bt, "the earth wire's bend doesn't fit in the niche"
    lin_ = P['land_w'] - P['wall']
    assert nx0 >= ix0 + lin_ + 0.5 + 1.2 + 0.5, "the earth niche runs into the cover's lip"
    G_ = PARTS['gland']; gy_ = -G_['y']
    assert yn >= gy_ + G_['nut_d'] / 2 + 0.5 or nx0 >= ix0 + 6, "the earth niche runs into the gland's locknut"
    P['ac_barrier'] = [[round(v, 2) for v in b_] for b_ in [
        [ix0 - 0.01, yb_, nx0 + bt, ya, z0, zb],          # along the board edge, the left wall to the earth niche
        [nx0, yn, nx0 + bt, ya, z0, zb], [nx0, yn, px0 + bt, yn + bt, z0, zb],   # the earth niche: its west and front walls
        [px0, yp, px0 + bt, ya, z0, zb], [px0, yp, px1, yp + bt, z0, zb], [px1 - bt, yp, px1, ya, z0, zb],   # the pocket
        [px1 - bt, yb_, wx0, ya, z0, zb],                 # on to the relay window
        [wx0, yb_, wx1, ya, z0, P['z_board'] - ACP['sill_gap']],   # the sill under the window
        [wx1, iyf - 0.01, wx1 + bt, ya, z0, zb]]]         # the right side, to the front wall
    P['ac_curtain'] = [round(v, 2) for v in [wx0, wx1 + 0.01, yb_, ya, zt_, zc0 - 0.5, 4.0]]   # over the window, under the cover; 45-degree haunches 4 mm
    assert (wx1 - wx0) - 2 * 4.0 <= 28, 'the curtain over the relay window is too long a bridge'
    # the receptacle: in the bay, clear of the right wall and the pocket
    oc = [c for c in conns if c['kind'] == 'outlet']
    assert len(oc) == 1 and oc[0]['face'] == 'front', 'the AC case has one receptacle, on the front'
    ox, oy = oc[0]['X'], oc[0]['y']
    assert ix0 + 0.5 <= ox - OU['strap_l'] / 2 and ox + OU['strap_l'] / 2 <= wx1 - 0.5, 'the receptacle strap runs into a wall or the barrier'
    assert iyf + OU['body_d'] + 10 <= yp, 'under 10 mm between the receptacle body and the TRIG1 pocket'
    zo = P['z_row'] + oy
    assert zo - OU['body_w'] / 2 >= P['z_floor'] + 2.5, 'the receptacle body sits on the floor fillet'
    assert zo + OU['body_w'] / 2 <= P['z_top'] - P['land_h'] - (P['land_w'] - P['wall']) - 0.5, "the receptacle body reaches the rim land's underside"
    for c in conns:   # the low-voltage jacks behind the front plate: right of the barrier
        if c['face'] == 'front' and c['kind'] != 'outlet':
            assert c['X'] - EXT[c['kind']][0] / 2 >= wx1 + bt + 0.5, f"{c['id']}: its jack body is not right of the barrier"
    # the cord's gland, in the left wall's stretch of the bay
    G = PARTS['gland']; gy, gz = -G['y'], G['z']
    assert iyf + 1 <= gy - G['nut_d'] / 2 and gy + G['nut_d'] / 2 <= yb_ - 0.5, 'the gland locknut is not inside the mains bay'
    assert gz - G['nut_d'] / 2 >= P['z_floor'] + 2.0, 'the gland locknut sits on the floor fillet'
    for r_ in P['ribs']:
        assert abs((iyf + iyb) / 2 + r_ - gy) >= G['nut_d'] / 2 + 1.5, 'a wall rib is under the gland locknut: move it ([ac.box] ribs)'
    P['ac_gland'] = [gy, gz, G['hole_d'], G['nut_d'], G['cap_d']]
    # the earth wire's way to H3 (the board's plated earth point, tied to GND): a hole in the barrier's edge wall west of it
    ex, ey = ACP['earth_point'][0], -ACP['earth_point'][1]
    assert any(abs(h[0] - ex) < 0.5 and abs(h[1] - ey) < 0.5 for h in holes), 'earth_point is not one of the board\'s mounting holes'
    er = ACP['earth_hole'] / 2
    # in the niche's front wall (above), earth_west + earth_bend west of H3: the wire comes in heading north, turns east on
    # the earth_bend radius inside the niche and runs on to the ring terminal's crimp, earth_west west of H3
    assert nx0 + bt + 0.5 <= hx - er and hx + er + 0.5 <= px0, 'no room in the earth niche for its hole'
    P['ac_earth'] = [round(hx, 2), round(P['z_board'] + board['thick'] + 2.5, 2), ACP['earth_hole'], round(yn + bt, 2)]
    P['ac_niche_out'] = [round(v, 2) for v in [nx0 - 0.5, yn - 0.5, px0 + 0.5, ya + 1]]   # cut from the cover
    # the second earth lead, to H2 (back right, the board's second plated earth point, on GND; hardware 3ec6fe1): out of the
    # bay through a hole in the barrier's right wall, earth2_back in front of the edge wall, then in front of the board's
    # edge to its right side and back along it to H2 (README: Earth)
    e2x, e2y = ACP['earth2_point'][0], -ACP['earth2_point'][1]
    assert any(abs(h[0] - e2x) < 0.5 and abs(h[1] - e2y) < 0.5 for h in holes), 'earth2_point is not one of the board\'s mounting holes'
    y2 = ya - bt - ACP['earth2_back']
    assert iyf + 1 + er <= y2, 'no room on the barrier\'s right wall for the second earth hole'
    P['ac_earth2'] = [round(wx1 - 1, 2), round(y2, 2), round(P['z_board'] + board['thick'] + 2.5, 2), ACP['earth_hole']]
    # the lid's ribs: one each side of every full-height barrier wall, rib_gap off it, overlapping its top by `overlap`
    rg, rw = ACP['rib_gap'], 1.6
    for b_ in P['ac_barrier']:
        if b_[5] < zb - 0.1: continue   # the sill
        if b_[2] - b_[0] >= b_[3] - b_[1]:   # along x: ribs in front of it and behind it
            P['ac_ribs'] += [[b_[0], b_[1] - rg - rw, b_[2], b_[1] - rg], [b_[0], b_[3] + rg, b_[2], b_[3] + rg + rw]]
        else:
            P['ac_ribs'] += [[b_[0] - rg - rw, b_[1], b_[0] - rg, b_[3]], [b_[2] + rg, b_[1], b_[2] + rg + rw, b_[3]]]
    P['ac_ribs'] = [[round(v, 2) for v in r_] for r_ in P['ac_ribs']]
    # a tie point in front of each relay terminal, on the floor: a block with a cable-tie slot across it, so each relay's
    # conductors can be tied together and to it (one slipping out of its terminal stays in the bay)
    tw, td, th, sw, sh = ACP['tie']
    for n in ('J_R1', 'J_R2'):
        r_ = bx(n); P['ac_ties'].append([round((r_[0] + r_[1]) / 2, 2), round(yb_ - 3 - td / 2, 2)])
    P['ac_tie'] = ACP['tie']
    # the receptacle's seat: the strap so placed that its faces come out face_set proud of the plate (a recess in the
    # wall's inside face if > 0, pads on it if < 0)
    seat = P['plate_t'] + P['wall'] + ACP['face_set'] - OU['face_proud']
    assert -6 <= seat <= P['wall'] - 1.0, f"the receptacle seat ({seat:.2f} mm) leaves under 1 mm of wall, or wants pads over 6 mm"
    P['ac_seat'] = round(seat, 2)
    # the receptacle's stops: a post from the floor behind each end of its strap, between the body and the slot's nut, 0.3 mm
    # off the strap's back, so a plug pushed in can't push the strap off the wall (its screws only hold it in tension)
    oc = [c for c in conns if c['kind'] == 'outlet'][0]; ox, oz = oc['X'], P['z_row'] + oc['y']
    ys0 = iyf + 1.0 - seat + 0.3
    for sgn in (-1, 1):
        be, nut = ox + sgn * OU['body_l'] / 2, ox + sgn * (OU['screw_pitch'] / 2 - 4.5)   # the body's end, the nut's inner edge
        xa_, xb_ = sorted([be + sgn * 0.5, be + sgn * 3.5])
        assert (xb_ <= nut - 0.5) if sgn > 0 else (xa_ >= nut + 0.5), "no room for the receptacle's stop between its body and the strap's nut"
        P['ac_stops'].append([round(v, 2) for v in [xa_, ys0, P['z_floor'] - 0.01, xb_, ys0 + 6.0, oz + 8.0]])
    # the cover: the bay (left wall to the barrier's right wall, front wall to the edge wall), less the TRIG1 pocket, 0.5 mm
    # off every wall; and over the relay window back to a chimney round J_R1 / J_R2 (its two sides down to 1 mm over the
    # board, its back down to just over the relays), so the terminals' screws are under it too
    m_ = 0.5
    # the cover comes out straight up past the rim land: it stops 0.5 mm inboard of the land's inner edge on the left and
    # front walls; the strip left is closed by a labyrinth: a shelf on each wall under the land (45-degree underside), its
    # top 1 mm under the cover, and a lip under the cover's edge, hanging 2.5 mm, 0.5 mm off the shelf's edge
    lin = P['land_w'] - P['wall']; cs = lin + 0.5
    P['ac_shelf'] = [round(v, 2) for v in [lin, zc0 - 1.0]]                         # its depth off the wall, its top
    P['ac_lip'] = [round(v, 2) for v in [cs, 1.2, 2.5]]                            # off the wall, thick, hanging
    assert 0.5 <= cs - lin and P['ac_lip'][2] - 1.0 >= 1.0, 'the cover labyrinth: under 0.5 mm off the shelf or under 1 mm of overlap'
    assert max(3.0, lin) < cs, 'the left wall ribs reach the cover'
    k1, k2 = KO['K1'], KO['K2']
    # the chimney's outside faces: its side walls 0.5 mm off J_R1 / J_R2; the right one 1.6 mm thick (2.0 the left), so it
    # also keeps 0.5 mm off J_O1 in the 2.9 mm between the two terminal blocks
    hwl, hwr = 2.0, 1.6
    c0, c1 = bx('J_R1')[0] - 0.5 - hwl, bx('J_R2')[1] + 0.5 + hwr
    assert c1 + 0.5 <= bx('J_O1')[0], "the cover hood's right wall under 0.5 mm off J_O1"
    cyb = -(max(k1[5], k2[5])) + 0.4                                               # its back wall's front face, over the relays' front edge
    zk = P['z_board'] + max(k1[6], k2[6]) + 1.0                                     # its back wall's bottom, 1 mm over them
    P['ac_cover'] = [[round(v, 2) for v in r_] for r_ in [
        [ix0 + cs, iyf + cs, wx1 - m_, yb_ - m_],                                 # the bay, clear of the land
        [wx0 + m_, yb_ - m_, wx1 - m_, ya + m_],                                  # over the curtain
        [c0, ya + m_, c1, cyb + 2.0]]]                                            # over the chimney
    P['ac_pocket_out'] = [round(v, 2) for v in [px0 - m_, yp - m_, px1 + m_, ya + 1]]   # cut from the bay
    zl = P['z_board'] + board['thick'] + 1.5                                       # its sides' bottoms, 1.5 mm over the board
    ys_ = -(bx('J_R1')[2]) + 0.2                                                   # the terminal blocks' back, less a little
    P['ac_chimney'] = [[round(v, 2) for v in r_] for r_ in [
        [c0, ya + m_, c0 + hwl, ys_, zl, zc0 + 0.01], [c0, ys_ - 0.01, c0 + hwl, cyb + 2.0, zk, zc0 + 0.01],   # left side
        [c1 - hwr, ya + m_, c1, ys_, zl, zc0 + 0.01], [c1 - hwr, ys_ - 0.01, c1, cyb + 2.0, zk, zc0 + 0.01],   # right side
        [c0, cyb, c1, cyb + 2.0, zk, zc0 + 0.01]]]                                                         # back
    for n in KO:   # nothing on the board where the chimney's sides come down to it
        r_ = KO[n]
        for q in P['ac_chimney']:
            if r_[1] < q[2] and q[0] < r_[4] and -r_[5] < q[3] and q[1] < -r_[2] and P['z_board'] + r_[6] > q[4] and n not in ('J_R1', 'J_R2'):
                raise AssertionError(f'the cover chimney runs into {n} on the board')
    assert zc0 >= zo + OU['body_w'] / 2 + 8, "under 8 mm over the receptacle's body for its terminal screws and wires"
    assert wx1 - 0.5 <= c1 and c0 <= wx0 + 0.5, 'the chimney does not meet the barrier'
    # its screws: M3 countersunk into heat-set inserts in bosses on the bay's walls, their tops at the cover's underside
    # [x, y, normal x, normal y, reach]: the screw `reach` - 4 off its wall; the left wall's clear of the land (its head
    # inboard of the land's edge, so it comes out straight up), the others 4 mm off theirs
    rl = round(cs + P['screw_head'] / 2 + 0.5 + 4, 2)
    P['ac_cover_bosses'] = [[round(v, 2) for v in q] for q in [
        [ix0, iyf + 14, 1, 0, rl], [ix0, yb_ - 14, 1, 0, rl],                       # on the left wall
        [wx1, iyf + 10, -1, 0, 8], [wx1, yb_ - 12, -1, 0, 8],                      # on the barrier's right wall
        [(px0 + px1) / 2, yp, 0, -1, 8]]]                                          # on the TRIG1 pocket's front wall
    # the screwed front plate's insert bosses in the mains bay: their caps (behind the insert, between the bay's wiring and
    # the screw, whose head is on the show face) at least bay_cap thick
    fw = walls['front']; lb = round(P['insert_depth'] + ACP['bay_cap'] - P['wall'], 2)
    for i, sc in enumerate(fw['screws']):
        if ix0 <= sc[0] - P['boss_w'] / 2 and sc[0] - P['boss_w'] / 2 <= wx1:
            fw['boss_lens'][i] = max(fw['boss_lens'][i], lb); fw['nuts'][i] = 0   # a blind insert: no nut or screw end in the bay
            for w in fw['windows']:   # a top boss's longer 45-degree wedge still clear of the windows under it
                if sc[1] > 0 and w[0] < sc[0] + P['boss_w'] / 2 and sc[0] - P['boss_w'] / 2 < w[1]:
                    assert w[3] + 0.4 <= sc[1] - P['boss_h'] / 2 - lb, 'a mains-bay insert boss wedge hangs over a window'
    for q in P['ac_cover_bosses']:   # the second earth hole well under the cover bosses' 45-degree wedges
        assert P['ac_earth2'][2] + er + 1 <= P['ac_cover_z'] - 8 - q[4], 'the second earth hole runs into a cover boss'
    for q in P['ac_cover_bosses']:   # each screw under the cover, its head clear of the land (straight up and out)
        sx, sy = q[0] + (q[4] - 4) * q[2], q[1] + (q[4] - 4) * q[3]
        assert sx - P['screw_head'] / 2 >= ix0 + lin + 0.3 and sy - P['screw_head'] / 2 >= iyf + lin + 0.3, 'a cover screw head is under the rim land'
        assert any(r_[0] + 0.3 <= sx - P['screw_head'] / 2 and sx + P['screw_head'] / 2 <= r_[2] - 0.3 and r_[1] + 0.3 <= sy - P['screw_head'] / 2 and sy + P['screw_head'] / 2 <= r_[3] - 0.3 for r_ in P['ac_cover']), \
            'a cover screw is not on the cover'
    for r_ in P['ac_cover']:   # the way out: straight up, everything of the cover inside the land's opening
        assert r_[0] >= ix0 + lin + 0.5 - 1e-6 and r_[1] >= iyf + lin + 0.5 - 1e-6, 'the cover runs under the rim land'
    assert zc0 - 16 >= P['ac_tie'][2] + P['z_floor'] + 10, "the cover bosses' wedges come down near the tie points"

data = {'note': 'generated by tools/print_params.py; edit that file, not this one', 'P': P, 'holes': holes,
        'board': {'w': board['w'], 'h': board['h']}, 'conns': conns, 'parts': PARTS, 'walls': walls}
if '--stdout' in sys.argv: print(json.dumps(data)); sys.exit()
json.dump(data, open(os.path.join(OUT, 'params.json'), 'w'), indent=1)

def scad(v):
    if isinstance(v, bool): return 'true' if v else 'false'
    if isinstance(v, str): return json.dumps(v)
    if isinstance(v, (list, tuple)): return '[' + ', '.join(scad(x) for x in v) + ']'
    if isinstance(v, float): return repr(round(v, 4))
    return str(v)

L = ['// generated by tools/print_params.py from design.toml and the board; edit design.toml, not this file', '']
for k, v in P.items(): L.append(f'{k} = {scad(v)};')
L.append(f'board_w = {board["w"]}; board_h = {board["h"]};')
L.append(f'board_holes = {scad(holes)};')
# the board's parts as keep-out boxes, for the assembled view: [x0, y0, z0, x1, y1, z1] in world X/Y, z above the board's bottom
_ko = json.load(open(os.path.join(ROOT, 'board-parts.json')))
_kb = [[round(x0, 2), round(-y1, 2), round(max(z0, board['thick']), 2), round(x1, 2), round(-y0, 2), round(z1, 2)]
       for n, x0, y0, z0, x1, y1, z1, kind in _ko if z1 - max(z0, board['thick']) >= 0.5 and x1 - x0 >= 0.3 and y1 - y0 >= 0.3]
L.append(f'board_parts = {scad(_kb)};')
L.append('// [id, label, face, X, kind]')
L.append('conns = ' + scad([[c['id'], c['label'], c['face'], c['X'], c['kind']] for c in conns]) + ';')
for k, d in PARTS.items():
    for kk, vv in d.items():
        if kk in ('name', 'assumed'): continue
        L.append(f'{k}_{kk} = {scad(vv)};')
for face, w in walls.items():
    for k, v in w.items(): L.append(f'{face}_{k} = {scad(list(v.values()) if isinstance(v, dict) else v)};')   # seal: [rect, discs, cuts]
open(os.path.join(OUT, 'params.scad'), 'w').write('\n'.join(L) + '\n')
print('params.json, params.scad:', len(conns), 'connectors; interior', ix1 - ix0, 'x', iyb - iyf, 'x', P['inner_h'],
      '; z_row', round(P['z_row'], 2), 'z_top', P['z_top'])
