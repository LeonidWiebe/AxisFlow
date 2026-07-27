# -*- coding: utf-8 -*-
"""AxisFlow transform commands -- Move and Copy today, Rotate / Scale / Array
on the same frame tomorrow.

WHY THIS LIVES OUTSIDE THE CORE
    AxisFlow supplies points and constraints; it creates and modifies nothing.
    Everything here is an ordinary consumer of `AxisFlow.getPoint()`, exactly
    like the line demo, and could have been written by anyone.

THE SHAPE OF A TRANSFORM TOOL
    Draft's own modification tools (see draftguitools/gui_move.py) are all the
    same two-stage machine: collect a selection, then collect points, then
    commit a command string. AxisFlow fits that mould with one difference that
    is the whole point of using it: once the base point is placed the compass
    anchors THERE, so the second point is entered as a displacement -- press X,
    type 150, Enter -- instead of being hunted for with the mouse.

    Subclasses declare how many points they need and how to turn them into
    commands. Move and Copy need two and differ by one flag; Rotate will need
    three (base, reference direction, new direction), Scale two plus a factor,
    Array two plus a count. None of that changes the machine below.

CONVENTIONS BORROWED FROM DRAFT, ON PURPOSE
  * The preview is a GHOST (draftguitools.gui_trackers.ghostTracker): Coin
    copies of the selected shapes. The document is not touched until the user
    commits.
  * The change is committed as COMMAND STRINGS through Gui.doCommand, so it
    lands in the undo stack and echoes into the Python console as a script the
    user can replay.
  * The commit is DELAYED out of the event callback. Draft does this through
    its todo queue; we use a zero-timer. Modifying the document from inside a
    scene callback is what produced an access violation in the line command,
    and the same trap is waiting here.
"""

import FreeCAD as App
import FreeCADGui as Gui

from PySide.QtCore import QT_TRANSLATE_NOOP

try:
    from PySide import QtCore
except ImportError:                                   # pragma: no cover
    from PySide2 import QtCore                        # older FreeCAD

try:
    from PySide import QtGui, QtWidgets
except ImportError:                                   # pragma: no cover
    try:
        from PySide2 import QtGui, QtWidgets
    except ImportError:
        QtGui = QtWidgets = None                      # cursor support optional

try:
    import AxisFlow as AF
except ImportError:                                   # pragma: no cover
    AF = None

_active = None          # the running tool, if any
_pick_cursor = None     # built once, reused


# ---------------------------------------------------------------------------
# pick mode: the cursor
# ---------------------------------------------------------------------------
#
# MicroStation says "locate" with a distinct cursor; FreeCAD has no such
# resource, but it does have the convention. Draft's Snapper draws a crosshair
# built from Draft_Cursor.svg plus a snap-type "tail" while a point is being
# placed, and its very first line is `if self.selectMode: view.unsetCursor()` --
# a plain arrow IS Draft's locate mode. We follow the same grammar and go one
# step further: arrow plus a target tail, so the phase is unmistakable next to
# both the navigation arrow and the point crosshair.
#
# Setting it has to go through the Quarter widget. Gui.ActiveDocument.ActiveView
# exposes no cursor method (View3DInventorPy has getObjectInfo, addEventCallback
# and friends, but nothing for the cursor), so we find the widget the way Draft
# does: walk the MDI area for children that inherit QuarterWidget.

def _quarter_widgets():
    """The 3D view widgets, or [] if Qt is unavailable."""
    if QtWidgets is None:
        return []
    try:
        mw = Gui.getMainWindow()
        area = mw.findChild(QtWidgets.QMdiArea)
        if area is None:
            return []
        return [w for w in area.findChildren(QtWidgets.QWidget)
                if w.inherits("SIM::Coin3D::Quarter::QuarterWidget")]
    except Exception:
        return []


