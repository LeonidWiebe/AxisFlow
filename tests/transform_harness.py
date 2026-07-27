# -*- coding: utf-8 -*-
"""Offline check of the transform tools' state machine.

Runs axisflow_transform.py against stubbed FreeCAD/Gui/AxisFlow modules, so the
parts that are pure bookkeeping can be verified with plain python3, without
FreeCAD:

  * a pre-existing selection goes straight to collecting points;
  * with nothing selected the FIRST click both picks the object under the
    cursor AND counts as the base point (one click, not two);
  * clicking empty space in that state does not consume the click;
  * the committed command string is the one Draft would have produced, and
    Move / Copy differ only by the copy flag;
  * the commit is DELAYED out of the callback (this is what crashed the line
    command when it was done inline);
  * a zero-length move commits nothing;
  * switching documents mid-command ends it.

    python3 tests/transform_harness.py
"""

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

    @property
    def Length(self):
        return (self.x ** 2 + self.y ** 2 + self.z ** 2) ** 0.5

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


class Obj:
    def __init__(self, name): self.Name = name


class Doc:
    Name = "Doc"
    def __init__(self): self.transactions = []
    def openTransaction(self, n): self.transactions.append(("open", n))
    def commitTransaction(self): self.transactions.append(("commit",))
    def abortTransaction(self): self.transactions.append(("abort",))
    def recompute(self): self.transactions.append(("recompute",))
    def getObject(self, name): return OBJECTS.get(name)


OBJECTS = {"Box": Obj("Box"), "Cyl": Obj("Cyl")}
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
    @staticmethod
    def getSelection(): return list(Selection.selected)
    @staticmethod
    def clearSelection(): Selection.selected = []
    @staticmethod
    def addSelection(o): Selection.selected.append(o)


class View:
    hit = None                       # what getObjectInfo should report
    @staticmethod
    def getObjectInfo(pos): return View.hit


Gui = types.ModuleType("FreeCADGui")
Gui.Selection = Selection
Gui.ActiveDocument = types.SimpleNamespace(ActiveView=View)
Gui.addModule = lambda m: None
COMMANDS = []
Gui.doCommand = lambda c: COMMANDS.append(c)
sys.modules["FreeCADGui"] = Gui

# QTimer.singleShot must NOT fire straight away: the whole point is that the
# commit lands on a later turn of the event loop. Queue it instead.
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

AF = types.ModuleType("AxisFlow")
AF.getPoint = lambda **k: None
AF.endSession = lambda: None
AF.setBasePoint = lambda v: None
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


def load():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "axisflow_transform.py")
    mod = types.ModuleType("axisflow_transform")
    exec(compile(open(path, encoding="utf-8").read(), path, "exec"),
         mod.__dict__)
    return mod


# ---------------------------------------------------------------------------
# checks
# ---------------------------------------------------------------------------

T = load()
# Skip the QPainter work: stubbing QPixmap/QPainter/QPolygonF well enough to
# draw would test Qt, not us. Pre-seeding the cached cursor makes the helper
# take its normal path, so what gets exercised is the set/unset bookkeeping --
# the part that can actually leak a cursor onto the user's viewport.
T._pick_cursor = "<stub cursor>"


def fresh():
    COMMANDS[:] = []
    PENDING[:] = []
    DOC.transactions[:] = []
    Selection.selected = []
    View.hit = None
    App.ActiveDocument = DOC
    CURSOR["set"] = CURSOR["unset"] = 0
    COMPASS["visible"] = True
    COMPASS["calls"][:] = []


# -- with a selection ------------------------------------------------------
fresh()
Selection.selected = [OBJECTS["Box"]]
t = T.Move()
assert t.start()
assert t.objects and not t._picking, "выделение не подхвачено"
t._on_point(V(0, 0, 0), True)
assert len(t.points) == 1, t.points
assert not COMMANDS, "команда выполнена слишком рано"
t._on_point(V(10, 5, 0), True)
print("после двух точек, до такта событий: команд =", len(COMMANDS))
assert not COMMANDS, "коммит НЕ отложен -- это и роняло линию"
run_pending()
print("после такта:", COMMANDS)
assert len(COMMANDS) == 1
assert "Draft.move(" in COMMANDS[0] and "copy=False" in COMMANDS[0]
assert "10.0, 5.0, 0.0" in COMMANDS[0], COMMANDS[0]
assert DOC.transactions[0] == ("open", "AxisFlow move")
assert ("commit",) in DOC.transactions
print("Move с выделением: ОК")

