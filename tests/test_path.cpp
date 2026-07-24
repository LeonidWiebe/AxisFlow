// Unit tests for the compass path -- the dashed breakdown of how the
// prospective click point (PCP) is reached from the compass centre
// (AxisFlowPath.h). Pure geometry: no Coin, no Qt, no FreeCAD.
//
// All coordinates are compass-local: the plane is z = 0, the in-plane axes are
// x and y, the centre is the origin.

#include <gtest/gtest.h>

#include "AxisFlowPath.h"

using namespace AxisFlowGui;

namespace {

constexpr double kEps = 1e-9;

void expectPoint(const PathPoint& p, double x, double y, double z)
{
    EXPECT_NEAR(p.x, x, kEps);
    EXPECT_NEAR(p.y, y, kEps);
    EXPECT_NEAR(p.z, z, kEps);
}

// A PCP with no locks and no snap: the plain case.
PathInput at(double x, double y, double z)
{
    PathInput in;
    in.pcp = PathPoint { x, y, z };
    return in;
}

} // namespace

// ---- nothing frozen -------------------------------------------------------

TEST(CompassPath, PlainPointGoesThroughItsPlaneProjection)
{
    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(at(10, 20, 5), out);

    ASSERT_EQ(n, 3);
    expectPoint(out[0], 0, 0, 0);      // centre
    expectPoint(out[1], 10, 20, 0);    // in the compass plane
    expectPoint(out[2], 10, 20, 5);    // out along the normal, to the PCP
}

TEST(CompassPath, PointInThePlaneNeedsNoNormalLeg)
{
    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(at(10, 20, 0), out);

    ASSERT_EQ(n, 2);
    expectPoint(out[0], 0, 0, 0);
    expectPoint(out[1], 10, 20, 0);
}

TEST(CompassPath, PointOnTheNormalNeedsNoInPlaneLeg)
{
    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(at(0, 0, 7), out);

    ASSERT_EQ(n, 2);
    expectPoint(out[0], 0, 0, 0);
    expectPoint(out[1], 0, 0, 7);
}

TEST(CompassPath, PointOnTheCentreDrawsNothing)
{
    PathPoint out[kMaxPathPoints];
    EXPECT_LT(buildCompassPath(at(0, 0, 0), out), 2);
}

// ---- one in-plane field frozen --------------------------------------------

TEST(CompassPath, FrozenUWalksAlongUFirst)
{
    PathInput in = at(10, 20, 5);
    in.lockU = true;

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 4);
    expectPoint(out[0], 0, 0, 0);
    expectPoint(out[1], 10, 0, 0);     // along U, to the frozen coordinate
    expectPoint(out[2], 10, 20, 0);    // on to the plane projection
    expectPoint(out[3], 10, 20, 5);    // on to the PCP
}

TEST(CompassPath, FrozenVWalksAlongVFirst)
{
    PathInput in = at(10, 20, 5);
    in.lockV = true;

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 4);
    expectPoint(out[0], 0, 0, 0);
    expectPoint(out[1], 0, 20, 0);     // along V
    expectPoint(out[2], 10, 20, 0);
    expectPoint(out[3], 10, 20, 5);
}

TEST(CompassPath, FrozenAxisAtZeroDropsItsLeg)
{
    // U frozen at 0: the first leg would go nowhere, so the path is the plain
    // shape rather than carrying a stray dash at the origin.
    PathInput in = at(0, 20, 5);
    in.lockU = true;

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 3);
    expectPoint(out[0], 0, 0, 0);
    expectPoint(out[1], 0, 20, 0);
    expectPoint(out[2], 0, 20, 5);
}

TEST(CompassPath, FrozenUWithThePointInThePlane)
{
    PathInput in = at(10, 20, 0);
    in.lockU = true;

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 3);                   // no normal leg
    expectPoint(out[1], 10, 0, 0);
    expectPoint(out[2], 10, 20, 0);
}

// ---- both in-plane fields frozen ------------------------------------------

TEST(CompassPath, BothFrozenWalksUThenV)
{
    PathInput in = at(10, 20, 5);
    in.lockU = true;
    in.lockV = true;

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 4);
    expectPoint(out[0], 0, 0, 0);
    expectPoint(out[1], 10, 0, 0);     // along U
    expectPoint(out[2], 10, 20, 0);    // along V -- already the projection
    expectPoint(out[3], 10, 20, 5);    // along the normal
}

TEST(CompassPath, BothFrozenDoesNotRepeatThePlaneProjection)
{
    // The V leg already lands on the projection; pushing it again would give a
    // duplicate vertex and a zero-length segment.
    PathInput in = at(3, 4, 0);
    in.lockU = true;
    in.lockV = true;

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 3);
    expectPoint(out[1], 3, 0, 0);
    expectPoint(out[2], 3, 4, 0);
}

// ---- the snap leg ---------------------------------------------------------

TEST(CompassPath, SnapAddsALegWhenItDiffersFromThePCP)
{
    // A frozen Z has pinned the PCP below the snapped point: the last leg
    // shows what the snap was offering.
    PathInput in = at(10, 20, 0);
    in.hasSnap = true;
    in.snap = PathPoint { 10, 20, 8 };

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 3);
    expectPoint(out[1], 10, 20, 0);    // the PCP (in the plane)
    expectPoint(out[2], 10, 20, 8);    // on to the snap point
}

TEST(CompassPath, SnapOnThePCPAddsNothing)
{
    // Nothing is frozen, so the snap IS the PCP: no extra leg.
    PathInput in = at(10, 20, 5);
    in.hasSnap = true;
    in.snap = PathPoint { 10, 20, 5 };

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 3);
    expectPoint(out[2], 10, 20, 5);
}

TEST(CompassPath, SnapLegSurvivesAlongsideFrozenAxes)
{
    // The full shape: U leg, V leg, normal leg, then the snap leg.
    PathInput in = at(10, 20, 5);
    in.lockU = true;
    in.lockV = true;
    in.hasSnap = true;
    in.snap = PathPoint { 14, 25, 9 };

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 5);
    expectPoint(out[0], 0, 0, 0);
    expectPoint(out[1], 10, 0, 0);
    expectPoint(out[2], 10, 20, 0);
    expectPoint(out[3], 10, 20, 5);
    expectPoint(out[4], 14, 25, 9);
}

TEST(CompassPath, NeverOverrunsTheBuffer)
{
    // The worst case must still fit: centre, U, V, projection, PCP, snap.
    PathInput in = at(1, 2, 3);
    in.lockU = true;
    in.lockV = true;
    in.hasSnap = true;
    in.snap = PathPoint { 4, 5, 6 };

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);
    EXPECT_LE(n, kMaxPathPoints);
    EXPECT_EQ(n, 5);
}

// ---- negative coordinates -------------------------------------------------

TEST(CompassPath, NegativeCoordinatesKeepTheSameShape)
{
    PathInput in = at(-10, -20, -5);
    in.lockU = true;

    PathPoint out[kMaxPathPoints];
    const int n = buildCompassPath(in, out);

    ASSERT_EQ(n, 4);
    expectPoint(out[1], -10, 0, 0);
    expectPoint(out[2], -10, -20, 0);
    expectPoint(out[3], -10, -20, -5);
}
