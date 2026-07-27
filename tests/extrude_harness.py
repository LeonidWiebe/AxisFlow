# -*- coding: utf-8 -*-
"""Offline check of the extrude command.

Runs axisflow_extrude.py against stubbed FreeCAD / Gui / Draft / pivy / AxisFlow
modules, so everything that is geometry bookkeeping rather than rendering can be
verified with plain python3, without FreeCAD:

  * the sweep direction: a real plane gives its axis, a STRAIGHT segment has no
    plane at all and falls back to vertical (and a vertical segment to
    horizontal, since sweeping it up would collapse the surface);
  * a non-planar wire is refused, and so is anything carrying faces;
  * the selection gate goes on at start and comes back off at the end, on every
    exit path including the refusal;
  * the picking click does NOT count as a point for this tool -- the compass
    cannot be aimed before the wire is known, so the point after it is the first;
  * the committed command string builds a Part::Extrusion with Solid=False, the
    direction normalised and the height in LengthFwd (so it stays editable);
  * an oblique drag stays oblique -- the sweep follows the cursor, not the normal;
  * the commit is DELAYED out of the callback;
  * the preview's quad bookkeeping matches the discretized wire.

    python3 tests/extrude_harness.py
"""

import math
import os
import sys
import types


# ---------------------------------------------------------------------------
# stubs
# ---------------------------------------------------------------------------

class V:
    def __init__(self, x=0, y=0, z=0):
        if hasattr(x, "x"):
            x, y, z = x.x, x.y, x.z
        self.x, self.y, self.z = float(x), float(y), float(z)

    def sub(self, o):
        return V(self.x - o.x, self.y - o.y, self.z - o.z)

    def add(self, o):
        return V(self.x + o.x, self.y + o.y, self.z + o.z)

    def normalize(self):
        n = self.Length
        if n:
            self.x, self.y, self.z = self.x / n, self.y / n, self.z / n
        return self

    @property
    def Length(self):
        return math.sqrt(self.x ** 2 + self.y ** 2 + self.z ** 2)

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    def __repr__(self):
        return "V(%g,%g,%g)" % (self.x, self.y, self.z)


class Console:
    out = []
    @staticmethod
    def PrintMessage(m): Console.out.append(("msg", m))
    @staticmethod
    def PrintWarning(m): Console.out.append(("warn", m))
    @staticmethod
    def PrintError(m): Console.out.append(("err", m))
    @staticmethod
    def PrintLog(m): Console.out.append(("log", m))


class Vertex:
    def __init__(self, p): self.Point = V(p)


class Edge:
    """Straight edge unless `curved` is set (then discretize returns 5 points)."""
    def __init__(self, a, b, curved=False):
        self._a, self._b, self.curved = V(a), V(b), curved

    def firstVertex(self): return Vertex(self._a)
    def lastVertex(self): return Vertex(self._b)

    @property
    def Length(self): return self._b.sub(self._a).Length

    def discretize(self, *a, **k):
        n = 5 if self.curved else 2
        return [V(self._a.x + (self._b.x - self._a.x) * i / (n - 1.0),
                  self._a.y + (self._b.y - self._a.y) * i / (n - 1.0),
                  self._a.z + (self._b.z - self._a.z) * i / (n - 1.0))
                for i in range(n)]


class Plane:
    def __init__(self, axis): self.Axis = V(axis)


class BoundBox:
    def __init__(self, diag): self.DiagonalLength = diag


