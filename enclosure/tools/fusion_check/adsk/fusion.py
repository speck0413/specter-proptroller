"""adsk.fusion stand-in that drives SolidCraft: each API call the enclosure script makes becomes the SolidCraft command
that does the same thing, so the script's timeline is rebuilt as a SolidCraft design. Internal units are cm (lengths)
and radians (angles), as in Fusion; SolidCraft takes mm and expressions.

Only the calls the script uses exist; anything else raises AttributeError, as a misspelt call would fail in Fusion.
"""
import math
from . import core

S = None   # the session: set by _new_document()


def _mm(v):
    return v * 10.0


def _vec(t):
    return core.Vector3D(*t)


def _sub(a, b):
    return tuple(a[i] - b[i] for i in range(3))


def _dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _norm(a):
    k = math.sqrt(_dot(a, a))
    return tuple(x / k for x in a)


class Session:
    def __init__(self, sc):
        self.sc = sc
        self.timeline = []           # TimelineObject per item, in order
        self.active_comp = 0
        self.active_sketch = None
        self.n = 0
        self.renames = []            # (kind, internal, display)
        self.params = None
        self.bodies = {}             # component id -> [BRepBody]
        self.topo = {}
        self.group_names = {}
        self.fixed_faces = []
        self.placements = {}
        self.design_name = 'Untitled'
        self.copies = []

    def uid(self, p):
        self.n += 1
        return f'{p}{self.n}'

    def x(self, cmd, **kw):
        return self.sc.x(cmd, **kw)

    def activate(self, comp):
        if comp.id != self.active_comp:
            self.finish_sketch()
            self.x('component.activate', component=comp.id)
            self.active_comp = comp.id

    def finish_sketch(self):
        if self.active_sketch is not None:
            self.x('sketch.finish')
            self.active_sketch = None

    def edit_sketch(self, sk):
        self.activate(sk.parentComponent)
        if self.active_sketch is not sk:
            self.finish_sketch()
            self.x('sketch.edit', sketch=sk._name)
            self.active_sketch = sk

    def add_timeline(self, obj, sc_id):
        t = TimelineObject(len(self.timeline), sc_id, obj)
        self.timeline.append(t)
        return t

    def invalidate(self):
        self.topo = {}

    def inspect(self):
        return self.sc.tool('inspect_design', {})

    def topology(self, body):
        if body not in self.topo:
            self.topo[body] = self.sc.tool('body_topology', {'body': body})
        return self.topo[body]


def _new_document():
    global S
    import os
    from solidcraft_mcp import SolidCraft
    S = Session(SolidCraft(log=os.environ.get('SHIM_LOG')))
    d = Design()
    core.Application.get().activeProduct = d
    return Document()


class Document(core.Base):
    @property
    def name(self):
        return S.design_name

    @name.setter
    def name(self, v):
        S.design_name = v


# ------------------------------------------------------------------------------------------------ enums

class FeatureOperations:
    JoinFeatureOperation, CutFeatureOperation, IntersectFeatureOperation, NewBodyFeatureOperation, NewComponentFeatureOperation = 0, 1, 2, 3, 4


_OPS = {0: 'join', 1: 'cut', 2: 'intersect', 3: 'new'}


class ExtentDirections:
    PositiveExtentDirection, NegativeExtentDirection, SymmetricExtentDirection = 0, 1, 2


class DimensionOrientations:
    AlignedDimensionOrientation, HorizontalDimensionOrientation, VerticalDimensionOrientation = 0, 1, 2


class BRepEntityTypes:
    BRepBodyEntityType, BRepFaceEntityType, BRepEdgeEntityType, BRepVertexEntityType = 0, 1, 2, 3


class PatternDistanceType:
    ExtentPatternDistanceType, SpacingPatternDistanceType = 0, 1


class DesignTypes:
    DirectDesignType, ParametricDesignType = 0, 1


class DistanceUnits:
    MillimeterDistanceUnits, CentimeterDistanceUnits = 0, 1


class TextStyles:
    TextStyleBold, TextStyleItalic, TextStyleUnderline = 1, 2, 4


class DistanceExtentDefinition(core.Base):
    def __init__(self, d):
        self.distance = d

    @staticmethod
    def create(distance):
        return DistanceExtentDefinition(distance)


# ------------------------------------------------------------------------------------------------ design

class UserParameter(core.Base):
    def __init__(self, name, value, unit, expression, comment):
        self.name, self._v, self.unit, self.expression, self.comment = name, value, unit, expression, comment

    @property
    def value(self):
        return self._v / 10.0 if self.unit == 'mm' else (math.radians(self._v) if self.unit == 'deg' else self._v)


class UserParameters(core.Base):
    def add(self, name, value, units, comment):
        e = value.expr()
        kw = dict(name=name, expression=e, comment=comment)
        if units:
            kw['unit'] = units
        S.x('parameters.add', **kw)
        S.params = None
        return self.itemByName(name)

    def itemByName(self, name):
        if S.params is None:
            r = S.x('parameters.list', filter='user')
            S.params = {p['name']: p for p in r['parameters']}
        p = S.params.get(name)
        if p is None:
            return None
        return UserParameter(name, p['value'], p['unit'], p['expression'], p['comment'])


class UnitsManager(core.Base):
    distanceDisplayUnits = None


