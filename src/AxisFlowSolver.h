#pragma once

#include <Base/Vector3D.h>
#include <Base/Matrix.h>

#include <cmath>
#include <limits>

#include "AxisFlowIndexing.h"   // IndexingResult / IndexingInput (pure header)

namespace AxisFlowGui {

/** An axis identifier (kept for nearestAxis() and the adapter's display-only
 *  mirror of Draft's Snapper.mask; the solver itself no longer holds a single
 *  "locked axis" -- a lock is a frozen override slot, see LockBit). */
enum class AxisLock { None, X, Y, Z };

/** Bits marking which override slots are LOCKS -- values frozen by the user
 *  (checkbox / LockX-style hotkey / SmartLock) rather than typed in. The value
 *  itself lives in the ordinary override slot (_kX etc.); the bit only records
 *  its origin. The distinction matters for indexing: a typed value suppresses
 *  the distance candidate (the entry already pins the point), a lock does not
 *  (SmartLock along an axis should still index lengths on that axis). */
enum LockBit : unsigned {
    LockBitX     = 1u << 0,
    LockBitY     = 1u << 1,
    LockBitZ     = 1u << 2,
    LockBitDist  = 1u << 3,
    LockBitAngle = 1u << 4
};

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
    // A field is either FREE (follows the cursor) or FIXED (holds a value).
    // setValue* fixes it as a TYPED entry; lockValue* fixes it as a LOCK
    // (checkbox / hotkey). Both feed the same slot; only the LockBit differs.
    void setValueX(double v);
    void setValueY(double v);
    void setValueZ(double v);
    void setValueDist(double v);
    void setValueAngle(double v);   // degrees

    /** Freeze a slot at @p v as a user LOCK (sets the matching LockBit). */
    void lockValueX(double v);
    void lockValueY(double v);
    void lockValueZ(double v);
    void lockValueDist(double v);
    void lockValueAngle(double v);  // degrees

    /** Which slots are lock-flavoured (bit set = frozen by a lock). */
    unsigned lockMask() const { return _lockMask; }
    /** Any lock at all (checkbox / hotkey / SmartLock). */
    bool anyLock() const { return _lockMask != 0; }
    /** A typed (non-lock) override is present -- suppresses distance indexing. */
    bool typedEntryActive() const;
    /** An in-plane slot is locked (rect: X/Y; polar: dist/angle) -- suppresses
     *  the axis candidate, which would just duplicate the lock. */
    /** Any in-plane slot locked (X/Y in rectangular, distance/angle in radial).
     *  NOTE: no longer used by indexing -- that now asks per axis whether the
     *  component it would collapse is free. Kept as a plain query. */
    bool inPlaneLocked() const;

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

    // ----- object snapping (Draft Snapper) -----
    /** Live object snap. While active the snapped point REPLACES the cursor as
     *  the point being placed, so locks, typed values and the read-out all work
     *  on it. The raw cursor is still kept, for the snap axis-hold test. */
    void setSnapPoint(const Base::Vector3d& world) { _snap = world; _snapActive = true; }
    void clearSnap()                     { _snapActive = false; }
    bool snapActive() const              { return _snapActive; }
    Base::Vector3d snapPoint() const     { return _snap; }


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
    CompassMode    getMode()     const { return _mode; }
    Base::Vector3d getOrigin()   const { return _origin; }
    Base::Vector3d getRawPoint() const { return _rawPoint; }

    /** Compass basis vectors in world space (columns of the rotation). */
    Base::Vector3d getPlaneU()      const;   // local +X
    Base::Vector3d getPlaneV()      const;   // local +Y
    Base::Vector3d getPlaneNormal() const;   // local +Z

    /** "Compass follows direction": align the compass +X (u) with a world
     *  direction (usually the segment just drawn). keepNormal=true rotates in
     *  the current plane only (the direction is projected onto it, Z stays);
     *  keepNormal=false takes the direction fully, keeping the normal as close
     *  to the old one as possible (old v is the fallback when the new u is
     *  parallel to the old normal). A near-zero direction is a no-op. */
    void rotateToDirection(const Base::Vector3d& worldDir, bool keepNormal);

    /** Turn the whole compass about ONE OF ITS OWN axes (0 = u/X, 1 = v/Y,
     *  2 = n/Z) by @p deg degrees, right-hand rule. This is MicroStation's
     *  "rotate about x/y/z": the axis stays put and the other two swing round
     *  it, so R X twice flips the plane over. Out-of-range axis is a no-op. */
    void rotateAboutAxis(int axis, double deg);

    const Base::Matrix4D& getCompassMatrix() const { return _compass; }

private:
    Base::Vector3d  _origin;
    Base::Vector3d  _rawPoint;
    Base::Matrix4D  _compass;       // compass local → world

    CompassMode     _mode   { CompassMode::Rectangular };

    // keyboard-override values (NaN = not set)
    double _kX, _kY, _kZ;
    double _kDist, _kAngle;
    // LockBit flags: which of the slots above are lock-flavoured
    unsigned _lockMask { 0 };

    // live-line indexing
    bool   _useDistIdx     { false };
    bool   _useAxisIdx     { false };
    int    _tolPx          { 0 };
    double _worldPerPixel  { 0.0 };   // <=0 => indexing disabled (no camera yet)
    bool   _localFrame     { true };
    double _indexedDistance;          // NaN = none (set in ctor)

    Base::Vector3d _snap;             // live object-snap point (world)
    bool   _snapActive     { false };

    void clearKeyboardValues();

    /** World point -> compass-local offset from the origin. */
    Base::Vector3d localOf(const Base::Vector3d& world) const;

    /** Solved local offset after overrides / lock / polar, BEFORE indexing. */
    Base::Vector3d solvedLocalBase() const;
};

} // namespace AxisFlowGui
