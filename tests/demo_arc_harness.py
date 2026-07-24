"""Offline check of the line demo's arc state machine.

Runs axisflow_demo.py against stubbed FreeCAD/Part modules, so the parts that
are pure bookkeeping -- which segment carries the arc, what a mode toggle
abandons, how degenerate arcs fall back to straight segments -- can be verified
without FreeCAD at all.

    python3 tests/demo_arc_harness.py

This is NOT part of the gtest suites (Tier 1) or the in-FreeCAD Python tests
(Tier 2): it needs neither, which is exactly the point.
"""
import os, sys, types, math

class V:
    def __init__(self, x=0, y=0, z=0):
        if hasattr(x, 'x'): x, y, z = x.x, x.y, x.z
        self.x, self.y, self.z = float(x), float(y), float(z)
    def __sub__(self, o): return V(self.x-o.x, self.y-o.y, self.z-o.z)
    def __add__(self, o): return V(self.x+o.x, self.y+o.y, self.z+o.z)
    def __mul__(self, k): return V(self.x*k, self.y*k, self.z*k)
    def __iter__(self): return iter((self.x, self.y, self.z))
    def __repr__(self): return f"({self.x:g},{self.y:g},{self.z:g})"
    def __eq__(self, o): return abs(self.x-o.x)+abs(self.y-o.y)+abs(self.z-o.z) < 1e-9
    @property
    def Length(self): return math.sqrt(self.x**2+self.y**2+self.z**2)

class Console:
    msgs = []
    @staticmethod
    def PrintMessage(m): Console.msgs.append(m)
    PrintWarning = PrintError = PrintMessage

App = types.ModuleType("FreeCAD"); App.Vector = V; App.Console = Console
class _Doc:
    Name = "Doc"
    def openTransaction(self,*a): pass
    def commitTransaction(self,*a): pass
    def abortTransaction(self,*a): pass
    def recompute(self,*a): pass
_doc = _Doc()
App.ActiveDocument = _doc; App.listDocuments = lambda: {"Doc": _doc}
App.getDocument = lambda n: _doc; App.newDocument = lambda: _doc
Gui = types.ModuleType("FreeCADGui"); Gui.ActiveDocument = None
sys.modules['FreeCAD'] = App; sys.modules['FreeCADGui'] = Gui
AF = types.ModuleType("AxisFlow")
AF.getPoint = lambda **k: None; AF.endSession = lambda: None
AF.setPlane = lambda *a, **k: None
base_points = []
AF.setBasePoint = lambda v: base_points.append(V(v))
sys.modules['AxisFlow'] = AF

src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'axisflow_demo.py')).read()
mod = types.ModuleType("demo"); mod.__dict__['__name__'] = 'demo'
exec(compile(src, 'axisflow_demo.py', 'exec'), mod.__dict__)
D = mod._LineDrawer

d = D(); d._doc_name = "Doc"
d._sep = None                    # no Coin preview
d._update_snap_object = lambda: None   # no document

# straight, straight, then arc mode: endpoint + bulge, then straight again
for p in [V(0,0,0), V(10,0,0)]:
    d._on_point(p, True)
print("после двух точек:", d._pts, "bulges:", d._bulges)

d._on_mode_toggle()
print("режим дуги:", d._arc_mode)
d._on_point(V(20,0,0), True)     # arc endpoint
print("  endpoint отложен, _pts:", d._pts, "arc_end:", d._arc_end)
d._on_point(V(15,5,0), True)     # point on the arc
print("  после дуги: _pts:", d._pts, "bulges:", d._bulges)

d._on_mode_toggle()
d._on_point(V(30,0,0), True)
print("после прямой:", d._pts, "bulges:", d._bulges)

# the bulge must belong to the segment that ends at the arc endpoint
seg = 2
assert seg in d._bulges, "дуга привязана не к тому сегменту"
assert d._bulges[seg] == V(15,5,0), d._bulges
assert d._pts[seg] == V(20,0,0)
assert 3 not in d._bulges, "прямой сегмент получил выпуклость"
print("индекс сегмента дуги: ОК")

# toggling mid-arc abandons it
d._on_mode_toggle()
d._on_point(V(40,0,0), True)
assert d._arc_end == V(40,0,0)
d._on_mode_toggle()
assert d._arc_end is None and not d._arc_mode
print("отмена наполовину поставленной дуги: ОК")
print("итог _pts:", d._pts, "bulges:", d._bulges)

# ---- _edges: correct edge order and the degenerate-arc fallback ----------
class _Edge:
    def __init__(self, kind, pts): self.kind, self.pts = kind, pts
