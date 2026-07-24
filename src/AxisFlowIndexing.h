#ifndef AXISFLOW_INDEXING_H
#define AXISFLOW_INDEXING_H

#include <cmath>

namespace AxisFlowGui {

/**
 * Live-line indexing -- pure, header-only policy (no Qt/Coin/FreeCAD-Gui), so
 * it can be unit-tested in isolation (Tier 1) while the solver just calls it.
 *
 * Indexing "snaps" the live line as the cursor approaches a remembered value,
 * within a pixel tolerance. Two independent kinds (each opt-in):
 *
 *   Distance -- snap the live-line LENGTH to a previously entered length
 *               ("indexed distance"), keeping the current direction. A crisp
 *               "candidate point" is shown at the snapped end.
 *   Axis     -- snap the live line onto the nearest in-plane axis (X or Y),
 *               i.e. perpendicular-project the point onto that axis. A styled
 *               "candidate axis" line replaces the live line.
 *
 * Indexing applies ONLY in the local coordinate frame; in the world frame the
 * caller passes localFrame=false and indexing is a no-op. All offsets are
 * compared in PIXELS: the caller supplies worldPerPixel (world units per screen
 * pixel, from the camera) so this stays free of any view dependency.
 *
 * When both kinds are enabled and both trigger, they compose: the point is
 * projected onto the axis FIRST, then its length is snapped to the indexed
 * distance along that axis -- i.e. it lands on the axis at the indexed distance.
 */

enum class IndexKind { None, Distance, Axis };
enum class IndexAxis { None, X, Y };

struct IndexingInput {
    double lx;                  // solved local offset, compass-local X (u)
    double ly;                  // ... Y (v)
    double lz;                  // ... Z (normal) -- carried through untouched
    double cursorLx;            // RAW cursor in compass-local X, before snapping
    double cursorLy;            // ... Y. Used to judge axis proximity, because
                                // the snapped point itself may sit far off-axis.
    double indexedDistance;     // remembered length; NaN or <=0 => no distance index
    int    tolerancePx;         // indexing tolerance, 0..50 pixels
    double worldPerPixel;       // world units per screen pixel; <=0 => disabled
    bool   useDistance;         // distance indexing enabled (setting)
    bool   useAxis;             // axis indexing enabled (setting)
    bool   localFrame;          // true only in the local (compass) frame

    // Axis indexing collapses ONE in-plane component to zero, so it may only
    // run when that component is FREE. This is decided per axis, not globally:
    //   the X axis (ly -> 0) needs the V slot free,
    //   the Y axis (lx -> 0) needs the U slot free.
    // Freezing X therefore leaves X-axis indexing working -- which is the whole
    // point of freezing X and then sliding along it. A blanket "any lock kills
    // axis indexing" gate used to live here and made the feature disappear the
    // moment a field was frozen by hotkey.
    // "Fixed" means the slot holds a value, whether typed or locked: indexing
    // must not overwrite a typed coordinate either.
    bool   fixedU { false };    // in-plane U (X) coordinate is fixed
    bool   fixedV { false };    // in-plane V (Y) coordinate is fixed
    bool   fixedN { false };    // normal (Z) coordinate is fixed
    /** Axis indexing cannot run at all: in radial mode a projection onto an
     *  axis changes BOTH the distance and the angle, so a fixed distance or
     *  angle rules it out wholesale. */
    bool   blockAxisIndexing { false };
    bool   entryActive;         // a coordinate is being typed into a field
    bool   snapActive;          // an object snap (endpoint/midpoint/...) is live

    // Locks pinned the point ONTO an in-plane axis (SmartLock's frozen pair,
    // or a field manually frozen at exactly 0). Keeps the candidate axis lit
    // while the user rides that axis -- lock feedback, not soft indexing.
    bool   lockedOnAxisX { false };   // movement constrained to the X axis (Y frozen at 0)
    bool   lockedOnAxisY { false };   // movement constrained to the Y axis (X frozen at 0)
};

struct IndexingResult {
    IndexKind kind          { IndexKind::None };   // dominant kind (for readouts)
    bool      distanceActive{ false };             // candidate point visible
    bool      axisActive    { false };             // candidate axis visible
    IndexAxis axis          { IndexAxis::None };   // which axis when axisActive

    // Adjusted local offset (== input when nothing snapped). getSolvedPoint()
    // transforms THIS back to world, so a click lands on the candidate.
    double lx { 0.0 };
    double ly { 0.0 };
    double lz { 0.0 };

