"""Specter Proptroller enclosure: builds the parametric model in a new Fusion design.

Run it from Utilities > Add-Ins > Scripts and Add-Ins: + (Script or add-in from device), pick this folder, Run. It opens
a new design and builds, in about a minute:

    Tub            the box: walls, floor, rim land and seal groove, board standoffs, feet bosses, screw bosses with nut
                   pockets, wall windows behind the faceplates, hinge knuckles, latch ears
    Lid            lid, skirt, tongue, ribs, hinge knuckle, latch keeper lugs, TRIG2 button hole, inlaid title
    Front plate    faceplate: jack holes, countersunk M3 screw holes, guide pins, seal groove, inlaid labels
    Back plate     the same, notched under the hinges
    Latch          the cam latch lever, four occurrences, closed

Everything is in place as assembled, Z up, the origin at the centre of the box on the underside of the floor; X runs
left to right as seen from the front, Y front to back. Sizes are user parameters (Modify > Change Parameters) and
the main sketches are dimensioned to them: box size, wall, floor, rim, lid, faceplate size and thickness, hole sizes,
jack positions. Derived detail (hinge and latch profiles, the faceplate seal groove's path round the screws, labels)
is drawn from the parameter values when the script runs and does not follow later edits; rerun the script after
changing those, or edit the sketch.

Sizes and the faceplate layout come from the OpenSCAD model's design.toml (through params.json); the block between
the layout markers is written by tools/fusion_layout.py. Built as the standard case: screwed faceplates, printed-pin
hinge, cam latches. Print every part in ASA; the inlays (bodies named "... text") are the second colour.
"""
import math
import traceback

import adsk.core
import adsk.fusion

# ---- layout: begin (tools/fusion_layout.py writes this block from params.json; edit design.toml, rebuild, rerun it)
PCB = {'left': -66.5, 'back': 40.0, 'holes': [(-62.5, 36.0), (63.5, 36.0), (-62.5, -36.0), (63.5, -36.0)]}
HINGE_X = 33.31   # each hinge's centre, either side of the centreline
LATCH_Y = 48.0   # each latch's centre on the end walls, either side of the centreline
BUTTON = (70.5, 34.0)
TITLE = 'SPECTER PROPTROLLER'
FRONT = {
    'plate': (-77.21, 77.21, -20.4, 20.7),
    'plate_top_glued': 14.3,
    'jacks': [('TRIG1', 'j35', -61.21), ('RLY1', 'j35', -35.39), ('RLY2', 'j35', -17.39), ('OUT1', 'dc', 0.61), ('OUT2', 'dc', 20.81), ('OUT3', 'dc', 41.01), ('OUT4', 'dc', 61.21)],
    'windows': [(-66.71, -55.71, -5.5, 5.5), (-40.89, -29.89, -5.5, 5.5), (-22.89, -11.89, -5.5, 5.5), (-5.99, 7.21, -6.6, 6.6), (14.21, 27.41, -6.6, 6.6), (34.41, 47.61, -6.6, 6.6), (54.61, 67.81, -6.6, 6.6)],
    'pins': [(-48.3, 0.0), (10.71, -5.5), (51.11, -5.5)],
    'screws': [(-72.56, -15.75), (0.0, -15.75), (72.56, -15.75), (-72.56, 16.05), (0.0, 16.05), (72.56, 16.05)],
    'seal_rect': (-72.56, 72.56, -15.75, 16.05),
    'seal_cuts': [],
    'notches': [],
    'labels': [('TRIG1', -61.21, 9.1, 4.0), ('RLY1', -35.39, 9.1, 4.0), ('RLY2', -17.39, 9.1, 4.0), ('OUT1', 0.61, 9.1, 4.0), ('OUT2', 20.81, 9.1, 4.0), ('OUT3', 41.01, 9.1, 4.0), ('OUT4', 61.21, 9.1, 4.0), ('T=NO R=NC S=COM', -26.39, -12.1, 3.0)],
}
BACK = {
    'plate': (-80.29, 80.29, -17.45, 20.7),
    'plate_top_glued': 14.3,
    'jacks': [('LINE', 'j35', -62.64), ('SPK', 'j35', -35.1), ('PWR', 'dc', -17.33), ('CC1', 'j35', 18.64), ('CC2', 'j35', 30.64), ('TRIG2', 'j35', 62.64)],
    'windows': [(-68.14, -57.14, -5.5, 5.5), (-40.6, -29.6, -5.5, 5.5), (-23.93, -10.73, -6.6, 6.6), (13.14, 36.14, -5.5, 5.5), (57.14, 68.14, -5.5, 5.5)],
    'pins': [(1.2, 0.0), (71.14, 0.0)],
    'screws': [(-75.64, -12.8), (0.0, -12.8), (75.64, -12.8), (-75.64, 16.05), (0.0, 16.05), (75.64, 16.05)],
    'seal_rect': (-75.64, 76.79, -12.8, 16.05),
    'seal_cuts': [(-51.01, -15.61, 10.8), (15.61, 51.01, 10.8)],
    'notches': [(-47.51, -19.11), (19.11, 47.51)],
    'labels': [('LINE', -62.64, 9.1, 4.0), ('SPK', -35.1, 9.1, 4.0), ('PWR', -17.33, 9.1, 4.0), ('CC1', 18.64, 9.1, 3.89), ('CC2', 30.64, 9.1, 3.89), ('TRIG2', 62.64, 9.1, 4.0)],
}
# Sizes, mm (Fusion user parameters: name, value, comment)
PARAMETERS = [
    ('inner_l', '195.0 mm', 'inside length, left to right'),
    ('inner_w', '156.0 mm', 'inside depth, front to back'),
    ('inner_h', '51.0 mm', 'inside height, floor to rim'),
    ('wall', '2.4 mm', 'wall thickness'),
    ('floor_t', '2.0 mm', 'floor thickness'),
    ('corner_r', '4.0 mm', 'outside corner radius'),
    ('outer_l', 'inner_l + 2 * wall', ''),
    ('outer_w', 'inner_w + 2 * wall', ''),
    ('box_h', 'floor_t + inner_h', 'rim height'),
    ('floor_fillet', '2 mm', '45-degree fillet where the floor meets the walls'),
    ('rib_t', '1.6 mm', 'end-wall ribs, thickness'),
    ('rib_d', '3 mm', 'end-wall ribs, depth off the wall'),
    ('rib_y', '40.0 mm', 'end-wall ribs, either side of the centreline'),
    ('land_w', '7.0 mm', 'rim land width, from the outside face'),
    ('land_h', '6.0 mm', 'rim land depth (a 45-degree underside below it)'),
    ('groove_w', '3.0 mm', 'seal groove in the rim, width'),
    ('groove_d', '2.0 mm', 'seal groove in the rim, depth'),
    ('pcb_t', '1.6 mm', 'board thickness'),
    ('standoff_h', '10.0 mm', 'board standoffs, height off the floor'),
    ('standoff_d', '8 mm', 'board standoffs, diameter'),
    ('standoff_hole', '4.0 mm', 'standoff hole: 4.0 heat-set insert, 2.6 self-tapping'),
    ('standoff_depth', '7.0 mm', ''),
    ('row_v', '12.0 mm', 'jack row height above the board'),
    ('row_z', 'floor_t + standoff_h + pcb_t + row_v', 'jack row, from the underside'),
    ('feet_in', '12.0 mm', 'feet bosses, in from the inside corners'),
    ('feet_boss_d', '9.0 mm', ''),
    ('feet_boss_h', '6.0 mm', ''),
    ('feet_pilot', '2.5 mm', 'rubber feet screws, pilot'),
    ('feet_depth', '7.0 mm', 'pilot depth from the underside'),
    ('plate_t', '3.0 mm', 'faceplate thickness'),
    ('plate_r', '3.0 mm', 'faceplate corner radius'),
    ('plate_chamfer', '1.0 mm', '45-degree chamfer round the back edge'),
    ('hole_j35', '8.0 mm', '3.5 mm jack hole'),
    ('hole_dc', '12.0 mm', 'DC barrel jack hole'),
    ('j35_behind', '9.8 mm', '3.5 mm jack body behind the plate'),
    ('dc_behind', '12.0 mm', 'DC jack behind the plate'),
    ('win_m', '0.6 mm', 'wall windows: clearance each side'),
    ('win_r', '2 mm', 'wall windows: corner radius'),
    ('j35_seat', '1.75 mm', 'plate left under a 3.5 mm jack (counterbored from the back)'),
    ('inlay', '0.4 mm', 'label inlay depth'),
    ('screw_d', '3.4 mm', 'faceplate screws, clearance'),
    ('screw_head', '6.3 mm', 'countersink diameter'),
    ('plate_pilot', '2.5 mm', 'faceplate screws, pilot through wall and boss'),
    ('boss_w', '10.0 mm', 'screw bosses behind the wall'),
    ('boss_h', '8.5 mm', ''),
    ('boss_len', '4.75 mm', 'how far a boss reaches in'),
    ('nut_af', '5.5 mm', 'hex nut across flats'),
    ('nut_t', '2.4 mm', 'hex nut thickness'),
    ('nut_clr', '0.2 mm', 'nut pocket clearance'),
    ('sg_w', '3.5 mm', 'plate seal groove, width'),
    ('sg_d', '1.6 mm', 'plate seal groove, depth'),
    ('sg_land', '3.6 mm', 'groove to screw centre'),
    ('sg_round', '1.2 mm', 'groove path corner radius'),
    ('guide_pin_d', '3.3 mm', 'faceplate guide pins'),
    ('guide_pin_l', '4.0 mm', ''),
    ('pin_clr', '0.15 mm', 'guide pin hole clearance (radial)'),
    ('seal_gap', '0.4 mm', 'rim to lid underside, closed'),
    ('lid_t', '2.4 mm', 'lid thickness'),
    ('lid_gap', '1.0 mm', 'lid skirt to box, each side'),
    ('skirt_t', '2.0 mm', 'lid skirt thickness'),
    ('drip', '4.0 mm', 'lid skirt depth'),
    ('tongue_w', '1.6 mm', 'lid tongue into the rim groove'),
    ('tongue_h', '0.7 mm', ''),
    ('lid_rib_h', '5.0 mm', 'lid ribs, depth'),
    ('lid_rib_t', '1.6 mm', 'lid ribs, thickness'),
    ('lid_rib_pitch', '40.0 mm', ''),
    ('hole_button', '10.4 mm', 'TRIG2 push button hole'),
    ('knuckle_d', '8.0 mm', 'hinge knuckles, diameter'),
    ('knuckle_l', '6.4 mm', 'hinge knuckles, length'),
    ('knuckle_gap', '0.6 mm', 'between knuckles'),
    ('knuckle_off', '1.25 mm', 'knuckle clear of the back wall'),
    ('pin_d', '3.3 mm', 'hinge and latch screws, clearance (M3 or #4)'),
    ('pin_pilot', '2.5 mm', 'hinge and latch screws, pilot in the far knuckle or ear'),
    ('latch_w', '10.0 mm', 'latch lever width'),
    ('latch_drop', '12.0 mm', 'pivot below the rim'),
    ('latch_bite', '0.4 mm', 'how far the latch draws the lid down'),
    ('latch_lead', '0.5 mm', 'keeper above a seated lid at first catch'),
    ('latch_detent', '0.4 mm', 'over-centre dip at the end of travel'),
    ('latch_dip', '6.0 deg', ''),
    ('latch_travel', '16.0 deg', 'lever travel from first catch to closed'),
    ('ear_t', '3.6 mm', 'thin latch ear and lug (the screw threads in)'),
    ('pin_span', '20.4 mm', 'hinge and latch screw length'),
]
# ---- layout: end

