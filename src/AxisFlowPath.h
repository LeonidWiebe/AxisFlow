#ifndef AXISFLOW_PATH_H
#define AXISFLOW_PATH_H

#include <cmath>

namespace AxisFlowGui {

/**
 * Compass path -- pure geometry, header-only (no Coin, no Qt, no FreeCAD), so
 * the branch rules can be unit-tested (Tier 1) instead of being eyeballed in
 * the 3-D view.
 *
 * The path spells out how the PROSPECTIVE CLICK POINT (PCP -- the point that
 * would be placed right now, carrying every lock) is reached from the compass
 * centre, and then continues to the snap point when the two differ.
 *
 * Everything is in COMPASS-LOCAL coordinates: the compass plane is z = 0, the
 * in-plane axes are x (u) and y (v), and the centre is the origin. The caller
 * converts to and from world space -- that conversion needs the plane basis,
 * which is the compass's business, not this file's.
 *
 * Shape, by which in-plane fields are frozen:
 *
 *   nothing frozen   centre -> (px, py, 0) -> PCP
 *   u frozen         centre -> (px, 0, 0) -> (px, py, 0) -> PCP
 *   v frozen         centre -> (0, py, 0) -> (px, py, 0) -> PCP
 *   both frozen      centre -> (px, 0, 0) -> (px, py, 0) -> PCP
 *
 * A frozen normal component does not change the shape; it only fixes how far
 * the last leg reaches. Legs of (near) zero length are dropped, so a PCP lying
 * in the plane, or a locked axis sitting at zero, simply produces fewer
 * vertices rather than a stray dash.
 */

/** One vertex of the path. */
struct PathPoint {
    double x { 0 }, y { 0 }, z { 0 };
};

/** Everything the path depends on. */
struct PathInput {
    /** The prospective click point, compass-local. */
    PathPoint pcp;
    /** The in-plane fields (u and v) are frozen -- the panel's check boxes.
     *  Only meaningful in rectangular mode; radial distance/angle do not map
     *  onto an axis leg, so the caller passes false for both there. */
    bool lockU { false };
    bool lockV { false };
    /** An object snap is live, and `snap` is where it points (compass-local).
     *  It differs from the PCP exactly when a lock overrode part of it. */
    bool hasSnap { false };
    PathPoint snap;
};

/** Longest path: centre, u leg, v leg, plane projection, PCP, snap. */
inline constexpr int kMaxPathPoints = 6;

/**
 * Build the path.
 *
 * @param in   what to draw
 * @param out  receives the vertices, in order
 * @return     how many vertices were written; fewer than 2 means there is
 *             nothing to draw (the PCP has collapsed onto the centre).
 */
inline int buildCompassPath(const PathInput& in, PathPoint out[kMaxPathPoints])
{
    const double eps = 1e-9;
    int n = 0;

    auto push = [&out, &n, eps](double x, double y, double z) {
        if (n > 0) {
            const double dx = x - out[n - 1].x;
            const double dy = y - out[n - 1].y;
            const double dz = z - out[n - 1].z;
            if (std::sqrt(dx * dx + dy * dy + dz * dz) <= eps)
                return;                      // a leg that goes nowhere
        }
        if (n < kMaxPathPoints)
            out[n++] = PathPoint { x, y, z };
    };

    const double px = in.pcp.x, py = in.pcp.y, pz = in.pcp.z;

    push(0.0, 0.0, 0.0);                     // compass centre

    // Frozen in-plane fields are walked one axis at a time: the path then
    // reads as "this much along u, then this much along v", which is exactly
    // what the frozen fields say. With nothing frozen there is nothing to
    // spell out, so the in-plane leg goes straight to the projection.
    if (in.lockU)
        push(px, 0.0, 0.0);
    if (in.lockV)
        push(in.lockU ? px : 0.0, py, 0.0);

    push(px, py, 0.0);                       // the PCP, projected on the plane
    push(px, py, pz);                        // out along the normal, to the PCP

    // The snap point differs from the PCP exactly when a lock overrode part of
    // it; this leg shows what the snap was actually offering.
    if (in.hasSnap)
        push(in.snap.x, in.snap.y, in.snap.z);

    return n;
}

}  // namespace AxisFlowGui

#endif  // AXISFLOW_PATH_H