def _build_pick_cursor():
    """Arrow with a target tail, drawn in code so it needs no resource file."""
    if QtGui is None:
        return None
    try:
        ratio = 1.0
        try:
            ratio = float(Gui.getMainWindow().devicePixelRatio())
        except Exception:
            pass
        size = 32
        pm = QtGui.QPixmap(int(size * ratio), int(size * ratio))
        pm.setDevicePixelRatio(ratio)
        pm.fill(QtCore.Qt.transparent)

        p = QtGui.QPainter(pm)
        p.setRenderHint(QtGui.QPainter.Antialiasing, True)

        # The pointer itself, white-outlined so it stays visible against both a
        # light background and a dark model.
        arrow = QtGui.QPolygonF([QtCore.QPointF(*xy) for xy in
                                 ((1, 1), (1, 17), (5.2, 13), (8.2, 19.5),
                                  (10.6, 18.4), (7.7, 12.2), (13, 12))])
        p.setPen(QtGui.QPen(QtGui.QColor(255, 255, 255), 2.0))
        p.setBrush(QtCore.Qt.NoBrush)
        p.drawPolygon(arrow)
        p.setPen(QtCore.Qt.NoPen)
        p.setBrush(QtGui.QColor(20, 20, 20))
        p.drawPolygon(arrow)

        # The tail: a small target, meaning "identify an element".
        p.setBrush(QtCore.Qt.NoBrush)
        p.setPen(QtGui.QPen(QtGui.QColor(255, 255, 255), 3.0))
        p.drawRect(17, 17, 12, 12)
        p.drawLine(23, 14, 23, 20)
        p.drawLine(23, 26, 23, 32)
        p.setPen(QtGui.QPen(QtGui.QColor(210, 60, 45), 1.6))
        p.drawRect(17, 17, 12, 12)
        p.drawLine(23, 14, 23, 20)
        p.drawLine(23, 26, 23, 32)
        p.end()

        return QtGui.QCursor(pm, 1, 1)      # hotspot on the arrow tip
    except Exception as exc:                # pragma: no cover
        App.Console.PrintLog(
            "axisflow_transform: no pick cursor ({0})\n".format(exc))
        return None


def _set_pick_cursor(on):
    """Show or clear the locate cursor on every 3D view."""
    global _pick_cursor
    widgets = _quarter_widgets()
    if not widgets:
        return
    if on:
        if _pick_cursor is None:
            _pick_cursor = _build_pick_cursor()
        if _pick_cursor is None:
            return
        for w in widgets:
            try:
                w.setCursor(_pick_cursor)
            except Exception:
                pass
    else:
        for w in widgets:
            try:
                w.unsetCursor()
            except Exception:
                pass



# Translation: the function MUST be named exactly `translate` and be called with
# both arguments, or lupdate cannot see the strings. Same convention as
# axisflow_settings.py; context "AxisFlow" groups the whole addon.
try:
    translate = App.Qt.translate
except Exception:                                     # pragma: no cover
    def translate(context, text):
        return QtCore.QCoreApplication.translate(context, text)


# ---------------------------------------------------------------------------
# the machine
# ---------------------------------------------------------------------------

