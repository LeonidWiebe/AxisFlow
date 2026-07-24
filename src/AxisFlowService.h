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

    /** Point the service at a hotkeys.ini and re-apply the whole layer stack:
     *  built-in defaults -> that file -> the user's own bindings. Called from
     *  InitGui.py at start-up (it knows the Mod folder); safe to call again to
     *  pick up edits without restarting. */
    void applyHotkeyDefaultsFile(const std::string& path);
    /**
     * Bind a key to an action. @p persist writes the whole map to the settings;
     * pass false for temporary, session-scoped rebinds (the Draft adapter
     * unbinds X/Y/Z/Enter/Escape while it is installed and must not clobber the
     * user's stored layout).
     */
    void setHotkey(int qtKey, AxisAction a, bool persist = true);
    /** Bind a two-key chord (e.g. R then X). Same persistence semantics. */
    void setHotkeyChord(KeyChord c, AxisAction a, bool persist = true);

    /** Bind an action to a whole list of keys at once (replacing its previous
     *  ones). Needed by the settings UI: an action may legitimately own several
     *  keys, e.g. ConfirmPoint on both Return and the numpad Enter. */
    bool setHotkeys(AxisAction a, const std::vector<KeyChord>& keys, bool persist = true);

    /** Forget the user's key overrides and fall back to the defaults layer
     *  (built-in plus hotkeys.ini). Other settings are untouched. */
    void resetHotkeys();
    /** Execute a semantic action (used by both the panel and the Coin handler). */
    void           performAction(AxisAction a);

    // ---------------------------------------------------------------------
    // Two-key chords (MicroStation style: R then X)
    // ---------------------------------------------------------------------
    /** The prefix we are waiting on, or 0 when no chord is in progress. */
    int  pendingPrefix() const { return _pendingPrefix; }
    /** Start waiting for the second key of a chord (emits stateChanged so the
     *  panel can show which keys are on offer). */
    void setPendingPrefix(int qtKey);
    /** Stop waiting -- the next key press ends it, whatever it was. */
    void clearPendingPrefix();
    /** Second keys still available for the pending prefix, with their actions.
     *  Empty when nothing is pending. This is the panel's hint. */
    std::vector<std::pair<int, AxisAction>> pendingOptions() const;

    /**
     * Commit a point as the new origin. Clears keyboard overrides and the axis
     * lock (an AxisFlow lock is per-point). Emits pointConfirmed + stateChanged.
     */
    void confirm(const Base::Vector3d& worldPt);
    /** "Compass follows direction" uses the segment from the previous
     *  reference point (the last confirmed point, or a seeded base point) to
     *  the newly confirmed one. A fresh session has no such segment yet --
     *  the tool calls this so the FIRST confirm never rotates the compass off
     *  a stale origin from the previous session. */
    void clearFollowReference()          { _hasFollowRef = false; }

    /** SetOrigin (the "O" key): re-anchor the compass at the CURRENT solved
     *  point (snap included) WITHOUT confirming a point -- no pointConfirmed,
     *  so the running draw command ignores it. Typed values and locks are
     *  cleared, like at any new reference point. */
    void setOriginToCursor();

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

    /**
     * Field locks. A lock FREEZES a coordinate field at a value (the panel
     * checkbox is its visual): the frozen component stops following the cursor,
     * the free ones keep moving. This replaces the old single-axis lock, whose
     * semantics were inverted ("movement along X" instead of "X frozen").
     * The frozen value lives in the same slot as a typed override; LockBit in
     * the solver records that it came from a lock.
     */
    enum class LockSlot { X, Y, Z, Dist, Angle };

    /** Toggle the lock of a PANEL FIELD (mode-mapped like setKeyValue:
     *  rect 0=X 1=Y 2=Z; polar 0=Dist 1=Angle 2=Z). A free field is frozen at
     *  the value it currently shows (the solved point's component, snap and
     *  indexing included); a fixed field (typed OR locked) is released. */
    void toggleFieldLock(int field);

    /** Freeze a slot at its current solved value (idempotent). */
    void setSlotLock(LockSlot slot);
    /** Release one slot's lock (and its value). */
    void clearSlotLock(LockSlot slot);
    bool slotLocked(LockSlot slot) const;

    /** Release every LOCK-flavoured slot. Typed overrides survive (the
     *  ClearLocks action clears those too, via clearKeyValues). */
    void clearLocks();

    /**
     * MicroStation-style smart lock: constrain the cursor to the axis it is
     * leaning towards by freezing the OTHER two fields:
     *   rect : cursor along X -> lock Y=0 and Z=0; along Y -> lock X=0 and Z=0;
     *   polar: lock Angle to the nearest multiple of 90 deg and Z=0 (the
     *          distance stays free along that ray).
     * If any lock is active, the press releases all locks instead (toggle).
     */
    void smartLockToggle();

    /**
     * Adapter-only: mirror Draft's own axis restriction (Snapper.mask) for
     * display. Draft's lock means "movement ALONG that axis"; the panel shows
     * it as checkboxes on the two frozen fields. Ignored (reset) while
     * constrainEnabled is true -- standalone locks are real field locks.
     */
    void setDisplayLock(AxisLock a);
    AxisLock displayLock() const { return _displayLock; }

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
    /** Any coordinate field is fixed -- typed OR locked. This is exactly what
     *  the panel's checkboxes show, and what makes the compass path worth
     *  drawing: the point is no longer simply "under the cursor". */
    bool anyFieldFixed() const;
    /** The signed override value for a field (NaN when unset). */
    double keyValue(int field) const;

    /**
     * Re-solve the last raw cursor point and re-emit cursorMoved.
     *
     * Typing a value or toggling a lock changes the solved point even though the
     * mouse never moved. Without this the guide line and the compass only catch
     * up on the next mouse move.
     */
    void refreshCursor();

    // ---------------------------------------------------------------------
    // Live-line indexing (settings-driven; local frame only)
    // ---------------------------------------------------------------------
    /** Pull the indexing enable flags + tolerance out of AxisFlowSettings into
     *  the solver. Called on activation and whenever settings change. */
    void applyIndexingSettings();

    /** World units per screen pixel (from the camera). Stored silently -- it is
     *  pushed on every mouse move, so it must NOT re-emit (that would recurse
     *  through the tool's syncCompassSize). */
    void setViewScale(double worldPerPixel);
    /** Current camera scale (world units per screen pixel; 0 = no camera). */
    double viewScale() const             { return _solver.viewScale(); }

    /** The panel's Local/World frame. Indexing runs only in the local frame. */
    void setLocalFrame(bool local);

    /** The remembered length distance-indexing snaps to (magnitude kept). */
    void setIndexedDistance(double d);
    void clearIndexedDistance();
    double indexedDistance() const   { return _solver.indexedDistance(); }
    bool   hasIndexedDistance() const { return _solver.hasIndexedDistance(); }

    /** The current indexing decision (candidate point / candidate axis). */
    IndexingResult currentIndexing() const { return _solver.indexing(); }

    // ---------------------------------------------------------------------
    // Object snapping (Draft Snapper)
    // ---------------------------------------------------------------------
    /** Feed a live object snap. While set, the snapped point replaces the
     *  cursor as the point being placed (locks, typed values and the read-out
     *  all act on it), and soft indexing yields to it. */
    void setSnapPoint(const Base::Vector3d& world);
    void clearSnap();
    bool snapActive() const              { return _solver.snapActive(); }
    Base::Vector3d snapPoint() const     { return _solver.snapPoint(); }


    // ---------------------------------------------------------------------
    // State getters
    // ---------------------------------------------------------------------
    CompassMode    mode()        const { return _solver.getMode(); }
    /** Bitmask of lock-flavoured slots (LockBit; 0 = no locks). */
    unsigned       lockMask()    const { return _solver.lockMask(); }
    Base::Vector3d origin()      const { return _solver.getOrigin(); }
    Base::Vector3d localOffset() const { return _solver.getLocalOffset(); }
    double         distance()    const { return _solver.getDistance(); }
    double         angleDeg()    const { return _solver.getAngleDeg(); }
    Base::Vector3d planeU()      const { return _solver.getPlaneU(); }
    Base::Vector3d planeV()      const { return _solver.getPlaneV(); }
    Base::Vector3d planeNormal() const { return _solver.getPlaneNormal(); }