class TimelineObject(core.Base):
    isGroup = False

    def __init__(self, index, sc_id, entity):
        self.index, self.sc_id, self.entity = index, sc_id, entity


class TimelineGroup(core.Base):
    def __init__(self, gid):
        self._id = gid
        self._name = None

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, v):
        self._name = v
        S.group_names[self._id] = v


class TimelineGroups(core.Base):
    def add(self, startIndex, endIndex):
        ids = [t.sc_id for t in S.timeline[startIndex:endIndex + 1] if t.sc_id is not None]
        S.finish_sketch()
        g = TimelineGroup(S.uid('G'))
        S.x('timeline.group', name=g._id, features=ids)
        return g


class Timeline(core.Base):
    timelineGroups = TimelineGroups()

    def item(self, i):
        return S.timeline[i]

    @property
    def count(self):
        return len(S.timeline)


class Snapshots(core.Base):
    def add(self):
        return True


class Design(core.Base):
    def __init__(self):
        self.userParameters = UserParameters()
        self.fusionUnitsManager = UnitsManager()
        self.timeline = Timeline()
        self.snapshots = Snapshots()
        self.designType = DesignTypes.ParametricDesignType
        self.rootComponent = Component(0, root=True)

    @staticmethod
    def cast(o):
        return o if isinstance(o, Design) else None


# ------------------------------------------------------------------------------------------------ components

class Occurrence(core.Base):
    def __init__(self, oid, comp):
        self._id, self.component = oid, comp

    @property
    def transform2(self):
        raise AttributeError

    @transform2.setter
    def transform2(self, m):
        _place(self._id, m)


def _place(oid, m):
    """Record the placement; finalize() writes it into the design (SolidCraft recomputes everything on a move)."""
    S.placements[oid] = m


def _matrix(m):
    return [list(m.ax[0]) + [0.0], list(m.ax[1]) + [0.0], list(m.ax[2]) + [0.0], [_mm(v) for v in m.o] + [1.0]]


class Occurrences(core.Base):
    def __init__(self, parent):
        self.parent = parent

    def addNewComponent(self, transform):
        S.finish_sketch()
        if S.active_comp != self.parent.id:
            S.x('component.activate', component=self.parent.id)
            S.active_comp = self.parent.id
        r = S.x('component.create', name=S.uid('Component'), parent=self.parent.id, activate=False)
        comp = Component(r['component'], name=r['name'])
        occ = Occurrence(r['occurrence'], comp)
        if transform.ax != core.Matrix3D().ax or any(transform.o):
            _place(occ._id, transform)
        return occ

    def addExistingComponent(self, comp, transform):
        oid = ('copy', len(S.copies))
        S.copies.append((comp, oid))
        _place(oid, transform)
        return Occurrence(oid, comp)


class Component(core.Base):
    def __init__(self, cid, root=False, name='root'):
        self.id, self._name = cid, name
        self.occurrences = Occurrences(self)
        self.sketches = Sketches(self)
        self.constructionPlanes = ConstructionPlanes(self)
        self.constructionAxes = ConstructionAxes(self)
        self.features = Features(self)
        self.xYConstructionPlane = ConstructionPlane(self, 'XY', (0, 0, 0), (0, 0, 1))
        self.xZConstructionPlane = ConstructionPlane(self, 'XZ', (0, 0, 0), (0, 1, 0))
        self.yZConstructionPlane = ConstructionPlane(self, 'YZ', (0, 0, 0), (1, 0, 0))
        self.xConstructionAxis = ConstructionAxis(self, (0, 0, 0), (1, 0, 0), 'X')
        self.yConstructionAxis = ConstructionAxis(self, (0, 0, 0), (0, 1, 0), 'Y')
        self.zConstructionAxis = ConstructionAxis(self, (0, 0, 0), (0, 0, 1), 'Z')
        S.bodies.setdefault(cid, [])

    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, v):
        S.x('component.rename', component=self.id, name=v)
        self._name = v

    @property
    def bRepBodies(self):
        return _coll(S.bodies[self.id])

    def findBRepUsingPoint(self, point, entityType, proximityTolerance=-1, visibleEntitiesOnly=True):
        p = (_mm(point.x), _mm(point.y), _mm(point.z))
        tol = max(_mm(proximityTolerance), 0.01) if proximityTolerance > 0 else 0.01
        hits = []
        for b in S.bodies[self.id]:
            topo = S.topology(b._name)
            if entityType == BRepEntityTypes.BRepFaceEntityType:
                for f in topo['faces']:
                    n = f.get('plane_normal')
                    if n is not None:
                        if abs(_dot(_sub(p, f['point']), n)) < tol and _inside_face(p, f, topo, n):
                            hits.append(BRepFace(b, f, p, topo))
                    elif f.get('cylinder'):
                        c = f['cylinder']
                        o, a = c['origin'], _norm(c['axis'])
                        d = _sub(p, o)
                        t = _dot(d, a)
                        q = _sub(d, tuple(t * x for x in a))
                        if abs(math.sqrt(_dot(q, q)) - c['radius']) < tol:
                            hits.append(BRepFace(b, f, p, topo))
            elif entityType == BRepEntityTypes.BRepEdgeEntityType:
                hits.append(BRepEdge(b, p))
        return _coll(hits)