class Shape:
    """Stub shape that answers the two questions the command asks.

    `flatness` is how far the wire strays out of any plane, and `straightness`
    how far it strays off a straight line. Both are compared against the
    tolerance passed in, because that comparison IS what the fix is about: the
    old code asked with OCC's ~1e-7 default and rejected wires that were flat to
    within float noise.
    """

    def __init__(self, edges, plane=None, straight=False, faces=(),
                 flatness=0.0, straightness=1e9, diag=1000.0):
        self.Edges, self.Faces = list(edges), list(faces)
        self._plane, self._straight = plane, straight
        self._flatness, self._straightness = flatness, straightness
        self.BoundBox = BoundBox(diag)

    def isNull(self): return False

    @property
    def Vertexes(self):
        return [Vertex(self.Edges[0].firstVertex().Point)] if self.Edges else []

    def is_straight(self, tol):
        return self._straight or self._straightness <= tol

    def distToShape(self, other):
        """Nearest point on this wire to `other`, clamped to each edge.

        Modelled rather than faked: the command relies on the projection being
        ON the wire and clamped to its ends, and a stub that just echoed the
        query point back would have let a broken anchor through.
        """
        q = other.Point
        best = None
        for e in self.Edges:
            a, b = e.firstVertex().Point, e.lastVertex().Point
            d = b.sub(a)
            L2 = d.x * d.x + d.y * d.y + d.z * d.z
            t = 0.0 if L2 == 0 else ((q.x - a.x) * d.x + (q.y - a.y) * d.y
                                     + (q.z - a.z) * d.z) / L2
            t = max(0.0, min(1.0, t))
            p = V(a.x + d.x * t, a.y + d.y * t, a.z + d.z * t)
            dist = p.sub(q).Length
            if best is None or dist < best[0]:
                best = (dist, p)
        return best[0], [(best[1], q)], [None]

    def findPlane(self, tol=-1):
        if self._plane is None:
            return None
        if tol is not None and tol >= 0 and self._flatness > tol:
            return None
        if (tol is None or tol < 0) and self._flatness > 1e-7:
            return None                    # OCC's default strictness
        return self._plane


class Obj:
    def __init__(self, name, shape): self.Name, self.Shape = name, shape


class Doc:
    Name = "Doc"
    def __init__(self): self.transactions = []
    def openTransaction(self, n): self.transactions.append(("open", n))
    def commitTransaction(self): self.transactions.append(("commit",))
    def abortTransaction(self): self.transactions.append(("abort",))
    def recompute(self): self.transactions.append(("recompute",))
    def getObject(self, name): return OBJECTS.get(name)


# A planar but NOT straight wire (two edges at an angle) lying in the XY plane.
PLANAR = Shape([Edge(V(0, 0, 0), V(10, 0, 0)),
                Edge(V(10, 0, 0), V(10, 10, 0), curved=True)],
               plane=Plane(V(0, 0, 1)))
# A single horizontal segment: planar in the "any plane holds it" sense, so
# find_plane() refuses to name one.
STRAIGHT = Shape([Edge(V(0, 0, 0), V(10, 0, 0))], straight=True)
# The same, standing up.
VERTICAL = Shape([Edge(V(0, 0, 0), V(0, 0, 10))], straight=True)
# Genuinely non-planar: a skew polyline. find_plane() returns None.
SKEW = Shape([Edge(V(0, 0, 0), V(10, 0, 0)),
              Edge(V(10, 0, 0), V(10, 10, 5))], plane=None)
# Drawn flat, but each point picked up float noise on the way through Coin:
# 1e-4 mm out of plane on a 1000 mm wire. OCC's default would reject it.
NOISY = Shape([Edge(V(0, 0, 0), V(1000, 0, 0)),
               Edge(V(1000, 0, 0), V(1000, 1000, 0), curved=True)],
              plane=Plane(V(0, 0, 1)), flatness=1e-4, diag=1414.0)
# Dead straight, same noise. Must still be treated as STRAIGHT: handing it to a
# loose find_plane() would return a plane with an arbitrary normal.
NOISY_STRAIGHT = Shape([Edge(V(0, 0, 0), V(1000, 0, 0))],
                       plane=Plane(V(0.3, 0.4, 0.86)), flatness=1e-4,
                       straightness=1e-4, diag=1000.0)
# Has a face -- a different command's job.
FACED = Shape([Edge(V(0, 0, 0), V(10, 0, 0))], plane=Plane(V(0, 0, 1)),
              faces=("f",))

