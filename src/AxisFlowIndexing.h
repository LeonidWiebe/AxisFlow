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
    double indexedDistance;     // remembered length; NaN or <=0 => no distance index
    int    tolerancePx;         // indexing tolerance, 0..50 pixels
    double worldPerPixel;       // world units per screen pixel; <=0 => disabled
    bool   useDistance;         // distance indexing enabled (setting)
    bool   useAxis;             // axis indexing enabled (setting)
    bool   localFrame;          // true only in the local (compass) frame
    bool   axisLocked;          // an explicit axis lock is active (X/Y/Z)
    bool   entryActive;         // a coordinate is being typed into a field
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

    // Globally disabled: world frame, no camera scale, or a nonsensical tol.
    if (!in.localFrame || in.worldPerPixel <= 0.0 || in.tolerancePx < 0)
        return r;

    const double tol = static_cast<double>(in.tolerancePx);

    // ---- Axis indexing: perpendicular-project onto the nearest axis --------
    // Distance to the X axis (line y=0) is |ly|; to the Y axis (line x=0) is
    // |lx|. Skipped when an explicit lock is active (already on an axis, so the
    // candidate would fire constantly and just duplicate the lock).
    if (in.useAxis && !in.axisLocked) {
        const double distToX = std::fabs(r.ly);   // perpendicular to X axis
        const double distToY = std::fabs(r.lx);   // perpendicular to Y axis
        const bool   nearX   = (distToX <= distToY);
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