FONT = 'Arial'          # metric-compatible with the OpenSCAD model's Liberation Sans
CAP_HEIGHT = 0.9555     # OpenSCAD text: capital height / size (Liberation Sans Bold)

app = adsk.core.Application.get()
design = None


# ------------------------------------------------------------------------------------------------ small helpers

def mm(v):
    return f'{v:.4f} mm'


def val(expr):
    """A ValueInput from an expression ('wall * 2') or a number of mm."""
    return adsk.core.ValueInput.createByString(expr if isinstance(expr, str) else mm(expr))


def p3(x, y, z=0.0):
    return adsk.core.Point3D.create(x / 10, y / 10, z / 10)


def v3(x, y, z):
    return adsk.core.Vector3D.create(x, y, z)


def par(name):
    """A user parameter's current value in mm (or degrees for angles)."""
    p = design.userParameters.itemByName(name)
    return math.degrees(p.value) if p.unit == 'deg' else p.value * 10


def collection(items):
    c = adsk.core.ObjectCollection.create()
    for i in items:
        c.add(i)
    return c


def dot(a, b):
    return a.x * b.x + a.y * b.y + a.z * b.z


class Frame:
    """A 2D drawing frame in model space: world = origin + u * ux + v * uy (mm)."""
    def __init__(self, origin, ux, uy):
        self.o, self.ux, self.uy = origin, ux, uy

    def world(self, u, v):
        o, a, b = self.o, self.ux, self.uy
        return (o[0] + u * a[0] + v * b[0], o[1] + u * a[1] + v * b[1], o[2] + u * a[2] + v * b[2])

    def local(self, p):
        d = [p[i] - self.o[i] for i in range(3)]
        return (sum(d[i] * self.ux[i] for i in range(3)), sum(d[i] * self.uy[i] for i in range(3)))


# Faceplate frames: u right of the centreline seen from outside, v up from the jack row, w out of the wall's face.
def plate_frame(face, w=0.0):
    ow = par('outer_w') / 2
    if face == 'front':
        return Frame((0, -ow - w, par('row_z')), (1, 0, 0), (0, 0, 1))
    return Frame((0, ow + w, par('row_z')), (-1, 0, 0), (0, 0, 1))


def wall_normal(face):
    return v3(0, -1, 0) if face == 'front' else v3(0, 1, 0)


class Sketch:
    """A sketch drawn in a Frame: points are given in frame mm and converted to sketch space."""
    def __init__(self, comp, plane, name, frame):
        self.comp, self.f = comp, frame
        self.sk = comp.sketches.add(plane)
        self.sk.name = name
        self.sk.isComputeDeferred = True
        a, b = self.sk_vec(frame.ux), self.sk_vec(frame.uy)
        self.flipped = a[0] * b[1] - a[1] * b[0] < 0   # frame u, v run clockwise in sketch space
        self.u_horizontal = abs(a[0]) > abs(a[1])
        self.o_uv = frame.local((0, 0, 0))            # the sketch origin (the model origin projected), in frame mm

    def sk_vec(self, d):
        o = self.sk.modelToSketchSpace(p3(*self.f.o))
        q = self.sk.modelToSketchSpace(p3(*[self.f.o[i] + d[i] for i in range(3)]))
        return (q.x - o.x, q.y - o.y)

    def pt(self, u, v):
        return self.sk.modelToSketchSpace(p3(*self.f.world(u, v)))

    @property
    def lines(self):
        return self.sk.sketchCurves.sketchLines

    @property
    def cons(self):
        return self.sk.geometricConstraints

    @property
    def dims(self):
        return self.sk.sketchDimensions

    def line(self, a, b, construction=False):
        ln = self.lines.addByTwoPoints(a if not isinstance(a, tuple) else self.pt(*a),
                                       b if not isinstance(b, tuple) else self.pt(*b))
        ln.isConstruction = construction
        return ln

    def arc(self, c, a, sweep_deg):
        """Arc about c from a through sweep_deg (counter-clockwise in the frame when positive). Returns the arc and its
        sketch points at the frame start and end."""
        t = math.atan2(a[1] - c[1], a[0] - c[0]) + math.radians(sweep_deg)
        r = math.hypot(a[0] - c[0], a[1] - c[1])
        b = (c[0] + r * math.cos(t), c[1] + r * math.sin(t))
        ccw = (sweep_deg > 0) != self.flipped          # counter-clockwise in sketch space
        arc = self.sk.sketchCurves.sketchArcs.addByCenterStartSweep(self.pt(*c), self.pt(*(a if ccw else b)),
                                                                    math.radians(abs(sweep_deg)))
        s0, s1 = arc.startSketchPoint, arc.endSketchPoint
        return (arc, s0, s1) if ccw else (arc, s1, s0)

    def circle(self, c, d):
        return self.sk.sketchCurves.sketchCircles.addByCenterRadius(self.pt(*c), d / 20)

    def polyline(self, pts, closed=True):
        sp = [self.pt(*p) for p in pts]
        out = []
        for i in range(len(sp) - (0 if closed else 1)):
            a = out[-1].endSketchPoint if out else sp[i]
            b = out[0].startSketchPoint if closed and i == len(sp) - 1 else sp[(i + 1) % len(sp)]
            out.append(self.lines.addByTwoPoints(a, b))
        return out

    def loop(self, segs):
        """A closed chain of ('line', p0, p1) and ('arc', centre, p0, sweep_deg) in frame mm, joined end to end."""
        made = []
        for s in segs:
            if s[0] == 'line':
                start = made[-1][2] if made else self.pt(*s[1])
                made.append(self._line_from(start, s[2]))
            else:
                arc, a0, a1 = self.arc(s[1], s[2], s[3])
                if made:
                    self.cons.addCoincident(made[-1][2], a0)
                made.append((arc, a0, a1))
        self.cons.addCoincident(made[-1][2], made[0][1])
        return [m[0] for m in made]

    def _line_from(self, start, p1):
        ln = self.lines.addByTwoPoints(start, self.pt(*p1))
        return (ln, ln.startSketchPoint, ln.endSketchPoint)

    def dim_h(self, a, b, expr, at):
        """Distance along frame u between two sketch points, driven by expr."""
        o = (adsk.fusion.DimensionOrientations.HorizontalDimensionOrientation if self.u_horizontal
             else adsk.fusion.DimensionOrientations.VerticalDimensionOrientation)
        d = self.dims.addDistanceDimension(a, b, o, self.pt(*at))
        d.parameter.expression = expr
        return d

    def dim_v(self, a, b, expr, at):
        o = (adsk.fusion.DimensionOrientations.VerticalDimensionOrientation if self.u_horizontal
             else adsk.fusion.DimensionOrientations.HorizontalDimensionOrientation)
        d = self.dims.addDistanceDimension(a, b, o, self.pt(*at))
        d.parameter.expression = expr
        return d

    def dim_d(self, circle, expr):
        c = circle.centerSketchPoint.geometry
        d = self.dims.addDiameterDimension(circle, adsk.core.Point3D.create(c.x + circle.radius, c.y + circle.radius, 0))
        d.parameter.expression = expr
        return d

    def dim_r(self, arc, expr):
        c = arc.centerSketchPoint.geometry
        d = self.dims.addRadialDimension(arc, adsk.core.Point3D.create(c.x + arc.radius, c.y + arc.radius, 0))
        d.parameter.expression = expr
        return d

    def done(self):
        self.sk.isComputeDeferred = False
        return self.sk

    # -------------------------------------------------- dimensioned shapes

    def origin_point(self):
        return self.sk.originPoint

    def rounded_rect(self, u0, v0, u1, v1, r, exprs=None):
        """A rounded rectangle from lines and tangent arcs. exprs, when given, drives it from parameters:
        {'w': width, 'h': height, 'r': radius, 'u0': left edge from the origin, 'v0': bottom edge from the origin}
        (u0 and v0 measured as positive distances, with their sign taken from the drawing)."""
        if r <= 0:
            pts = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
            ln = self.polyline(pts)
            for i, l in enumerate(ln):
                (self.cons.addHorizontal if (i % 2 == 0) == self.u_horizontal else self.cons.addVertical)(l)
            if exprs:
                self._dim_box(ln[3].endSketchPoint, ln[1].startSketchPoint, ln[0].startSketchPoint, ln[2].startSketchPoint,
                              u0, v0, u1, v1, exprs)
            return ln
        segs = [('line', (u0 + r, v0), (u1 - r, v0)), ('arc', (u1 - r, v0 + r), (u1 - r, v0), 90),
                ('line', (u1, v0 + r), (u1, v1 - r)), ('arc', (u1 - r, v1 - r), (u1, v1 - r), 90),
                ('line', (u1 - r, v1), (u0 + r, v1)), ('arc', (u0 + r, v1 - r), (u0 + r, v1), 90),
                ('line', (u0, v1 - r), (u0, v0 + r)), ('arc', (u0 + r, v0 + r), (u0, v0 + r), 90)]
        c = self.loop(segs)
        lines, arcs = c[0::2], c[1::2]
        for i, l in enumerate(lines):
            (self.cons.addHorizontal if (i % 2 == 0) == self.u_horizontal else self.cons.addVertical)(l)
        for i, a in enumerate(arcs):
            self.cons.addTangent(lines[i], a)
            self.cons.addTangent(a, lines[(i + 1) % 4])
        for a in arcs[1:]:
            self.cons.addEqual(arcs[0], a)
        if exprs:
            self.dim_r(arcs[0], exprs['r'])
            self._dim_box(lines[3].startSketchPoint, lines[1].startSketchPoint, lines[0].startSketchPoint,
                          lines[2].startSketchPoint, u0, v0, u1, v1, exprs)
        return c

    def _dim_box(self, left, right, bottom, top, u0, v0, u1, v1, exprs):
        self.dim_h(left, right, exprs['w'], ((u0 + u1) / 2, v0 - 6))
        self.dim_v(bottom, top, exprs['h'], (u1 + 6, (v0 + v1) / 2))
        o, (ou, ov) = self.origin_point(), self.o_uv
        if abs(u0 - ou) > 1e-6:
            self.dim_h(o, left, exprs['u0'], ((u0 + ou) / 2, v0 - 12))
        if abs(v0 - ov) > 1e-6:
            self.dim_v(o, bottom, exprs['v0'], (u0 - 12, (v0 + ov) / 2))

    def centred_circle(self, c, d, d_expr, cu_expr=None, cv_expr=None):
        """A circle, its diameter and (optionally) its centre dimensioned from the sketch origin."""
        ci = self.circle(c, d)
        self.dim_d(ci, d_expr)
        o, (ou, ov) = self.origin_point(), self.o_uv
        if cu_expr and abs(c[0] - ou) > 1e-6:
            self.dim_h(o, ci.centerSketchPoint, cu_expr, ((c[0] + ou) / 2, c[1] - 4))
        if cv_expr and abs(c[1] - ov) > 1e-6:
            self.dim_v(o, ci.centerSketchPoint, cv_expr, (c[0] + 4, (c[1] + ov) / 2))
        return ci

    def teardrop(self, c, r):
        """A round hole with a 45-degree roof, cut off flat at the circle's top: prints through a wall unsupported."""
        cu, cv = c
        s = r / math.sqrt(2)
        k = r * (math.sqrt(2) - 1)                     # half the flat at the top
        return self.loop([('arc', c, (cu - s, cv + s), 270),
                          ('line', (cu + s, cv + s), (cu + k, cv + r)),
                          ('line', (cu + k, cv + r), (cu - k, cv + r)),
                          ('line', (cu - k, cv + r), (cu - s, cv + s))])

    def hull(self, circles, points=()):
        """The convex outline round circles (centre, r) and points, as tangent lines and arcs."""
        segs = hull_segments(circles, points)
        return self.loop(segs)

    def text(self, s, base_u, base_v, cap, align='center'):
        """Text on a baseline in the frame, reading along +u. cap: capital height, mm."""
        if self.flipped:
            raise RuntimeError(f'{self.sk.name}: text would come out mirrored; sketch it on the face it reads from')
        path = self.line((base_u - 200, base_v), (base_u + 200, base_v), construction=True)
        texts = self.sk.sketchTexts
        if hasattr(texts, 'createInput3'):
            inp = texts.createInput3(f"'{s}'", val(cap))
        else:
            inp = texts.createInput2(s, cap / 10)
        inp.setAsAlongPath(path, True, {'center': adsk.core.HorizontalAlignments.CenterHorizontalAlignment,
                                        'left': adsk.core.HorizontalAlignments.LeftHorizontalAlignment}[align], 0)
        inp.fontName = FONT
        inp.textStyle = adsk.fusion.TextStyles.TextStyleBold
        return texts.add(inp)


