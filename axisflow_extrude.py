# -*- coding: utf-8 -*-
"""AxisFlow extrude -- sweep a planar wire into a surface.

WHAT IT DOES
    Pick a wire, and the compass stands up along that wire's normal: its X axis
    becomes the extrusion direction. Drag, and the surface grows live; click, or
    type "X 150 Enter", and it is built. Because the compass X IS the sweep,
    the height is a number you type rather than a distance you hunt for with the
    mouse -- which is the whole reason the command exists.

    Like the line demo and the transform tools, this is an ordinary consumer of
    AxisFlow.getPoint(). The core supplies points and constraints and creates
    nothing; every document object below is made here.

WHY THE COMPASS GOES WHERE IT GOES
    "Perpendicular to the wire's plane" means the compass X axis is the plane's
    normal. The compass plane (X-Y) therefore CONTAINS the normal, i.e. it
    stands perpendicular to the wire -- which is what you want to see while
    dragging: the sweep runs across the screen, not into it.

    The basis is built by AxisFlow.alignCompassX(), never by hand. setPlane()
    normalises its three vectors but does not orthogonalise them, and the solver
    inverts the compass matrix by transposing it -- correct only for an
    orthonormal frame. Handing it a triple assembled with cross products that
    were not quite perpendicular would give a silently skewed coordinate system.

STRAIGHT SEGMENTS HAVE NO PLANE
    Not a limitation of ours. Draft's own draftgeoutils carries is_straight_line()
    precisely because Part.Shape.findPlane() hands straight wires an arbitrary
    plane -- "creating privileged directions", as the docstring puts it -- and
    find_plane()/get_normal() return None for them. Infinitely many planes hold a
    straight line, so there is nothing to be perpendicular to.

    The fallback is the horizontal plane: a single segment extrudes UP, giving
    the vertical wall that is nearly always meant. A vertical segment cannot use
    that (the sweep would collapse onto the segment), so it extrudes along world
    X instead -- also a vertical wall, just facing the other way.

    Note that DraftGeomUtils.get_normal() is deliberately NOT used even where it
    would work: it ends by flipping the normal towards the camera
    ("if normal.getAngle(v_dir) < 0.78: normal = normal.negative()"). For a sweep
    that would make which way the surface grows depend on where you happened to
    be looking. find_plane().Axis is taken raw.

OBLIQUE SWEEPS ARE ALLOWED
    The drag is not forced onto the compass X axis. Let the cursor wander off it
    and you get an oblique extrusion, which Part::Extrusion supports natively --
    Dir is just a vector. Perpendicular is what the axis locks are for: press X,
    or SmartLock, and the sweep is square to the wire. Nothing needs forbidding.
"""

import FreeCAD as App
import FreeCADGui as Gui

from PySide.QtCore import QT_TRANSLATE_NOOP

try:
    from PySide import QtCore
except ImportError:                                   # pragma: no cover
    from PySide2 import QtCore                        # older FreeCAD

try:
    import AxisFlow as AF
except ImportError:                                   # pragma: no cover
    AF = None

from axisflow_transform import TransformTool, translate, _vector_expr

_TOL = 1e-7


# ---------------------------------------------------------------------------
# geometry: is this wire extrudable, and along what?
# ---------------------------------------------------------------------------

def _shape_of(obj):
    shape = getattr(obj, "Shape", None)
    if shape is None or shape.isNull():
        return None
    return shape


def is_extrudable(obj):
    """True if obj is a planar wire we can sweep.

    Wires only: edges and no faces. A face or a solid would sweep too, but
    "extrude a surface into a surface" is a different command with different
    expectations, so it is refused rather than half-supported.
    """
    return wire_normal(obj) is not None


def _planar_tol(shape):
    """How far out of plane a wire may sit and still count as flat.

    OCC's default is around 1e-7, which is a statement about exact geometry, not
    about geometry that came through a graphics pipeline. Points picked in the 3D
    view are unprojected in Coin's single precision, so before that was fixed
    every point of a flat polyline carried its own offset of roughly 1e-4 mm at
    metre-scale coordinates -- and drawings made before the fix still carry it.
    Snapped points can also sit genuinely, if slightly, off the compass plane.

    So the tolerance is scaled to the wire: a millionth of its bounding-box
    diagonal, which is about a micron on a metre-long wire. Comfortably above
    float noise, and far below any deviation a person would call non-planar.
    """
    try:
        diag = shape.BoundBox.DiagonalLength
    except Exception:
        diag = 0.0
    return max(1e-7, diag * 1e-6)


