#pragma once

#include <QtCore/QObject>

#include <Base/Vector3D.h>

#include "AxisFlowSolver.h"   // AxisLock, CompassMode, AxisFlowSolver
#include "AxisFlowHotkeys.h"  // AxisAction, Hotkeys

namespace AxisFlowGui {

/**
 * AxisFlowService
 *
 * The central, view-independent "brain" of AxisFlow.
 *
 *  - Owns the single AxisFlowSolver instance and all its state
 *    (origin, plane orientation, mode, axis locks, keyboard overrides).
 *  - Is a process-wide singleton, reachable from C++  (AxisFlowService::instance())
 *    and from Python via the module binding (AxisFlow.solve(...) etc.).
 *  - Emits Qt signals so UI clients (compass, panel, tool) can react to state
 *    changes that originate anywhere — including from a Python adapter.
 *
 * The service knows nothing about Coin3D or Draft. UI and integration layers
 * are clients that talk to it; the math lives in AxisFlowSolver.
 */
class AxisFlowService : public QObject
{
    Q_OBJECT

public:
    /** The one and only instance. */
    static AxisFlowService& instance();

    // ---------------------------------------------------------------------
    // Logical activation (independent of any particular 3-D view)
    // ---------------------------------------------------------------------
    void activate();
    void deactivate();
    bool isActive() const { return _active; }
    /** Flip active state (bound to the global ToggleActive hotkey). */
    void toggleActive();

    /**
     * Whether solve()/updateCursor() actually constrain the point.
     *
     * In the Draft adapter this is turned OFF: Draft already constrains the
     * point itself (Snapper.mask -> Snapper.constrain -> projection onto the
     * working-plane axis) before our hook ever sees it, so constraining again
     * would apply two independent locks. With this off, the axis lock is kept
     * purely as UI state: it still drives the compass and the panel, while the
     * point passes through untouched. Standalone mode leaves it ON.
     */
    void setConstrainEnabled(bool on);
    bool constrainEnabled() const { return _constrainEnabled; }

    // ---------------------------------------------------------------------
    // Core query
    // ---------------------------------------------------------------------
    /**
     * Constrain a raw world-space point.
     *
     * @param raw   cursor point already projected onto the working plane.
     * @param last  optional origin for THIS solve (the previous data point).
     *              If non-null it is used as the compass origin; if null the
     *              service's stored origin is used.
     *
     * NOTE: solve() does NOT clear keyboard overrides, so a value typed by the
     * user stays "sticky" across mouse moves until confirm()/clearKeyValues().
     */
    Base::Vector3d solve(const Base::Vector3d& raw,
                         const Base::Vector3d* last = nullptr);

    /**
     * Like solve(), but also broadcasts cursorMoved(solved) so UI clients
     * (compass, panel) update. Both the standalone Coin callback and the
     * Python Snapper adapter call this on every mouse move.
     */
    Base::Vector3d updateCursor(const Base::Vector3d& raw,
                                const Base::Vector3d* last = nullptr);

    /** The solved point for the current raw/override state (no recompute cost concern). */
    Base::Vector3d currentSolved() const { return _solver.getSolvedPoint(); }

    // ---- reconfigurable hotkeys ----
    Hotkeys&       hotkeys()       { return _hotkeys; }
    const Hotkeys& hotkeys() const { return _hotkeys; }
    AxisAction     actionForKey(int qtKey) const { return _hotkeys.action(qtKey); }

    /**
     * Pull the stored key bindings out of AxisFlowSettings on top of the
     * built-in defaults. Called lazily on the first activation, because the
     * parameter tree only exists once App is up.
     */
    void loadHotkeysFromSettings();
    /**
     * Bind a key to an action. @p persist writes the whole map to the settings;
     * pass false for temporary, session-scoped rebinds (the Draft adapter
     * unbinds X/Y/Z/Enter/Escape while it is installed and must not clobber the
     * user's stored layout).
     */
    void setHotkey(int qtKey, AxisAction a, bool persist = true);
    /** Execute a semantic action (used by both the panel and the Coin handler). */
    void           performAction(AxisAction a);

