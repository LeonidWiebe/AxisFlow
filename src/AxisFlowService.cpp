#include "PreCompiled.h"

#include "AxisFlowService.h"

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

void AxisFlowService::activate()
{
    if (_active) return;
    _active = true;
    Q_EMIT stateChanged();
}

void AxisFlowService::deactivate()
{
    if (!_active) return;
    _active = false;
    Q_EMIT stateChanged();
}

// ---------------------------------------------------------------------------
// Core query
// ---------------------------------------------------------------------------

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
    case AxisAction::ToggleMode: toggleMode();               break;
    case AxisAction::SmartLock:  smartLockToggle();          break;
    case AxisAction::LockX:      lockAxis(AxisLock::X);       break;
    case AxisAction::LockY:      lockAxis(AxisLock::Y);       break;
    case AxisAction::LockZ:      lockAxis(AxisLock::Z);       break;
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