def wire_normal(obj):
    """Direction to sweep obj along, or None if it cannot be swept.

    None means: no shape, no edges, has faces, or genuinely non-planar. It is
    the single gate used by both the selection filter and the command, so the
    cursor can never allow something the command would then reject.
    """
    shape = _shape_of(obj)
    if shape is None:
        return None
    if not shape.Edges or shape.Faces:
        return None

    try:
        from draftgeoutils import geometry as dgeo
    except ImportError:                               # pragma: no cover
        dgeo = None

    tol = _planar_tol(shape)

    # -- straight: no plane exists, fall back --------------------------------
    # The SAME tolerance goes to the straightness test, and that matters more
    # than it looks. A dead-straight line carrying a little noise is not exactly
    # straight; with a strict straightness test it would fall through to
    # find_plane(), which -- asked with a loose tolerance -- would happily return
    # a plane whose normal is arbitrary. That is precisely the "privileged
    # directions" trap Draft's own docstring warns about, and we would extrude
    # in a meaningless direction with no error message. One tolerance for both
    # questions keeps the two answers consistent.
    if dgeo is not None and dgeo.is_straight_line(shape, tol):
        return _fallback_normal(shape)

    # -- planar: take the plane's axis, unflipped ----------------------------
    plane = None
    try:
        # find_plane() is the workaround-carrying version (it also copes with
        # BSpline surfaces); plain findPlane() is the fallback if Draft is
        # absent, in which case the straight-line guard above did not run and
        # findPlane() may invent a plane -- acceptable, since without Draft
        # there is no snapping either and this is already a degraded mode.
        plane = dgeo.find_plane(shape, tol) if dgeo is not None \
            else shape.findPlane(tol)
    except Exception:
        plane = None
    if plane is None:
        return None                                   # genuinely non-planar

    normal = App.Vector(plane.Axis)
    if normal.Length < _TOL:
        return None
    normal.normalize()
    return normal


def _fallback_normal(shape):
    """Sweep direction for a straight segment, which has no plane of its own.

    Horizontal plane -> sweep up -> a vertical wall, which is what a single
    segment nearly always means. A vertical segment would collapse that way, so
    it sweeps along world X instead: still a vertical wall, turned 90 degrees.
    """
    edge = shape.Edges[0]
    direction = edge.lastVertex().Point.sub(edge.firstVertex().Point)
    if direction.Length < _TOL:
        return None                                   # zero-length segment
    direction.normalize()
    if abs(direction.z) > 0.999:                      # the segment IS vertical
        return App.Vector(1, 0, 0)
    return App.Vector(0, 0, 1)


def _anchor(shape, near=None):
    """Where the compass sits: the point of the wire nearest where it was picked.

    Anchoring at vertex zero put the compass at whichever end of the wire came
    first, which on a long wall is metres away from where the user was looking.
    `near` is the 3D point the picking ray hit, so the compass lands under the
    cursor -- and still ON the wire, which matters: only the drag delta is used
    for the sweep, but a compass floating beside the geometry reads as a bug.

    distToShape does the projection rather than the raw hit point being used as
    is. The hit comes back through Coin in single precision and a wire is picked
    with a tolerance, so it sits near the wire, not on it; projecting also
    clamps to the wire's ends, which the raw point does not.

    Falls back to the first vertex when there is no pick point -- an object
    selected before the command started was never clicked.
    """
    if near is not None:
        try:
            import Part
            _dist, pts, _info = shape.distToShape(Part.Vertex(App.Vector(near)))
            if pts:
                return App.Vector(pts[0][0])
        except Exception:
            pass
    try:
        return App.Vector(shape.Vertexes[0].Point)
    except Exception:
        return App.Vector(0, 0, 0)


# ---------------------------------------------------------------------------
# selection gate: a non-planar wire cannot be picked in the first place
# ---------------------------------------------------------------------------

class _ExtrudeGate(object):
    """Refuses anything the command could not sweep.

    FreeCAD checks this in addSelection(), so it covers the 3D view, the tree
    AND our own pick-under-cursor path, and it answers with the forbidden
    cursor plus a status-bar message instead of a silent no-op.

    It does NOT retract a selection made before the command started, so
    on_objects_ready() re-checks. Two gates for one rule, but the rule itself
    lives once, in wire_normal().
    """

    def allow(self, doc, obj, sub):
        try:
            return is_extrudable(obj)
        except Exception:
            return False


# ---------------------------------------------------------------------------
# preview: a Coin ruled surface that follows the cursor
# ---------------------------------------------------------------------------