# -- copy differs only by the flag ----------------------------------------
fresh()
Selection.selected = [OBJECTS["Box"]]
t = T.Copy()
t.start()
t._on_point(V(0, 0, 0), True)
t._on_point(V(3, 0, 0), True)
run_pending()
assert "copy=True" in COMMANDS[0], COMMANDS[0]
print("Copy: ОК")

# -- no selection: the first click picks AND is the base point -------------
fresh()
View.hit = {"Object": "Cyl"}
t = T.Move()
t.start()
assert t._picking, "должен ждать выбор"
t._on_point(V(1, 1, 0), True)          # one click does both jobs
assert not t._picking, "объект не выбран"
assert [o.Name for o in t.objects] == ["Cyl"], t.objects
assert len(t.points) == 1, "клик выбора не засчитан как базовая точка"
t._on_point(V(4, 1, 0), True)
run_pending()
assert "FreeCAD.ActiveDocument.Cyl" in COMMANDS[0], COMMANDS[0]
print("выбор совмещён с базовой точкой: ОК")

# -- clicking nothing must not consume the click ---------------------------
fresh()
View.hit = None
t = T.Move()
t.start()
t._on_point(V(1, 1, 0), True)
assert t._picking and not t.points, "пустой клик засчитан"
View.hit = {"Object": "Box"}
t._on_point(V(2, 2, 0), True)
assert len(t.points) == 1 and t.objects
print("клик по пустому месту: ОК")

# -- a move that goes nowhere commits nothing ------------------------------
fresh()
Selection.selected = [OBJECTS["Box"]]
t = T.Move()
t.start()
t._on_point(V(7, 7, 7), True)
t._on_point(V(7, 7, 7), True)
run_pending()
assert not COMMANDS, "нулевое перемещение всё-таки закоммитило"
print("нулевое перемещение: ОК")

# -- the document changing under the command ends it -----------------------
fresh()
Selection.selected = [OBJECTS["Box"]]
t = T.Move()
t.start()
t._on_point(V(0, 0, 0), True)
other = Doc(); other.Name = "Doc2"
App.ActiveDocument = other
t._on_move(V(1, 1, 1))
assert t._ended, "команда не завершилась при смене документа"
print("смена документа: ОК")

# -- teardown is idempotent ------------------------------------------------
fresh()
Selection.selected = [OBJECTS["Box"]]
t = T.Move()
t.start()
ends = []
AF.endSession = lambda: ends.append(1)
t._end(); t._end(); t._end()
assert len(ends) == 1, ends
print("идемпотентность завершения: ОК")


# ---------------------------------------------------------------------------
# pick mode: compass out of the way, locate cursor up
# ---------------------------------------------------------------------------

fresh()
View.hit = {"Object": "Box"}
t = T.Move()
assert t.start()
assert t._picking
assert COMPASS["calls"] == [False], COMPASS["calls"]
assert COMPASS["visible"] is False, "компас не спрятан на время выбора"
assert CURSOR["set"] == 1, CURSOR
print("режим выбора: компас спрятан, курсор поставлен: ОК")

t._on_point(V(1, 2, 0), True)                 # picks AND is the base point
assert not t._picking
assert COMPASS["visible"] is True, "компас не вернулся после выбора"
assert CURSOR["unset"] == 1, CURSOR
assert len(t.points) == 1, "Move потерял двойное назначение первого клика"
print("после выбора: компас возвращён, курсор снят, точка засчитана: ОК")

# a pre-existing selection must not touch either
fresh()
Selection.selected = [OBJECTS["Box"]]
t = T.Move()
assert t.start()
assert COMPASS["calls"] == [], COMPASS["calls"]
assert CURSOR["set"] == 0, CURSOR
t._end()
print("при готовом выделении режим выбора не включается: ОК")

# cancelled while still picking: everything must come back
fresh()
View.hit = None
t = T.Move()
assert t.start()
assert COMPASS["visible"] is False and CURSOR["set"] == 1
t._end()                                       # Escape during the pick
assert COMPASS["visible"] is True, "компас не восстановлен после отмены"
assert CURSOR["unset"] == 1, "курсор не снят после отмены"
print("отмена во время выбора всё восстанавливает: ОК")

# hide then show, in that order, exactly once each
fresh()
View.hit = {"Object": "Box"}
t = T.Move()
assert t.start()
t._on_point(V(0, 0, 0), True)
t._end()
assert COMPASS["calls"] == [False, True], COMPASS["calls"]
print("компас прячется и возвращается ровно по разу: ОК")

# _end twice must not double-restore
fresh()
View.hit = None
t = T.Move()
assert t.start()
t._end()
t._end()
assert CURSOR["unset"] == 1, CURSOR
print("идемпотентность выхода из режима выбора: ОК")

print("\nвсе проверки пройдены")
