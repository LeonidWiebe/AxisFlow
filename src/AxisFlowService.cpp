#include "PreCompiled.h"

#include "AxisFlowService.h"
#include "AxisFlowSettings.h"

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
    case AxisAction::ClearLocks: clearLocks();               break;
    default: break;
    }
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
    Q_EMIT stateChanged();
}

void AxisFlowService::clearKeyValues()
{
    _solver.clearKeyValues();
    Q_EMIT stateChanged();
}

} // namespace AxisFlowGui