def _face_edges(f, topo):
    return [e for e in topo['edges'] if f['name'] in e['name'].split('|')]


def _inside_face(p, f, topo, n):
    """Is p within the bounding box of the face's own edges (enough to tell coplanar faces apart here)."""
    pts = [q for e in _face_edges(f, topo) for q in (e['start'], e['end'], e['mid'])]
    if not pts:
        return False
    lo = [min(q[i] for q in pts) - 1e-3 for i in range(3)]
    hi = [max(q[i] for q in pts) + 1e-3 for i in range(3)]
    return all(lo[i] <= p[i] <= hi[i] for i in range(3))


def _coll(items):
    c = core.ObjectCollection()
    for i in items:
        c.add(i)
    return c


class BRepBody(core.Base):
    def __init__(self, name, comp):
        self._name, self.comp = name, comp
        self._display = None

    @property
    def name(self):
        return self._display or self._name

    @name.setter
    def name(self, v):
        self._display = v
        S.renames.append(('body', self, v))


class BRepEdge(core.Base):
    def __init__(self, body, point):
        self.body, self._point = body, point

    @property
    def pointOnEdge(self):
        return core.Point3D(*[v / 10 for v in self._point])


class BRepLoop(core.Base):
    def __init__(self, edges, outer):
        self.edges, self.isOuter = edges, outer


class BRepFace(core.Base):
    def __init__(self, body, f, p, topo):
        self.body, self._f, self._p, self._topo = body, f, p, topo
        n = f.get('plane_normal')
        self.geometry = core.Plane(core.Point3D(*[v / 10 for v in f['point']]), _vec(n)) if n else None

    @property
    def loops(self):
        """The planar face's edges, as one outer loop (the faces this script takes loops from have no holes yet)."""
        es = [BRepEdge(self.body, e['mid']) for e in _face_edges(self._f, self._topo)]
        return [BRepLoop(es, True)]


# ------------------------------------------------------------------------------------------------ construction

class ConstructionPlane(core.Base):
    def __init__(self, comp, ref, origin, normal, x_dir=None):
        self.comp, self._ref = comp, ref
        self.geometry = core.Plane(core.Point3D(*[v / 10 for v in origin]), _vec(normal))
        self._o, self._n = origin, normal
        self._display = None
        self.isLightBulbOn = True

    @property
    def name(self):
        return self._display or self._ref

    @name.setter
    def name(self, v):
        self._display = v
        S.renames.append(('feature', self._ref, v))

    @property
    def timelineObject(self):
        return self._tl


class ConstructionPlaneInput(core.Base):
    def __init__(self, comp):
        self.comp = comp
        self.kind = None

    def setByOffset(self, planarEntity, offset):
        self.kind, self.base, self.offset = 'offset', planarEntity, offset
        return True

    def setByAngle(self, linearEntity, angle, planarEntity):
        self.kind, self.axis, self.angle, self.base = 'angle', linearEntity, angle, planarEntity
        return True


def _eval(expr):
    """Evaluate a length expression to mm with SolidCraft (through a throwaway user parameter)."""
    name = S.uid('tmp_')
    r = S.x('parameters.add', name=name, expression=expr)
    v = r['value']
    S.x('parameters.change', delete=name)
    return v