OBJECTS = {
    "Wire": Obj("Wire", PLANAR),
    "Seg": Obj("Seg", STRAIGHT),
    "Post": Obj("Post", VERTICAL),
    "Skew": Obj("Skew", SKEW),
    "Face": Obj("Face", FACED),
    "Noisy": Obj("Noisy", NOISY),
    "NoisyStraight": Obj("NoisyStraight", NOISY_STRAIGHT),
}
DOC = Doc()

App = types.ModuleType("FreeCAD")
App.Vector = V
App.Console = Console
App.ActiveDocument = DOC
App.listDocuments = lambda: {"Doc": DOC}
App.Qt = types.SimpleNamespace(translate=staticmethod(lambda ctx, t: t))
sys.modules["FreeCAD"] = App


class Selection:
    selected = []
    gate = None
    @staticmethod
    def getSelection(): return list(Selection.selected)
    @staticmethod
    def clearSelection(): Selection.selected = []
    @staticmethod
    def addSelection(o): Selection.selected.append(o)
    @staticmethod
    def addSelectionGate(g): Selection.gate = g
    @staticmethod
    def removeSelectionGate(): Selection.gate = None


class SceneGraph:
    def __init__(self): self.children, self.refs = [], 0
    def addChild(self, c): self.children.append(c)
    def removeChild(self, c): self.children.remove(c)
    def ref(self): self.refs += 1
    def unref(self): self.refs -= 1


SG = SceneGraph()


class View:
    hit = None
    @staticmethod
    def getObjectInfo(pos): return View.hit
    @staticmethod
    def getSceneGraph(): return SG


Gui = types.ModuleType("FreeCADGui")
Gui.Selection = Selection
Gui.ActiveDocument = types.SimpleNamespace(ActiveView=View)
Gui.addModule = lambda m: None
COMMANDS = []
Gui.doCommand = lambda c: COMMANDS.append(c)
sys.modules["FreeCADGui"] = Gui

PENDING = []
QtCore = types.ModuleType("PySide.QtCore")
QtCore.QTimer = types.SimpleNamespace(
    singleShot=staticmethod(lambda ms, fn: PENDING.append(fn)))
QtCore.QT_TRANSLATE_NOOP = lambda ctx, text: text
QtCore.QCoreApplication = types.SimpleNamespace(
    translate=staticmethod(lambda ctx, text: text))
# Qt GUI/Widgets are stubbed only far enough for the pick cursor to be
# EXERCISED rather than skipped: without QMdiArea and QuarterWidget lookalikes
# the helper bails out early and the set/unset bookkeeping is never tested.
CURSOR = {"set": 0, "unset": 0}


class _QuarterWidget:
    def inherits(self, name):
        return name == "SIM::Coin3D::Quarter::QuarterWidget"

    def setCursor(self, c):
        CURSOR["set"] += 1

    def unsetCursor(self):
        CURSOR["unset"] += 1


class _MdiArea:
    def findChildren(self, kind):
        return [_QUARTER]


_QUARTER = _QuarterWidget()
_MDI = _MdiArea()

QtWidgets = types.ModuleType("PySide.QtWidgets")
QtWidgets.QMdiArea = _MdiArea
QtWidgets.QWidget = object
QtGui = types.ModuleType("PySide.QtGui")
# QCursor construction is not stubbed in detail; _build_pick_cursor catches and
# logs, and the helper then simply has nothing to set. The harness asserts on
# the CALLS, which is the part with the lifetime bug potential.
Gui.getMainWindow = lambda: types.SimpleNamespace(
    findChild=lambda kind: _MDI,
    devicePixelRatio=lambda: 1.0)

_pyside = types.ModuleType("PySide")
_pyside.QtCore = QtCore
_pyside.QtGui = QtGui
_pyside.QtWidgets = QtWidgets
sys.modules["PySide"] = _pyside
sys.modules["PySide.QtCore"] = QtCore
sys.modules["PySide.QtGui"] = QtGui
sys.modules["PySide.QtWidgets"] = QtWidgets

