#include "PreCompiled.h"

#include "AxisFlowService.h"
#include "AxisFlowSettings.h"

#include <cmath>
#include <limits>

namespace AxisFlowGui {

// ---------------------------------------------------------------------------
// Singleton (Meyers) — thread-safe static local init
// ---------------------------------------------------------------------------

AxisFlowService& AxisFlowService::instance()
{
    static AxisFlowService inst;
    return inst;
}

// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------

void AxisFlowService::loadHotkeysFromSettings()
{
    // Layering, later wins: built-in (already in the map from the Hotkeys ctor)
    // -> hotkeys.ini -> the user's own bindings. The file is the DEFAULTS layer,
    // so editing it never overrides a key the user deliberately rebound.
    if (!_hotkeyDefaultsPath.empty())
        AxisFlowSettings::instance().loadHotkeyDefaultsFile(_hotkeyDefaultsPath, _hotkeys);

    AxisFlowSettings::instance().loadHotkeys(_hotkeys);
    _hotkeysLoaded = true;
}

bool AxisFlowService::setHotkeys(AxisAction a, const std::vector<KeyChord>& keys, bool persist)
{
    if (a == AxisAction::None)
        return false;

    // bind() drops the action's previous keys first, so this is a replace.
    // Assigning a key that another action owns moves it: the map is key ->
    // action. It refuses outright when the result would break the prefix rule
    // (a key cannot be both a single binding and the start of a chord).
    if (!_hotkeys.bind(a, keys))
        return false;

    if (persist)
        AxisFlowSettings::instance().saveHotkeys(_hotkeys);
    Q_EMIT stateChanged();
    return true;
}

void AxisFlowService::setHotkeyChord(KeyChord c, AxisAction a, bool persist)
{
    if (a == AxisAction::None) {
        _hotkeys.remove(c);
    }
    else if (!_hotkeys.bind(a, { c })) {
        return;   // prefix-rule violation: leave the map untouched
    }
    if (persist)
        AxisFlowSettings::instance().saveHotkeys(_hotkeys);
    Q_EMIT stateChanged();
}

// ---------------------------------------------------------------------------
// Chord state
// ---------------------------------------------------------------------------

void AxisFlowService::setPendingPrefix(int qtKey)
{
    if (_pendingPrefix == qtKey)
        return;
    _pendingPrefix = qtKey;
    Q_EMIT stateChanged();   // the panel shows the available second keys
}

void AxisFlowService::clearPendingPrefix()
{
    if (_pendingPrefix == 0)
        return;
    _pendingPrefix = 0;
    Q_EMIT stateChanged();
}

std::vector<std::pair<int, AxisAction>> AxisFlowService::pendingOptions() const
{
    if (_pendingPrefix == 0)
        return {};
    return _hotkeys.continuations(_pendingPrefix);
}

void AxisFlowService::resetHotkeys()
{
    AxisFlowSettings::instance().clearStoredHotkeys();

    // Rebuild the live map from the defaults layer only.
    _hotkeys.setDefaults();
    if (!_hotkeyDefaultsPath.empty())
        AxisFlowSettings::instance().loadHotkeyDefaultsFile(_hotkeyDefaultsPath, _hotkeys);

    Q_EMIT stateChanged();
}

void AxisFlowService::applyHotkeyDefaultsFile(const std::string& path)
{
    _hotkeyDefaultsPath = path;

    _hotkeys.setDefaults();   // start from the built-in layout, then re-layer
    AxisFlowSettings::instance().loadHotkeyDefaultsFile(path, _hotkeys);
    if (_hotkeysLoaded)       // user layer only once it has been pulled in
        AxisFlowSettings::instance().loadHotkeys(_hotkeys);

    Q_EMIT stateChanged();
}

void AxisFlowService::setHotkey(int qtKey, AxisAction a, bool persist)
{
    if (a == AxisAction::None) {
        // Unbind just this key.
        _hotkeys.remove(KeyChord{qtKey, 0});
    }
    else {
        // Rebind the action to exactly this key. Replacing (not adding) is what
        // keeps the live map in step with the settings, which store one key list
        // per action: otherwise 'S -> SmartLock' would leave Return/Enter live
        // but unsaved.
        if (!_hotkeys.bind(a, { KeyChord{qtKey, 0} }))
            return;   // would break the prefix rule
    }

    if (persist)
        AxisFlowSettings::instance().saveHotkeys(_hotkeys);
    Q_EMIT stateChanged();
}

void AxisFlowService::activate()
{
    if (_active) return;
    // First activation: the parameter tree is up by now, so pull the user's
    // stored key bindings on top of the built-in defaults.
    if (!_hotkeysLoaded)
        loadHotkeysFromSettings();
    _active = true;
    Q_EMIT activeChanged(true);
    Q_EMIT stateChanged();
}

void AxisFlowService::deactivate()
{
    // A hidden compass is per-command state. Letting it outlive the command --
    // an aborted pick, say -- would leave the user with no compass and no way
    // to guess why.
    setCompassHidden(false);
    if (!_active) return;
    _active = false;
    Q_EMIT activeChanged(false);
    Q_EMIT stateChanged();
}

void AxisFlowService::setCompassHidden(bool hidden)
{
    if (_compassHidden == hidden) return;
    _compassHidden = hidden;
    Q_EMIT compassHiddenChanged(hidden);
}

void AxisFlowService::toggleActive()
{
    if (_active) deactivate();
    else         activate();
}

// ---------------------------------------------------------------------------
// Core query
// ---------------------------------------------------------------------------

void AxisFlowService::setConstrainEnabled(bool on)
{
    if (_constrainEnabled == on) return;
    _constrainEnabled = on;
    if (on)
        _displayLock = AxisLock::None;   // the adapter mirror is display-only
    Q_EMIT stateChanged();
}

Base::Vector3d AxisFlowService::solve(const Base::Vector3d& raw,
                                      const Base::Vector3d* last)
{
    // In adapter mode 'last' is the current base point supplied every move; it
    // is idempotent between commits. We deliberately do NOT clear keyboard
    // overrides here (setOrigin no longer clears), so typed values stay sticky.
    if (last) {
        _solver.setOrigin(*last);
    }
    _solver.setRawPoint(raw);

    // Adapter mode: Draft has already constrained this point (Snapper.mask).
    // Feed the solver so the compass/read-out stay live, but hand back the
    // point untouched instead of applying a second, independent lock.
    if (!_constrainEnabled)
        return raw;

    return _solver.getSolvedPoint();
}

Base::Vector3d AxisFlowService::updateCursor(const Base::Vector3d& raw,
                                             const Base::Vector3d* last)
{
    Base::Vector3d solved = solve(raw, last);
    Q_EMIT cursorMoved(solved);
    return solved;
}

void AxisFlowService::performAction(AxisAction a)
{
    switch (a) {
    case AxisAction::ToggleActive: toggleActive();           break;
    case AxisAction::ToggleMode: toggleMode();               break;
    case AxisAction::SmartLock:  smartLockToggle();          break;
    // Lock keys toggle a FIELD FREEZE: the field stops following the cursor,
    // holding the value it showed at the keypress; pressing again releases it.
    // LockX/LockY act only in rectangular mode (polar has no X/Y fields);
    // LockD freezes the distance in polar; LockZ works in both modes -- with an
    // object snap active it pins the snapped point's height in compass space.
    case AxisAction::LockX:
        if (mode() == CompassMode::Rectangular) toggleFieldLock(0);
        break;
    case AxisAction::LockY:
        if (mode() == CompassMode::Rectangular) toggleFieldLock(1);
        break;
    case AxisAction::LockZ: toggleFieldLock(2); break;
    case AxisAction::LockD:
        if (mode() == CompassMode::Polar) toggleFieldLock(0);
        break;
    // View / origin actions. The rotations are plain setPlane() bases (the
    // compass and the solver both follow stateChanged); RotateByView is served
    // by the tool, which owns the camera.
    case AxisAction::SetOrigin:    setOriginToCursor(); break;
    case AxisAction::RotateTop:
        setPlane(Base::Vector3d(1, 0, 0), Base::Vector3d(0, 1, 0), Base::Vector3d(0, 0, 1));
        _rotateCycle = 1;                              // next in the cycle: Front
        break;
    case AxisAction::RotateFront:
        // Compass Z -> global -Y, compass Y -> global +Z; u = v x n = +X.
        setPlane(Base::Vector3d(1, 0, 0), Base::Vector3d(0, 0, 1), Base::Vector3d(0, -1, 0));
        _rotateCycle = 2;                              // next: Side
        break;
    case AxisAction::RotateSide:
        // Compass Z -> global +X, compass Y -> global +Z; u = v x n = +Y.
        setPlane(Base::Vector3d(0, 1, 0), Base::Vector3d(0, 0, 1), Base::Vector3d(1, 0, 0));
        _rotateCycle = 0;                              // next: Top
        break;
    case AxisAction::RotateByView:
        Q_EMIT alignToViewRequested();
        break;
    // Turn the compass 90 deg about one of its OWN axes (chords R X / R Y /
    // R Z). Unlike RotateTop/Front/Side these are relative: they work from
    // wherever the compass currently is, which is the point of AccuDraw's
    // rotate-about keys.
    case AxisAction::RotateAboutX:
        _solver.rotateAboutAxis(0, 90.0);
        Q_EMIT stateChanged();
        break;
    case AxisAction::RotateAboutY:
        _solver.rotateAboutAxis(1, 90.0);
        Q_EMIT stateChanged();
        break;
    case AxisAction::RotateAboutZ:
        _solver.rotateAboutAxis(2, 90.0);
        Q_EMIT stateChanged();
        break;
    case AxisAction::RotateCycle:
        // Top -> Front -> Side -> Top...; each explicit Rotate* re-seeds the
        // cycle so E always continues from what the user sees.
        performAction(_rotateCycle == 0 ? AxisAction::RotateTop
                    : _rotateCycle == 1 ? AxisAction::RotateFront
                                        : AxisAction::RotateSide);
        break;
    // "Let go of everything": locks AND typed overrides, so all the fields
    // start following the cursor again.
    case AxisAction::ClearLocks: clearLocks(); clearKeyValues(); break;
    // CancelEntry is normally intercepted by the panel (it owns entry state).
    // Reaching here means no panel context, so fall back to the second layer.
    case AxisAction::CancelEntry: clearKeyValues(); clearLocks(); break;
    case AxisAction::CancelCommand: Q_EMIT cancelRequested(); break;
    case AxisAction::FinishLine:    Q_EMIT finishRequested(); break;
    case AxisAction::ToggleSegmentMode: Q_EMIT segmentModeRequested(); break;
    case AxisAction::ConfirmPoint:  Q_EMIT confirmRequested(); break;
    default: break;
    }

    // A lock or a mode change moves the solved point without any mouse motion.
    refreshCursor();
}

void AxisFlowService::confirm(const Base::Vector3d& worldPt)
{
    clearPendingPrefix();   // a placed point ends any half-typed chord
    // "Compass follows direction": rotate BEFORE the origin moves, using the
    // segment from the previous reference point to the point being confirmed.
    // 1 = keep the normal (in-plane), 2 = full. Gated on _hasFollowRef so the
    // first point of a fresh session never rotates off a stale origin.
    const int follow = AxisFlowSettings::instance().compassFollowDirection();
    if (follow != 0 && _hasFollowRef)
        _solver.rotateToDirection(worldPt - _solver.getOrigin(), follow == 1);
    _hasFollowRef = true;

    _solver.setOrigin(worldPt);
    _solver.clearKeyValues();   // typed values AND locks are per-point in AxisFlow
    Q_EMIT pointConfirmed(worldPt);
    Q_EMIT stateChanged();
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void AxisFlowService::setOriginToCursor()
{
    // The solved point, not the raw cursor: with a snap live, "O" re-anchors
    // the compass exactly at the snapped vertex.
    const Base::Vector3d p = _solver.getSolvedPoint();
    _solver.setOrigin(p);
    _solver.clearKeyValues();   // a new reference point, same as after confirm
    _hasFollowRef = true;       // the next confirmed segment starts here
    Q_EMIT stateChanged();
    refreshCursor();
}

void AxisFlowService::setOrigin(const Base::Vector3d& o)
{
    _solver.setOrigin(o);
    _hasFollowRef = true;   // a seeded base point IS a valid segment start
    Q_EMIT stateChanged();
}

void AxisFlowService::alignCompassX(const Base::Vector3d& dir)
{
    if (dir.Length() < 1e-12)
        return;                 // nothing to aim at; leave the frame alone
    _solver.alignCompassX(dir);
    Q_EMIT stateChanged();
}

void AxisFlowService::setPlane(const Base::Vector3d& uDir,
                               const Base::Vector3d& vDir,
                               const Base::Vector3d& normal)
{
    _solver.setPlane(uDir, vDir, normal);
    Q_EMIT stateChanged();
}

void AxisFlowService::resetPlane()
{
    _solver.resetCompassRotation();
    Q_EMIT stateChanged();
}

void AxisFlowService::setMode(CompassMode m)
{
    _solver.setMode(m);
    Q_EMIT stateChanged();
}

void AxisFlowService::toggleMode()
{
    setMode(_solver.getMode() == CompassMode::Rectangular
                ? CompassMode::Polar
                : CompassMode::Rectangular);
}

void AxisFlowService::toggleFieldLock(int field)
{
    // The checkbox semantics: checked = the field is fixed (typed or locked).
    // Toggling a fixed field therefore ALWAYS releases it, whichever way it was
    // fixed; toggling a free field freezes it at the value it currently shows.
    if (hasKeyValue(field)) {
        clearKeyValue(field);   // emits + refreshes
        return;
    }
    const bool rect = (_solver.getMode() == CompassMode::Rectangular);
    switch (field) {
    case 0: setSlotLock(rect ? LockSlot::X : LockSlot::Dist);  break;
    case 1: setSlotLock(rect ? LockSlot::Y : LockSlot::Angle); break;
    case 2: setSlotLock(LockSlot::Z);                          break;
    default: break;
    }
}

void AxisFlowService::setSlotLock(LockSlot slot)
{
    if (slotLocked(slot))
        return;   // idempotent

    // Capture what the user currently SEES: the solved point's component, with
    // snap and indexing applied (indexing() carries the adjusted local offset).
    const IndexingResult ix = _solver.indexing();
    switch (slot) {
    case LockSlot::X:     _solver.lockValueX(ix.lx); break;
    case LockSlot::Y:     _solver.lockValueY(ix.ly); break;
    case LockSlot::Z:     _solver.lockValueZ(ix.lz); break;
    case LockSlot::Dist:  _solver.lockValueDist(std::sqrt(ix.lx*ix.lx + ix.ly*ix.ly)); break;
    case LockSlot::Angle: _solver.lockValueAngle(std::atan2(ix.ly, ix.lx) * 180.0 / M_PI); break;
    }
    Q_EMIT stateChanged();
    refreshCursor();
}

void AxisFlowService::clearSlotLock(LockSlot slot)
{
    if (!slotLocked(slot))
        return;
    switch (slot) {
    case LockSlot::X:     _solver.clearValueX();     break;
    case LockSlot::Y:     _solver.clearValueY();     break;
    case LockSlot::Z:     _solver.clearValueZ();     break;
    case LockSlot::Dist:  _solver.clearValueDist();  break;
    case LockSlot::Angle: _solver.clearValueAngle(); break;
    }
    Q_EMIT stateChanged();
    refreshCursor();
}

bool AxisFlowService::slotLocked(LockSlot slot) const
{
    const unsigned m = _solver.lockMask();
    switch (slot) {
    case LockSlot::X:     return (m & LockBitX)     != 0;
    case LockSlot::Y:     return (m & LockBitY)     != 0;
    case LockSlot::Z:     return (m & LockBitZ)     != 0;
    case LockSlot::Dist:  return (m & LockBitDist)  != 0;
    case LockSlot::Angle: return (m & LockBitAngle) != 0;
    }
    return false;
}

void AxisFlowService::clearLocks()
{
    const unsigned m = _solver.lockMask();
    if (m == 0 && _displayLock == AxisLock::None)
        return;
    if (m & LockBitX)     _solver.clearValueX();
    if (m & LockBitY)     _solver.clearValueY();
    if (m & LockBitZ)     _solver.clearValueZ();
    if (m & LockBitDist)  _solver.clearValueDist();
    if (m & LockBitAngle) _solver.clearValueAngle();
    _displayLock = AxisLock::None;
    Q_EMIT stateChanged();
    refreshCursor();
}

void AxisFlowService::smartLockToggle()
{
    // Toggle off: any active lock releases them all.
    if (_solver.anyLock()) {
        clearLocks();
        return;
    }

    if (_solver.getMode() == CompassMode::Rectangular) {
        // Constrain movement to the axis the cursor leans towards by freezing
        // the OTHER two fields at 0 (the axis line through the origin).
        if (_solver.nearestAxis() == AxisLock::X) {
            _solver.lockValueY(0.0);
            _solver.lockValueZ(0.0);
        } else {
            _solver.lockValueX(0.0);
            _solver.lockValueZ(0.0);
        }
    } else {
        // Polar: freeze the angle at the nearest axis direction (0/90/180/270)
        // and Z at 0; the distance stays free along that ray.
        double a = std::round(_solver.getAngleDeg() / 90.0) * 90.0;
        if (a <= -180.0) a = 180.0;   // atan2 range is (-180, 180]
        _solver.lockValueAngle(a);
        _solver.lockValueZ(0.0);
    }
    Q_EMIT stateChanged();
    refreshCursor();
}

void AxisFlowService::setDisplayLock(AxisLock a)
{
    // Meaningful only while Draft owns the constraint (adapter mode).
    if (_constrainEnabled)
        a = AxisLock::None;
    if (_displayLock == a)
        return;
    _displayLock = a;
    Q_EMIT stateChanged();
}

void AxisFlowService::setKeyValue(int field, double value)
{
    if (_solver.getMode() == CompassMode::Rectangular) {
        switch (field) {
        case 0: _solver.setValueX(value); break;
        case 1: _solver.setValueY(value); break;
        case 2: _solver.setValueZ(value); break;
        default: break;
        }
    }
    else { // Polar
        switch (field) {
        case 0: _solver.setValueDist(value);  break;
        case 1: _solver.setValueAngle(value); break;
        case 2: _solver.setValueZ(value);     break;
        default: break;
        }
    }

    // Distance indexing: a value the user fixed into a coordinate field is
    // remembered as the "indexed distance" (a length to snap the live line to
    // later). In rect that is the X or Y field; in polar only the distance
    // field (0), never the angle (1). Only in the local frame -- a world-frame
    // value is an absolute coordinate, not a length from the origin. The
    // remembered length is a plain scalar, so it survives mode switches (1.3).
    if (_solver.localFrame()) {
        const bool rect = (_solver.getMode() == CompassMode::Rectangular);
        if ((rect && (field == 0 || field == 1)) || (!rect && field == 0))
            _solver.setIndexedDistance(std::fabs(value));
    }

    Q_EMIT stateChanged();
    refreshCursor();   // the typed value moves the point even with a still mouse
}

void AxisFlowService::clearKeyValue(int field)
{
    if (_solver.getMode() == CompassMode::Rectangular) {
        switch (field) {
        case 0: _solver.clearValueX(); break;
        case 1: _solver.clearValueY(); break;
        case 2: _solver.clearValueZ(); break;
        default: break;
        }
    }
    else {
        switch (field) {
        case 0: _solver.clearValueDist();  break;
        case 1: _solver.clearValueAngle(); break;
        case 2: _solver.clearValueZ();     break;
        default: break;
        }
    }
    Q_EMIT stateChanged();
    refreshCursor();
}

bool AxisFlowService::hasKeyValue(int field) const
{
    if (_solver.getMode() == CompassMode::Rectangular) {
        switch (field) {
        case 0: return _solver.hasValueX();
        case 1: return _solver.hasValueY();
        case 2: return _solver.hasValueZ();
        default: return false;
        }
    }
    switch (field) {
    case 0: return _solver.hasValueDist();
    case 1: return _solver.hasValueAngle();
    case 2: return _solver.hasValueZ();
    default: return false;
    }
}

bool AxisFlowService::anyFieldFixed() const
{
    return hasKeyValue(0) || hasKeyValue(1) || hasKeyValue(2);
}

double AxisFlowService::keyValue(int field) const
{
    if (_solver.getMode() == CompassMode::Rectangular) {
        switch (field) {
        case 0: return _solver.getValueX();
        case 1: return _solver.getValueY();
        case 2: return _solver.getValueZ();
        default: break;
        }
    }
    else {
        switch (field) {
        case 0: return _solver.getValueDist();
        case 1: return _solver.getValueAngle();
        case 2: return _solver.getValueZ();
        default: break;
        }
    }
    return std::numeric_limits<double>::quiet_NaN();
}

void AxisFlowService::refreshCursor()
{
    if (!_active) return;
    // Re-solve from the raw point the solver already holds; no mouse motion is
    // needed for a typed value or a new lock to take effect.
    const Base::Vector3d solved =
        _constrainEnabled ? _solver.getSolvedPoint() : _solver.getRawPoint();
    Q_EMIT cursorMoved(solved);
}

void AxisFlowService::clearKeyValues()
{
    _solver.clearKeyValues();
    Q_EMIT stateChanged();
}

// ---------------------------------------------------------------------------
// Live-line indexing
// ---------------------------------------------------------------------------

void AxisFlowService::applyIndexingSettings()
{
    AxisFlowSettings& s = AxisFlowSettings::instance();
    _solver.setUseDistanceIndexing(s.useDistanceIndexing());
    _solver.setUseAxisIndexing(s.useAxesIndexing());
    _solver.setIndexTolerancePx(s.indexingTolerance());
    // A tolerance/enable change moves the candidate state without mouse motion.
    refreshCursor();
}

void AxisFlowService::setViewScale(double worldPerPixel)
{
    // Silent on purpose: this is pushed on every mouse move (and on every
    // camera change). Emitting here would re-enter through the tool's
    // syncCompassSize -> setViewScale and loop.
    _solver.setViewScale(worldPerPixel);
}

void AxisFlowService::setLocalFrame(bool local)
{
    if (_solver.localFrame() == local) return;
    _solver.setLocalFrame(local);
    Q_EMIT stateChanged();
    refreshCursor();   // candidates appear/disappear as the frame flips
}

void AxisFlowService::setIndexedDistance(double d)
{
    _solver.setIndexedDistance(d);
    Q_EMIT stateChanged();
    refreshCursor();
}

void AxisFlowService::clearIndexedDistance()
{
    _solver.clearIndexedDistance();
    Q_EMIT stateChanged();
    refreshCursor();
}

void AxisFlowService::setSnapPoint(const Base::Vector3d& world)
{
    // Silent: pushed from the tool on every mouse move, right before
    // updateCursor(), which does the emitting.
    _solver.setSnapPoint(world);
}

void AxisFlowService::clearSnap()
{
    _solver.clearSnap();
}


} // namespace AxisFlowGui
