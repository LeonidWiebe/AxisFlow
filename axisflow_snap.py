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

import time

import FreeCAD as App
import FreeCADGui as Gui

from PySide import QtCore, QtWidgets   # FreeCAD's version-agnostic Qt shim

_orig_snap = None      # captured bound method of the real Snapper.snap
_debug = False
_last_plane = None     # cache of the last working-plane basis pushed to AD
_latch = None          # QObject event filter that holds Draft's typed value


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


_MASK_TO_AXIS = {"x": "x", "y": "y", "z": "z"}


def _mirror_draft_lock():
    """Reflect Draft's own axis lock (Snapper.mask, set by its RestrictX/Y/Z
    shortcuts) into AxisFlow, so the compass and the panel checkboxes show it.
    Display only: constraining stays Draft's job (setConstrainEnabled(False))."""
    try:
        import AxisFlow as AD
        mask = getattr(Gui.Snapper, "mask", None)
        AD.lockAxis(_MASK_TO_AXIS.get(mask, "none"))
    except Exception:
        pass


def _axis_snap(*args, **kwargs):
    # 1) Normal FreeCAD snapping. NOTE: this already applies Draft's own lock --
    #    snap() ends with `fp = self.cstr(lastpoint, constrain, point)`, which
    #    projects onto the axis held in Snapper.mask. So `pt` is ALREADY
    #    constrained; AxisFlow must not constrain it a second time.
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

        # 2) Show Draft's lock in our UI, then feed the (already constrained)
        #    point to AxisFlow purely to drive the compass + read-out.
        _mirror_draft_lock()
        AD.updateCursor(pt, origin)

        # Hand Draft's point straight back: the geometry is Draft's to decide.
        return pt

    except Exception as e:
        # Never break the Snapper: on any error fall back to the raw point.
        if _debug:
            _log("error, passing through: {0}".format(e))
        return pt


# --- Typed-value hold ------------------------------------------------------
#
# Draft already latches typed values: checkSpecialChars() sees a digit and calls
# setMouseMode(mode=False), which sets tb.mouse = False (so get_point() stops
# overwriting the fields from the cursor) and arms
#   QTimer.singleShot(MouseDelay * 1000, -> setMouseMode(True, recorded_start))
# The catch: setMouseMode's body is `elif self.mouse:` -- once mouse is already
# False, later keystrokes arm nothing. So the hold expires MouseDelay seconds
# after the FIRST digit (default 1 s) and the cursor wipes what you were typing.
#
# We don't fight that; we use Draft's own escape hatch. setMouseMode ignores a
# pending restore whose recorded_input_start no longer matches
# tb.mouse_delay_input_start ("Do nothing if a new input sequence has started").
# So on every value keystroke we stamp a new input start and re-arm a fresh
# timer, which both invalidates the old restore and extends the hold. Only
# public-ish DraftToolBar members are touched, and validatePoint()/Esc restore
# tb.mouse = True as usual.

_VALUE_CHARS = "0123456789.,-+"
_hold_typing = True


def set_hold_typing(on=True):
    """Enable/disable extending Draft's typed-value hold on each keystroke."""
    global _hold_typing
    _hold_typing = bool(on)


class _MouseLatch(QtCore.QObject):
    """App-wide key watcher; never consumes events."""

    def eventFilter(self, obj, ev):
        try:
            if _hold_typing and ev.type() == QtCore.QEvent.KeyPress:
                self._handle(ev)
        except Exception:
            pass
        return False   # observe only, never consume

    @staticmethod
    def _handle(ev):
        tb = getattr(Gui, "draftToolBar", None)
        if tb is None or not getattr(tb, "sourceCmd", None):
            return
        if not hasattr(tb, "setMouseMode"):
            return   # older Draft without the delay mechanism

        txt = ev.text()
        editing = (bool(txt) and txt[0] in _VALUE_CHARS) or ev.key() in (
            QtCore.Qt.Key_Backspace, QtCore.Qt.Key_Delete,
        )
        if not editing:
            return

        # Re-arm: stamp a new input sequence (invalidates any pending restore),
        # then let Draft start a fresh hold with its own configured delay.
        tb.mouse = True
        tb.mouse_delay_input_start = time.time()
        tb.setMouseMode(mode=False)


def _install_latch():
    global _latch
    try:
        app = QtWidgets.QApplication.instance()
        if app is None:
            return
        if _latch is None:
            _latch = _MouseLatch()
        # Installed AFTER beginSession's C++ filter, so on qApp it runs FIRST and
        # sees every key (even the lock keys the C++ filter later consumes).
        app.installEventFilter(_latch)
    except Exception as e:
        _log("mouse-latch install failed: {0}".format(e))


def _remove_latch():
    global _latch
    try:
        app = QtWidgets.QApplication.instance()
        if app is not None and _latch is not None:
            app.removeEventFilter(_latch)
        tb = getattr(Gui, "draftToolBar", None)
        if tb is not None:
            tb.mouse = True    # don't leave Draft pinned
    except Exception:
        pass
    try:
        import AxisFlow as AD
        AD.setConstrainEnabled(True)      # standalone constrains again
        AD.loadHotkeysFromSettings()      # undo the session-scoped unbinds
    except Exception:
        pass


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
        # Cooperative model: Draft constrains, AxisFlow visualises. Turn our own
        # constraining off so the point is not locked twice, and start clean.
        AD.setConstrainEnabled(False)
        AD.clearLocks()

        has_ui = AD.beginSession()   # activates service, shows compass + panel
        if not has_ui:
            _log("no active 3D view - constraints active, but no compass/panel UI")

        # AFTER beginSession: its first activate() pulls the stored key map from
        # the settings, which would put X/Y/Z back. Unbind once that has run.
        #
        # AxisFlow's own layout owns X/Y/Z/Enter/Escape because in its own tools
        # it owns the keyboard. Inside Draft those keys are Draft's: X/Y/Z are
        # RestrictX/Y/Z (typed into the coordinate field), Enter commits the
        # point, Escape aborts. persist=False keeps this session-scoped so the
        # user's stored layout survives.
        # Qt codes: X=0x58 Y=0x59 Z=0x5A, Return=0x1000004, Enter=0x1000005,
        # Escape=0x1000000.
        for code in (0x58, 0x59, 0x5A, 0x1000004, 0x1000005, 0x1000000):
            AD.setHotkey(code, "none", False)
    except Exception as e:
        _log("beginSession failed: {0}".format(e))

    _install_latch()   # extend Draft's typed-value hold while you type

    _log("installed")


def uninstall():
    """Restore the original Snapper.snap and deactivate the service."""
    global _orig_snap
    _remove_latch()

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