class TransformTool(object):
    """Selection -> points -> commit. Subclasses fill in the last part.

    Subclass contract:
        name        command name, used for the transaction and messages
        needs       how many points to collect
        prompts     one message per point, shown as it is awaited
        commands(objects, points) -> list[str]
                    the Python that performs the transform; it is executed
                    through Gui.doCommand, so it must be self-contained
    """

    name = "Transform"
    needs = 2
    # Does the click that picks an object also count as the first point?
    #
    # For Move and Copy it does, and that is the ergonomic win over Draft: one
    # click both chooses the object and sets the base point. A tool that has to
    # INSPECT the object before it can accept a point cannot do that -- Extrude
    # orients the compass along the wire's normal, which it can only know once
    # the wire is picked. Such tools set this False: the picking click is spent
    # on picking, on_objects_ready() runs, and only the next click is a point.
    pick_consumes_point = True
    # Imported into the console namespace before the command strings run, so a
    # replayed script works standalone. Move/Copy only need Draft; Extrude adds
    # FreeCADGui because it hides the swept wire.
    modules = ("Draft",)
    # Marked for extraction here; translated where they are printed.
    prompts = (QT_TRANSLATE_NOOP("AxisFlow", "Pick base point"),
               QT_TRANSLATE_NOOP("AxisFlow", "Pick target point"))

    def __init__(self):
        self.objects = []        # App::DocumentObject list
        self.points = []         # confirmed points
        self.ghost = None
        self._doc_name = None
        self._ended = False
        # No selection yet: the first click has to do double duty, picking the
        # object under the cursor AND serving as the base point.
        self._picking = False
        # Pick mode: compass hidden, locate cursor up. Both have to be undone on
        # EVERY exit path, cancellation included, or the user is left with a
        # stray cursor and no compass.
        self._cursor_set = False
        self._in_pick_mode = False
        # Where on the object the picking click landed, in 3D. Tools that need
        # to anchor ON the geometry use it; None when the objects came from a
        # selection made before the command started, since there was no click.
        self.pick_point = None

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        doc = App.ActiveDocument
        if doc is None:
            App.Console.PrintError(
                translate("AxisFlow", "No active document") + "\n")
            return False
        self._doc_name = doc.Name

        self.objects = self._expand(Gui.Selection.getSelection())
        self._picking = not self.objects
        if self._picking:
            self._enter_pick_mode()
            App.Console.PrintMessage(
                translate("AxisFlow", "Click an object to transform "
                                      "(that click is also the base point)") + "\n")
        else:
            # Hook first: a subclass may need to inspect the objects before the
            # preview is built (Extrude aims the compass from them).
            if not self.on_objects_ready():
                return False
            self._make_ghost()
            App.Console.PrintMessage(
                translate("AxisFlow", self.prompts[0]) + "\n")

        AF.getPoint(callback=self._on_point,
                    movecallback=self._on_move,
                    finishcallback=self._on_finish,
                    continuous=True)
        return True

    @staticmethod
    def _expand(objects):
        """Groups stand for their contents, as they do in Draft."""
        try:
            import draftutils.groups as groups
            return groups.get_group_contents(objects, addgroups=True,
                                             spaces=True, noarchchild=True)
        except Exception:
            return list(objects)

    def _doc(self):
        if self._doc_name is None:
            return None
        return App.listDocuments().get(self._doc_name)

    def _document_changed(self):
        doc = self._doc()
        if doc is None:
            return True
        active = App.ActiveDocument
        return active is not None and active.Name != self._doc_name

    # -- the ghost ---------------------------------------------------------
    def _make_ghost(self):
        if self.ghost is not None or not self.objects:
            return
        try:
            import draftguitools.gui_trackers as trackers
            self.ghost = trackers.ghostTracker(self.objects)
        except Exception as exc:
            # No Draft, or it refused the shapes: the command still works, it
            # just has no live preview.
            App.Console.PrintWarning(
                "axisflow_transform: no preview ({0})\n".format(exc))
            self.ghost = None

    def _move_ghost(self, live_point):
        """Default preview: the ghost follows base -> live point."""
        if self.ghost is None or not self.points:
            return
        try:
            self.ghost.move(App.Vector(live_point).sub(self.points[0]))
            self.ghost.on()
        except Exception:
            pass

    def _drop_ghost(self):
        if self.ghost is None:
            return
        try:
            self.ghost.finalize()
        except Exception:
            pass
        self.ghost = None

    # -- point stream ------------------------------------------------------
    def _on_move(self, point):
        if self._document_changed():
            self._end()
            return
        self._move_ghost(point)

    def _on_point(self, point, ok):
        if not ok:                       # cancel key
            self._end()
            return
        if self._document_changed():
            self._end()
            return

        p = App.Vector(point)

        # First click with nothing selected: it picks the object AND counts as
        # the base point, so one click does what Draft asks two for.
        if self._picking:
            if not self._pick_object_under_cursor():
                App.Console.PrintMessage(
                    translate("AxisFlow",
                              "Nothing under the cursor -- click an object") + "\n")
                return
            self._picking = False
            # The compass comes back the instant the object is known: from here
            # on this is ordinary AxisFlow input, and the compass is the point
            # of the whole exercise.
            self._leave_pick_mode()
            if not self.on_objects_ready():
                self._end()
                return
            self._make_ghost()
            if not self.pick_consumes_point:
                # The click bought the object, not a point. Prompt for the
                # first real one and drop this coordinate on the floor.
                App.Console.PrintMessage(
                    translate("AxisFlow", self.prompts[0]) + "\n")
                return

        self.points.append(p)
        if len(self.points) < self.needs:
            # The compass stays where the point was placed (getPoint's
            # continuous mode does that), which is what makes the next point an
            # offset from it.
            App.Console.PrintMessage(
                translate("AxisFlow", self.prompts[len(self.points)]) + "\n")
            return

        self._commit_later()

    def _enter_pick_mode(self):
        """Hide the compass, show the locate cursor.

        AF.setCompassVisible(False) hides the COMPASS ONLY: the coordinate panel
        stays up and the service keeps running.

        The obvious-looking AF.deactivate() does not work here, and the reason is
        worth recording. It hides the panel too, it changes whether constraints
        apply -- and AxisFlowTool::activate() calls AxisFlowService::activate()
        itself, so the getPoint() two lines below switched the compass straight
        back on. The hide appeared to do nothing at all.

        AlwaysShowCompass outranks this: someone who asked for the compass at all
        times gets it during the pick too. The setting is an explicit
        instruction; hiding is a convenience.
        """
        try:
            AF.setCompassVisible(False)
        except Exception:
            pass
        _set_pick_cursor(True)
        self._cursor_set = True
        self._in_pick_mode = True

    def _leave_pick_mode(self):
        """Restore the cursor and the compass. Safe to call twice.

        The service also clears the flag on deactivate(), so an abort that never
        reaches here still cannot strand the user without a compass.
        """
        if not self._in_pick_mode:
            return
        self._in_pick_mode = False
        if self._cursor_set:
            _set_pick_cursor(False)
            self._cursor_set = False
        try:
            AF.setCompassVisible(True)
        except Exception:
            pass

    def _pick_object_under_cursor(self):
        """Select whatever the cursor is over. True if something was found."""
        pixel = AF.getCursorPixel()
        if pixel is None:
            return False
        try:
            view = Gui.ActiveDocument.ActiveView
            info = view.getObjectInfo(tuple(pixel))
        except Exception:
            info = None
        if not info:
            return False
        doc = self._doc()
        obj = doc.getObject(info["Object"]) if doc else None
        if obj is None:
            return False
        # getObjectInfo also reports WHERE the ray hit, which is the difference
        # between "the user picked this wire" and "the user picked this wire
        # here". Extrude anchors the compass at the nearest point on the wire so
        # the sweep starts where the eye is, not at whichever end happens to be
        # vertex zero.
        try:
            self.pick_point = App.Vector(float(info["x"]), float(info["y"]),
                                         float(info["z"]))
        except (KeyError, TypeError, ValueError):
            self.pick_point = None
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(obj)
        self.objects = self._expand([obj])
        return bool(self.objects)

    def _on_finish(self):
        # FinishLine on a transform means "drop this one and set up the next",
        # which for a tool with no partial state is simply a restart.
        self.points = []
        self._drop_ghost()
        self._make_ghost()

    # -- commit ------------------------------------------------------------
    def _commit_later(self):
        """Run the document change on the NEXT event-loop turn.

        We are inside the Coin event callback right now: touching the document
        (and, through the recompute, the scenegraph) from here is what crashed
        the line command. Draft delays its commits for the same reason.
        """
        objects, points = list(self.objects), list(self.points)
        QtCore.QTimer.singleShot(0, lambda: self._commit(objects, points))
        self._end(keep_selection=True)

    def _commit(self, objects, points):
        doc = self._doc()
        if doc is None or not objects:
            return
        try:
            cmds = self.commands(objects, points)
        except Exception as exc:
            App.Console.PrintError(
                "axisflow_transform: {0}\n".format(exc))
            return
        if not cmds:
            return
        doc.openTransaction(self.name)
        try:
            for module in self.modules:
                Gui.addModule(module)
            for c in cmds:
                Gui.doCommand(c)
            doc.commitTransaction()
            doc.recompute()
        except Exception as exc:
            doc.abortTransaction()
            App.Console.PrintError("axisflow_transform: {0}\n".format(exc))

    def on_objects_ready(self):
        """Called once `self.objects` is known, before the preview is built.

        Return False to abort the command -- that is how a subclass rejects a
        selection it cannot work with (Extrude refuses a non-planar wire).
        Runs for a pre-existing selection and again after a picking click, so
        anything derived from the objects can be set up in one place.
        """
        return True

    def on_end(self):
        """Subclass teardown, run once from _end() before anything else is torn
        down. Anything installed globally -- a selection gate, a scenegraph node
        -- comes back off here."""

    def commands(self, objects, points):
        raise NotImplementedError

    # -- teardown ----------------------------------------------------------
    def _end(self, keep_selection=False):
        global _active
        if self._ended:
            return
        self._ended = True
        # Pick mode first, and unconditionally: _end() is the one path every
        # exit shares, including Escape pressed while still picking, so this is
        # where the cursor and the compass are guaranteed to come back.
        try:
            self._leave_pick_mode()
        except Exception as exc:
            App.Console.PrintError("axisflow_transform: {0}\n".format(exc))
        # Subclass teardown next, while the tool still owns everything it set
        # up (Extrude uses this to take its selection gate back off).
        try:
            self.on_end()
        except Exception as exc:
            App.Console.PrintError("axisflow_transform: {0}\n".format(exc))
        self._drop_ghost()
        if not keep_selection:
            try:
                Gui.Selection.clearSelection()
            except Exception:
                pass
        try:
            AF.endSession()
        except Exception:
            pass
        if _active is self:
            _active = None