# Draft's geometry helpers, with the behaviour that matters here: a straight
# wire has NO plane (that is why is_straight_line exists in the real module).
_dgeo = types.ModuleType("draftgeoutils.geometry")
_dgeo.is_straight_line = lambda shape, tol=-1: shape.is_straight(tol)
_dgeo.find_plane = lambda shape, tol=-1: shape.findPlane(tol)
_draftgeoutils = types.ModuleType("draftgeoutils")
_draftgeoutils.geometry = _dgeo
sys.modules["draftgeoutils"] = _draftgeoutils
sys.modules["draftgeoutils.geometry"] = _dgeo


class _Field(list):
    def setValues(self, start, num, vals): self[:] = list(vals)
    def setValue(self, *a): self[:] = list(a)
    def setNum(self, n): del self[n:]


class _Node:
    def __init__(self, *a, **k):
        self.children = []
        self.point = _Field()
        self.numVertices = _Field()
        self.vertexOrdering = None
        self.shapeType = None
        self.diffuseColor = _Field()
        self.transparency = _Field()
    def addChild(self, c): self.children.append(c)


coin = types.ModuleType("pivy.coin")
coin.SoSeparator = _Node
coin.SoCoordinate3 = _Node
coin.SoFaceSet = _Node
coin.SoMaterial = _Node


class _Hints(_Node):
    UNKNOWN_ORDERING = 0
    UNKNOWN_SHAPE_TYPE = 0


coin.SoShapeHints = _Hints
_part = types.ModuleType("Part")
_part.Vertex = Vertex
sys.modules["Part"] = _part

_pivy = types.ModuleType("pivy")
_pivy.coin = coin
sys.modules["pivy"] = _pivy
sys.modules["pivy.coin"] = coin

AIMED = []
AF = types.ModuleType("AxisFlow")
AF.getPoint = lambda **k: None
AF.endSession = lambda: None
AF.setBasePoint = lambda v: AIMED.append(("base", V(v)))
AF.alignCompassX = lambda v: AIMED.append(("aim", V(v)))
AF.getCursorPixel = lambda: (100, 100)
# The compass-visibility flag. Deliberately NOT modelled as activate/deactivate:
# the whole point of setCompassVisible is that it is display state only, and an
# earlier attempt built on deactivate() failed because AxisFlowTool::activate()
# re-activates the service from inside getPoint().
COMPASS = {"visible": True, "calls": []}


def _af_set_compass_visible(on):
    COMPASS["visible"] = bool(on)
    COMPASS["calls"].append(bool(on))


AF.setCompassVisible = _af_set_compass_visible
AF.isActive = lambda: True
sys.modules["AxisFlow"] = AF


def run_pending():
    while PENDING:
        PENDING.pop(0)()


def load(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                        name + ".py")
    mod = types.ModuleType(name)
    mod.__file__ = path
    sys.modules[name] = mod
    exec(compile(open(path, encoding="utf-8").read(), path, "exec"),
         mod.__dict__)
    return mod


T = load("axisflow_transform")
# See transform_harness: the cached cursor is pre-seeded so the helper takes its
# normal path without needing a stubbed QPainter.
T._pick_cursor = "<stub cursor>"
E = load("axisflow_extrude")


def fresh():
    COMMANDS[:] = []
    PENDING[:] = []
    AIMED[:] = []
    DOC.transactions[:] = []
    Selection.selected = []
    Selection.gate = None
    View.hit = None
    App.ActiveDocument = DOC
    SG.children[:] = []
    SG.refs = 0


def vec_eq(v, x, y, z, eps=1e-9):
    return abs(v.x - x) < eps and abs(v.y - y) < eps and abs(v.z - z) < eps


# ---------------------------------------------------------------------------
# sweep direction
# ---------------------------------------------------------------------------

n = E.wire_normal(OBJECTS["Wire"])
assert vec_eq(n, 0, 0, 1), n
print("нормаль плоской проволоки:", n, "ОК")

n = E.wire_normal(OBJECTS["Seg"])
assert vec_eq(n, 0, 0, 1), n
print("прямой отрезок -> вертикальная протяжка:", n, "ОК")