def hull_segments(circles, points=()):
    """Convex hull of circles [(cx, cy, r)] and points [(x, y)]: a closed list of ('line', a, b) and
    ('arc', centre, start, sweep_deg), counter-clockwise."""
    items = [(c[0], c[1], c[2]) for c in circles] + [(p[0], p[1], 0.0) for p in points]
    # sample each item's boundary densely, hull the samples, then rebuild exact tangents between consecutive items
    samples = []
    for i, (x, y, r) in enumerate(items):
        n = 720 if r > 0 else 1
        for k in range(n):
            a = 2 * math.pi * k / n
            samples.append((x + r * math.cos(a), y + r * math.sin(a), i))
    samples.sort(key=lambda s: (s[0], s[1]))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for s in samples:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], s) <= 0:
            lo.pop()
        lo.append(s)
    for s in reversed(samples):
        while len(up) >= 2 and cross(up[-2], up[-1], s) <= 0:
            up.pop()
        up.append(s)
    ring = lo[:-1] + up[:-1]
    order = []
    for s in ring:
        if not order or order[-1] != s[2]:
            order.append(s[2])
    if len(order) > 1 and order[0] == order[-1]:
        order.pop()
    # exact outer tangent from item a to item b (counter-clockwise hull: the hull is on the left)
    def tangent(a, b):
        (x1, y1, r1), (x2, y2, r2) = items[a], items[b]
        dx, dy = x2 - x1, y2 - y1
        d = math.hypot(dx, dy)
        c = (r1 - r2) / d
        h = math.sqrt(max(0.0, 1 - c * c))
        nx, ny = (dx * c + dy * h) / d, (dy * c - dx * h) / d      # unit normal, pointing out of the hull (right side)
        return (x1 + r1 * nx, y1 + r1 * ny), (x2 + r2 * nx, y2 + r2 * ny)
    tans = [tangent(order[i], order[(i + 1) % len(order)]) for i in range(len(order))]
    segs = []
    for i in range(len(order)):
        a = order[i]
        x, y, r = items[a]
        t_in = tans[i - 1][1]
        t_out = tans[i][0]
        if r > 0:
            a0 = math.atan2(t_in[1] - y, t_in[0] - x)
            a1 = math.atan2(t_out[1] - y, t_out[0] - x)
            sweep = math.degrees((a1 - a0) % (2 * math.pi))
            if sweep > 1e-6:
                segs.append(('arc', (x, y), t_in, sweep))
        segs.append(('line', t_out, tans[i][1]))
    return segs


# ------------------------------------------------------------------------------------------------ features

def ops():
    return adsk.fusion.FeatureOperations


def profiles(sk, loops=None, largest=False, smallest=False):
    ps = [p for p in sk.profiles if loops is None or p.profileLoops.count == loops]
    if largest or smallest:
        ps.sort(key=lambda p: p.areaProperties().area, reverse=largest)
        ps = ps[:1]
    return collection(ps)


def sketch_normal(sk):
    return sk.xDirection.crossProduct(sk.yDirection)


def extrude(comp, prof, dist, op, toward, name, sk=None, participants=None, symmetric=False):
    """Extrude prof (a profile collection or sketch text) dist toward a model-space direction (or symmetric)."""
    ex = comp.features.extrudeFeatures
    inp = ex.createInput(prof, op)
    if symmetric:
        inp.setSymmetricExtent(val(dist), True)
    else:
        n = sketch_normal(sk)
        side = (adsk.fusion.ExtentDirections.PositiveExtentDirection if dot(n, toward) > 0
                else adsk.fusion.ExtentDirections.NegativeExtentDirection)
        inp.setOneSideExtent(adsk.fusion.DistanceExtentDefinition.create(val(dist)), side)
    if op == ops().CutFeatureOperation:            # cut only this part, never its neighbours in the assembly
        inp.participantBodies = participants or [b for b in comp.bRepBodies]
    f = ex.add(inp)
    f.name = name
    return f


def offset_plane(comp, base, axis, expr, name):
    """A construction plane parallel to an origin plane, expr along +axis ('x', 'y' or 'z')."""
    n = base.geometry.normal
    s = {'x': n.x, 'y': n.y, 'z': n.z}[axis]
    inp = comp.constructionPlanes.createInput()
    inp.setByOffset(base, val(expr if s > 0 else f'-({expr})'))
    pl = comp.constructionPlanes.add(inp)
    pl.name = name
    return pl


def face_at(comp, x, y, z):
    hits = comp.findBRepUsingPoint(p3(x, y, z), adsk.fusion.BRepEntityTypes.BRepFaceEntityType, 0.001, False)
    return hits.item(0)


def edges_at(comp, pts):
    out = []
    for p in pts:
        hits = comp.findBRepUsingPoint(p3(*p), adsk.fusion.BRepEntityTypes.BRepEdgeEntityType, 0.001, False)
        out.append(hits.item(0))
    return collection(out)


def chamfer(comp, edges, dist, name):
    ch = comp.features.chamferFeatures
    inp = ch.createInput2()
    inp.chamferEdgeSets.addEqualDistanceChamferEdgeSet(edges, val(dist), True)
    f = ch.add(inp)
    f.name = name
    return f


def mirror(comp, feats, plane, name):
    mi = comp.features.mirrorFeatures
    f = mi.add(mi.createInput(collection(feats), plane))
    f.name = name
    return f


def half_turn(comp, feats, name):
    """A copy of feats turned 180 degrees about the vertical axis through the origin (the diagonally opposite corner)."""
    cp = comp.features.circularPatternFeatures
    inp = cp.createInput(collection(feats), comp.zConstructionAxis)
    inp.quantity = val('2')
    inp.totalAngle = val('360 deg')
    f = cp.add(inp)
    f.name = name
    return f


def combine(comp, target, tools, op, name, keep=False):
    co = comp.features.combineFeatures
    inp = co.createInput(target, collection(tools))
    inp.operation = op
    inp.isKeepToolBodies = keep
    f = co.add(inp)
    f.name = name
    return f


_group_end = None


