"""AxisFlow <-> Draft/Arch Snapper adapter (step 1: constraint validation).

Wraps FreeCADGui.Snapper.snap so every point Draft/Arch tools request is passed
through the AxisFlow constraint service (the AxisFlow C++ module) before
being returned. Object snapping (endpoint / midpoint / ...) happens FIRST;
AxisFlow only constrains the already-snapped point onto the locked axis and any
typed override values.

Because Arch Wall and Arch Structure place their points through the same
Snapper singleton (Snapper.getPoint -> snap), this one hook covers them too.

Usage from the FreeCAD Python console:

    import axisflow_snap
    axisflow_snap.install()           # start constraining (also activates AD)
    axisflow_snap.set_debug(True)     # optional: log raw -> solved deltas

    import AxisFlow as AD
    AD.lockAxis('x')                  # lock the wall baseline to the WP X axis
    #   ... draw an Arch Wall; the segment now stays on the X axis ...
    AD.clearLocks()

    axisflow_snap.uninstall()         # restore original Snapper behaviour
"""

import FreeCAD as App
import FreeCADGui as Gui

_orig_snap = None      # captured bound method of the real Snapper.snap
_debug = False
_last_plane = None     # cache of the last working-plane basis pushed to AD


def _log(msg):
    App.Console.PrintMessage("[AxisFlow/snap] " + str(msg) + "\n")


def set_debug(on=True):
    """Enable/disable logging of points that AxisFlow actually changed."""
    global _debug
    _debug = bool(on)


def _get_working_plane():
    """Return the active working plane (has .u/.v/.axis/.position) or None."""
    try:
        import WorkingPlane
        wp = WorkingPlane.get_working_plane()
        if wp is not None:
            return wp
    except Exception:
        pass
    return getattr(App, "DraftWorkingPlane", None)  # compatibility fallback


def _axis_snap(*args, **kwargs):
    # 1) Normal FreeCAD object snapping first.
    pt = _orig_snap(*args, **kwargs)

    try:
        import AxisFlow as AD
    except Exception:
        return pt

    if pt is None or not AD.isActive():
        return pt

    try:
        # lastpoint = previous data point; AxisFlow uses it as the origin.
        lastpoint = kwargs.get("lastpoint", None)
        if lastpoint is None and len(args) > 1:
            lastpoint = args[1]

        wp = _get_working_plane()
        if wp is not None:
            # Orient the compass to the active working plane (crucial for Arch
            # walls on non-horizontal planes). Only push it when it changed.
            global _last_plane
            key = (tuple(wp.u), tuple(wp.v), tuple(wp.axis))
            if key != _last_plane:
                AD.setPlane(wp.u, wp.v, wp.axis)
                _last_plane = key
            origin = lastpoint if lastpoint is not None else wp.position
        else:
            origin = lastpoint

        # 2) AxisFlow constrains the already-snapped point AND updates the UI
        #    (panel readout + compass) via updateCursor.
        solved = AD.updateCursor(pt, origin)

        # Snapper's own tracking line was drawn (inside the original snap) to the
        # UNconstrained point. Move its endpoint to the constrained point so the
        # visual matches the geometry that will actually be placed.
        try:
            tl = getattr(Gui.Snapper, "trackLine", None)
            if tl is not None and lastpoint is not None:
                tl.p2(solved)
        except Exception:
            pass

        if _debug:
            try:
                if (App.Vector(solved) - App.Vector(pt)).Length > 1e-7:
                    _log("raw={0} -> solved={1}".format(tuple(pt), tuple(solved)))
            except Exception:
                pass

        return solved

    except Exception as e:
        # Never break the Snapper: on any error fall back to the raw point.
        if _debug:
            _log("error, passing through: {0}".format(e))
        return pt


def install():
    """Install the AxisFlow hook into the Snapper and activate the service."""
    global _orig_snap, _last_plane
    if _orig_snap is not None:
        _log("already installed")
        return
    _last_plane = None

    snapper = getattr(Gui, "Snapper", None)
    if snapper is None:
        _log("Gui.Snapper not available yet - start a Draft/Arch tool once, then retry")
        return

    _orig_snap = snapper.snap        # bound method (self already bound)
    snapper.snap = _axis_snap        # instance attribute shadows the class method

    try:
        import AxisFlow as AD
        has_ui = AD.beginSession()   # activates service, shows compass + panel
        if not has_ui:
            _log("no active 3D view - constraints active, but no compass/panel UI")
    except Exception as e:
        _log("beginSession failed: {0}".format(e))

    _log("installed")


def uninstall():
    """Restore the original Snapper.snap and deactivate the service."""
    global _orig_snap
    snapper = getattr(Gui, "Snapper", None)
    if snapper is not None and _orig_snap is not None:
        snapper.snap = _orig_snap
    _orig_snap = None

    try:
        import AxisFlow as AD
        AD.endSession()
    except Exception:
        pass

    _log("uninstalled")


def is_installed():
    return _orig_snap is not None