n = E.wire_normal(OBJECTS["Post"])
assert vec_eq(n, 1, 0, 0), n
print("вертикальный отрезок -> горизонтальная:", n, "ОК")

assert E.wire_normal(OBJECTS["Skew"]) is None
assert not E.is_extrudable(OBJECTS["Skew"])
print("неплоская проволока отвергнута: ОК")

assert E.wire_normal(OBJECTS["Face"]) is None
print("форма с гранями отвергнута: ОК")

# -- tolerance -------------------------------------------------------------
# The bug this exists for: a line drawn flat with the compass was rejected as
# non-planar, because the points come out of Coin in single precision and land
# ~1e-4 mm off the plane at metre scale, while OCC's default asks for ~1e-7.
tol = E._planar_tol(NOISY)
assert 1e-4 < tol < 1e-2, tol
print("допуск масштабируется по габариту:", tol, "ОК")

assert NOISY.findPlane() is None, "заглушка не воспроизводит строгость OCC"
n = E.wire_normal(OBJECTS["Noisy"])
assert n is not None and vec_eq(n, 0, 0, 1), n
print("плоская линия с float-шумом принята: ОК")

# and the interaction that is easy to get wrong: a straight line carrying the
# same noise must stay STRAIGHT, or a loose find_plane() hands back a plane with
# an arbitrary normal and we sweep in a meaningless direction.
n = E.wire_normal(OBJECTS["NoisyStraight"])
assert vec_eq(n, 0, 0, 1), ("прямая с шумом получила произвольную плоскость", n)
print("прямая с шумом осталась прямой, а не получила случайную нормаль: ОК")

# the loosened tolerance must NOT start accepting genuinely skew wires
assert E.wire_normal(OBJECTS["Skew"]) is None
print("послабление не пропустило по-настоящему неплоскую: ОК")


# ---------------------------------------------------------------------------
# the selection gate
# ---------------------------------------------------------------------------

gate = E._ExtrudeGate()
assert gate.allow(DOC, OBJECTS["Wire"], "") is True
assert gate.allow(DOC, OBJECTS["Skew"], "") is False
assert gate.allow(DOC, None, "") is False
print("фильтр выбора: ОК")

fresh()
Selection.selected = [OBJECTS["Wire"]]
t = E.Extrude()
assert t.start()
assert Selection.gate is not None, "фильтр не установлен"
t._end()
assert Selection.gate is None, "фильтр не снят"
print("фильтр ставится и снимается: ОК")

# refusal path must also clean up
fresh()
Selection.selected = [OBJECTS["Skew"]]
t = E.Extrude()
assert not t.start(), "неплоская проволока принята"
assert Selection.gate is None, "фильтр остался висеть после отказа"
assert any(k == "err" for k, _ in Console.out[-3:])
print("отказ на неплоской проволоке чистит за собой: ОК")


# ---------------------------------------------------------------------------
# compass aiming
# ---------------------------------------------------------------------------

fresh()
Selection.selected = [OBJECTS["Wire"]]
t = E.Extrude()
assert t.start()
kinds = [k for k, _ in AIMED]
assert kinds == ["aim", "base"], kinds
assert vec_eq(AIMED[0][1], 0, 0, 1), AIMED[0]
assert vec_eq(AIMED[1][1], 0, 0, 0), AIMED[1]     # first vertex of the wire
print("компас наведён по нормали, якорь на первой вершине: ОК")
t._end()


# ---------------------------------------------------------------------------
# the picking click does not consume a point
# ---------------------------------------------------------------------------

fresh()
View.hit = {"Object": "Wire"}
t = E.Extrude()
assert t.start()
assert t._picking, "без выделения должен быть режим выбора"
t._on_point(V(3, 3, 3), True)                     # this click only picks
assert not t._picking
assert t.points == [], "клик выбора израсходовал точку"
assert not COMMANDS and not PENDING, "команда стартовала с одного клика"
t._on_point(V(0, 0, 25), True)                    # THIS is the height
assert len(t.points) == 1, t.points
run_pending()
assert COMMANDS, "коммит не произошёл"
print("клик выбора точку не расходует: ОК")