def group(name, last):
    """Collect the timeline since the previous group, up to and including last, into a named group."""
    global _group_end
    tl = design.timeline
    start = 0 if _group_end is None else _group_end.timelineObject.index + 1
    while tl.item(start).isGroup:
        start += 1
    g = tl.timelineGroups.add(start, last.timelineObject.index)
    g.name = name
    _group_end = last
    return g


def xy_plane(comp, z_expr, name):
    return offset_plane(comp, comp.xYConstructionPlane, 'z', z_expr, name) if z_expr else comp.xYConstructionPlane


XY = Frame((0, 0, 0), (1, 0, 0), (0, 1, 0))


def at_z(z):
    return Frame((0, 0, z), (1, 0, 0), (0, 1, 0))


# Cross-section frames: across the front/back walls (u = Y, v = Z) and across the end walls (u = X, v = Z).
def yz_frame(x):
    return Frame((x, 0, 0), (0, 1, 0), (0, 0, 1))


def xz_frame(y):
    return Frame((0, y, 0), (1, 0, 0), (0, 0, 1))


# ------------------------------------------------------------------------------------------------ tub

def build_tub(comp):
    X, Y, Z = v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1)
    L, W = par('outer_l') / 2, par('outer_w') / 2
    il, iw = par('inner_l') / 2, par('inner_w') / 2
    cr, wall, ft, top = par('corner_r'), par('wall'), par('floor_t'), par('box_h')

    # walls and floor
    s = Sketch(comp, comp.xYConstructionPlane, 'Box outline', XY)
    s.rounded_rect(-L, -W, L, W, cr, {'w': 'outer_l', 'h': 'outer_w', 'r': 'corner_r', 'u0': 'outer_l / 2', 'v0': 'outer_w / 2'})
    s.rounded_rect(-il, -iw, il, iw, cr - wall, {'w': 'inner_l', 'h': 'inner_w', 'r': 'corner_r - wall',
                                                  'u0': 'inner_l / 2', 'v0': 'inner_w / 2'})
    sk = s.done()
    walls = extrude(comp, profiles(sk, loops=2), 'box_h', ops().NewBodyFeatureOperation, Z, 'Walls', sk)
    tub = walls.bodies.item(0)
    tub.name = 'Tub'
    floor = extrude(comp, profiles(sk, loops=1), 'floor_t', ops().JoinFeatureOperation, Z, 'Floor', sk)

    # 45-degree fillet strips where the floor meets the walls, and the rim land's 45-degree underside
    ff, lh, lw = par('floor_fillet'), par('land_h'), par('land_w')
    li = lw - wall                                 # how far the land reaches in past the wall
    z_land = top - lh - li                         # the underside starts here on the wall
    e = 0.5                                        # the strips start inside the wall, so they join without coincident faces
    s = Sketch(comp, comp.yZConstructionPlane, 'Front and back wall strips', yz_frame(0))
    for sgn in (-1, 1):
        s.polyline([(sgn * (iw + e), ft), (sgn * (iw - ff), ft), (sgn * (iw + e), ft + ff + e)])
        s.polyline([(sgn * (iw + e), z_land - e), (sgn * (iw - li), z_land + li), (sgn * (iw - li), top), (sgn * (iw + e), top)])
    sk = s.done()
    strips_y = extrude(comp, profiles(sk), 'inner_l', ops().JoinFeatureOperation, X, 'Floor fillet and rim land, front and back', symmetric=True)
    s = Sketch(comp, comp.xZConstructionPlane, 'End wall strips', xz_frame(0))
    for sgn in (-1, 1):
        s.polyline([(sgn * (il + e), ft), (sgn * (il - ff), ft), (sgn * (il + e), ft + ff + e)])
        s.polyline([(sgn * (il + e), z_land - e), (sgn * (il - li), z_land + li), (sgn * (il - li), top), (sgn * (il + e), top)])
    sk = s.done()
    strips_x = extrude(comp, profiles(sk), 'inner_w', ops().JoinFeatureOperation, Y, 'Floor fillet and rim land, ends', symmetric=True)
    group('Box', strips_x)

    # seal groove in the rim
    gw = par('groove_w')
    s = Sketch(comp, xy_plane(comp, 'box_h', 'Rim'), 'Rim groove', at_z(top))
    a = lw / 2 - gw / 2                            # groove's outer edge in from the outside face
    s.rounded_rect(-L + a, -W + a, L - a, W - a, cr - a, {'w': 'outer_l - land_w + groove_w', 'h': 'outer_w - land_w + groove_w',
                                                          'r': 'corner_r - land_w / 2 + groove_w / 2',
                                                          'u0': 'outer_l / 2 - land_w / 2 + groove_w / 2',
                                                          'v0': 'outer_w / 2 - land_w / 2 + groove_w / 2'})
    b = lw / 2 + gw / 2
    s.rounded_rect(-L + b, -W + b, L - b, W - b, 0, {'w': 'outer_l - land_w - groove_w', 'h': 'outer_w - land_w - groove_w',
                                                     'u0': 'outer_l / 2 - land_w / 2 - groove_w / 2',
                                                     'v0': 'outer_w / 2 - land_w / 2 - groove_w / 2'})
    sk = s.done()
    groove = extrude(comp, profiles(sk, loops=2), 'groove_d', ops().CutFeatureOperation, v3(0, 0, -1), 'Rim groove', sk)
    group('Rim groove', groove)

    # end-wall ribs
    rt, rd, ry = par('rib_t'), par('rib_d'), par('rib_y')
    s = Sketch(comp, xy_plane(comp, 'floor_t', 'Floor top'), 'End wall ribs', at_z(ft))
    for sx in (-1, 1):
        for sy in (-1, 1):
            u0, u1 = sorted((sx * il, sx * (il - rd)))
            s.rounded_rect(u0, sy * ry - rt / 2, u1, sy * ry + rt / 2, 0)
    sk = s.done()
    ribs = extrude(comp, profiles(sk), f'{mm(z_land - ft)}', ops().JoinFeatureOperation, Z, 'End wall ribs', sk)

    # feet bosses and their pilot holes
    fi = par('feet_in')
    feet_pts = [(sx * (il - fi), sy * (iw - fi)) for sx in (-1, 1) for sy in (-1, 1)]
    s = Sketch(comp, xy_plane(comp, 'floor_t', 'Floor top'), 'Feet bosses', at_z(ft))
    for c in feet_pts:
        s.centred_circle(c, par('feet_boss_d'), 'feet_boss_d', 'inner_l / 2 - feet_in', 'inner_w / 2 - feet_in')
    sk = s.done()
    feet = extrude(comp, profiles(sk), 'feet_boss_h', ops().JoinFeatureOperation, Z, 'Feet bosses', sk)
    s = Sketch(comp, comp.xYConstructionPlane, 'Feet pilots', XY)
    for c in feet_pts:
        s.centred_circle(c, par('feet_pilot'), 'feet_pilot', 'inner_l / 2 - feet_in', 'inner_w / 2 - feet_in')
    sk = s.done()
    pilots = extrude(comp, profiles(sk), 'feet_depth', ops().CutFeatureOperation, Z, 'Feet pilots', sk)
    group('Ribs and feet', pilots)

    # board standoffs: posts, ribs, insert holes
    holes = sorted(PCB['holes'])
    s = Sketch(comp, xy_plane(comp, 'floor_t', 'Floor top'), 'Standoffs', at_z(ft))
    for c in holes:
        s.circle(c, par('standoff_d'))
    sk = s.done()
    posts = extrude(comp, profiles(sk), 'standoff_h', ops().JoinFeatureOperation, Z, 'Standoffs', sk)
    s = Sketch(comp, xy_plane(comp, 'floor_t + standoff_h', 'Standoff tops'), 'Standoff holes', at_z(ft + par('standoff_h')))
    for c in holes:
        s.circle(c, par('standoff_hole'))
    sk = s.done()
    sholes = extrude(comp, profiles(sk), 'standoff_depth', ops().CutFeatureOperation, v3(0, 0, -1), 'Standoff holes', sk)
    gussets = []
    sd = par('standoff_d')
    for c in holes:
        post = face_at(comp, c[0] + sd / 2, c[1], ft + 5)
        for ang in (45, 135):
            pl = standoff_plane(comp, post, ang)
            n = pl.geometry.normal
            d = (n.y, -n.x, 0)                     # horizontal, in the plane
            s = Sketch(comp, pl, 'Standoff ribs', Frame((c[0], c[1], 0), d, (0, 0, 1)))
            for sgn in (-1, 1):
                s.polyline([(sgn * 3, ft), (sgn * 8, ft), (sgn * 3, ft + 5)])
            sk = s.done()
            gussets.append(extrude(comp, profiles(sk), '1.2 mm', ops().JoinFeatureOperation, None, 'Standoff ribs', symmetric=True))
    group('Standoffs', gussets[-1])

    # screw bosses behind the faceplates: a 45-degree underside unless the floor is close, then down to it
    zr = par('row_z')
    bw, bh, bl = par('boss_w'), par('boss_h'), par('boss_len')
    bosses = []
    for col in sorted({abs(round(u, 3)) for f in (FRONT, BACK) for u, v in f['screws']}):
        s = Sketch(comp, offset_plane(comp, comp.yZConstructionPlane, 'x', mm(col), f'Boss plane {col:g}') if col else comp.yZConstructionPlane,
                   f'Screw bosses {col:g}', yz_frame(col))
        for face, lay in (('front', FRONT), ('back', BACK)):
            for u, v in lay['screws']:
                if abs(abs(u) - col) > 1e-3:
                    continue
                sgn = -1 if face == 'front' else 1
                yw = sgn * iw                      # the wall's inside face
                drop = zr + v - bh / 2 - ft        # room below the boss
                ztop = zr + v + bh / 2
                if drop < bl + 1:                  # stands on the floor
                    zb = ft - 0.5
                    pts = [(yw, zb), (yw, ztop), (yw - sgn * bl, ztop), (yw - sgn * bl, zb)]
                else:
                    pts = [(yw, zr + v - bh / 2 - bl), (yw, ztop), (yw - sgn * bl, ztop), (yw - sgn * bl, zr + v - bh / 2)]
                s.polyline(pts)
        sk = s.done()
        f = extrude(comp, profiles(sk), 'boss_w', ops().JoinFeatureOperation, None, f'Screw bosses {col:g}', symmetric=True)
        bosses.append(f)
        if col:
            bosses.append(mirror(comp, [f], comp.yZConstructionPlane, f'Screw bosses -{col:g}'))

    # the wall cutouts behind each faceplate: windows, guide pin holes, screw pilots, nut pockets
    cut_feats = []
    for face, lay in (('front', FRONT), ('back', BACK)):
        nrm = wall_normal(face)
        sgn = -1 if face == 'front' else 1
        fr = plate_frame(face)
        pl = offset_plane(comp, comp.xZConstructionPlane, 'y', f'{"-" if sgn < 0 else ""}outer_w / 2', f'{face.title()} wall')
        s = Sketch(comp, pl, f'{face.title()} wall windows', fr)
        jack = {}
        for name, kind, u in lay['jacks']:
            jack[name] = (kind, u)
        for u0, u1, v0, v1 in lay['windows']:
            s.rounded_rect(u0, v0, u1, v1, par('win_r'))
        for u, v in lay['pins']:
            s.teardrop((u, v), par('guide_pin_d') / 2 + par('pin_clr'))
        sk = s.done()
        cut_feats.append(extrude(comp, profiles(sk), 'wall + 1 mm', ops().CutFeatureOperation, v3(-nrm.x, -nrm.y, -nrm.z),
                                 f'{face.title()} wall windows', sk))
        s = Sketch(comp, pl, f'{face.title()} wall screw pilots', fr)
        for u, v in lay['screws']:
            s.teardrop((u, v), par('plate_pilot') / 2)
        sk = s.done()
        cut_feats.append(extrude(comp, profiles(sk), 'wall + boss_len + 1 mm', ops().CutFeatureOperation,
                                 v3(-nrm.x, -nrm.y, -nrm.z), f'{face.title()} screw pilots', sk))
        pl2 = offset_plane(comp, comp.xZConstructionPlane, 'y', f'{"-" if sgn < 0 else ""}(inner_w / 2 - boss_len)', f'{face.title()} boss ends')
        s = Sketch(comp, pl2, f'{face.title()} nut pockets', plate_frame(face, -wall - bl))
        rr = (par('nut_af') + 2 * par('nut_clr')) / math.sqrt(3)
        for u, v in lay['screws']:
            s.polyline([(u + rr * math.cos(math.radians(60 * k)), v + rr * math.sin(math.radians(60 * k))) for k in range(6)])
        sk = s.done()
        cut_feats.append(extrude(comp, profiles(sk), 'nut_t + 0.4 mm', ops().CutFeatureOperation, nrm, f'{face.title()} nut pockets', sk))
    group('Faceplate mounting', cut_feats[-1])

    # hinge knuckles on the back wall: the outer two of each hinge, a 45-degree gusset under each
    kd, kl, kg, ko = par('knuckle_d'), par('knuckle_l'), par('knuckle_gap'), par('knuckle_off')
    z_lt = top + par('seal_gap') + par('lid_t')
    y_ax, z_ax = W + kd / 2 + ko, z_lt - kd / 2
    kt = kd / 2 / math.sqrt(2) + 0.15
    k_zw = (z_ax - kt) - (y_ax + kt - W)
    hx = -HINGE_X
    x0 = hx - 1.5 * kl - kg                        # the outer knuckle's outer end
    pl = offset_plane(comp, comp.yZConstructionPlane, 'x', f'-({mm(HINGE_X)} + 1.5 * knuckle_l + knuckle_gap)', 'Hinge end')
    s = Sketch(comp, pl, 'Hinge knuckle', yz_frame(x0))
    s.hull([(y_ax, z_ax, kd / 2)], [(W, k_zw), (W, z_ax)])
    sk = s.done()
    knuckle = extrude(comp, profiles(sk), 'knuckle_l', ops().JoinFeatureOperation, X, 'Hinge knuckle', sk)
    s = Sketch(comp, pl, 'Hinge screw', yz_frame(x0))
    s.circle((y_ax, z_ax), par('pin_d'))
    sk = s.done()
    bore = extrude(comp, profiles(sk), '2 * (knuckle_l + knuckle_gap)', ops().CutFeatureOperation, X, 'Hinge screw', sk)
    pl = offset_plane(comp, comp.yZConstructionPlane, 'x', f'-({mm(HINGE_X)} - 0.5 * knuckle_l - knuckle_gap)', 'Hinge inner knuckle')
    x2 = hx + 0.5 * kl + kg
    s = Sketch(comp, pl, 'Hinge knuckle', yz_frame(x2))
    s.hull([(y_ax, z_ax, kd / 2)], [(W, k_zw), (W, z_ax)])
    sk = s.done()
    knuckle2 = extrude(comp, profiles(sk), 'knuckle_l', ops().JoinFeatureOperation, X, 'Hinge knuckle', sk)
    s = Sketch(comp, pl, 'Hinge screw pilot', yz_frame(x2))
    s.circle((y_ax, z_ax), par('pin_pilot'))
    sk = s.done()
    pilot = extrude(comp, profiles(sk), 'knuckle_l', ops().CutFeatureOperation, X, 'Hinge screw pilot', sk)
    hinge2 = mirror(comp, [knuckle, bore, knuckle2, pilot], comp.yZConstructionPlane, 'Second hinge')
    group('Hinge', hinge2)

    # cam latch ears on the end walls: a thick ear (clearance) toward the corner, a thin one (pilot) toward the middle
    g = latch_geometry()
    ears = []
    for t, x0e, hole in ((par('ear_t'), g['latch_g'], 'pin_pilot'), (g['ear_f'], -g['latch_g'] - g['ear_f'], 'pin_d')):
        y0 = -LATCH_Y + x0e                        # the ear's face nearer the front
        pl = offset_plane(comp, comp.xZConstructionPlane, 'y', f'-({mm(LATCH_Y)} - ({mm(x0e)}))', 'Latch ear')
        fr = Frame((-L, y0, 0), (-1, 0, 0), (0, 0, 1))   # u out of the left wall, v up
        s = Sketch(comp, pl, 'Latch ear', fr)
        py, pz, ky, r = g['p'][0], g['p'][1], g['ky'], 3.6
        s.hull([(py, pz, r)], [(0, pz - r - ky - 1.5), (0, pz + r)])
        s.circle((py, pz), par(hole))
        sk = s.done()
        ears.append(extrude(comp, profiles(sk, loops=2), mm(t), ops().JoinFeatureOperation, Y, 'Latch ear', sk))
    m1 = mirror(comp, ears, comp.xZConstructionPlane, 'Latch ears, left back')
    m2 = mirror(comp, ears, comp.yZConstructionPlane, 'Latch ears, right front')
    m3 = half_turn(comp, ears, 'Latch ears, right back')
    group('Latch ears', m3)
    return tub


