"""
axisflow_demo -- reference consumer of AxisFlow (NOT part of the AxisFlow core).

A minimal "draw a polyline" command that exercises the whole input cycle:
compass -> unit-aware entry -> axis lock -> confirmed point -> real geometry with
undo, a live preview of the in-progress polyline, and multi-line drawing.

Keys while drawing (defaults):
    click / Enter   place a point (Enter commits typed coordinates)
    Escape          finish THIS line (it is saved) and start a new one; the
                    compass and coordinate window stay up
    backtick  (`)   finish the current line AND end the command (tears down)

The working plane is picked up from FreeCAD's active working plane at start, so
points land on the plane the user is actually drawing on (not always world XY).

The preview is the consumer's job, not AxisFlow's: AxisFlow hands back points
and never guesses what is being drawn. Here it is a small, self-owned Coin
(pivy) node inserted into the active 3-D view's scene graph. The final geometry
is Draft.make_wire; each finished line is its own undo step.

Everything goes through the public getPoint() API; no AxisFlow internals used.

    import axisflow_demo
    axisflow_demo.line()          # draw; Escape starts a new line; ` ends
"""

import FreeCAD as App
import FreeCADGui as Gui

try:
    import AxisFlow as AF
except Exception as exc:      # pragma: no cover - only meaningful inside FreeCAD
    AF = None
    App.Console.PrintError("axisflow_demo: AxisFlow module not available: "
                           "{0}\n".format(exc))


def _sync_working_plane():
    """Push FreeCAD's active working plane into AxisFlow so the compass and the
    constrained points align with the plane the user is drawing on."""
    if AF is None:
        return
    wp = None
    try:
        import WorkingPlane
        try:
            wp = WorkingPlane.get_working_plane(update=False)
        except TypeError:
            wp = WorkingPlane.get_working_plane()
    except Exception:
        wp = getattr(App, "DraftWorkingPlane", None)
    if wp is None:
        return
    try:
        u = App.Vector(wp.u)
        v = App.Vector(wp.v)
        n = App.Vector(getattr(wp, "axis", None) or u.cross(v))
        AF.setPlane(u, v, n)
    except Exception as exc:
        App.Console.PrintWarning(
            "axisflow_demo: working-plane sync failed: {0}\n".format(exc))


class _LineDrawer(object):
    """Collects points from AxisFlow into Draft wires, with a live Coin preview
    managed entirely on the consumer side. Escape finishes a line and starts a
    new one; backtick ends the command."""

    def __init__(self):
        self._pts = []           # confirmed points of the CURRENT line
        self._sep = None          # our preview SoSeparator (in the view's SG)
        self._coords = None
        self._lineset = None
        self._doc = App.ActiveDocument or App.newDocument()

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        _sync_working_plane()
        self._make_preview()
        self._doc.openTransaction("AxisFlow line")
        AF.getPoint(callback=self._on_point,
                    movecallback=self._on_move,
                    finishcallback=self._on_finish,   # Escape: finish + continue
                    continuous=True)

    def _scene_graph(self):
        return Gui.ActiveDocument.ActiveView.getSceneGraph()

    def _make_preview(self):
        try:
            from pivy import coin
            self._sep = coin.SoSeparator()
            color = coin.SoBaseColor()
            color.rgb = (1.0, 0.6, 0.1)
            style = coin.SoDrawStyle()
            style.lineWidth = 2.0
            self._coords = coin.SoCoordinate3()
            self._lineset = coin.SoLineSet()
            self._lineset.numVertices.setValue(0)
            self._sep.addChild(color)
            self._sep.addChild(style)
            self._sep.addChild(self._coords)
            self._sep.addChild(self._lineset)
            self._scene_graph().addChild(self._sep)
        except Exception as exc:
            self._sep = None
            App.Console.PrintWarning(
                "axisflow_demo: preview unavailable: {0}\n".format(exc))

    def _update_preview(self, cursor=None):
        if self._sep is None:
            return
        pts = list(self._pts)
        if cursor is not None:
            pts.append(App.Vector(cursor))
        if len(pts) >= 2:
            self._coords.point.setValues(0, len(pts),
                                         [[p.x, p.y, p.z] for p in pts])
            self._coords.point.setNum(len(pts))
            self._lineset.numVertices.setValue(len(pts))
        else:
            self._lineset.numVertices.setValue(0)

    def _clear_selection(self):
        # A freshly made Draft wire is auto-selected (highlighted). Clear it so
        # the finished line looks like plain, unselected geometry.
        try:
            Gui.Selection.clearSelection()
        except Exception:
            pass

    def _remove_preview(self):
        if self._sep is None:
            return
        try:
            self._scene_graph().removeChild(self._sep)
        except Exception:
            pass
        self._sep = None
        self._coords = None
        self._lineset = None

    # -- point stream ------------------------------------------------------
    def _on_point(self, point, ok):
        if not ok:                       # backtick / cancel -> end the command
            self._end()
            return
        self._pts.append(App.Vector(point))
        App.Console.PrintMessage(
            "axisflow_demo: point {0} -> {1}\n".format(len(self._pts), point))
        self._update_preview()

    def _on_move(self, point):
        self._update_preview(cursor=point)

    def _on_finish(self):
        # Escape: save the current line, keep the session, start a new line.
        drew = self._commit_current_wire()
        if drew:
            self._doc.commitTransaction()
            self._doc.recompute()
            self._clear_selection()
        else:
            self._doc.abortTransaction()
        self._pts = []
        self._update_preview()                    # clears the rubber band
        self._doc.openTransaction("AxisFlow line")  # ready for the next line

    # -- geometry ----------------------------------------------------------
    def _commit_current_wire(self):
        """Turn the current points into a Draft wire. Returns True if drawn."""
        if len(self._pts) < 2:
            return False
        try:
            import Draft
            Draft.make_wire(self._pts, closed=False)
            App.Console.PrintMessage(
                "axisflow_demo: wire with {0} points\n".format(len(self._pts)))
            return True
        except Exception as exc:
            App.Console.PrintError("axisflow_demo: {0}\n".format(exc))
            return False

    def _end(self):
        # Final teardown: commit whatever is in progress, then end the session.
        drew = self._commit_current_wire()
        if drew:
            self._doc.commitTransaction()
            self._doc.recompute()
            self._clear_selection()
        else:
            self._doc.abortTransaction()
            App.Console.PrintMessage("axisflow_demo: ended\n")
        self._remove_preview()
        try:
            AF.endSession()
        except Exception:
            pass


_active = None


def line():
    """Start the demo line command."""
    global _active
    if AF is None:
        App.Console.PrintError("axisflow_demo: AxisFlow not loaded\n")
        return
    _active = _LineDrawer()
    _active.start()
    return _active
