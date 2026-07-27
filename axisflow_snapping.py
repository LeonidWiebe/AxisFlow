"""AxisFlow -> Draft Snapper bridge.

AxisFlow does not implement its own object snapping: it borrows FreeCAD's, so
endpoint / midpoint / centre / intersection / grid / extension ... all behave
exactly as everywhere else, share the same toolbar state and draw the same
markers. This module is the thin Python layer the C++ tool calls on each mouse
move (marshalling plain floats keeps the C++ side free of App.Vector juggling).

Precedence inside AxisFlow, later winning: raw cursor -> indexing -> object
snap -> axis lock -> typed value. Note that the object snap outranks indexing
and not the other way round: a snap is an explicit geometric target, indexing
only a soft guide, so it must not drag the point off a real endpoint (see the
"Object snap wins over indexing" branch in AxisFlowIndexing.h). The one
exception is axis hold -- a cursor hugging an axis keeps the snapped point on
that axis. Snapping is deliberately queried with
constrain=False: constraining is AxisFlow's job, and letting Draft do it too
would apply it twice.
"""

import FreeCAD as App
import FreeCADGui as Gui

_debug = False
_warned = False


def set_debug(on=True):
    global _debug
    _debug = bool(on)


def _snapper():
    """Return the Snapper singleton, loading Draft once if needed."""
    snapper = getattr(Gui, "Snapper", None)
    if snapper is not None:
        return snapper
    try:
        import Draft  # noqa: F401  (registers Gui.Snapper as a side effect)
        import DraftTools  # noqa: F401
    except Exception as exc:
        global _warned
        if not _warned:
            _warned = True
            App.Console.PrintLog(
                "AxisFlow: Draft not available, object snapping is off ({0})\n".format(exc))
        return None
    return getattr(Gui, "Snapper", None)


def available():
    """True if object snapping can be used at all."""
    return _snapper() is not None


def snap(x, y, ox, oy, oz, has_origin):
    """Ask Draft for a snapped point at screen position (x, y).

    Returns (px, py, pz, hit) where hit is 1 when a real snap was found and 0
    when the cursor was over empty space. On any error it returns hit = 0, so a
    broken Snapper can never break the drawing tool.

    ox/oy/oz + has_origin carry the previous point; Draft needs it for the
    snaps that are defined relative to it (extension, parallel, perpendicular,
    ortho), so passing it markedly improves the snap quality.
    """
    snapper = _snapper()
    if snapper is None:
        return (0.0, 0.0, 0.0, 0)

    try:
        lastpoint = App.Vector(ox, oy, oz) if has_origin else None

        # constrain=False on purpose -- AxisFlow owns constraining.
        # noTracker=True suppresses Draft's own tracking line from lastpoint to
        # the cursor: AxisFlow already draws that (its live line), so without
        # this the two overlap. It only affects Snapper.trackLine -- the snap
        # marker (Snapper.tracker) still appears, which is what we read below.
        pt = snapper.snap((int(x), int(y)), lastpoint=lastpoint,
                          active=True, constrain=False, noTracker=True)
        if pt is None:
            return (0.0, 0.0, 0.0, 0)

        # noTracker=True is not enough. Draft honours it only on the path where
        # NOTHING was snapped; when a snap actually wins, Snapper.snapToObject()
        # switches trackLine on unconditionally -- it is not even passed the
        # flag (see gui_snapper.py, "# setting the trackline"). The result is a
        # second line, in Draft's default line colour, drawn from lastpoint to
        # the snapped point, right on top of AxisFlow's own guide and compass
        # path. Switching it off here is explicit and survives Draft changing
        # its mind about the flag. It does NOT touch snapper.tracker, the snap
        # marker we read below to tell whether a snap happened.
        _hide_track_line(snapper)

        # snap() also returns a point when nothing was snapped (the cursor
        # projected on the working plane), so the return value alone cannot
        # tell us whether a snap happened. The snap marker is the reliable
        # signal: Draft only shows it when it actually found something.
        hit = False
        tracker = getattr(snapper, "tracker", None)
        if tracker is not None and getattr(tracker, "Visible", False):
            hit = True
        if not hit:
            info = getattr(snapper, "snapInfo", None)
            if info and info.get("Object"):
                hit = True

        if _debug and hit:
            App.Console.PrintMessage(
                "[AxisFlow/snap] {0}, {1}, {2}\n".format(pt.x, pt.y, pt.z))

        return (float(pt.x), float(pt.y), float(pt.z), 1 if hit else 0)

    except Exception as exc:
        if _debug:
            App.Console.PrintMessage("[AxisFlow/snap] error: {0}\n".format(exc))
        return (0.0, 0.0, 0.0, 0)


def _hide_track_line(snapper):
    """Switch off Draft's own track line (see the note in snap())."""
    track = getattr(snapper, "trackLine", None)
    if track is None:
        return
    try:
        track.off()
    except Exception:
        pass


def hide():
    """Hide snap markers/trackers; called when an AxisFlow tool ends."""
    snapper = getattr(Gui, "Snapper", None)
    if snapper is None:
        return
    _hide_track_line(snapper)
    try:
        snapper.off()
    except Exception:
        pass


# Draft registers each snap as a normal command, and commands are global, so
# AxisFlow can simply put them on its own toolbar: the user gets the standard
# snap palette and the state stays shared with Draft/BIM.
SNAP_COMMANDS = [
    "Draft_Snap_Lock",           # master switch
    "Draft_Snap_Endpoint",
    "Draft_Snap_Midpoint",
    "Draft_Snap_Center",
    "Draft_Snap_Perpendicular",
    "Draft_Snap_Intersection",
    "Draft_Snap_Near",
    "Draft_Snap_Extension",
    "Draft_Snap_Parallel",
    "Draft_Snap_Angle",
    "Draft_Snap_Ortho",
    "Draft_Snap_Grid",
    "Draft_Snap_Special",
    "Draft_Snap_Dimensions",
    "Draft_Snap_WorkingPlane",
]


def snap_commands():
    """Snap commands to show in AxisFlow's toolbar, or [] if Draft is absent."""
    if _snapper() is None:
        return []
    try:
        known = set(Gui.listCommands())
    except Exception:
        return list(SNAP_COMMANDS)
    return [c for c in SNAP_COMMANDS if c in known]