def standoff_plane(comp, post, ang):
    """A vertical plane through a standoff's axis, ang degrees off the XZ plane."""
    inp = comp.constructionAxes.createInput()
    inp.setByCircularFace(post)
    ax = comp.constructionAxes.add(inp)
    ax.isLightBulbOn = False
    pin = comp.constructionPlanes.createInput()
    pin.setByAngle(ax, val(f'{ang} deg'), comp.xZConstructionPlane)
    pl = comp.constructionPlanes.add(pin)
    pl.isLightBulbOn = False
    return pl


# ------------------------------------------------------------------------------------------------ latch geometry

def latch_geometry():
    """The cam latch in its wall frame (y out of the wall's outside face, z up): pivot, keeper and lever sizes."""
    top = par('box_h')
    z_lu = top + par('seal_gap')
    lk_r = 1.5                                     # keeper screw radius (M3)
    yl = par('lid_gap') + par('skirt_t')           # the lid skirt's outside face
    kz = z_lu - 1.5
    pz = top - par('latch_drop')
    d = kz - pz
    rm = d + par('latch_bite') + par('latch_lead')
    ct = 2.6
    ro = rm + lk_r + ct
    travel, dip, det = par('latch_travel'), par('latch_dip'), par('latch_detent')
    tip = travel + math.degrees(math.asin(lk_r / rm)) / 2
    ky = yl + 0.4 + (rm + lk_r) * math.sin(math.radians(tip))
    lg = par('latch_w') / 2 + 0.4
    ear_f = par('pin_span') - par('ear_t') - 2 * lg

    def rho(a):
        if a <= 0:
            return d
        if a < dip:
            return d - det * a / dip
        if a < travel:
            return d - det + (rm - d + det) * (a - dip) / (travel - dip)
        return rm

    def kat(a):
        t = math.radians(a)
        return (ky - rho(a) * math.sin(t), pz + rho(a) * math.cos(t))
    return dict(lk_r=lk_r, yl=yl, kz=kz, pz=pz, d=d, rm=rm, ct=ct, ro=ro, tip=tip, ky=ky, p=(ky, pz), k=(ky, kz),
                latch_g=lg, ear_f=ear_f, travel=travel, kat=kat, z_lu=z_lu)


# ------------------------------------------------------------------------------------------------ lid