class _Preview(object):
    """Live surface between the wire and its swept copy.

    Draft's ghostTracker -- what Move and Copy preview with -- is no use here.
    It is a static Coin copy under an SoTransform, so it can only be moved,
    rotated or scaled; an extrusion changes the geometry itself. Rebuilding the
    ghost per mouse move would mean an OCC extrude plus a fresh scenegraph every
    frame.

    So: discretize the wire ONCE (it never changes), and on each move rewrite
    only the coordinates. Curved edges are why discretizing is needed at all --
    a wire from the line command may hold arcs.
    """

    def __init__(self, shape):
        self._sep = None
        self._sg = None
        self._coords = None
        self._strips = _discretize(shape)

    def attach(self):
        if self._sep is not None or not self._strips:
            return False
        try:
            from pivy import coin
            sep = coin.SoSeparator()

            # Two-sided, unlit-ish surface: a sweep is watched from whichever
            # side the user happens to be on, and back-face culling would make
            # half the previews look empty.
            hints = coin.SoShapeHints()
            hints.vertexOrdering = coin.SoShapeHints.UNKNOWN_ORDERING
            hints.shapeType = coin.SoShapeHints.UNKNOWN_SHAPE_TYPE
            sep.addChild(hints)

            material = coin.SoMaterial()
            material.diffuseColor.setValue(1.0, 0.6, 0.0)
            material.transparency.setValue(0.6)
            sep.addChild(material)

            self._coords = coin.SoCoordinate3()
            sep.addChild(self._coords)

            faces = coin.SoFaceSet()
            # Quad count is fixed for the whole command, so numVertices is
            # written once here and never touched again during the drag.
            quads = sum(len(s) - 1 for s in self._strips)
            faces.numVertices.setValues(0, quads, [4] * quads)
            sep.addChild(faces)

            view = Gui.ActiveDocument.ActiveView
            sg = view.getSceneGraph()
            sg.addChild(sep)
            # Hold the graph we attached to, exactly as the line demo does:
            # asking the ACTIVE view for its graph at removal time is wrong the
            # moment the user switches documents, and removeChild() on a
            # stranger's graph is a delete at index -1.
            sg.ref()
            self._sg = sg
            self._sep = sep
            self.update(App.Vector(0, 0, 0))
            return True
        except Exception as exc:
            App.Console.PrintWarning(
                "axisflow_extrude: no preview ({0})\n".format(exc))
            self._sep = None
            return False

    def update(self, delta):
        if self._sep is None or self._coords is None:
            return
        points = []
        for strip in self._strips:
            for a, b in zip(strip, strip[1:]):
                points.append(tuple(a))
                points.append(tuple(b))
                points.append(tuple(b.add(delta)))
                points.append(tuple(a.add(delta)))
        if not points:
            return
        try:
            self._coords.point.setValues(0, len(points), points)
            self._coords.point.setNum(len(points))
        except Exception:
            pass

    def finalize(self):
        """Named for ghostTracker's method so TransformTool._drop_ghost() can
        take either without knowing which it holds."""
        try:
            if self._sg is not None and self._sep is not None:
                self._sg.removeChild(self._sep)
        except Exception:
            pass
        try:
            if self._sg is not None:
                self._sg.unref()
        except Exception:
            pass
        self._sep = None
        self._sg = None
        self._coords = None


def _discretize(shape):
    """Each edge as a polyline, in order. Straight edges stay two points."""
    strips = []
    for edge in shape.Edges:
        points = None
        for attempt in (lambda e: e.discretize(Deflection=edge.Length / 40.0),
                        lambda e: e.discretize(24),
                        lambda e: [e.firstVertex().Point, e.lastVertex().Point]):
            try:
                points = attempt(edge)
                if points and len(points) >= 2:
                    break
            except Exception:
                points = None
        if points and len(points) >= 2:
            strips.append([App.Vector(p) for p in points])
    return strips


# ---------------------------------------------------------------------------
# the tool
# ---------------------------------------------------------------------------