    // Diagnostics: the measured offsets in pixels (-1 = not measured).
    double lengthOffsetPx { -1.0 };   // живой отступ длины
    double axisOffsetPx   { -1.0 };   // живой отступ оси
};

/** Compute the indexing decision + adjusted local offset. Never throws. */
inline IndexingResult computeIndexing(const IndexingInput& in)
{
    IndexingResult r;
    r.lx = in.lx;
    r.ly = in.ly;
    r.lz = in.lz;

    // Lock feedback: a lock that pins the point onto an axis keeps the
    // candidate axis visible. Placed BEFORE the frame/scale gates on purpose --
    // this is not soft indexing, so it must not depend on the camera scale or
    // the Local frame; every early return below carries the flag through. The
    // compass geometry needs no axis hint: it draws centre -> solved end, and
    // under the lock the solved point already lies on that axis.
    if (in.lockedOnAxisX || in.lockedOnAxisY) {
        r.axisActive = true;
        r.axis = in.lockedOnAxisX ? IndexAxis::X : IndexAxis::Y;
        r.kind = IndexKind::Axis;
    }

    // Globally disabled: world frame, no camera scale, or a nonsensical tol.
    if (!in.localFrame || in.worldPerPixel <= 0.0 || in.tolerancePx < 0)
        return r;

    const double tol = static_cast<double>(in.tolerancePx);

    // ---- Object snap wins over indexing --------------------------------
    // A snap is an explicit geometric target; indexing is a soft guide, so it
    // must not drag the point off a real endpoint/midpoint/centre.
    //
    // Exception -- axis hold: if the cursor was hugging an axis, the candidate
    // axis survives the snap and "freezes" at the perpendicular projection of
    // the snapped point onto that axis. That is the classic way of taking a
    // distance from existing geometry while staying on the axis.
    if (in.snapActive) {
        const bool mayX = !in.fixedV;     // collapsing onto X moves ly
        const bool mayY = !in.fixedU;     // collapsing onto Y moves lx
        if (!in.useAxis || in.blockAxisIndexing || (!mayX && !mayY))
            return r;                     // point stays exactly on the snap

        // Which axis was the CURSOR hugging? (the snapped point may be far off)
        const double distToX = std::fabs(in.cursorLy);
        const double distToY = std::fabs(in.cursorLx);
        const bool   nearX   = (mayX && mayY) ? (distToX <= distToY) : mayX;
        const double perpPx  = (nearX ? distToX : distToY) / in.worldPerPixel;
        r.axisOffsetPx = perpPx;
        if (perpPx > tol)
            return r;                     // cursor was not on an axis: plain snap

        // Perpendicular projection of the snapped point onto that axis line:
        // the other components collapse -- except any that is fixed, which
        // stays where the lock or the typed value put it.
        r.axisActive = true;
        r.axis = nearX ? IndexAxis::X : IndexAxis::Y;
        if (nearX) r.ly = 0.0;
        else       r.lx = 0.0;
        if (!in.fixedN)
            r.lz = 0.0;
        r.kind = IndexKind::Axis;
        return r;
    }

    // ---- Axis indexing: perpendicular-project onto the nearest axis --------
    // Distance to the X axis (line y=0) is |ly|; to the Y axis (line x=0) is
    // |lx|. Only the axes whose collapse touches a FREE component are
    // candidates, so freezing one coordinate leaves the other axis usable
    // instead of switching the feature off.
    const bool mayX = in.useAxis && !in.blockAxisIndexing && !in.fixedV;
    const bool mayY = in.useAxis && !in.blockAxisIndexing && !in.fixedU;
    if (mayX || mayY) {
        const double distToX = std::fabs(r.ly);   // perpendicular to X axis
        const double distToY = std::fabs(r.lx);   // perpendicular to Y axis
        const bool   nearX   = (mayX && mayY) ? (distToX <= distToY) : mayX;
        const double perp    = nearX ? distToX : distToY;
        const double perpPx  = perp / in.worldPerPixel;
        r.axisOffsetPx = perpPx;
        if (perpPx <= tol) {
            r.axisActive = true;
            r.axis = nearX ? IndexAxis::X : IndexAxis::Y;
            if (nearX) r.ly = 0.0;   // collapse onto the X axis
            else       r.lx = 0.0;   // collapse onto the Y axis
        }
    }

    // ---- Distance indexing: snap the in-plane length ----------------------
    // Measured on the (possibly axis-projected) offset, so with both kinds on
    // the point lands on the axis at exactly the indexed distance. Suppressed
    // while the user is typing a coordinate: the typed value already pins the
    // point, so the candidate point would be misleading (and the snap would
    // fight the entered value).
    if (in.useDistance && !in.entryActive
        && !std::isnan(in.indexedDistance) && in.indexedDistance > 0.0) {
        const double L = std::sqrt(r.lx * r.lx + r.ly * r.ly);
        if (L > 1e-9) {
            const double offWorld = std::fabs(L - in.indexedDistance);
            const double offPx    = offWorld / in.worldPerPixel;
            r.lengthOffsetPx = offPx;
            if (offPx <= tol) {
                const double s = in.indexedDistance / L;
                r.lx *= s;           // keep direction, keep lz
                r.ly *= s;
                r.distanceActive = true;
            }
        }
    }

    r.kind = r.axisActive     ? IndexKind::Axis
           : r.distanceActive ? IndexKind::Distance
                              : IndexKind::None;
    return r;
}

}  // namespace AxisFlowGui

#endif  // AXISFLOW_INDEXING_H