def build_lid(comp):
    X, Y, Z = v3(1, 0, 0), v3(0, 1, 0), v3(0, 0, 1)
    L, W = par('outer_l') / 2, par('outer_w') / 2
    cr, top = par('corner_r'), par('box_h')
    z_lu = top + par('seal_gap')
    z_lt = z_lu + par('lid_t')
    o = par('lid_gap') + par('skirt_t')
    back = W - 0.5                                 # the lid stops short of the hinge knuckles
    under = xy_plane(comp, 'box_h + seal_gap', 'Lid underside')

    s = Sketch(comp, under, 'Lid outline', at_z(z_lu))
    s.rounded_rect(-L - o, -W - o, L + o, W + o, cr + o, {'w': 'outer_l + 2 * (lid_gap + skirt_t)', 'h': 'outer_w + 2 * (lid_gap + skirt_t)',
                                                         'r': 'corner_r + lid_gap + skirt_t',
                                                         'u0': 'outer_l / 2 + lid_gap + skirt_t', 'v0': 'outer_w / 2 + lid_gap + skirt_t'})
    cut = s.line((-L - o - 5, back), (L + o + 5, back))
    s.cons.addHorizontal(cut)
    s.dim_v(s.origin_point(), cut.startSketchPoint, 'outer_w / 2 - 0.5 mm', (-L - o - 10, back / 2))
    sk = s.done()
    plate = extrude(comp, profiles(sk, largest=True), 'lid_t', ops().NewBodyFeatureOperation, Z, 'Lid', sk)
    lid = plate.bodies.item(0)
    lid.name = 'Lid'

    s = Sketch(comp, under, 'Skirt', at_z(z_lu))
    s.rounded_rect(-L - o, -W - o, L + o, W + o, cr + o, {'w': 'outer_l + 2 * (lid_gap + skirt_t)', 'h': 'outer_w + 2 * (lid_gap + skirt_t)',
                                                         'r': 'corner_r + lid_gap + skirt_t',
                                                         'u0': 'outer_l / 2 + lid_gap + skirt_t', 'v0': 'outer_w / 2 + lid_gap + skirt_t'})
    g = par('lid_gap')
    s.rounded_rect(-L - g, -W - g, L + g, W + g, cr + g, {'w': 'outer_l + 2 * lid_gap', 'h': 'outer_w + 2 * lid_gap',
                                                         'r': 'corner_r + lid_gap', 'u0': 'outer_l / 2 + lid_gap', 'v0': 'outer_w / 2 + lid_gap'})
    sk = s.done()
    skirt = extrude(comp, profiles(sk, loops=2), 'drip', ops().JoinFeatureOperation, v3(0, 0, -1), 'Skirt', sk)
    s = Sketch(comp, under, 'Skirt opening at the hinge', at_z(z_lu))
    s.rounded_rect(-L - o - 1, W - cr, L + o + 1, W + o + 1, 0)
    sk = s.done()
    opening = extrude(comp, profiles(sk), 'drip', ops().CutFeatureOperation, v3(0, 0, -1), 'Skirt opening at the hinge', sk)

    lw, tw = par('land_w'), par('tongue_w')
    s = Sketch(comp, under, 'Tongue', at_z(z_lu))
    a, b = lw / 2 - tw / 2, lw / 2 + tw / 2
    s.rounded_rect(-L + a, -W + a, L - a, W - a, cr - a, {'w': 'outer_l - land_w + tongue_w', 'h': 'outer_w - land_w + tongue_w',
                                                          'r': 'corner_r - land_w / 2 + tongue_w / 2',
                                                          'u0': 'outer_l / 2 - land_w / 2 + tongue_w / 2',
                                                          'v0': 'outer_w / 2 - land_w / 2 + tongue_w / 2'})
    s.rounded_rect(-L + b, -W + b, L - b, W - b, 0, {'w': 'outer_l - land_w - tongue_w', 'h': 'outer_w - land_w - tongue_w',
                                                     'u0': 'outer_l / 2 - land_w / 2 - tongue_w / 2',
                                                     'v0': 'outer_w / 2 - land_w / 2 - tongue_w / 2'})
    sk = s.done()
    tongue = extrude(comp, profiles(sk, loops=2), 'tongue_h', ops().JoinFeatureOperation, v3(0, 0, -1), 'Tongue', sk)
    group('Lid and skirt', tongue)

    # ribs under the lid, inside the rim land, clear of the TRIG2 button
    rt, pitch = par('lid_rib_t'), par('lid_rib_pitch')
    ex, ey = L - lw - 1, W - lw - 1
    s = Sketch(comp, under, 'Lid ribs across', at_z(z_lu))
    for k in (-2, -1, 0, 1, 2):
        s.rounded_rect(k * pitch - rt / 2, -ey, k * pitch + rt / 2, ey, 0)
    sk = s.done()
    r1 = extrude(comp, profiles(sk), 'lid_rib_h', ops().JoinFeatureOperation, v3(0, 0, -1), 'Lid ribs across', sk)
    s = Sketch(comp, under, 'Lid ribs along', at_z(z_lu))
    for k in (-1, 0, 1):
        s.rounded_rect(-ex, k * pitch - rt / 2, ex, k * pitch + rt / 2, 0)
    sk = s.done()
    r2 = extrude(comp, profiles(sk), 'lid_rib_h', ops().JoinFeatureOperation, v3(0, 0, -1), 'Lid ribs along', sk)
    s = Sketch(comp, under, 'Rib clearance round the button', at_z(z_lu))
    s.circle(BUTTON, par('hole_button') + 12)
    sk = s.done()
    r3 = extrude(comp, profiles(sk), 'lid_rib_h + 1 mm', ops().CutFeatureOperation, v3(0, 0, -1), 'Rib clearance round the button', sk)
    s = Sketch(comp, xy_plane(comp, 'box_h + seal_gap + lid_t', 'Lid top'), 'Button hole', at_z(z_lt))
    s.centred_circle(BUTTON, par('hole_button'), 'hole_button')
    sk = s.done()
    bhole = extrude(comp, profiles(sk), 'lid_t + 1 mm', ops().CutFeatureOperation, v3(0, 0, -1), 'Button hole', sk)
    group('Ribs and button', bhole)

    # hinge: the middle knuckle of each hinge and its web into the lid's back edge
    kd, kl, kg, ko = par('knuckle_d'), par('knuckle_l'), par('knuckle_gap'), par('knuckle_off')
    y_ax, z_ax = W + kd / 2 + ko, z_lt - kd / 2
    r_web = kd / 2 + 0.6
    lt = par('lid_t')
    hx = -HINGE_X
    x0 = hx - kl / 2
    pl = offset_plane(comp, comp.yZConstructionPlane, 'x', f'-({mm(HINGE_X)} + 0.5 * knuckle_l)', 'Lid knuckle end')
    s = Sketch(comp, pl, 'Lid knuckle', yz_frame(x0))
    yw = y_ax - math.sqrt(r_web ** 2 - (z_lu - z_ax) ** 2)
    s.hull([(y_ax, z_ax, kd / 2)], [(yw, z_lu), (yw + 2, z_lu), (yw, z_lu + lt), (yw + 2, z_lu + lt), (y_ax - 2, z_lt), (y_ax + 2, z_lt)])
    sk = s.done()
    kn = extrude(comp, profiles(sk), 'knuckle_l', ops().JoinFeatureOperation, X, 'Lid knuckle', sk)
    s = Sketch(comp, pl, 'Lid knuckle web', yz_frame(x0))
    s.rounded_rect(W - 6, z_lu, y_ax - 2, z_lt, 0)
    sk = s.done()
    web = extrude(comp, profiles(sk), 'knuckle_l', ops().JoinFeatureOperation, X, 'Lid knuckle web', sk)
    pl = offset_plane(comp, comp.yZConstructionPlane, 'x', f'-({mm(HINGE_X)} + 1.5 * knuckle_l + knuckle_gap)', 'Hinge end')
    s = Sketch(comp, pl, 'Hinge screw', yz_frame(hx - 1.5 * kl - kg))
    s.circle((y_ax, z_ax), par('pin_d'))
    sk = s.done()
    bore = extrude(comp, profiles(sk), '2 * (knuckle_l + knuckle_gap)', ops().CutFeatureOperation, X, 'Hinge screw', sk)
    h2 = mirror(comp, [kn, web, bore], comp.yZConstructionPlane, 'Second hinge')
    group('Hinge', h2)

    # cam latch keeper lugs
    g = latch_geometry()
    lugs = []
    for t, x0e, hole in ((par('ear_t'), g['latch_g'], 'pin_pilot'), (g['ear_f'], -g['latch_g'] - g['ear_f'], 'pin_d')):
        y0 = -LATCH_Y + x0e
        pl = offset_plane(comp, comp.xZConstructionPlane, 'y', f'-({mm(LATCH_Y)} - ({mm(x0e)}))', 'Keeper lug')
        fr = Frame((-L, y0, 0), (-1, 0, 0), (0, 0, 1))
        s = Sketch(comp, pl, 'Keeper lug', fr)
        ky, kz, r = g['ky'], g['kz'], 3.6
        y1 = par('lid_gap') + 0.5
        s.hull([(ky, kz, r)], [(y1, z_lu - par('drip') + 0.5), (y1, z_lt), (ky + r, z_lt)])
        s.circle((ky, kz), par(hole))
        sk = s.done()
        lugs.append(extrude(comp, profiles(sk, loops=2), mm(t), ops().JoinFeatureOperation, Y, 'Keeper lug', sk))
    m1 = mirror(comp, lugs, comp.xZConstructionPlane, 'Keeper lugs, left back')
    m2 = mirror(comp, lugs, comp.yZConstructionPlane, 'Keeper lugs, right front')
    m3 = half_turn(comp, lugs, 'Keeper lugs, right back')
    group('Keeper lugs', m3)

    # inlaid title and button label
    s = Sketch(comp, face_at(comp, 0, 0, z_lt), 'Lid text', at_z(z_lt))
    s.text(TITLE, 0, -7 * CAP_HEIGHT / 2, 7 * CAP_HEIGHT)
    s.text('TRIG2', BUTTON[0], BUTTON[1] - par('hole_button') / 2 - 3 - 4 * CAP_HEIGHT, 4 * CAP_HEIGHT)
    sk = s.done()
    texts = [sk.sketchTexts.item(i) for i in range(sk.sketchTexts.count)]
    t1 = inlay(comp, lid, texts, v3(0, 0, -1), 'Lid text', sk)
    group('Lid text', t1[-1])
    return lid


def inlay(comp, body, texts, into, name, sk):
    """Cut the text into the body and keep it as its own body (the second colour)."""
    feats, tools = [], []
    for t in texts:
        f = extrude(comp, t, 'inlay', ops().NewBodyFeatureOperation, into, name, sk)
        feats.append(f)
        tools += [f.bodies.item(i) for i in range(f.bodies.count)]
    feats.append(combine(comp, body, tools, ops().CutFeatureOperation, f'{name} inlay', keep=True))
    if len(tools) > 1:
        feats.append(combine(comp, tools[0], tools[1:], ops().JoinFeatureOperation, f'{name} body'))
    tools[0].name = name
    return feats