class _StubPart(types.ModuleType):
    class Line: pass
    class _Curve:
        def __init__(self, kind): self.kind = kind
    class LineSegment:
        def __init__(self, a, b): self.a, self.b = a, b
        def toShape(self): return _Edge('line', (self.a, self.b))
    class Arc:
        def __init__(self, a, m, b):
            # OCC raises when the three points have no circle through them
            ab, am = (b.x-a.x, b.y-a.y, b.z-a.z), (m.x-a.x, m.y-a.y, m.z-a.z)
            cr = (ab[1]*am[2]-ab[2]*am[1], ab[2]*am[0]-ab[0]*am[2], ab[0]*am[1]-ab[1]*am[0])
            if sum(c*c for c in cr) < 1e-18:
                raise ValueError("collinear points, no arc")
            self.a, self.m, self.b = a, m, b
        def toShape(self): return _Edge('arc', (self.a, self.m, self.b))
    Wire = staticmethod(lambda edges: ('wire', edges))
    Vertex = staticmethod(lambda p: ('vertex', p))
sys.modules['Part'] = _StubPart('Part')

kinds = [e.kind for e in d._edges()]
print("рёбра:", kinds)
assert kinds == ['line', 'arc', 'line'], kinds
print("порядок рёбер: ОК")

# an arc whose three points are collinear must degrade to a straight segment
d2 = D(); d2._doc_name = "Doc"; d2._sep = None
d2._update_snap_object = lambda: None
d2._pts = [V(0,0,0), V(10,0,0)]
d2._bulges = {1: V(5,0,0)}            # bulge point sits on the chord
print("вырожденная дуга ->", [e.kind for e in d2._edges()])
assert [e.kind for e in d2._edges()] == ['line']
print("откат вырожденной дуги: ОК")

# a zero-length segment produces no edge at all
d3 = D(); d3._doc_name = "Doc"; d3._sep = None
d3._pts = [V(0,0,0), V(0,0,0), V(5,0,0)]
print("нулевой сегмент ->", [e.kind for e in d3._edges()])
assert [e.kind for e in d3._edges()] == ['line']
print("нулевой сегмент отброшен: ОК")


# ---- switching documents mid-line ----------------------------------------
# The crash scenario: draw, create a new file, draw there. The command must
# end cleanly, drop its scratch object and touch nothing of the old view.
removed = []
class _Doc2(_Doc):
    Name = "Doc2"
_doc2 = _Doc2()

d4 = D(); d4._doc_name = "Doc"; d4._sep = None
d4._update_snap_object = lambda: None
d4._remove_snap_object = lambda: removed.append("snap")
ended = []
AF.endSession = lambda: ended.append(True)
d4._remove_preview = lambda: removed.append("preview")
d4._pts = [V(0,0,0), V(10,0,0)]
d4._bulges = {1: V(5,5,0)}

App.ActiveDocument = _doc2          # the user created a new file
App.listDocuments = lambda: {"Doc": _doc, "Doc2": _doc2}
assert d4._doc_is_gone(), "смена документа не обнаружена"
handled = d4._on_move(V(1,1,1))
print("после смены документа: removed =", removed, "endSession =", bool(ended))
assert "snap" in removed, "временный объект не убран"
assert d4._pts == [] and d4._bulges == {}, "недорисованная линия не сброшена"
assert ended, "сессия AxisFlow не закрыта"
print("завершение при смене документа: ОК")


# ---- _end() must be idempotent -------------------------------------------
# It is reachable from the cancel key, from the document-changed check and from
# line() starting a new run, and those can overlap.
calls = []
d5 = D(); d5._doc_name = "Doc"; d5._sep = None
d5._remove_snap_object = lambda: calls.append("snap")
d5._remove_preview = lambda: calls.append("preview")
d5._commit_current_wire = lambda: False
AF.endSession = lambda: calls.append("endSession")
d5._end()
first = list(calls)
d5._end()
d5._end()
print("вызовы после трёх _end():", calls)
assert calls == first, "повторный _end() снова всё снёс"
print("идемпотентность _end(): ОК")


# ---- the compass follows the arc ------------------------------------------
# While the bulge point is picked the compass belongs on the chord midpoint;
# once the arc is closed it belongs on the arc's end, not on the bulge point.
App.ActiveDocument = _doc        # the earlier test switched documents
base_points.clear()
d6 = D(); d6._doc_name = "Doc"; d6._sep = None
d6._update_snap_object = lambda: None
d6._on_point(V(0,0,0), True)
d6._on_point(V(10,0,0), True)
d6._on_mode_toggle()
base_points.clear()
d6._on_point(V(20,0,0), True)          # arc endpoint
print("после конечной точки дуги компас в:", base_points)
assert base_points == [V(15,0,0)], base_points   # midpoint of 10..20
d6._on_point(V(15,6,0), True)          # point on the arc
print("после дуги компас в:", base_points[-1])
assert base_points[-1] == V(20,0,0), base_points
print("перенос компаса: ОК")

# abandoning the arc puts the compass back on the last vertex
base_points.clear()
d6._on_point(V(30,0,0), True)          # a new arc endpoint
d6._on_mode_toggle()                   # ... abandoned
print("после отмены дуги компас в:", base_points[-1])
assert base_points[-1] == V(20,0,0), base_points
print("возврат компаса при отмене: ОК")

print("\nвсе проверки пройдены")