# ---------------------------------------------------------------------------
# the tools
# ---------------------------------------------------------------------------

def _objects_expr(objects):
    return "[" + ", ".join("FreeCAD.ActiveDocument." + o.Name
                           for o in objects) + "]"


def _vector_expr(v):
    return "FreeCAD.Vector({0}, {1}, {2})".format(v.x, v.y, v.z)


class Move(TransformTool):
    name = "AxisFlow move"
    needs = 2
    copy = False

    def commands(self, objects, points):
        delta = points[1].sub(points[0])
        if delta.Length < 1e-12:
            return []                    # a move to where it already is
        return ["Draft.move({0}, {1}, copy={2})".format(
            _objects_expr(objects), _vector_expr(delta), self.copy)]


class Copy(Move):
    name = "AxisFlow copy"
    copy = True


# ---------------------------------------------------------------------------
# entry points (called by the workbench commands)
# ---------------------------------------------------------------------------

def _run(tool_class):
    global _active
    if AF is None:
        App.Console.PrintError("axisflow_transform: AxisFlow not loaded\n")
        return None
    if _active is not None:
        _active._end()                   # one transform at a time
        _active = None
    tool = tool_class()
    if not tool.start():
        return None
    _active = tool
    return tool


def move():
    """Move the selected objects, or the one clicked first."""
    return _run(Move)


def copy():
    """Same, leaving the originals in place."""
    return _run(Copy)