# ------------------------------------------------------------------------------------------------ faceplates

def seal_edges(lay):
    """The faceplate seal groove's two edges. Its centreline runs round the rectangle through the screw rows, in round
    each screw (sg_land clear of it) and under the hinge notches, its corners rounded to sg_round; the groove is sg_w
    wide on it. Returned as (outer, inner): sharp-cornered loops, and the radius each one's corners take (the outer
    edge rounds its outside corners, the inner edge its inside ones; the others stay sharp)."""
    u0, u1, v0, v1 = lay['seal_rect']
    w = par('sg_w') / 2
    rd = par('sg_land') + w
    rf = par('sg_round') + w
    edges = []
    for k in (1, -1):                              # the outer edge, then the inner
        d = k * w
        loop = region_boundary((u0 - d, u1 + d, v0 - d, v1 + d), [(u, v, rd - d) for u, v in lay['screws']],
                               [(a + d, b - d, y + d) for a, b, y in lay['seal_cuts']])
        edges.append((loop, rf, k > 0))
    return edges


def turns_left(a, b):
    """At the joint of segment a into segment b, does the path turn left (counter-clockwise)?"""
    def tangent(seg, at_end):
        if seg[0] == 'line':
            (x0, y0), (x1, y1) = seg[1], seg[2]
            return (x1 - x0, y1 - y0)
        c, p, sw = seg[1], seg[2], seg[3]
        t = math.atan2(p[1] - c[1], p[0] - c[0]) + (math.radians(sw) if at_end else 0)
        sgn = 1 if sw > 0 else -1
        return (-math.sin(t) * sgn, math.cos(t) * sgn)
    ta, tb = tangent(a, True), tangent(b, False)
    return ta[0] * tb[1] - ta[1] * tb[0] > 0


def region_boundary(rect, discs, cuts):
    """Boundary of rect less discs [(u, v, r)] less cuts [(u0, u1, v)] (everything above v between u0 and u1),
    counter-clockwise, as ('line', a, b) / ('arc', centre, a, sweep_deg) with the arcs clockwise as negative sweeps."""
    u0, u1, v0, v1 = rect
    lines = [((u0, v0), (u1, v0)), ((u1, v0), (u1, v1)), ((u1, v1), (u0, v1)), ((u0, v1), (u0, v0))]
    for a, b, v in cuts:   # a notch cut: down its right side, along its bottom, up its left side (still counter-clockwise for E)
        lines += [((b, v1), (b, v)), ((b, v), (a, v)), ((a, v), (a, v1))]

    def inside_E(p, eps=1e-7):
        x, y = p
        if not (u0 - eps <= x <= u1 + eps and v0 - eps <= y <= v1 + eps):
            return False
        for cx, cy, r in discs:
            if math.hypot(x - cx, y - cy) < r - eps:
                return False
        for a, b, v in cuts:
            if a + eps < x < b - eps and y > v + eps:
                return False
        return True

    def strictly_inside(p):
        x, y = p
        if not (u0 < x < u1 and v0 < y < v1):
            return False
        for cx, cy, r in discs:
            if math.hypot(x - cx, y - cy) <= r:
                return False
        for a, b, v in cuts:
            if a <= x <= b and y >= v:
                return False
        return True

    pieces = []
    # straight pieces: split each line at its crossings with the discs and the other lines
    for a, b in lines:
        ts = {0.0, 1.0}
        dx, dy = b[0] - a[0], b[1] - a[1]
        for cx, cy, r in discs:
            fx, fy = a[0] - cx, a[1] - cy
            A, B, C = dx * dx + dy * dy, 2 * (fx * dx + fy * dy), fx * fx + fy * fy - r * r
            disc = B * B - 4 * A * C
            if disc > 0:
                for t in ((-B - math.sqrt(disc)) / (2 * A), (-B + math.sqrt(disc)) / (2 * A)):
                    if 0 < t < 1:
                        ts.add(t)
        for c, d in lines:
            den = dx * (d[1] - c[1]) - dy * (d[0] - c[0])
            if abs(den) < 1e-12:
                continue
            t = ((c[0] - a[0]) * (d[1] - c[1]) - (c[1] - a[1]) * (d[0] - c[0])) / den
            s = ((c[0] - a[0]) * dy - (c[1] - a[1]) * dx) / den
            if 0 < t < 1 and -1e-9 <= s <= 1 + 1e-9:
                ts.add(t)
        ts = sorted(ts)
        for t0, t1 in zip(ts, ts[1:]):
            m = (a[0] + dx * (t0 + t1) / 2, a[1] + dy * (t0 + t1) / 2)
            # on the boundary when the left side (the region) is inside and the right side is outside
            n = (-dy, dx); k = math.hypot(*n); n = (n[0] / k * 1e-4, n[1] / k * 1e-4)
            if strictly_inside((m[0] + n[0], m[1] + n[1])) and not strictly_inside((m[0] - n[0], m[1] - n[1])):
                pieces.append(('line', (a[0] + dx * t0, a[1] + dy * t0), (a[0] + dx * t1, a[1] + dy * t1)))
    # arc pieces: each disc's circle, split at its crossings, kept where E is outside the disc (clockwise round it)
    for cx, cy, r in discs:
        angs = set()
        for a, b in lines:
            dx, dy = b[0] - a[0], b[1] - a[1]
            fx, fy = a[0] - cx, a[1] - cy
            A, B, C = dx * dx + dy * dy, 2 * (fx * dx + fy * dy), fx * fx + fy * fy - r * r
            disc = B * B - 4 * A * C
            if disc > 0:
                for t in ((-B - math.sqrt(disc)) / (2 * A), (-B + math.sqrt(disc)) / (2 * A)):
                    if -1e-9 <= t <= 1 + 1e-9:
                        angs.add(math.atan2(a[1] + dy * t - cy, a[0] + dx * t - cx) % (2 * math.pi))
        angs = sorted(angs)
        for i in range(len(angs)):
            a0, a1 = angs[i], angs[(i + 1) % len(angs)]
            if a1 <= a0:
                a1 += 2 * math.pi
            m = (a0 + a1) / 2
            out = (cx + (r + 1e-4) * math.cos(m), cy + (r + 1e-4) * math.sin(m))
            if strictly_inside(out):
                p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))   # clockwise from a1 back to a0
                pieces.append(('arc', (cx, cy), p1, -math.degrees(a1 - a0)))
    # chain them end to start
    def start(pc):
        return pc[1] if pc[0] == 'line' else pc[2]

    def end(pc):
        if pc[0] == 'line':
            return pc[2]
        c, a, sw = pc[1], pc[2], math.radians(pc[3])
        ang = math.atan2(a[1] - c[1], a[0] - c[0]) + sw
        r = math.hypot(a[0] - c[0], a[1] - c[1])
        return (c[0] + r * math.cos(ang), c[1] + r * math.sin(ang))
    chain = [pieces.pop(0)]
    while pieces:
        e = end(chain[-1])
        k = min(range(len(pieces)), key=lambda i: math.dist(start(pieces[i]), e))
        assert math.dist(start(pieces[k]), e) < 1e-6, 'the seal path does not close'
        chain.append(pieces.pop(k))
    # merge collinear line pieces
    merged = []
    for pc in chain:
        if merged and pc[0] == 'line' and merged[-1][0] == 'line':
            a, b = merged[-1][1], merged[-1][2]
            c = pc[2]
            if abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) < 1e-9:
                merged[-1] = ('line', a, c)
                continue
        merged.append(pc)
    if len(merged) > 1 and merged[0][0] == 'line' and merged[-1][0] == 'line':
        a, b, c = merged[-1][1], merged[-1][2], merged[0][2]
        if abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) < 1e-9:
            merged[0] = ('line', a, c)
            merged.pop()
    return merged


def shared_point(a, b):
    """The end point of curve a that meets curve b."""
    ends = [a.startSketchPoint, a.endSketchPoint]
    others = [b.startSketchPoint, b.endSketchPoint]
    return min(ends, key=lambda p: min(p.geometry.distanceTo(q.geometry) for q in others))


