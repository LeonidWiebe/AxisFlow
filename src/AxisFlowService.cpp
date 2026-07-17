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
    AxisFlowSettings::instance().loadHotkeys(_hotkeys);
    _hotkeysLoaded = true;
}

void AxisFlowService::setHotkey(int qtKey, AxisAction a, bool persist)
{
    if (a == AxisAction::None) {
        // Unbind just this key.
        _hotkeys.remove(qtKey);
    }
    else {
        // Rebind the action to exactly this key. Replacing (not adding) is what
        // keeps the live map in step with the settings, which store one key list
        // per action: otherwise 'S -> SmartLock' would leave Return/Enter live
        // but unsaved.
        _hotkeys.bind(a, { qtKey });
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
    if (!_active) return;
    _active = false;
    Q_EMIT activeChanged(false);
    Q_EMIT stateChanged();
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
    // Lock keys toggle: pressing the bound key again releases the lock. This is
    // what lets a keyboard-only user clear a lock without Escape (reserved for
    // Draft in the adapter).
    case AxisAction::LockX: (lock() == AxisLock::X) ? clearLocks() : lockAxis(AxisLock::X); break;
    case AxisAction::LockY: (lock() == AxisLock::Y) ? clearLocks() : lockAxis(AxisLock::Y); break;
    case AxisAction::LockZ: (lock() == AxisLock::Z) ? clearLocks() : lockAxis(AxisLock::Z); break;
    // Escape is the single "let go" key: it releases the axis lock AND every
    // typed override, so the fields start following the cursor again.
    case AxisAction::ClearLocks: clearLocks(); clearKeyValues(); break;
    // CancelEntry is normally intercepted by the panel (it owns entry state).
    // Reaching here means no panel context, so fall back to the second layer.
    case AxisAction::CancelEntry: clearKeyValues(); clearLocks(); break;
    case AxisAction::CancelCommand: Q_EMIT cancelRequested(); break;
    case AxisAction::FinishLine:    Q_EMIT finishRequested(); break;
    case AxisAction::ConfirmPoint:  Q_EMIT confirmRequested(); break;
    default: break;
    }

    // A lock or a mode change moves the solved point without any mouse motion.
    refreshCursor();
}

void AxisFlowService::confirm(const Base::Vector3d& worldPt)
{
    _solver.setOrigin(worldPt);
    _solver.clearKeyValues();
    _solver.setAxisLock(AxisLock::None);   // lock is per-point in AxisFlow
    Q_EMIT pointConfirmed(worldPt);
    Q_EMIT stateChanged();
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void AxisFlowService::setOrigin(const Base::Vector3d& o)
{
    _solver.setOrigin(o);
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

void AxisFlowService::lockAxis(AxisLock a)
{
    _solver.setAxisLock(a);
    Q_EMIT stateChanged();
}

void AxisFlowService::clearLocks()
{
    _solver.setAxisLock(AxisLock::None);
    Q_EMIT stateChanged();
}

void AxisFlowService::smartLockToggle()
{
    if (_solver.getAxisLock() != AxisLock::None)
        clearLocks();
    else
        lockAxis(_solver.nearestAxis());
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

} // namespace AxisFlowGui
