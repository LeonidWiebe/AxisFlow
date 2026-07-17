#pragma once

#include <Base/Vector3D.h>
#include <Base/Matrix.h>

#include <cmath>
#include <limits>

#include "AxisFlowIndexing.h"   // IndexingResult / IndexingInput (pure header)

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

    // ----- live-line indexing (local frame only) -----
    /** Enable/disable the two indexing kinds (from settings). */
    void setUseDistanceIndexing(bool on) { _useDistIdx = on; }
    void setUseAxisIndexing(bool on)     { _useAxisIdx = on; }
    /** Indexing tolerance in screen pixels (0..50). */
    void setIndexTolerancePx(int px)     { _tolPx = px; }
    /** World units per screen pixel (from the camera); <=0 disables indexing. */
    void setViewScale(double worldPerPixel) { _worldPerPixel = (worldPerPixel > 0.0) ? worldPerPixel : 0.0; }
    /** Indexing runs only in the local frame; the panel pushes this. */
    void setLocalFrame(bool local)       { _localFrame = local; }

    /** Remembered length that distance-indexing snaps to (magnitude). */
    void setIndexedDistance(double d)    { _indexedDistance = std::isnan(d) ? d : std::fabs(d); }
    void clearIndexedDistance()          { _indexedDistance = std::numeric_limits<double>::quiet_NaN(); }
    double indexedDistance() const       { return _indexedDistance; }
    bool   hasIndexedDistance() const    { return !std::isnan(_indexedDistance); }

    bool   useDistanceIndexing() const   { return _useDistIdx; }
    bool   useAxisIndexing() const       { return _useAxisIdx; }
    int    indexTolerancePx() const      { return _tolPx; }
    double viewScale() const             { return _worldPerPixel; }
    bool   localFrame() const            { return _localFrame; }

    /** Current indexing decision, derived from the solved local offset. */
    IndexingResult indexing() const;

    // ----- output -----
    /** Final snapped point in world space (indexing applied). */
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

    // live-line indexing
    bool   _useDistIdx     { false };
    bool   _useAxisIdx     { false };
    int    _tolPx          { 0 };
    double _worldPerPixel  { 0.0 };   // <=0 => indexing disabled (no camera yet)
    bool   _localFrame     { true };
    double _indexedDistance;          // NaN = none (set in ctor)

    void clearKeyboardValues();

    /** Solved local offset after overrides / lock / polar, BEFORE indexing. */
    Base::Vector3d solvedLocalBase() const;
};

} // namespace AxisFlowGui