def build_plate(comp, face, lay):
    nrm = wall_normal(face)
    out, into = nrm, v3(-nrm.x, -nrm.y, -nrm.z)
    title = face.title()
    sgn = -1 if face == 'front' else 1
    zr, pt_ = par('row_z'), par('plate_t')
    u0, u1, v0, v1 = lay['plate']
    back_plane = offset_plane(comp, comp.xZConstructionPlane, 'y', f'{"-" if sgn < 0 else ""}outer_w / 2', f'{title} wall face')
    fr = plate_frame(face)
    s = Sketch(comp, back_plane, f'{title} plate', fr)
    s.rounded_rect(u0, v0, u1, v1, par('plate_r'), {'w': mm(u1 - u0), 'h': mm(v1 - v0), 'r': 'plate_r',
                                                    'u0': mm(abs(u0)), 'v0': f'row_z - {mm(-v0)}'})
    sk = s.done()
    body_f = extrude(comp, profiles(sk), 'plate_t', ops().NewBodyFeatureOperation, out, f'{title} plate', sk)
    body = body_f.bodies.item(0)
    body.name = f'{title} plate'
    first = body_f
    if lay['notches']:
        s = Sketch(comp, back_plane, 'Hinge notches', fr)
        for a, b in lay['notches']:
            s.rounded_rect(min(a, b), lay['plate_top_glued'], max(a, b), lay['plate_top_glued'] + 50, 1)
        sk = s.done()
        extrude(comp, profiles(sk), 'plate_t', ops().CutFeatureOperation, out, 'Hinge notches', sk)

    # the back edge's 45-degree chamfer: the edges of the back face's outer loop
    bf = face_at(comp, *fr.world((u0 + u1) / 2, v0 + 1.5))
    loop = [lp for lp in bf.loops if lp.isOuter][0]
    chamfer(comp, collection([e for e in loop.edges]), 'plate_chamfer', 'Back edge chamfer')

    show_frame = plate_frame(face, pt_)
    j35, dc = par('hole_j35'), par('hole_dc')
    s = Sketch(comp, back_plane, f'{title} jack holes', fr)
    for name, kind, u in lay['jacks']:
        s.centred_circle((u, 0), j35 if kind == 'j35' else dc, 'hole_j35' if kind == 'j35' else 'hole_dc', mm(abs(u)), 'row_z')
    sk = s.done()
    holes = extrude(comp, profiles(sk), 'plate_t', ops().CutFeatureOperation, out, f'{title} jack holes', sk)
    s = Sketch(comp, back_plane, f'{title} jack counterbores', fr)
    for name, kind, u in lay['jacks']:
        if kind == 'j35':
            s.circle((u, 0), par('j35_behind') + 2 * par('win_m'))
    sk = s.done()
    if sk.profiles.count:
        extrude(comp, profiles(sk), 'plate_t - j35_seat', ops().CutFeatureOperation, out, f'{title} jack counterbores', sk)

    # countersunk screw holes from the show face
    s = Sketch(comp, face_at(comp, *show_frame.world(0, v0 + 1)), f'{title} screw holes', show_frame)
    pts = collection([s.sk.sketchPoints.add(s.pt(u, v)) for u, v in lay['screws']])
    sk = s.done()
    hf = comp.features.holeFeatures
    hin = hf.createCountersinkInput(val('screw_d'), val('screw_head'), val('90 deg'))
    hin.setPositionBySketchPoints(pts)
    hin.setDistanceExtent(val('plate_t'))
    hole = hf.add(hin)
    hole.name = f'{title} screw holes'

    # the seal groove round the back: its outer and inner edges, each rounded where the centreline's rounding leaves it
    s = Sketch(comp, back_plane, f'{title} seal groove', fr)
    for loop, rf, outer in seal_edges(lay):
        curves = s.loop(loop)
        for i in range(len(curves)):
            if turns_left(loop[i], loop[(i + 1) % len(loop)]) == outer:
                a, b = curves[i], curves[(i + 1) % len(curves)]
                pa = shared_point(a, b).geometry
                s.sk.sketchCurves.sketchArcs.addFillet(a, pa, b, pa, rf / 10)
    sk = s.done()
    groove = extrude(comp, profiles(sk, loops=2), 'sg_d', ops().CutFeatureOperation, out, f'{title} seal groove', sk)

    # guide pins, chamfered tips
    s = Sketch(comp, back_plane, f'{title} guide pins', fr)
    for u, v in lay['pins']:
        s.centred_circle((u, v), par('guide_pin_d'), 'guide_pin_d', mm(abs(u)), f'row_z + ({mm(v)})')
    sk = s.done()
    pins = extrude(comp, profiles(sk), 'guide_pin_l', ops().JoinFeatureOperation, into, f'{title} guide pins', sk)
    tip = []
    gl = par('guide_pin_l')
    for u, v in lay['pins']:
        w = fr.world(u + par('guide_pin_d') / 2, v)
        tip.append((w[0] - nrm.x * gl, w[1] - nrm.y * gl, w[2]))
    chamfer(comp, edges_at(comp, tip), '0.6 mm', 'Guide pin tips')

    # inlaid labels
    s = Sketch(comp, face_at(comp, *show_frame.world(0, v0 + 1)), f'{title} labels', show_frame)
    for text, u, base, size in lay['labels']:
        s.text(text, u, base, size * CAP_HEIGHT)
    sk = s.done()
    texts = [sk.sketchTexts.item(i) for i in range(sk.sketchTexts.count)]
    t = inlay(comp, body, texts, into, f'{title} plate text', sk)
    group(f'{title} plate', t[-1])
    return body


# ------------------------------------------------------------------------------------------------ latch lever

def build_latch(comp):
    """The cam latch lever in the component's own frame: x along the wall, y out of the wall's outside face, z up."""
    g = latch_geometry()
    fr = yz_frame(0)
    py, pz = g['p']
    lk, ro, d, tip = g['lk_r'], g['ro'], g['d'], g['tip']
    y_in = g['ky'] + lk + 0.6                      # the spine's inside face, just outside the keeper
    st = 2.6                                       # spine thickness
    feats = []

    def solid(name, draw, op=None):
        s = Sketch(comp, comp.yZConstructionPlane, name, fr)
        draw(s)
        sk = s.done()
        f = extrude(comp, profiles(sk), 'latch_w', op or (ops().NewBodyFeatureOperation if not feats else ops().JoinFeatureOperation),
                    None, name, symmetric=True)
        feats.append(f)
        return f

    def at(a, r):
        t = math.radians(a)
        return (py - r * math.sin(t), pz + r * math.cos(t))

    solid('Pivot boss', lambda s: s.hull([(py, pz, 3.6)], [(y_in, pz), (y_in + st, pz), (y_in, pz + 0.1), (y_in + st, pz + 0.1)]))
    solid('Spine', lambda s: s.rounded_rect(y_in, pz, y_in + st, pz + ro, 0))
    a0 = -math.degrees(math.asin((lk + 0.6) / d))
    a1 = tip + 6
    a2 = -math.degrees(math.asin((lk + 0.6 + st) / ro))
    # the hook: a sector round the pivot from the spine, over the keeper, toward the wall
    solid('Hook', lambda s: s.loop([('arc', (py, pz), at(a0, d), a1 - a0),
                                    ('line', at(a1, d), at(a1, ro)),
                                    ('arc', (py, pz), at(a1, ro), a2 - a1),
                                    ('line', at(a2, ro), at(a0, d))]))
    solid('Thumb tab', lambda s: s.hull([(y_in + st - 1.3, pz + ro + 3, 1.3)], [(y_in, pz + ro - 3), (y_in + st, pz + ro - 3)]))
    lever = feats[0].bodies.item(0)
    lever.name = 'Latch'

    # the pivot hole, and the keeper's path as the lever opens: it shapes the hook's cam
    s = Sketch(comp, comp.yZConstructionPlane, 'Pivot hole', fr)
    s.circle((py, pz), par('pin_d') + 0.3)
    sk = s.done()
    feats.append(extrude(comp, profiles(sk), 'latch_w', ops().CutFeatureOperation, None, 'Pivot hole', symmetric=True))
    s = Sketch(comp, comp.yZConstructionPlane, 'Keeper path', fr)
    keeper_slot(s, g, lk + 0.05)
    sk = s.done()
    feats.append(extrude(comp, profiles(sk), 'latch_w', ops().CutFeatureOperation, None, 'Keeper path', symmetric=True))
    s = Sketch(comp, comp.yZConstructionPlane, 'Clear of the lid', fr)
    s.rounded_rect(py - 20, pz - 20, g['yl'] + 0.4, pz + 40, 0)
    sk = s.done()
    feats.append(extrude(comp, profiles(sk), 'latch_w', ops().CutFeatureOperation, None, 'Clear of the lid', symmetric=True))
    group('Latch lever', feats[-1])
    return lever


def keeper_slot(s, g, r):
    """The region swept by the keeper (radius r) as the lever opens from closed to past its mouth: two fitted splines
    either side of the keeper's path, closed by half circles at its ends."""
    kat = g['kat']
    angs = [a * 0.5 for a in range(0, int((g['travel'] + 40) * 2) + 1)]
    pts = [kat(a) for a in angs]
    def normal(i):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        k = math.hypot(dx, dy)
        return (-dy / k, dx / k)
    left = [(p[0] + r * normal(i)[0], p[1] + r * normal(i)[1]) for i, p in enumerate(pts)]
    right = [(p[0] - r * normal(i)[0], p[1] - r * normal(i)[1]) for i, p in enumerate(pts)]
    sl = s.sk.sketchCurves.sketchFittedSplines.add(collection([s.pt(*p) for p in left]))
    sr = s.sk.sketchCurves.sketchFittedSplines.add(collection([s.pt(*p) for p in right]))
    for i, j in ((0, 0), (-1, -1)):
        c = pts[i]
        a, b = (sl.startSketchPoint, sr.startSketchPoint) if i == 0 else (sl.endSketchPoint, sr.endSketchPoint)
        s.sk.sketchCurves.sketchArcs.addByThreePoints(a, s.pt(*end_cap(c, pts, i, r)), b)


def end_cap(c, pts, i, r):
    """The point of an end cap's half circle beyond the path's end."""
    a, b = (pts[1], pts[0]) if i == 0 else (pts[-2], pts[-1])
    dx, dy = b[0] - a[0], b[1] - a[1]
    k = math.hypot(dx, dy)
    return (c[0] + r * dx / k, c[1] + r * dy / k)


# ------------------------------------------------------------------------------------------------ run

def add_parameters():
    ups = design.userParameters
    for name, expr, comment in PARAMETERS:
        unit = 'deg' if expr.endswith('deg') else ('' if expr.replace('.', '', 1).isdigit() else 'mm')
        ups.add(name, val(expr), unit, comment)


def new_component(name):
    occ = design.rootComponent.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ


def latch_transform(side, y):
    """Place the latch's frame on an end wall: its x along the wall, y out of it."""
    m = adsk.core.Matrix3D.create()
    L = par('outer_l') / 2
    if side == 'left':
        m.setWithCoordinateSystem(p3(-L, y, 0), v3(0, 1, 0), v3(-1, 0, 0), v3(0, 0, 1))
    else:
        m.setWithCoordinateSystem(p3(L, y, 0), v3(0, -1, 0), v3(1, 0, 0), v3(0, 0, 1))
    return m


def run(context):
    global design
    ui = app.userInterface
    try:
        doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        design.fusionUnitsManager.distanceDisplayUnits = adsk.fusion.DistanceUnits.MillimeterDistanceUnits
        add_parameters()
        build_tub(new_component('Tub').component)
        build_lid(new_component('Lid').component)
        build_plate(new_component('Front plate').component, 'front', FRONT)
        build_plate(new_component('Back plate').component, 'back', BACK)
        occ = new_component('Latch')
        build_latch(occ.component)
        root = design.rootComponent
        places = [('left', -LATCH_Y), ('left', LATCH_Y), ('right', -LATCH_Y), ('right', LATCH_Y)]
        occ.transform2 = latch_transform(*places[0])
        for side, y in places[1:]:
            root.occurrences.addExistingComponent(occ.component, latch_transform(side, y))
        design.snapshots.add()
    except Exception:
        ui.messageBox('Failed:\n{}'.format(traceback.format_exc()))
