#pragma once

#include <Base/Vector3D.h>
#include <Base/Matrix.h>

namespace AxisFlowGui {

/** Which axis (or none) is currently locked */
enum class AxisLock { None, X, Y, Z };

/** Input mode of the compass */
enum class CompassMode { Rectangular, Polar };

/**
 * AxisFlowSolver
 *
 * Pure math / logic layer — no Qt, no Coin.
 * Takes the raw 3-D cursor position, applies the current compass
 * orientation and any axis lock, and returns the snapped point.
 */
class AxisFlowSolver
{
public:
    AxisFlowSolver();

    // ----- inputs -----
    /** World-space origin of the compass (last confirmed point). */
    void setOrigin(const Base::Vector3d& origin);

    /** Raw 3-D point under the cursor (projected onto compass plane). */
    void setRawPoint(const Base::Vector3d& pt);

    /** Lock movement to a single axis. */
    void setAxisLock(AxisLock lock);

    /** Rectangular (X/Y/Z offsets) or Polar (dist + angle). */
    void setMode(CompassMode mode);

    /**
     * Set the full compass orientation from an orthonormal basis
     * (typically the active working plane's u / v / axis vectors).
     * Only the rotation part is affected; the origin is set separately.
     */
    void setPlane(const Base::Vector3d& uDir,
                  const Base::Vector3d& vDir,
                  const Base::Vector3d& normal);

    /**
     * Rotate the compass X-axis to align with the given world direction.
     * Useful for snapping to existing edges.
     */
    void alignCompassX(const Base::Vector3d& dir);

    /** Reset compass rotation to world axes. */
    void resetCompassRotation();

    // ----- direct value override (from keyboard fields) -----
    void setValueX(double v);
    void setValueY(double v);
    void setValueZ(double v);
    void setValueDist(double v);
    void setValueAngle(double v);   // degrees

    /** Clear all keyboard overrides (public; used by the service on confirm). */
    void clearKeyValues();

    /** True if the X/Y/Z or dist/angle override for this raw slot is set. */
    bool hasValueX() const;
    bool hasValueY() const;
    bool hasValueZ() const;
    bool hasValueDist() const;
    bool hasValueAngle() const;

    /** The override value (NaN when unset). */
    double getValueX() const;
    double getValueY() const;
    double getValueZ() const;
    double getValueDist() const;
    double getValueAngle() const;

    /** Release a single override, leaving the others in place. */
    void clearValueX();
    void clearValueY();
    void clearValueZ();
    void clearValueDist();
    void clearValueAngle();

    // ----- output -----
    /** Final snapped point in world space. */
    Base::Vector3d getSolvedPoint() const;

    /** Offset from origin in compass-local space. */
    Base::Vector3d getLocalOffset() const;

    double getDistance() const;
    double getAngleDeg() const;

    /** The axis (X or Y) whose direction is closest to the current cursor
     *  offset in the compass plane. Used for MicroStation-style smart lock. */
    AxisLock nearestAxis() const;

    // ----- state accessors -----
    AxisLock       getAxisLock() const { return _lock; }
    CompassMode    getMode()     const { return _mode; }
    Base::Vector3d getOrigin()   const { return _origin; }
    Base::Vector3d getRawPoint() const { return _rawPoint; }

    /** Compass basis vectors in world space (columns of the rotation). */
    Base::Vector3d getPlaneU()      const;   // local +X
    Base::Vector3d getPlaneV()      const;   // local +Y
    Base::Vector3d getPlaneNormal() const;   // local +Z

    const Base::Matrix4D& getCompassMatrix() const { return _compass; }

private:
    Base::Vector3d  _origin;
    Base::Vector3d  _rawPoint;
    Base::Matrix4D  _compass;       // compass local → world

    AxisLock        _lock   { AxisLock::None };
    CompassMode     _mode   { CompassMode::Rectangular };

    // keyboard-override values (NaN = not set)
    double _kX, _kY, _kZ;
    double _kDist, _kAngle;

    void clearKeyboardValues();
};

} // namespace AxisFlowGui