Q_SIGNALS:
    /** Mode / lock / key value / plane / origin changed. */
    void stateChanged();
    /** Active state toggled on/off (UI shows/hides in response). */
    void activeChanged(bool active);
    /** A point was committed as the new origin. */
    void pointConfirmed(Base::Vector3d worldPt);
    /** A consumer/tool should end the current drawing command (CancelCommand). */
    void cancelRequested();
    /** Finish the current line/polyline but keep the session running so the
     *  consumer can immediately start the next one (FinishLine / Escape). */
    void finishRequested();
    /** A consumer-defined mode toggle (ToggleSegmentMode). AxisFlow attaches no
     *  meaning to it: the getPoint consumer decides what the mode is. The line
     *  demo switches between straight and arc segments. */
    void segmentModeRequested();
    /** Enter/ConfirmPoint: the tool should place the current solved point. */
    void confirmRequested();
    /** RotateByView needs the camera; the view-owning tool serves it. */
    void alignToViewRequested();
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
    AxisLock       _displayLock { AxisLock::None };  // adapter-only mirror
    bool           _hasFollowRef { false };          // a previous point exists
    int            _rotateCycle { 0 };               // RotateCycle position
    bool           _hotkeysLoaded { false };     // settings pulled in yet?
    int            _pendingPrefix { 0 };         // chord prefix awaiting key 2
    std::string    _hotkeyDefaultsPath;          // hotkeys.ini, empty = none
};

} // namespace AxisFlowGui