    /**
     * Commit a point as the new origin. Clears keyboard overrides and the axis
     * lock (an AxisFlow lock is per-point). Emits pointConfirmed + stateChanged.
     */
    void confirm(const Base::Vector3d& worldPt);

    // ---------------------------------------------------------------------
    // State setters (forwarded to the solver, each emits stateChanged)
    // ---------------------------------------------------------------------
    void setOrigin(const Base::Vector3d& o);

    /** Set compass / working-plane orientation from an orthonormal basis. */
    void setPlane(const Base::Vector3d& uDir,
                  const Base::Vector3d& vDir,
                  const Base::Vector3d& normal);

    /** Reset compass orientation back to world axes. */
    void resetPlane();

    void setMode(CompassMode m);
    void toggleMode();

    void lockAxis(AxisLock a);
    void clearLocks();

    /**
     * MicroStation-style smart lock (bound to Enter):
     *   - if no axis is locked, lock the one nearest the cursor direction;
     *   - if an axis is locked, clear the lock.
     */
    void smartLockToggle();

    /** Axis nearest the current cursor direction (X or Y). */
    AxisLock nearestAxis() const { return _solver.nearestAxis(); }

    /**
     * Set a keyboard override. Field indices are mode-dependent:
     *   Rectangular: 0 = X, 1 = Y, 2 = Z
     *   Polar:       0 = Distance, 1 = Angle(deg), 2 = Z
     */
    void setKeyValue(int field, double value);
    void clearKeyValues();
    /** Release one field's override (0/1/2), leaving the others alone. */
    void clearKeyValue(int field);
    /** True if field 0/1/2 currently carries a typed override. */
    bool hasKeyValue(int field) const;

    /**
     * Re-solve the last raw cursor point and re-emit cursorMoved.
     *
     * Typing a value or toggling a lock changes the solved point even though the
     * mouse never moved. Without this the guide line and the compass only catch
     * up on the next mouse move.
     */
    void refreshCursor();

    // ---------------------------------------------------------------------
    // State getters
    // ---------------------------------------------------------------------
    CompassMode    mode()        const { return _solver.getMode(); }
    AxisLock       lock()        const { return _solver.getAxisLock(); }
    Base::Vector3d origin()      const { return _solver.getOrigin(); }
    Base::Vector3d localOffset() const { return _solver.getLocalOffset(); }
    double         distance()    const { return _solver.getDistance(); }
    double         angleDeg()    const { return _solver.getAngleDeg(); }
    Base::Vector3d planeU()      const { return _solver.getPlaneU(); }
    Base::Vector3d planeV()      const { return _solver.getPlaneV(); }
    Base::Vector3d planeNormal() const { return _solver.getPlaneNormal(); }

    /** Escape hatch for clients that need the raw solver (kept minimal). */
    AxisFlowSolver&       solver()       { return _solver; }
    const AxisFlowSolver& solver() const { return _solver; }

Q_SIGNALS:
    /** Mode / lock / key value / plane / origin changed. */
    void stateChanged();
    /** Active state toggled on/off (UI shows/hides in response). */
    void activeChanged(bool active);
    /** A point was committed as the new origin. */
    void pointConfirmed(Base::Vector3d worldPt);
    /** The cursor moved; carries the solved (constrained) world point. */
    void cursorMoved(Base::Vector3d solvedWorld);

private:
    AxisFlowService() = default;
    ~AxisFlowService() override = default;
    AxisFlowService(const AxisFlowService&) = delete;
    AxisFlowService& operator=(const AxisFlowService&) = delete;

    AxisFlowSolver _solver;
    Hotkeys        _hotkeys;
    bool           _active { false };
    bool           _constrainEnabled { true };   // off in the Draft adapter
    bool           _hotkeysLoaded { false };     // settings pulled in yet?
};

} // namespace AxisFlowGui