class Extrude(TransformTool):
    """Selection -> compass on the normal -> one point -> Part::Extrusion."""

    name = "AxisFlow extrude"
    needs = 1
    # The picking click only picks: the compass cannot be aimed until the wire
    # is known, so that click cannot also be a point.
    pick_consumes_point = False
    modules = ("Part", "FreeCADGui")
    prompts = (QT_TRANSLATE_NOOP("AxisFlow", "Drag or type the extrusion height"),)

    def __init__(self):
        super(Extrude, self).__init__()
        self._normal = None
        self._base = None
        self._gate = None

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        # The gate goes on BEFORE the base class reads the selection, so the
        # picking click is filtered from the very first move.
        self._install_gate()
        started = super(Extrude, self).start()
        if not started:
            self._remove_gate()
        return started

    def on_objects_ready(self):
        """Aim the compass along the wire's normal, or refuse the selection."""
        obj = self.objects[0] if self.objects else None
        if obj is None:
            return False

        normal = wire_normal(obj)
        if normal is None:
            App.Console.PrintError(
                translate("AxisFlow",
                          "AxisFlow extrude: the wire is not planar -- "
                          "there is no plane to be perpendicular to") + "\n")
            return False

        if len(self.objects) > 1:
            # Everything is swept along ONE direction, taken from the first
            # wire. Coplanar wires give what you expect; wires in parallel
            # planes still do; wires in crossing planes do not, hence the note.
            App.Console.PrintMessage(
                translate("AxisFlow",
                          "AxisFlow extrude: sweeping all selected wires along "
                          "the first one's normal") + "\n")

        shape = _shape_of(obj)
        self._normal = normal
        self._base = _anchor(shape, self.pick_point)

        try:
            AF.alignCompassX(self._normal)
            AF.setBasePoint(self._base)
        except Exception as exc:
            App.Console.PrintError("axisflow_extrude: {0}\n".format(exc))
            return False
        return True

    def _on_finish(self):
        """Escape cancels the sweep.

        The base class treats FinishLine as "drop this one and set up the next",
        which suits a tool that can start over in place. Extrude cannot: after
        the wire is picked the only thing outstanding is the height, and there is
        nothing partial worth keeping. Note this is a per-tool decision, not a
        key rebinding -- Escape stays bound to FinishLine globally, because for
        the line command it means "finish this polyline and keep drawing", which
        is worth more than uniformity. The consumer decides what finishing means.
        """
        self._end()

    def on_end(self):
        self._remove_gate()

    # -- selection gate ----------------------------------------------------
    def _install_gate(self):
        if self._gate is not None:
            return
        try:
            self._gate = _ExtrudeGate()
            Gui.Selection.addSelectionGate(self._gate)
        except Exception as exc:                      # pragma: no cover
            # A missing gate costs the forbidden cursor, nothing more:
            # on_objects_ready() still refuses a non-planar wire.
            App.Console.PrintLog(
                "axisflow_extrude: no selection gate ({0})\n".format(exc))
            self._gate = None

    def _remove_gate(self):
        if self._gate is None:
            return
        self._gate = None
        try:
            Gui.Selection.removeSelectionGate()
        except Exception:
            pass

    # -- preview -----------------------------------------------------------
    def _make_ghost(self):
        """Replaces the base class's Draft ghost with the ruled surface."""
        if self.ghost is not None or not self.objects:
            return
        shape = _shape_of(self.objects[0])
        if shape is None:
            return
        preview = _Preview(shape)
        self.ghost = preview if preview.attach() else None

    def _move_ghost(self, live_point):
        if self.ghost is None or self._base is None:
            return
        try:
            self.ghost.update(App.Vector(live_point).sub(self._base))
        except Exception:
            pass

    # -- commit ------------------------------------------------------------
    def commands(self, objects, points):
        delta = App.Vector(points[0]).sub(self._base)
        length = delta.Length
        if length < 1e-9:
            return []                                 # a sweep of nothing

        # Dir carries the direction and LengthFwd the size, rather than folding
        # both into Dir. Part::Extrusion accepts either (it falls back to
        # Dir.Length when both lengths are zero), but keeping them apart is what
        # makes the height a single editable number in the property view
        # afterwards -- the same number that was typed into the compass.
        direction = App.Vector(delta)
        direction.normalize()

        cmds = []
        for obj in objects:
            cmds.append(
                "_af_ext = FreeCAD.ActiveDocument.addObject("
                "'Part::Extrusion', 'AxisFlow_Extrusion')")
            cmds.append("_af_ext.Base = FreeCAD.ActiveDocument.{0}".format(obj.Name))
            cmds.append("_af_ext.DirMode = 'Custom'")
            cmds.append("_af_ext.Dir = {0}".format(_vector_expr(direction)))
            cmds.append("_af_ext.LengthFwd = {0}".format(length))
            cmds.append("_af_ext.LengthRev = 0.0")
            cmds.append("_af_ext.Solid = False")
            cmds.append("_af_ext.Reversed = False")
            cmds.append("_af_ext.Symmetric = False")
            cmds.append("_af_ext.TaperAngle = 0.0")
            # The extrusion claims the wire as a child; leaving the wire visible
            # too draws it twice, once inside its own result.
            cmds.append(
                "FreeCADGui.ActiveDocument.getObject('{0}').Visibility = False"
                .format(obj.Name))
        if cmds:
            cmds.append("del _af_ext")
        return cmds


# ---------------------------------------------------------------------------
# entry point (called by the workbench command)
# ---------------------------------------------------------------------------

def extrude():
    """Sweep the selected planar wire into a surface."""
    import axisflow_transform
    if AF is None:
        App.Console.PrintError("axisflow_extrude: AxisFlow not loaded\n")
        return None
    if axisflow_transform._active is not None:
        axisflow_transform._active._end()             # one transform at a time
        axisflow_transform._active = None
    tool = Extrude()
    if not tool.start():
        return None
    axisflow_transform._active = tool
    return tool