class ConstructionPlanes(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput(self):
        return ConstructionPlaneInput(self.comp)

    def add(self, inp):
        S.activate(self.comp)
        S.finish_sketch()
        name = S.uid('Plane')
        if inp.kind == 'offset':
            base = inp.base
            e = inp.offset.expr()
            r = S.x('construct.plane.offset', base=base._ref, offset=e, name=name)
            d = _eval(e)
            o = tuple(base._o[i] + d * base._n[i] for i in range(3))
            pl = ConstructionPlane(self.comp, name, o, base._n)
        else:
            ax = inp.axis
            ang = math.radians(_eval_angle(inp.angle.expr('rad')))
            base = inp.base
            # Fusion: through the axis, at the angle to the plane. SolidCraft turns a plane about an axis, so first a
            # plane parallel to the base through the axis.
            d = _dot(_sub(ax._o, base._o), base._n)
            mid = S.uid('Plane')
            rm = S.x('construct.plane.offset', base=base._ref, offset=f'{d} mm', name=mid)
            S.add_timeline(None, rm['feature'])
            r = S.x('construct.plane.at_angle', base=mid, axis={'origin': list(ax._o), 'dir': list(ax._d)}, angle=f'{math.degrees(ang)} deg', name=name)
            n0, a = base._n, _norm(ax._d)
            c, s = math.cos(ang), math.sin(ang)
            k = _cross(a, n0)
            n = tuple(n0[i] * c + k[i] * s + a[i] * _dot(a, n0) * (1 - c) for i in range(3))
            pl = ConstructionPlane(self.comp, name, ax._o, n)
        pl._tl = S.add_timeline(pl, r['feature'])
        return pl


def _eval_angle(expr):
    name = S.uid('tmp_')
    r = S.x('parameters.add', name=name, expression=expr, unit='deg')
    v = r['display_value']
    S.x('parameters.change', delete=name)
    return v


class ConstructionAxis(core.Base):
    def __init__(self, comp, o, d, ref):
        self.comp, self._o, self._d, self._ref = comp, o, d, ref
        self.isLightBulbOn = True


class ConstructionAxisInput(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def setByCircularFace(self, face):
        self.face = face
        return True


class ConstructionAxes(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput(self):
        return ConstructionAxisInput(self.comp)

    def add(self, inp):
        S.activate(self.comp)
        S.finish_sketch()
        c = inp.face._f['cylinder']
        name = S.uid('Axis')
        r = S.x('construct.axis.through_cylinder', face={'face': list(inp.face._p), 'body': inp.face.body._name}, name=name)
        ax = ConstructionAxis(self.comp, tuple(c['origin']), tuple(_norm(c['axis'])), name)
        ax._tl = S.add_timeline(ax, r['feature'])
        return ax


# ------------------------------------------------------------------------------------------------ sketches

class SketchPoint(core.Base):
    def __init__(self, sk, pid, geometry):
        self.sk, self._id, self._g = sk, pid, geometry

    @property
    def geometry(self):
        return self._g.copy()


def _ref(sk, p):
    """A SolidCraft point reference: a point id, or sketch coordinates in mm."""
    if isinstance(p, SketchPoint):
        return p._id
    return [_mm(p.x), _mm(p.y)]


def _geom(p):
    return p.geometry if isinstance(p, SketchPoint) else core.Point3D(p.x, p.y, 0)


class SketchCurve(core.Base):
    def __init__(self, sk, cid):
        self.sk, self._id = sk, cid
        self._construction = False

    @property
    def isConstruction(self):
        return self._construction

    @isConstruction.setter
    def isConstruction(self, v):
        if bool(v) != self._construction:
            S.edit_sketch(self.sk)
            S.x('sketch.construction', curves=[self._id], value=bool(v))
            self._construction = bool(v)

    @property
    def parentSketch(self):
        return self.sk


class SketchLine(SketchCurve):
    objectType = 'adsk::fusion::SketchLine'

    def __init__(self, sk, cid, a, b):
        super().__init__(sk, cid)
        self.startSketchPoint, self.endSketchPoint = a, b


class SketchArc(SketchCurve):
    objectType = 'adsk::fusion::SketchArc'

    def __init__(self, sk, cid, c, a, b, r):
        super().__init__(sk, cid)
        self.centerSketchPoint, self.startSketchPoint, self.endSketchPoint, self.radius = c, a, b, r


class SketchCircle(SketchCurve):
    objectType = 'adsk::fusion::SketchCircle'

    def __init__(self, sk, cid, c, r):
        super().__init__(sk, cid)
        self.centerSketchPoint, self.radius = c, r


class SketchSpline(SketchCurve):
    objectType = 'adsk::fusion::SketchFittedSpline'

    def __init__(self, sk, cid, a, b):
        super().__init__(sk, cid)
        self.startSketchPoint, self.endSketchPoint = a, b


def _pt(created_points, key):
    return key if key in created_points else None


class SketchLines(core.Base):
    def __init__(self, sk):
        self.sk = sk

    def addByTwoPoints(self, a, b):
        S.edit_sketch(self.sk)
        r = S.x('sketch.line', points=[_ref(self.sk, a), _ref(self.sk, b)], connect=False)
        cid = r['curves'][0]
        pa = a if isinstance(a, SketchPoint) else SketchPoint(self.sk, f'{cid}.start', _geom(a))
        pb = b if isinstance(b, SketchPoint) else SketchPoint(self.sk, f'{cid}.end', _geom(b))
        return SketchLine(self.sk, cid, pa, pb)


class SketchArcs(core.Base):
    def __init__(self, sk):
        self.sk = sk

    def addByCenterStartSweep(self, center, start, sweepAngle):
        S.edit_sketch(self.sk)
        c, a = _geom(center), _geom(start)
        r = S.x('sketch.arc.center_point', center=_ref(self.sk, center), start=_ref(self.sk, start),
                sweep=math.degrees(sweepAngle), connect=False)
        cid = r['curves'][0]
        rad = math.hypot(a.x - c.x, a.y - c.y)
        t = math.atan2(a.y - c.y, a.x - c.x) + sweepAngle
        b = core.Point3D(c.x + rad * math.cos(t), c.y + rad * math.sin(t), 0)
        pc = center if isinstance(center, SketchPoint) else SketchPoint(self.sk, f'{cid}.center', c)
        pa = start if isinstance(start, SketchPoint) else SketchPoint(self.sk, f'{cid}.start', a)
        return SketchArc(self.sk, cid, pc, pa, SketchPoint(self.sk, f'{cid}.end', b), rad)

    def addByThreePoints(self, startPoint, point, endPoint):
        S.edit_sketch(self.sk)
        r = S.x('sketch.arc.three_point', start=_ref(self.sk, startPoint), end=_ref(self.sk, endPoint),
                through=_ref(self.sk, point), connect=False)
        cid = r['curves'][0]
        a, m, b = _geom(startPoint), _geom(point), _geom(endPoint)
        c, rad = _circle3(a, m, b)
        return SketchArc(self.sk, cid, SketchPoint(self.sk, f'{cid}.center', c),
                         startPoint if isinstance(startPoint, SketchPoint) else SketchPoint(self.sk, f'{cid}.start', a),
                         endPoint if isinstance(endPoint, SketchPoint) else SketchPoint(self.sk, f'{cid}.end', b), rad)

    def addFillet(self, firstEntity, firstEntityPoint, secondEntity, secondEntityPoint, radius):
        S.edit_sketch(self.sk)
        r = S.x('sketch.fillet', a=firstEntity._id, b=secondEntity._id, radius=f'{_mm(radius)} mm')
        cid = [c for c in r['created']['curves']][0]
        arc = SketchArc(self.sk, cid, None, None, None, radius)
        return arc


def _circle3(a, b, c):
    ax, ay, bx, by, cx, cy = a.x, a.y, b.x, b.y, c.x, c.y
    d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d
    uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d
    return core.Point3D(ux, uy, 0), math.hypot(ax - ux, ay - uy)


class SketchCircles(core.Base):
    def __init__(self, sk):
        self.sk = sk

    def addByCenterRadius(self, center, radius):
        S.edit_sketch(self.sk)
        r = S.x('sketch.circle.center', center=_ref(self.sk, center), radius=_mm(radius), connect=False)
        cid = r['curves'][0]
        pc = center if isinstance(center, SketchPoint) else SketchPoint(self.sk, f'{cid}.center', _geom(center))
        return SketchCircle(self.sk, cid, pc, radius)


class SketchFittedSplines(core.Base):
    def __init__(self, sk):
        self.sk = sk

    def add(self, points):
        S.edit_sketch(self.sk)
        pts = list(points)
        r = S.x('sketch.spline.fit_point', points=[_ref(self.sk, p) for p in pts])
        cid = r['curves'][0]
        cp = r['created']['points']
        a = SketchPoint(self.sk, cp[0], _geom(pts[0]))
        b = SketchPoint(self.sk, cp[len(pts) - 1] if len(cp) >= len(pts) else cp[-1], _geom(pts[-1]))
        return SketchSpline(self.sk, cid, a, b)


class SketchCurves(core.Base):
    def __init__(self, sk):
        self.sketchLines = SketchLines(sk)
        self.sketchArcs = SketchArcs(sk)
        self.sketchCircles = SketchCircles(sk)
        self.sketchFittedSplines = SketchFittedSplines(sk)


class SketchPoints(core.Base):
    def __init__(self, sk):
        self.sk = sk

    def add(self, point):
        S.edit_sketch(self.sk)
        r = S.x('sketch.point', point=_ref(self.sk, point), connect=False)
        return SketchPoint(self.sk, r['created']['points'][0], _geom(point))


class OffsetConstraintInput(core.Base):
    def __init__(self, curves, offset):
        self.curves, self.offset = curves, offset


class OffsetConstraint(core.Base):
    def __init__(self, sk, curves):
        self.childCurves = [SketchCurve(sk, c) for c in curves]


class GeometricConstraints(core.Base):
    def __init__(self, sk):
        self.sk = sk

    def _x(self, cmd, **kw):
        S.edit_sketch(self.sk)
        return S.x(cmd, **kw)

    def addCoincident(self, point, entity):
        return self._x('sketch.constraint.coincident', a=point._id, b=entity._id)

    def addHorizontal(self, line):
        return self._x('sketch.constraint.horizontal_vertical', line=line._id, mode='horizontal')

    def addVertical(self, line):
        return self._x('sketch.constraint.horizontal_vertical', line=line._id, mode='vertical')

    def addTangent(self, a, b):
        return self._x('sketch.constraint.tangent', a=a._id, b=b._id)

    def addEqual(self, a, b):
        return self._x('sketch.constraint.equal', a=a._id, b=b._id)

    def createOffsetInput(self, curves, offset):
        return OffsetConstraintInput(curves, offset)

    def addTwoSidesOffset(self, inp, linkOffsets):
        first = list(inp.curves)[0]
        r = self._x('sketch.offset', curves=[first._id], chain=True, distance=inp.offset.expr(), symmetric=True)
        return [OffsetConstraint(self.sk, r['created']['curves'])]


class Parameter(core.Base):
    def __init__(self, name, expression):
        self.name, self._expr = name, expression

    @property
    def expression(self):
        return self._expr

    @expression.setter
    def expression(self, e):
        S.x('parameters.change', name=self.name, expression=e)
        self._expr = e


class SketchDimension(core.Base):
    def __init__(self, r):
        self.parameter = Parameter(r['param'], r['expression'])


class SketchDimensions(core.Base):
    def __init__(self, sk):
        self.sk = sk

    def _add(self, **kw):
        S.edit_sketch(self.sk)
        return SketchDimension(S.x('sketch.dimension', **kw))

    def addDistanceDimension(self, pointOne, pointTwo, orientation, textPoint, isDriving=True):
        t = {DimensionOrientations.HorizontalDimensionOrientation: 'horizontal',
             DimensionOrientations.VerticalDimensionOrientation: 'vertical',
             DimensionOrientations.AlignedDimensionOrientation: 'distance'}[orientation]
        return self._add(entities=[pointOne._id, pointTwo._id], type=t, text_at=[_mm(textPoint.x), _mm(textPoint.y)])

    def addDiameterDimension(self, entity, textPoint, isDriving=True):
        return self._add(entities=[entity._id], type='diameter', text_at=[_mm(textPoint.x), _mm(textPoint.y)])

    def addRadialDimension(self, entity, textPoint, isDriving=True):
        return self._add(entities=[entity._id], type='radius', text_at=[_mm(textPoint.x), _mm(textPoint.y)])


class SketchTextInput(core.Base):
    def __init__(self, text, height):
        self.text, self.height2 = text, height
        self.fontName = None
        self.textStyle = 0
        self.isHorizontalFlip = False
        self.isVerticalFlip = False

    def setAsAlongPath(self, path, isAbovePath, horizontalAlignment, characterSpacing):
        self.path, self.above, self.align = path, isAbovePath, horizontalAlignment
        return True


class SketchText(core.Base):
    def __init__(self, sk, curves):
        self.sk, self._curves = sk, curves

    @property
    def parentSketch(self):
        return self.sk


FONTS = {('Arial', True): 'Liberation Sans Bold', ('Arial', False): 'Liberation Sans'}


class SketchTexts(core.Base):
    def __init__(self, sk):
        self.sk = sk
        self._items = []

    def createInput3(self, expression, height):
        e = expression.strip()
        if not (e.startswith("'") and e.endswith("'")):
            raise ValueError('createInput3: literal text must be quoted')
        return SketchTextInput(e[1:-1], height)

    def add(self, inp):
        S.edit_sketch(self.sk)
        if inp.isHorizontalFlip or inp.isVerticalFlip or not inp.above:
            raise NotImplementedError('flipped or below-path text')
        pa = inp.path.startSketchPoint.geometry
        pb = inp.path.endSketchPoint.geometry
        font = FONTS[(inp.fontName, bool(inp.textStyle & TextStyles.TextStyleBold))]
        align = {0: 'left', 1: 'center', 2: 'right'}[inp.align]
        r = S.x('sketch.text', text=inp.text, at=[_mm((pa.x + pb.x) / 2), _mm((pa.y + pb.y) / 2)], path=inp.path._id,
                height=inp.height2.expr(), align=align, font=font)
        t = SketchText(self.sk, r['created']['curves'])
        self._items.append(t)
        return t

    def item(self, i):
        return self._items[i]

    @property
    def count(self):
        return len(self._items)


class ProfileLoops(core.Base):
    def __init__(self, n):
        self.count = n


class AreaProperties(core.Base):
    def __init__(self, area):
        self.area = area


class Profile(core.Base):
    def __init__(self, sk, info):
        self.sk, self._i = sk, info
        self.profileLoops = ProfileLoops(1 + len(info['holes']))

    def areaProperties(self, accuracy=0):
        return AreaProperties(self._i['area_mm2'] / 100)

    @property
    def parentSketch(self):
        return self.sk


class Sketch(core.Base):
    def __init__(self, comp, name, frame, sc_id):
        self.parentComponent, self._name, self._frame, self._sc = comp, name, frame, sc_id
        self.sketchCurves = SketchCurves(self)
        self.sketchPoints = SketchPoints(self)
        self.geometricConstraints = GeometricConstraints(self)
        self.sketchDimensions = SketchDimensions(self)
        self.sketchTexts = SketchTexts(self)
        self.isComputeDeferred = False
        self._display = None
        self.originPoint = SketchPoint(self, 'origin', core.Point3D(0, 0, 0))

    @property
    def name(self):
        return self._display or self._name

    @name.setter
    def name(self, v):
        self._display = v
        S.renames.append(('feature', self._name, v))

    @property
    def xDirection(self):
        return _vec(self._frame['x_dir'])

    @property
    def yDirection(self):
        return _vec(self._frame['y_dir'])

    def modelToSketchSpace(self, p):
        q = _sub((_mm(p.x), _mm(p.y), _mm(p.z)), self._frame['origin'])
        n = _cross(self._frame['x_dir'], self._frame['y_dir'])
        return core.Point3D(_dot(q, self._frame['x_dir']) / 10, _dot(q, self._frame['y_dir']) / 10, _dot(q, n) / 10)

    def findConnectedCurves(self, curve):
        return _coll([curve])

    def _info(self):
        for s in S.inspect()['sketches']:
            if s['name'] == self._name:
                return s
        raise KeyError(self._name)

    @property
    def profiles(self):
        info = self._info()
        text = set(c for t in self.sketchTexts._items for c in t._curves)
        return [Profile(self, p) for p in info['profiles'] if not (set(p['curves']) & text)]

    def _text_profiles(self, t):
        info = self._info()
        tc = set(t._curves)
        ps = [p for p in info['profiles'] if set(p['curves']) <= tc]
        holes = {tuple(sorted(set(h))) for p in ps for h in p['holes']}
        return [p['index'] for p in ps if tuple(sorted(set(p['curves']))) not in holes]

    @property
    def timelineObject(self):
        return self._tl


class Sketches(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def add(self, planarEntity):
        S.activate(self.comp)
        S.finish_sketch()
        name = S.uid('Sketch')
        if isinstance(planarEntity, ConstructionPlane):
            r = S.x('sketch.create', plane=planarEntity._ref, name=name)
        elif isinstance(planarEntity, BRepFace):
            plane = {'face': list(planarEntity._p), 'body': planarEntity.body._name, 'index': planarEntity._f['index']}
            try:
                r = S.x('sketch.create', plane=plane, name=name, project_edges=False)
            except Exception as e:
                if "can't keep an attachment" not in str(e):
                    raise
                # SolidCraft can't follow this face through its history: sketch on its plane as it stands
                r = S.x('sketch.create', plane={**plane, 'fixed': True}, name=name, project_edges=False)
                S.fixed_faces.append(name)
        else:
            raise TypeError(planarEntity)
        info = [s for s in S.inspect()['sketches'] if s['name'] == name][0]
        sk = Sketch(self.comp, name, info['plane'], r['sketch'])
        S.active_sketch = sk
        sk._tl = S.add_timeline(sk, r['sketch'])
        return sk


# ------------------------------------------------------------------------------------------------ features

class Feature(core.Base):
    def __init__(self, comp, name, r, bodies):
        self.comp, self._name, self._r = comp, name, r
        self.bodies = _coll(bodies)
        self._display = None
        self._tl = S.add_timeline(self, r['feature'])

    @property
    def name(self):
        return self._display or self._name

    @name.setter
    def name(self, v):
        self._display = v
        S.renames.append(('feature', self._name, v))

    @property
    def timelineObject(self):
        return self._tl


def _result_bodies(comp, r):
    out = []
    for n in r.get('created_bodies', []):
        b = BRepBody(n, comp)
        S.bodies[comp.id].append(b)
        out.append(b)
    known = {b._name: b for b in S.bodies[comp.id]}
    for n in r.get('changed_bodies', []):
        if n in known and known[n] not in out:
            out.append(known[n])
    removed = set(r.get('removed_bodies', []))
    S.bodies[comp.id] = [b for b in S.bodies[comp.id] if b._name not in removed]
    S.invalidate()
    return out


class ExtrudeFeatureInput(core.Base):
    def __init__(self, comp, prof, op):
        self.comp, self.prof, self.op = comp, prof, op
        self.participantBodies = None
        self.direction = None

    def setOneSideExtent(self, extent, direction, taperAngle=None):
        self.dist, self.direction = extent.distance, {0: 'positive', 1: 'negative'}[direction]
        return True

    def setSymmetricExtent(self, distance, isFullLength, taperAngle=None):
        if not isFullLength:
            raise NotImplementedError
        self.dist, self.direction = distance, 'symmetric'
        return True


class ExtrudeFeatures(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput(self, profile, operation):
        return ExtrudeFeatureInput(self.comp, profile, operation)

    def add(self, inp):
        prof = inp.prof
        if isinstance(prof, SketchText):
            sk, idx = prof.sk, prof.sk._text_profiles(prof)
        else:
            items = list(prof) if isinstance(prof, core.ObjectCollection) else [prof]
            if not items:
                raise RuntimeError('extrude: no profiles')
            sk = items[0].sk
            idx = [p._i['index'] for p in items]
        S.activate(self.comp)
        S.finish_sketch()
        name = S.uid('Extrude')
        kw = dict(sketch=sk._name, profiles=idx, distance=inp.dist.expr(), direction=inp.direction,
                  operation=_OPS[inp.op], name=name)
        if inp.op == FeatureOperations.NewBodyFeatureOperation:
            kw['body_name'] = S.uid('Body')
        else:
            parts = list(inp.participantBodies) if inp.participantBodies is not None else S.bodies[self.comp.id]
            kw['targets'] = [b._name for b in parts]
        r = S.x('solid.extrude', **kw)
        return Feature(self.comp, name, r, _result_bodies(self.comp, r))


class ChamferEdgeSets(core.Base):
    def __init__(self):
        self.sets = []

    def addEqualDistanceChamferEdgeSet(self, edges, distance, isTangentChain):
        self.sets.append((list(edges), distance))
        return True


class ChamferFeatureInput(core.Base):
    def __init__(self):
        self.chamferEdgeSets = ChamferEdgeSets()


class ChamferFeatures(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput2(self):
        return ChamferFeatureInput()

    def add(self, inp):
        S.activate(self.comp)
        S.finish_sketch()
        (edges, d), = inp.chamferEdgeSets.sets
        r = S.x('solid.chamfer', edges=[list(e._point) for e in edges], distance=d.expr(), body=edges[0].body._name)
        return Feature(self.comp, r['name'], r, _result_bodies(self.comp, r))


class MirrorFeatureInput(core.Base):
    def __init__(self, entities, plane):
        self.entities, self.plane = list(entities), plane


class MirrorFeatures(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput(self, inputEntities, mirrorPlane):
        return MirrorFeatureInput(inputEntities, mirrorPlane)

    def add(self, inp):
        S.activate(self.comp)
        S.finish_sketch()
        r = S.x('solid.mirror', features=[f._name for f in inp.entities], plane=inp.plane._ref)
        return Feature(self.comp, r['name'], r, _result_bodies(self.comp, r))


class RectangularPatternFeatureInput(core.Base):
    def __init__(self, entities, axis, q, d, t):
        self.entities, self.axis, self.q, self.d, self.t = list(entities), axis, q, d, t


class RectangularPatternFeatures(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput(self, inputEntities, directionOneEntity, quantityOne, distanceOne, patternDistanceType):
        return RectangularPatternFeatureInput(inputEntities, directionOneEntity, quantityOne, distanceOne, patternDistanceType)

    def add(self, inp):
        if inp.t != PatternDistanceType.SpacingPatternDistanceType:
            raise NotImplementedError
        S.activate(self.comp)
        S.finish_sketch()
        r = S.x('solid.pattern.rectangular', features=[f._name for f in inp.entities], dir1=list(inp.axis._d),
                count1=int(inp.q.expr()), spacing1=inp.d.expr())
        return Feature(self.comp, r['name'], r, _result_bodies(self.comp, r))


class CircularPatternFeatureInput(core.Base):
    def __init__(self, entities, axis):
        self.entities, self.axis = list(entities), axis
        self.quantity = None
        self.totalAngle = None


class CircularPatternFeatures(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput(self, inputEntities, axis):
        return CircularPatternFeatureInput(inputEntities, axis)

    def add(self, inp):
        S.activate(self.comp)
        S.finish_sketch()
        r = S.x('solid.pattern.circular', features=[f._name for f in inp.entities], axis=inp.axis._ref,
                count=int(inp.quantity.expr()), angle=inp.totalAngle.expr('rad'))
        return Feature(self.comp, r['name'], r, _result_bodies(self.comp, r))


class CombineFeatureInput(core.Base):
    def __init__(self, target, tools):
        self.target, self.tools = target, list(tools)
        self.operation = FeatureOperations.JoinFeatureOperation
        self.isKeepToolBodies = False


class CombineFeatures(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createInput(self, targetBody, toolBodies):
        return CombineFeatureInput(targetBody, toolBodies)

    def add(self, inp):
        S.activate(self.comp)
        S.finish_sketch()
        r = S.x('solid.combine', target=inp.target._name, tools=[b._name for b in inp.tools],
                operation=_OPS[inp.operation], keep_tools=bool(inp.isKeepToolBodies))
        return Feature(self.comp, r['name'], r, _result_bodies(self.comp, r))


class HoleFeatureInput(core.Base):
    def __init__(self, d, csd, ang):
        self.d, self.csd, self.ang = d, csd, ang

    def setPositionBySketchPoints(self, points):
        self.points = list(points)
        return True

    def setDistanceExtent(self, distance):
        self.depth = distance
        return True


class HoleFeatures(core.Base):
    def __init__(self, comp):
        self.comp = comp

    def createCountersinkInput(self, holeDiameter, countersinkDiameter, countersinkAngle):
        return HoleFeatureInput(holeDiameter, countersinkDiameter, countersinkAngle)

    def add(self, inp):
        S.activate(self.comp)
        S.finish_sketch()
        sk = inp.points[0].sk
        r = S.x('solid.hole', sketch=sk._name, points=[p._id for p in inp.points], diameter=inp.d.expr(), type='countersink',
                cs_diameter=inp.csd.expr(), cs_angle=inp.ang.expr('rad'), depth=inp.depth.expr())
        return Feature(self.comp, r['name'], r, _result_bodies(self.comp, r))


class Features(core.Base):
    def __init__(self, comp):
        self.extrudeFeatures = ExtrudeFeatures(comp)
        self.chamferFeatures = ChamferFeatures(comp)
        self.mirrorFeatures = MirrorFeatures(comp)
        self.rectangularPatternFeatures = RectangularPatternFeatures(comp)
        self.circularPatternFeatures = CircularPatternFeatures(comp)
        self.combineFeatures = CombineFeatures(comp)
        self.holeFeatures = HoleFeatures(comp)


# ------------------------------------------------------------------------------------------------ after run()

def finalize(tmp_json):
    """Apply the display names. SolidCraft recomputes the timeline on every rename, so the names are written into a
    saved copy of the design, which is then opened again. Features refer to each other by name there, so names are
    made unique (Fusion lets several features share one)."""
    import json
    S.finish_sketch()
    S.x('file.save', path=tmp_json)
    d = json.load(open(tmp_json))
    used, feat, body = {'body': {}, 'feature': {}}, {}, {}
    def unique(kind, name):
        k = used[kind].get(name, 0)
        used[kind][name] = k + 1
        return name if k == 0 else f'{name} ({k + 1})'
    for kind, ref, name in S.renames:
        if name is None:
            continue
        if kind == 'body':
            body[ref._name] = unique(kind, name)
        else:
            feat[ref] = unique(kind, name)
    for g in d['timeline_groups']:
        g['name'] = S.group_names.get(g['name'], g['name'])
    names = {**feat, **body}
    def walk(o):
        if isinstance(o, dict):
            return {k: walk(v) for k, v in o.items()}
        if isinstance(o, list):
            return [walk(v) for v in o]
        if isinstance(o, str) and o in names:
            return names[o]
        return o
    d['features'] = walk(d['features'])
    d['name'] = S.design_name
    occs = {o['id']: o for o in d['occurrences']}
    for comp, oid in S.copies:
        src = [o for o in d['occurrences'] if o['component'] == comp.id][0]
        n = sum(1 for o in d['occurrences'] if o['component'] == comp.id) + 1
        o = dict(src, id=d['next_id'], name=f"{src['name'].rsplit(':', 1)[0]}:{n}")
        d['next_id'] += 1
        d['occurrences'].append(o)
        occs[oid] = o
    for oid, m in S.placements.items():
        occs[oid]['transform'] = _matrix(m)
    json.dump(d, open(tmp_json, 'w'))
    S.sc.tool('open', {'path': tmp_json})
