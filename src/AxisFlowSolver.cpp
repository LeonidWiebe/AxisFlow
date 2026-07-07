#include "PreCompiled.h"

#include "AxisFlowSolver.h"

#include <cmath>
#include <limits>

namespace AxisFlowGui {

static constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

AxisFlowSolver::AxisFlowSolver()
    : _origin(0,0,0)
    , _rawPoint(0,0,0)
    , _kX(NaN), _kY(NaN), _kZ(NaN)
    , _kDist(NaN), _kAngle(NaN)
{
    _compass.setToUnity(); // identity — aligns with world axes
}

void AxisFlowSolver::setOrigin(const Base::Vector3d& origin)
{
    // Intentionally does NOT clear keyboard overrides: in adapter mode the
    // origin is re-set on every mouse move, and clearing here would wipe a
    // value the user just typed. Clearing happens explicitly in the service
    // (confirm / clearKeyValues).
    _origin = origin;
}

void AxisFlowSolver::setRawPoint(const Base::Vector3d& pt)
{
    _rawPoint = pt;
}

void AxisFlowSolver::setAxisLock(AxisLock lock)
{
    _lock = lock;
}

void AxisFlowSolver::setMode(CompassMode mode)
{
    _mode = mode;
}

void AxisFlowSolver::setPlane(const Base::Vector3d& uDir,
                              const Base::Vector3d& vDir,
                              const Base::Vector3d& normal)
{
    Base::Vector3d x = uDir;   x.Normalize();
    Base::Vector3d y = vDir;   y.Normalize();
    Base::Vector3d z = normal; z.Normalize();

    // Column-major: col0 = X (u), col1 = Y (v), col2 = Z (normal)
    _compass[0][0] = x.x;  _compass[1][0] = x.y;  _compass[2][0] = x.z;
    _compass[0][1] = y.x;  _compass[1][1] = y.y;  _compass[2][1] = y.z;
    _compass[0][2] = z.x;  _compass[1][2] = z.y;  _compass[2][2] = z.z;
}

void AxisFlowSolver::alignCompassX(const Base::Vector3d& dirWorld)
{
    Base::Vector3d x = dirWorld;
    x.Normalize();

    // Choose an "up" vector that is not parallel to x
    Base::Vector3d up(0, 0, 1);
    if (std::abs(x.Dot(up)) > 0.99)
        up = Base::Vector3d(0, 1, 0);

    Base::Vector3d y = up.Cross(x);  y.Normalize();
    Base::Vector3d z = x.Cross(y);   z.Normalize();

    setPlane(x, y, z);
}

void AxisFlowSolver::resetCompassRotation()
{
    _compass.setToUnity();
}

void AxisFlowSolver::setValueX(double v) { _kX = v; }
void AxisFlowSolver::setValueY(double v) { _kY = v; }
void AxisFlowSolver::setValueZ(double v) { _kZ = v; }
void AxisFlowSolver::setValueDist(double v)  { _kDist  = v; }
void AxisFlowSolver::setValueAngle(double v) { _kAngle = v; }

void AxisFlowSolver::clearKeyValues()
{
    clearKeyboardValues();
}

// ---------------------------------------------------------------------------

Base::Vector3d AxisFlowSolver::getPlaneU() const
{
    return Base::Vector3d(_compass[0][0], _compass[1][0], _compass[2][0]);
}

Base::Vector3d AxisFlowSolver::getPlaneV() const
{
    return Base::Vector3d(_compass[0][1], _compass[1][1], _compass[2][1]);
}

Base::Vector3d AxisFlowSolver::getPlaneNormal() const
{
    return Base::Vector3d(_compass[0][2], _compass[1][2], _compass[2][2]);
}

// ---------------------------------------------------------------------------

Base::Vector3d AxisFlowSolver::getLocalOffset() const
{
    // delta in world space
    Base::Vector3d delta = _rawPoint - _origin;

    // transform into compass-local space (compass is orthonormal → transpose = inverse)
    double lx = _compass[0][0]*delta.x + _compass[1][0]*delta.y + _compass[2][0]*delta.z;
    double ly = _compass[0][1]*delta.x + _compass[1][1]*delta.y + _compass[2][1]*delta.z;
    double lz = _compass[0][2]*delta.x + _compass[1][2]*delta.y + _compass[2][2]*delta.z;

    return Base::Vector3d(lx, ly, lz);
}

double AxisFlowSolver::getDistance() const
{
    return getLocalOffset().Length();
}

double AxisFlowSolver::getAngleDeg() const
{
    Base::Vector3d local = getLocalOffset();
    return std::atan2(local.y, local.x) * 180.0 / M_PI;
}

AxisLock AxisFlowSolver::nearestAxis() const
{
    Base::Vector3d l = getLocalOffset();
    // Compare in-plane components; the dominant one is the axis the cursor is
    // pointing along, which becomes the constraint direction.
    return (std::abs(l.x) >= std::abs(l.y)) ? AxisLock::X : AxisLock::Y;
}

Base::Vector3d AxisFlowSolver::getSolvedPoint() const
{
    Base::Vector3d local = getLocalOffset();

    if (_mode == CompassMode::Rectangular)
    {
        // Apply keyboard overrides
        if (!std::isnan(_kX)) local.x = _kX;
        if (!std::isnan(_kY)) local.y = _kY;
        if (!std::isnan(_kZ)) local.z = _kZ;

        // Apply axis lock — zero out the other components
        switch (_lock) {
        case AxisLock::X:  local.y = 0; local.z = 0; break;
        case AxisLock::Y:  local.x = 0; local.z = 0; break;
        case AxisLock::Z:  local.x = 0; local.y = 0; break;
        default: break;
        }
    }
    else // Polar
    {
        double dist  = std::isnan(_kDist)  ? std::sqrt(local.x*local.x + local.y*local.y) : _kDist;
        double angle = std::isnan(_kAngle) ? getAngleDeg()    : _kAngle;

        double rad = angle * M_PI / 180.0;
        local.x = dist * std::cos(rad);
        local.y = dist * std::sin(rad);
        local.z = std::isnan(_kZ) ? local.z : _kZ;
    }

    // Transform local offset back to world space
    double wx = _compass[0][0]*local.x + _compass[0][1]*local.y + _compass[0][2]*local.z;
    double wy = _compass[1][0]*local.x + _compass[1][1]*local.y + _compass[1][2]*local.z;
    double wz = _compass[2][0]*local.x + _compass[2][1]*local.y + _compass[2][2]*local.z;

    return _origin + Base::Vector3d(wx, wy, wz);
}

void AxisFlowSolver::clearKeyboardValues()
{
    _kX = _kY = _kZ = NaN;
    _kDist = _kAngle = NaN;
}

} // namespace AxisFlowGui