# and Move keeps the opposite behaviour
fresh()
View.hit = {"Object": "Wire"}
m = T.Move()
assert m.start()
m._on_point(V(1, 2, 0), True)
assert len(m.points) == 1, "Move потерял двойное назначение первого клика"
print("Move сохранил прежнее поведение: ОК")


# ---------------------------------------------------------------------------
# the committed command
# ---------------------------------------------------------------------------

fresh()
Selection.selected = [OBJECTS["Wire"]]
t = E.Extrude()
assert t.start()
t._on_point(V(0, 0, 40), True)
assert not COMMANDS, "коммит НЕ отложен"
run_pending()
script = "\n".join(COMMANDS)
assert "Part::Extrusion" in script
assert "_af_ext.Solid = False" in script
assert "_af_ext.LengthFwd = 40.0" in script, script
assert "_af_ext.DirMode = 'Custom'" in script
assert "_af_ext.Base = FreeCAD.ActiveDocument.Wire" in script
assert "Visibility = False" in script
assert ("open", "AxisFlow extrude") in DOC.transactions
assert ("commit",) in DOC.transactions
print("команда Part::Extrusion собрана: ОК")

# direction is normalised, not the raw delta
line = [c for c in COMMANDS if c.startswith("_af_ext.Dir =")][0]
assert "FreeCAD.Vector(0.0, 0.0, 1.0)" in line, line
print("направление нормировано:", line.split("= ")[1], "ОК")


# ---------------------------------------------------------------------------
# an oblique drag stays oblique
# ---------------------------------------------------------------------------

fresh()
Selection.selected = [OBJECTS["Wire"]]
t = E.Extrude()
assert t.start()
t._on_point(V(0, 30, 40), True)                   # off the compass X axis
run_pending()
line = [c for c in COMMANDS if c.startswith("_af_ext.Dir =")][0]
length = [c for c in COMMANDS if c.startswith("_af_ext.LengthFwd")][0]
assert "0.6" in line and "0.8" in line, line      # (0, 0.6, 0.8)
assert "50.0" in length, length                   # 3-4-5
print("косая протяжка сохранена:", line.split("= ")[1], length.split("= ")[1], "ОК")


# ---------------------------------------------------------------------------
# a zero-height sweep commits nothing
# ---------------------------------------------------------------------------

fresh()
Selection.selected = [OBJECTS["Wire"]]
t = E.Extrude()
assert t.start()
t._on_point(V(0, 0, 0), True)                     # exactly the anchor
run_pending()
assert not COMMANDS, COMMANDS
print("нулевая протяжка не создаёт объект: ОК")


# ---------------------------------------------------------------------------
# preview bookkeeping
# ---------------------------------------------------------------------------

fresh()
p = E._Preview(PLANAR)
assert p.attach(), "превью не прицепилось"
assert SG.refs == 1, "граф сцены не удержан"
# 1 straight edge (2 pts) + 1 curved edge (5 pts) = 1 + 4 = 5 quads
quads = sum(len(s) - 1 for s in p._strips)
assert quads == 5, quads
assert list(p._sep.children[-1].numVertices) == [4] * 5
p.update(V(0, 0, 7))
pts = list(p._coords.point)
assert len(pts) == 4 * quads, len(pts)
# every quad is base, base, base+delta, base+delta
for i in range(quads):
    a, b, b2, a2 = pts[4 * i:4 * i + 4]
    assert abs(b2[2] - b[2] - 7) < 1e-9 and abs(a2[2] - a[2] - 7) < 1e-9
print("превью:", quads, "четырёхугольников, смещение верного ряда: ОК")
p.finalize()
assert SG.children == [] and SG.refs == 0, "превью не убрано за собой"
print("превью снято: ОК")

# finalize twice must be harmless: _drop_ghost can be reached twice
p.finalize()
print("идемпотентность превью: ОК")

# ---------------------------------------------------------------------------
# pick mode: for Extrude the picking click buys the object only, so the compass
# must come back BEFORE the height point is asked for
# ---------------------------------------------------------------------------

