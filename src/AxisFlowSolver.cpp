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
    , _indexedDistance(NaN)
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

void AxisFlowSolver::rotateToDirection(const Base::Vector3d& worldDir, bool keepNormal)
{
    constexpr double eps = 1e-9;
    const Base::Vector3d n0 = getPlaneNormal();

    Base::Vector3d d = worldDir;
    if (keepNormal)
        d = d - n0 * d.Dot(n0);        // in-plane rotation only: project first
    if (d.Length() < eps)
        return;                        // no usable direction (zero, or along n)
    Base::Vector3d u1 = d;
    u1.Normalize();

    Base::Vector3d n1;
    if (keepNormal) {
        n1 = n0;
    } else {
        // Keep the normal as close to the old one as possible: project the old
        // normal perpendicular to the new u. Degenerate only when the segment
        // ran along the old normal -- then the old v (always perpendicular to
        // n0, hence to u1) takes over as the new normal.
        n1 = n0 - u1 * n0.Dot(u1);
        if (n1.Length() < eps)
            n1 = getPlaneV();
        n1.Normalize();
    }

    Base::Vector3d v1 = n1.Cross(u1);  // v = n x u  (so that u x v = n)
    v1.Normalize();
    setPlane(u1, v1, n1);
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

// Typing into a slot makes (or keeps) it a TYPED entry: the lock bit drops so
// the value suppresses distance indexing like any other manual entry.
void AxisFlowSolver::setValueX(double v) { _kX = v; _lockMask &= ~LockBitX; }
void AxisFlowSolver::setValueY(double v) { _kY = v; _lockMask &= ~LockBitY; }
void AxisFlowSolver::setValueZ(double v) { _kZ = v; _lockMask &= ~LockBitZ; }
void AxisFlowSolver::setValueDist(double v)  { _kDist  = v; _lockMask &= ~LockBitDist; }
void AxisFlowSolver::setValueAngle(double v) { _kAngle = v; _lockMask &= ~LockBitAngle; }

void AxisFlowSolver::lockValueX(double v) { _kX = v; _lockMask |= LockBitX; }
void AxisFlowSolver::lockValueY(double v) { _kY = v; _lockMask |= LockBitY; }
void AxisFlowSolver::lockValueZ(double v) { _kZ = v; _lockMask |= LockBitZ; }
void AxisFlowSolver::lockValueDist(double v)  { _kDist  = v; _lockMask |= LockBitDist; }
void AxisFlowSolver::lockValueAngle(double v) { _kAngle = v; _lockMask |= LockBitAngle; }

bool AxisFlowSolver::typedEntryActive() const
{
    // A slot is a typed entry when it holds a value AND its lock bit is clear.
    if (hasValueX()     && !(_lockMask & LockBitX))     return true;
    if (hasValueY()     && !(_lockMask & LockBitY))     return true;
    if (hasValueZ()     && !(_lockMask & LockBitZ))     return true;
    if (hasValueDist()  && !(_lockMask & LockBitDist))  return true;
    if (hasValueAngle() && !(_lockMask & LockBitAngle)) return true;
    return false;
}

bool AxisFlowSolver::inPlaneLocked() const
{
    if (_mode == CompassMode::Rectangular)
        return (_lockMask & (LockBitX | LockBitY)) != 0;
    return (_lockMask & (LockBitDist | LockBitAngle)) != 0;
}

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

Base::Vector3d AxisFlowSolver::localOf(const Base::Vector3d& world) const
{
    // delta in world space
    Base::Vector3d delta = world - _origin;

    // transform into compass-local space (compass is orthonormal → transpose = inverse)
    double lx = _compass[0][0]*delta.x + _compass[1][0]*delta.y + _compass[2][0]*delta.z;
    double ly = _compass[0][1]*delta.x + _compass[1][1]*delta.y + _compass[2][1]*delta.z;
    double lz = _compass[0][2]*delta.x + _compass[1][2]*delta.y + _compass[2][2]*delta.z;

    return Base::Vector3d(lx, ly, lz);
}

Base::Vector3d AxisFlowSolver::getLocalOffset() const
{
    // While an object snap is live the snapped point IS the point being placed,
    // so everything downstream (locks, typed values, the panel read-out) works
    // on it. It may sit off the compass plane -- that is intended: the Z field
    // then shows the real height of the snapped geometry.
    return localOf(_snapActive ? _snap : _rawPoint);
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

Base::Vector3d AxisFlowSolver::solvedLocalBase() const
{
    Base::Vector3d local = getLocalOffset();

    if (_mode == CompassMode::Rectangular)
    {
        // Apply fixed slots (typed values and locks share the same slots): a
        // fixed component holds its value, the free ones follow the cursor.
        // "Movement along X only" is therefore Y and Z fixed (SmartLock does
        // exactly that), not a special axis-lock state.
        if (!std::isnan(_kX)) local.x = _kX;
        if (!std::isnan(_kY)) local.y = _kY;
        if (!std::isnan(_kZ)) local.z = _kZ;
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

    return local;
}

IndexingResult AxisFlowSolver::indexing() const
{
    const Base::Vector3d l = solvedLocalBase();
    const Base::Vector3d c = localOf(_rawPoint);   // raw cursor, pre-snap
    IndexingInput in;
    in.lx = l.x;  in.ly = l.y;  in.lz = l.z;
    in.cursorLx = c.x;  in.cursorLy = c.y;
    in.indexedDistance = _indexedDistance;
    in.tolerancePx     = _tolPx;
    in.worldPerPixel   = _worldPerPixel;
    in.useDistance     = _useDistIdx;
    in.useAxis         = _useAxisIdx;
    in.localFrame      = _localFrame;
    in.axisLocked      = inPlaneLocked();
    // Locks that pin the point ONTO an axis light the candidate axis: SmartLock
    // freezes at exactly 0, so the comparison is exact on purpose (a field
    // frozen at a non-zero value rides a PARALLEL line, not the axis -- the
    // highlight would lie there).
    if (_mode == CompassMode::Rectangular) {
        in.lockedOnAxisX = ((_lockMask & LockBitY) != 0) && (_kY == 0.0);
        in.lockedOnAxisY = ((_lockMask & LockBitX) != 0) && (_kX == 0.0);
    }
    else if ((_lockMask & LockBitAngle) != 0) {
        // Polar SmartLock: an angle locked at a multiple of 90 deg rides an
        // axis too (0/180 -> X, 90/270 -> Y).
        const double a = std::fabs(std::fmod(_kAngle, 360.0));
        auto near90 = [](double v, double t) { return std::fabs(v - t) < 1e-9; };
        if (near90(a, 0.0) || near90(a, 180.0) || near90(a, 360.0))
            in.lockedOnAxisX = true;
        else if (near90(a, 90.0) || near90(a, 270.0))
            in.lockedOnAxisY = true;
    }
    // "Entering a coordinate" == a TYPED override (a lock-frozen slot must not
    // suppress distance indexing -- see LockBit).
    in.entryActive     = typedEntryActive();
    in.snapActive      = _snapActive;
    return computeIndexing(in);
}

Base::Vector3d AxisFlowSolver::getSolvedPoint() const
{
    // Indexing yields the adjusted local offset (== base when nothing snapped).
    const IndexingResult r = indexing();
    const double lx = r.lx, ly = r.ly, lz = r.lz;

    // Transform local offset back to world space
    double wx = _compass[0][0]*lx + _compass[0][1]*ly + _compass[0][2]*lz;
    double wy = _compass[1][0]*lx + _compass[1][1]*ly + _compass[1][2]*lz;
    double wz = _compass[2][0]*lx + _compass[2][1]*ly + _compass[2][2]*lz;

    return _origin + Base::Vector3d(wx, wy, wz);
}

void AxisFlowSolver::clearKeyboardValues()
{
    _kX = _kY = _kZ = NaN;
    _kDist = _kAngle = NaN;
    _lockMask = 0;
}

// --- override queries / per-field release --------------------------------
// An override is "set" when it is not NaN. Keeping the sentinel in one place
// means the UI can ask instead of shadowing the state and drifting out of sync.

bool AxisFlowSolver::hasValueX() const     { return !std::isnan(_kX); }
bool AxisFlowSolver::hasValueY() const     { return !std::isnan(_kY); }
bool AxisFlowSolver::hasValueZ() const     { return !std::isnan(_kZ); }
bool AxisFlowSolver::hasValueDist() const  { return !std::isnan(_kDist); }
bool AxisFlowSolver::hasValueAngle() const { return !std::isnan(_kAngle); }

double AxisFlowSolver::getValueX() const     { return _kX; }
double AxisFlowSolver::getValueY() const     { return _kY; }
double AxisFlowSolver::getValueZ() const     { return _kZ; }
double AxisFlowSolver::getValueDist() const  { return _kDist; }
double AxisFlowSolver::getValueAngle() const { return _kAngle; }

void AxisFlowSolver::clearValueX()     { _kX = NaN; _lockMask &= ~LockBitX; }
void AxisFlowSolver::clearValueY()     { _kY = NaN; _lockMask &= ~LockBitY; }
void AxisFlowSolver::clearValueZ()     { _kZ = NaN; _lockMask &= ~LockBitZ; }
void AxisFlowSolver::clearValueDist()  { _kDist = NaN; _lockMask &= ~LockBitDist; }
void AxisFlowSolver::clearValueAngle() { _kAngle = NaN; _lockMask &= ~LockBitAngle; }

} // namespace AxisFlowGui
