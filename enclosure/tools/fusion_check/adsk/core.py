"""adsk.core stand-in: the subset of Fusion's core API the enclosure script uses (internal units: cm, radians)."""
import math


class Base:
    @property
    def objectType(self):
        return 'adsk::fusion::' + type(self).__name__


class Point3D(Base):
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = float(x), float(y), float(z)

    @staticmethod
    def create(x=0.0, y=0.0, z=0.0):
        return Point3D(x, y, z)

    def distanceTo(self, o):
        return math.dist((self.x, self.y, self.z), (o.x, o.y, o.z))

    def copy(self):
        return Point3D(self.x, self.y, self.z)

    def __repr__(self):
        return f'Point3D({self.x:.5g}, {self.y:.5g}, {self.z:.5g})'


class Vector3D(Base):
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = float(x), float(y), float(z)

    @staticmethod
    def create(x=0.0, y=0.0, z=0.0):
        return Vector3D(x, y, z)

    def crossProduct(self, o):
        return Vector3D(self.y * o.z - self.z * o.y, self.z * o.x - self.x * o.z, self.x * o.y - self.y * o.x)

    def dotProduct(self, o):
        return self.x * o.x + self.y * o.y + self.z * o.z

    @property
    def length(self):
        return math.sqrt(self.dotProduct(self))

    def normalize(self):
        k = self.length
        self.x, self.y, self.z = self.x / k, self.y / k, self.z / k
        return True

    def __repr__(self):
        return f'Vector3D({self.x:.5g}, {self.y:.5g}, {self.z:.5g})'


class Plane(Base):
    def __init__(self, origin, normal, u=None, v=None):
        self.origin, self.normal, self.uDirection, self.vDirection = origin, normal, u, v


class Matrix3D(Base):
    def __init__(self):
        self.o = (0.0, 0.0, 0.0)
        self.ax = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))

    @staticmethod
    def create():
        return Matrix3D()

    def setWithCoordinateSystem(self, origin, xAxis, yAxis, zAxis):
        self.o = (origin.x, origin.y, origin.z)
        self.ax = tuple((a.x, a.y, a.z) for a in (xAxis, yAxis, zAxis))
        return True


class ValueInput(Base):
    def __init__(self, s=None, real=None):
        self.stringValue, self.realValue = s, real

    @staticmethod
    def createByString(s):
        if not isinstance(s, str):
            raise TypeError('createByString takes a string')
        return ValueInput(s=s)

    @staticmethod
    def createByReal(v):
        return ValueInput(real=float(v))

    def expr(self, unit='cm'):
        """The SolidCraft expression: a string as is, a real in the internal unit."""
        if self.stringValue is not None:
            return self.stringValue
        return f'{self.realValue * 10} mm' if unit == 'cm' else f'{math.degrees(self.realValue)} deg'


class ObjectCollection(Base):
    def __init__(self):
        self._items = []

    @staticmethod
    def create():
        return ObjectCollection()

    def add(self, item):
        self._items.append(item)
        return True

    def item(self, i):
        return self._items[i]

    @property
    def count(self):
        return len(self._items)

    def __iter__(self):
        return iter(self._items)

    def __len__(self):
        return len(self._items)


class HorizontalAlignments:
    LeftHorizontalAlignment, CenterHorizontalAlignment, RightHorizontalAlignment = 0, 1, 2


class VerticalAlignments:
    TopVerticalAlignment, MiddleVerticalAlignment, BottomVerticalAlignment = 0, 1, 2


class DocumentTypes:
    FusionDesignDocumentType = 0


class UserInterface(Base):
    def messageBox(self, text, *a):
        raise RuntimeError('messageBox: ' + text)


class Documents(Base):
    def add(self, kind):
        from . import fusion
        return fusion._new_document()


class Application(Base):
    _app = None

    def __init__(self):
        self.userInterface = UserInterface()
        self.documents = Documents()
        self.activeProduct = None

    @staticmethod
    def get():
        if Application._app is None:
            Application._app = Application()
        return Application._app