fresh()
CURSOR["set"] = CURSOR["unset"] = 0
COMPASS["visible"] = True
COMPASS["calls"][:] = []
View.hit = {"Object": "Wire"}
t = E.Extrude()
assert t.start()
assert COMPASS["visible"] is False and CURSOR["set"] == 1, "режим выбора не включился"
t._on_point(V(3, 3, 3), True)                     # picks only
assert COMPASS["visible"] is True, "компас не вернулся перед вводом высоты"
assert CURSOR["unset"] == 1
assert t.points == [], "клик выбора израсходовал точку"
assert Selection.gate is not None, "фильтр снят слишком рано"
t._end()
assert Selection.gate is None
print("протяжка: компас возвращается до ввода высоты: ОК")

# refusal must restore the cursor and the compass too
fresh()
CURSOR["set"] = CURSOR["unset"] = 0
COMPASS["visible"] = True
View.hit = {"Object": "Skew"}
t = E.Extrude()
assert t.start()
t._on_point(V(1, 1, 1), True)                     # picks a non-planar wire
assert COMPASS["visible"] is True, "отказ оставил компас спрятанным"
assert CURSOR["unset"] >= 1, "отказ оставил курсор выбора"
assert Selection.gate is None
print("отказ на неплоской проволоке восстанавливает курсор и компас: ОК")

# ---------------------------------------------------------------------------
# anchor: on the wire, nearest to where it was picked
# ---------------------------------------------------------------------------

fresh()
CURSOR["set"] = CURSOR["unset"] = 0
COMPASS["visible"] = True
# PLANAR runs (0,0,0)->(10,0,0)->(10,10,0). Click near the far end, off the
# wire, as a real pick always is.
View.hit = {"Object": "Wire", "x": 10.4, "y": 7.0, "z": 0.3}
t = E.Extrude()
assert t.start()
t._on_point(V(0, 0, 0), True)
base = [v for k, v in AIMED if k == "base"][0]
assert vec_eq(base, 10, 7, 0), base
print("якорь спроецирован на проволоку у точки выбора:", base, "ОК")

# clamped to the wire, not extrapolated along it
fresh()
View.hit = {"Object": "Wire", "x": -50.0, "y": -50.0, "z": 0.0}
t = E.Extrude()
assert t.start()
t._on_point(V(0, 0, 0), True)
base = [v for k, v in AIMED if k == "base"][0]
assert vec_eq(base, 0, 0, 0), base
print("проекция прижата к концу проволоки:", base, "ОК")

# a selection made BEFORE the command was never clicked: first vertex
fresh()
Selection.selected = [OBJECTS["Wire"]]
t = E.Extrude()
assert t.start()
base = [v for k, v in AIMED if k == "base"][0]
assert vec_eq(base, 0, 0, 0), base
assert t.pick_point is None
print("без клика якорь на первой вершине: ОК")
t._end()


# ---------------------------------------------------------------------------
# Escape cancels the sweep
# ---------------------------------------------------------------------------

fresh()
Selection.selected = [OBJECTS["Wire"]]
t = E.Extrude()
assert t.start()
assert t.ghost is not None, "превью не построено"
t._on_finish()                                    # Escape -> FinishLine
run_pending()
assert not COMMANDS, "Escape всё-таки построил объект"
assert t._ended, "Escape не завершил команду"
assert t.ghost is None, "призрак остался на экране"
assert Selection.gate is None, "фильтр выбора остался висеть"
assert COMPASS["visible"] is True
print("Escape отменяет протяжку и убирает призрак: ОК")

# and Move must keep the old meaning of FinishLine: start over, do not quit
fresh()
Selection.selected = [OBJECTS["Wire"]]
m = T.Move()
assert m.start()
m._on_point(V(1, 1, 0), True)
assert len(m.points) == 1
m._on_finish()
assert not m._ended, "Escape завершил Move, хотя должен был начать заново"
assert m.points == []
print("у Move Escape по-прежнему значит «начать заново»: ОК")

print("\nвсе проверки пройдены")
