#!/usr/bin/env python3
"""Write the layout block of fusion/SpecterEnclosure/SpecterEnclosure.py from params.json.

    python3 tools/fusion_layout.py            # rewrite the block in place
    python3 tools/fusion_layout.py --check    # exit 1 if the block is out of date

The Fusion script models the case with screwed faceplates and the hinged lid (not the default inset plates or screwed lid). Its sizes and the
faceplate layout come from the same params.json as the OpenSCAD model, moved to the Fusion design's origin: the centre
of the box, on the underside of the floor. On a faceplate, u is mm to the right of the box's centreline seen from
outside that face and v is mm above the jack row.
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, 'fusion', 'SpecterEnclosure', 'SpecterEnclosure.py')
BEGIN, END = '# ---- layout: begin', '# ---- layout: end'

D = json.load(open(os.path.join(ROOT, 'params.json')))
P = D['P']
assert P['mount'] == 'screws' and P.get('lid_mount') == 'hinge', 'set [plates] mount = "screws" and [lid] lid_mount = "hinge" first'
XC, YC = (P['ix0'] + P['ix1']) / 2, (P['iyf'] + P['iyb']) / 2


def r(v, n=3):
    v = round(float(v), n)
    return 0.0 if v == 0 else v


def u_of(face, x):   # plate frame x -> u (right of the centreline, seen from outside)
    return r(x - XC) if face == 'front' else r(x + XC)


def face_block(face):
    w = D['walls'][face]
    kind = {'j35p': 'j35', 'dcp': 'dc', 'usb': 'usb'}
    jacks = [(c[0], kind[c[1]], u_of(face, c[2])) for c in w['conns']]
    for c in w['conns']: assert c[3] == 0.0, 'every jack sits on the row'
    wins = [(u_of(face, a), u_of(face, b), r(c), r(d)) for a, b, c, d, td in w['windows'] if not td]
    assert all(not x[4] for x in w['windows']), 'teardrop windows (USB) are not modelled'
    labels = [(t, u_of(face, x), r(base), r(size)) for t, x, row, size, base in w['labels']]
    seal = w['seal']
    out = {
        'plate': (u_of(face, w['x0']), u_of(face, w['x1']), r(w['pf_s'][0]), r(w['pf_s'][1])),
        'plate_top_glued': r(w['pf'][1]),
        'jacks': jacks,
        'windows': wins,
        'pins': [(u_of(face, x), r(y)) for x, y in w['pins']],
        'screws': [(u_of(face, x), r(y)) for x, y in w['screws']],
        'seal_rect': (u_of(face, seal['rect'][0]), u_of(face, seal['rect'][1]), r(seal['rect'][2]), r(seal['rect'][3])),
        'seal_cuts': [(u_of(face, a), u_of(face, b), r(y)) for a, b, y in seal['cuts']],
        'notches': [(u_of(face, a), u_of(face, b)) for a, b in w['notches']],
        'labels': labels,
    }
    assert set(w['boss_lens']) == {P['boss_len']} and set(w['nuts']) == {1}, 'every boss is a nut boss of boss_len'
    for d_ in seal['discs']: assert abs(d_[2] - (P['sg_land'] + P['sg_w'] / 2)) < 1e-6
    return out


def block():
    holes = D['holes']
    hx = sorted(r(x - XC) for x in P['hinge_xs'])
    lat = sorted({r(abs(s - YC)) for _, s in P['latches']})
    assert hx[0] == -hx[1] and len(lat) == 1 and {l[0] for l in P['latches']} == {'left', 'right'}, 'hinges and latches are symmetric'
    pcb = {
        'left': r(-XC), 'back': r(-YC),
        'holes': [(r(x - XC), r(y - YC)) for x, y in holes],
    }
    L = []
    L.append(BEGIN + ' (tools/fusion_layout.py writes this block from params.json; edit design.toml, rebuild, rerun it)')
    L.append(f'PCB = {pcb!r}')
    L.append(f'HINGE_X = {hx[1]!r}   # each hinge\'s centre, either side of the centreline')
    L.append(f'LATCH_Y = {lat[0]!r}   # each latch\'s centre on the end walls, either side of the centreline')
    L.append(f'BUTTON = {(r(P["button_xy"][0] - XC), r(P["button_xy"][1] - YC))!r}')
    L.append(f'TITLE = {P["title"]!r}')
    for face in ('front', 'back'):
        b = face_block(face)
        L.append(f'{face.upper()} = {{')
        for k, v in b.items(): L.append(f'    {k!r}: {v!r},')
        L.append('}')
    L.append('# Sizes, mm (Fusion user parameters: name, value, comment)')
    L.append('PARAMETERS = [')
    for name, expr, comment in parameters():
        L.append(f'    ({name!r}, {expr!r}, {comment!r}),')
    L.append(']')
    L.append(END)
    return '\n'.join(L)


def mm(v): return f'{r(v)} mm'


def parameters():
    g = P.get
    out = [
        # box
        ('inner_l', mm(g('ix1') - g('ix0')), 'inside length, left to right'),
        ('inner_w', mm(g('iyb') - g('iyf')), 'inside depth, front to back'),
        ('inner_h', mm(g('inner_h')), 'inside height, floor to rim'),
        ('wall', mm(g('wall')), 'wall thickness'),
        ('floor_t', mm(g('floor_t')), 'floor thickness'),
        ('corner_r', mm(g('corner_r')), 'outside corner radius'),
        ('outer_l', 'inner_l + 2 * wall', ''),
        ('outer_w', 'inner_w + 2 * wall', ''),
        ('box_h', 'floor_t + inner_h', 'rim height'),
        ('floor_fillet', '2 mm', '45-degree fillet where the floor meets the walls'),
        ('rib_t', '1.6 mm', 'end-wall ribs, thickness'),
        ('rib_d', '3 mm', 'end-wall ribs, depth off the wall'),
        ('rib_y', mm(abs(g('ribs')[0])), 'end-wall ribs, either side of the centreline'),
        # rim and lid seal
        ('land_w', mm(g('land_w')), 'rim land width, from the outside face'),
        ('land_h', mm(g('land_h')), 'rim land depth (a 45-degree underside below it)'),
        ('groove_w', mm(g('groove_w')), 'seal groove in the rim, width'),
        ('groove_d', mm(g('groove_d')), 'seal groove in the rim, depth'),
        # board and standoffs
        ('pcb_t', mm(g('board_t')), 'board thickness'),
        ('standoff_h', mm(g('standoff_h')), 'board standoffs, height off the floor'),
        ('standoff_d', '8 mm', 'board standoffs, diameter'),
        ('standoff_hole', mm(g('standoff_hole')), 'standoff hole: 4.0 heat-set insert, 2.6 self-tapping'),
        ('standoff_depth', mm(g('standoff_depth')), ''),
        ('row_v', mm(g('row_v')), 'jack row height above the board'),
        ('row_z', 'floor_t + standoff_h + pcb_t + row_v', 'jack row, from the underside'),
        # feet
        ('feet_in', mm(g('feet_in')), 'feet bosses, in from the inside corners'),
        ('feet_boss_d', mm(g('feet_boss_d')), ''),
        ('feet_boss_h', mm(g('feet_boss_h')), ''),
        ('feet_pilot', mm(g('feet_pilot')), 'rubber feet screws, pilot'),
        ('feet_depth', mm(g('feet_depth')), 'pilot depth from the underside'),
        # faceplates
        ('plate_t', mm(g('plate_t')), 'faceplate thickness'),
        ('plate_r', mm(g('plate_r')), 'faceplate corner radius'),
        ('plate_chamfer', mm(g('plate_chamfer')), '45-degree chamfer round the back edge'),
        ('hole_j35', mm(g('hole_j35p')), '3.5 mm jack hole'),
        ('hole_dc', mm(g('hole_dcp')), 'DC barrel jack hole'),
        ('j35_behind', mm(D['parts']['j35p']['behind_d']), '3.5 mm jack body behind the plate'),
        ('dc_behind', mm(D['parts']['dcp']['behind_d']), 'DC jack behind the plate'),
        ('win_m', mm(g('win_m')), 'wall windows: clearance each side'),
        ('win_r', '2 mm', 'wall windows: corner radius'),
        ('j35_seat', mm(g('j35_seat')), 'plate left under a 3.5 mm jack (counterbored from the back)'),
        ('inlay', mm(g('inlay')), 'label inlay depth'),
        ('screw_d', mm(g('screw_d')), 'faceplate screws, clearance'),
        ('screw_head', mm(g('screw_head')), 'countersink diameter'),
        ('plate_pilot', mm(g('plate_pilot')), 'faceplate screws, pilot through wall and boss'),
        ('boss_w', mm(g('boss_w')), 'screw bosses behind the wall'),
        ('boss_h', mm(g('boss_h')), ''),
        ('boss_len', mm(g('boss_len')), 'how far a boss reaches in'),
        ('nut_af', mm(g('nut_af')), 'hex nut across flats'),
        ('nut_t', mm(g('nut_t')), 'hex nut thickness'),
        ('nut_clr', mm(g('nut_clr')), 'nut pocket clearance'),
        ('sg_w', mm(g('sg_w')), 'plate seal groove, width'),
        ('sg_d', mm(g('sg_d')), 'plate seal groove, depth'),
        ('sg_land', mm(g('sg_land')), 'groove to screw centre'),
        ('sg_round', mm(g('sg_round')), 'groove path corner radius'),
        ('guide_pin_d', mm(g('pin_d')), 'faceplate guide pins'),
        ('guide_pin_l', mm(g('pin_l')), ''),
        ('pin_clr', mm(g('pin_clr')), 'guide pin hole clearance (radial)'),
        # lid
        ('seal_gap', mm(g('seal_gap')), 'rim to lid underside, closed'),
        ('lid_t', mm(g('lid_t')), 'lid thickness'),
        ('lid_gap', mm(g('lid_gap')), 'lid skirt to box, each side'),
        ('skirt_t', mm(g('skirt_t')), 'lid skirt thickness'),
        ('drip', mm(g('drip')), 'lid skirt depth'),
        ('tongue_w', mm(g('tongue_w')), 'lid tongue into the rim groove'),
        ('tongue_h', mm(g('tongue_h')), ''),
        ('lid_rib_h', mm(g('lid_rib_h')), 'lid ribs, depth'),
        ('lid_rib_t', '1.6 mm', 'lid ribs, thickness'),
        ('lid_rib_pitch', mm(g('lid_rib_pitch')), ''),
        ('hole_button', mm(g('hole_button')), 'TRIG2 push button hole'),
        # hinge
        ('knuckle_d', mm(g('knuckle_d')), 'hinge knuckles, diameter'),
        ('knuckle_l', mm(g('knuckle_l')), 'hinge knuckles, length'),
        ('knuckle_gap', mm(g('knuckle_gap')), 'between knuckles'),
        ('knuckle_off', mm(g('knuckle_off')), 'knuckle clear of the back wall'),
        ('pin_d', mm(g('pin_d')), 'hinge and latch screws, clearance (M3 or #4)'),
        ('pin_pilot', mm(g('pin_pilot')), 'hinge and latch screws, pilot in the far knuckle or ear'),
        # cam latches
        ('latch_w', mm(g('latch_w')), 'latch lever width'),
        ('latch_drop', mm(g('latch_drop')), 'pivot below the rim'),
        ('latch_bite', mm(g('latch_bite')), 'how far the latch draws the lid down'),
        ('latch_lead', mm(g('latch_lead')), 'keeper above a seated lid at first catch'),
        ('latch_detent', mm(g('latch_detent')), 'over-centre dip at the end of travel'),
        ('latch_dip', f"{r(g('latch_dip'))} deg", ''),
        ('latch_travel', f"{r(g('latch_travel'))} deg", 'lever travel from first catch to closed'),
        ('ear_t', mm(g('ear_t')), 'thin latch ear and lug (the screw threads in)'),
        ('pin_span', mm(g('pin_span')), 'hinge and latch screw length'),
    ]
    return out


def main():
    src = open(SCRIPT).read()
    new = block()
    m = re.search(re.escape(BEGIN) + r'.*?' + re.escape(END), src, re.S)
    if not m: sys.exit(f'{SCRIPT}: no layout block')
    out = src[:m.start()] + new + src[m.end():]
    if '--check' in sys.argv:
        sys.exit(0 if out == src else 'the layout block is out of date: run tools/fusion_layout.py')
    if out != src: open(SCRIPT, 'w').write(out)


if __name__ == '__main__':
    main()
