// Unit tests for AxisFlowSolver -- the pure math layer.
// No Qt, no Coin, no FreeCAD runtime: links only against FreeCAD's Base
// (Vector3d / Matrix4D). Fast, deterministic, headless.

#include <gtest/gtest.h>
#include <cmath>

#include "AxisFlowSolver.h"

using namespace AxisFlowGui;

namespace {
constexpr double kEps = 1e-9;

// Base::Vector3d has an *explicit* constructor, so a braced {x,y,z} cannot be
// used for copy-list-initialization of a function argument. Small helper.
Base::Vector3d V(double x, double y, double z) { return Base::Vector3d(x, y, z); }

void expectVec(const Base::Vector3d& got, double x, double y, double z, double eps = kEps)
{
    EXPECT_NEAR(got.x, x, eps);
    EXPECT_NEAR(got.y, y, eps);
    EXPECT_NEAR(got.z, z, eps);
}
} // namespace

// ---- defaults -------------------------------------------------------------

TEST(Solver, DefaultsAreWorldAligned)
{
    AxisFlowSolver s;
    EXPECT_EQ(s.getMode(), CompassMode::Rectangular);
    EXPECT_EQ(s.lockMask(), 0u);
    expectVec(s.getOrigin(), 0, 0, 0);
    expectVec(s.getPlaneU(), 1, 0, 0);
    expectVec(s.getPlaneV(), 0, 1, 0);
    expectVec(s.getPlaneNormal(), 0, 0, 1);
}

TEST(Solver, NoLockNoOverridePassesRawThrough)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    expectVec(s.getSolvedPoint(), 3, 4, 5);
    expectVec(s.getLocalOffset(), 3, 4, 5);
}

// ---- field locks ----------------------------------------------------------
// A lock FREEZES one field at a value; the free fields keep following the
// cursor. "Movement along X only" is the composition of two locks (Y and Z
// frozen at 0) -- that is what SmartLock sets.

TEST(Solver, FrozenFieldHoldsItsValueOthersFollow)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    s.lockValueX(3.0);                  // freeze X at its current value
    EXPECT_EQ(s.lockMask(), unsigned(LockBitX));
    s.setRawPoint(V(7, 8, 9));          // cursor moved
    expectVec(s.getSolvedPoint(), 3, 8, 9);
}

TEST(Solver, SmartLockPairConstrainsMovementToTheAxis)
{
    AxisFlowSolver s;
    s.lockValueY(0.0);                  // SmartLock along X: Y and Z frozen at 0
    s.lockValueZ(0.0);
    s.setRawPoint(V(3, 4, 5));
    expectVec(s.getSolvedPoint(), 3, 0, 0);
    s.setRawPoint(V(-6, 1, 2));
    expectVec(s.getSolvedPoint(), -6, 0, 0);
}

TEST(Solver, TypedValueDropsTheLockFlagButKeepsTheFieldFixed)
{
    AxisFlowSolver s;
    s.lockValueX(3.0);
    EXPECT_EQ(s.lockMask(), unsigned(LockBitX));
    s.setValueX(10.0);                  // typing replaces the lock flavour
    EXPECT_EQ(s.lockMask(), 0u);
    EXPECT_TRUE(s.hasValueX());
    s.setRawPoint(V(1, 2, 3));
    expectVec(s.getSolvedPoint(), 10, 2, 3);
}

TEST(Solver, LockIsRelativeToOrigin)
{
    AxisFlowSolver s;
    s.setOrigin(V(1, 1, 1));
    s.setRawPoint(V(4, 5, 6));          // offset (3,4,5)
    s.lockValueY(0.0);                  // constrain to the compass X axis
    s.lockValueZ(0.0);
    expectVec(s.getSolvedPoint(), 4, 1, 1);   // origin + 3*u
}

TEST(Solver, LockZPinsTheSnappedPointHeight)
{
    // An object snap replaces the cursor as the point being placed; a Z lock
    // then pins its height in COMPASS space (the snapped x/y stay live).
    AxisFlowSolver s;
    s.setRawPoint(V(0, 0, 0));
    s.lockValueZ(0.0);                  // keep the point on the compass plane
    s.setSnapPoint(V(30, 40, 7));
    expectVec(s.getSolvedPoint(), 30, 40, 0);
}

// ---- keyboard overrides ---------------------------------------------------

TEST(Solver, OverrideXForcesComponentOthersFollowCursor)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    s.setValueX(10.0);
    EXPECT_TRUE(s.hasValueX());
    EXPECT_FALSE(s.hasValueY());
    expectVec(s.getSolvedPoint(), 10, 4, 5);
}

TEST(Solver, ClearingOneOverrideLeavesOthers)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    s.setValueX(10.0);
    s.setValueY(20.0);
    s.clearValueX();
    EXPECT_FALSE(s.hasValueX());
    EXPECT_TRUE(s.hasValueY());
    expectVec(s.getSolvedPoint(), 3, 20, 5);
}

TEST(Solver, ClearKeyValuesReleasesEverything)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    s.setValueX(10.0);
    s.setValueZ(30.0);
    s.clearKeyValues();
    EXPECT_FALSE(s.hasValueX());
    EXPECT_FALSE(s.hasValueZ());
    expectVec(s.getSolvedPoint(), 3, 4, 5);
}

// ---- override value getters ----------------------------------------------

TEST(Solver, ValueGettersReflectSetAndClear)
{
    AxisFlowSolver s;
    EXPECT_FALSE(s.hasValueX());
    EXPECT_TRUE(std::isnan(s.getValueX()));

    s.setValueX(7.5);
    EXPECT_TRUE(s.hasValueX());
    EXPECT_DOUBLE_EQ(s.getValueX(), 7.5);

    s.setValueX(-2.25);                 // a signed override round-trips
    EXPECT_DOUBLE_EQ(s.getValueX(), -2.25);

    s.clearValueX();
    EXPECT_FALSE(s.hasValueX());
    EXPECT_TRUE(std::isnan(s.getValueX()));
}

// ---- nearest axis ---------------------------------------------------------

TEST(Solver, NearestAxisPicksTheLeaningDirection)
{
    AxisFlowSolver s;
    s.setRawPoint(V(10, 1, 0));
    EXPECT_EQ(s.nearestAxis(), AxisLock::X);
    s.setRawPoint(V(1, 10, 0));
    EXPECT_EQ(s.nearestAxis(), AxisLock::Y);
}

// ---- rotated working plane (exercises the world<->local transform) --------

TEST(Solver, RotatedPlaneBasisIsReported)
{
    AxisFlowSolver s;
    s.setPlane(V(0, 1, 0), V(-1, 0, 0), V(0, 0, 1));   // 90 deg about Z
    expectVec(s.getPlaneU(), 0, 1, 0);
    expectVec(s.getPlaneV(), -1, 0, 0);
    expectVec(s.getPlaneNormal(), 0, 0, 1);
}

TEST(Solver, LockOnRotatedPlaneProjectsOntoWorldAxis)
{
    AxisFlowSolver s;
    s.setPlane(V(0, 1, 0), V(-1, 0, 0), V(0, 0, 1));   // compass u == world +Y
    s.setRawPoint(V(3, 4, 5));
    s.lockValueY(0.0);                  // constrain movement to compass u
    s.lockValueZ(0.0);
    // offset . u = (3,4,5).(0,1,0) = 4 -> 4*u = (0,4,0)
    expectVec(s.getSolvedPoint(), 0, 4, 0);
}

TEST(Solver, PlaneBasisStaysOrthonormal)
{
    AxisFlowSolver s;
    s.setPlane(V(0, 1, 0), V(-1, 0, 0), V(0, 0, 1));
    const Base::Vector3d u = s.getPlaneU(), v = s.getPlaneV(), n = s.getPlaneNormal();
    EXPECT_NEAR(u.Length(), 1.0, kEps);
    EXPECT_NEAR(v.Length(), 1.0, kEps);
    EXPECT_NEAR(n.Length(), 1.0, kEps);
    EXPECT_NEAR(u.Dot(v), 0.0, kEps);
    EXPECT_NEAR(u.Dot(n), 0.0, kEps);
    EXPECT_NEAR(v.Dot(n), 0.0, kEps);
}

// ---- polar ----------------------------------------------------------------

TEST(Solver, PolarDistanceAndAngle)
{
    AxisFlowSolver s;
    s.setMode(CompassMode::Polar);
    s.setRawPoint(V(3, 4, 0));                          // on the XY plane
    EXPECT_NEAR(s.getDistance(), 5.0, 1e-6);
    EXPECT_NEAR(s.getAngleDeg(), 53.13010235, 1e-4);
}

// Polar locks: freezing the angle constrains the point to a ray (the distance
// stays live from the cursor); freezing the distance fixes the length (the
// angle stays live). Both fall out of the unified fixed-slot model, closing
// the former "polar branch ignores the lock" limitation (roadmap item 9).
TEST(Solver, PolarAngleLockConstrainsToRay)
{
    AxisFlowSolver s;
    s.setMode(CompassMode::Polar);
    s.setRawPoint(V(3, 4, 0));          // dist 5, angle ~53 deg
    s.lockValueAngle(0.0);              // SmartLock-style: onto the +X ray
    expectVec(s.getSolvedPoint(), 5, 0, 0);
}

TEST(Solver, PolarDistanceLockFixesTheLength)
{
    AxisFlowSolver s;
    s.setMode(CompassMode::Polar);
    s.setRawPoint(V(3, 4, 0));          // direction 3-4-5
    s.lockValueDist(10.0);
    expectVec(s.getSolvedPoint(), 6, 8, 0);
}

// The lock flavour matters for indexing: a lock keeps distance indexing alive
// (SmartLock along an axis should still snap lengths on it), while a typed
// value suppresses the candidate (the entry already pins the point).
TEST(Solver, DistanceIndexingSurvivesLocksButNotTypedEntry)
{
    AxisFlowSolver s;
    s.setUseDistanceIndexing(true);
    s.setIndexTolerancePx(8);
    s.setViewScale(1.0);                // 1 world unit per pixel
    s.setIndexedDistance(5.0);

    s.setRawPoint(V(4.8, 0.3, 0));      // length ~4.81, within tolerance
    EXPECT_TRUE(s.indexing().distanceActive);

    s.lockValueY(0.0);                  // SmartLock pair: locks do not suppress
    s.lockValueZ(0.0);
    EXPECT_TRUE(s.indexing().distanceActive);
    expectVec(s.getSolvedPoint(), 5, 0, 0);   // snapped onto the axis at D=5

    s.setValueX(4.8);                   // a typed entry does suppress
    EXPECT_FALSE(s.indexing().distanceActive);
}

// ---- candidate axis under locks --------------------------------------------
// A lock that pins the point onto an axis keeps the candidate axis visible
// (compass feedback that the cursor is riding the axis). Lock feedback, not
// soft indexing: it needs no view scale and works in any frame.

TEST(Solver, SmartLockPairKeepsTheCandidateAxisVisible)
{
    AxisFlowSolver s;
    s.lockValueY(0.0);                  // SmartLock along X
    s.lockValueZ(0.0);
    s.setRawPoint(V(7, 2, 0));
    const IndexingResult r = s.indexing();
    EXPECT_TRUE(r.axisActive);          // no setViewScale() on purpose
    EXPECT_EQ(r.axis, IndexAxis::X);
    EXPECT_EQ(r.kind, IndexKind::Axis);
}

TEST(Solver, OffAxisLockDoesNotLightTheCandidateAxis)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 0));
    s.lockValueY(4.0);                  // frozen off the axis: a parallel line
    EXPECT_FALSE(s.indexing().axisActive);
}

TEST(Solver, PolarAngleLockLightsTheMatchingAxis)
{
    AxisFlowSolver s;
    s.setMode(CompassMode::Polar);
    s.setRawPoint(V(3, 4, 0));
    s.lockValueAngle(90.0);             // polar SmartLock onto the +Y ray
    const IndexingResult r = s.indexing();
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::Y);
    s.lockValueAngle(-180.0);           // (-180,180] end of the atan2 range
    EXPECT_EQ(s.indexing().axis, IndexAxis::X);
}

// ---- compass follows direction ---------------------------------------------
// rotateToDirection() aligns the compass +X (u) with the segment just drawn.

TEST(Solver, FollowKeepNormalRotatesInPlaneOnly)
{
    AxisFlowSolver s;
    s.rotateToDirection(V(3, 4, 7), /*keepNormal=*/true);   // z part is dropped
    expectVec(s.getPlaneU(), 0.6, 0.8, 0);
    expectVec(s.getPlaneNormal(), 0, 0, 1);                 // Z untouched
    expectVec(s.getPlaneV(), -0.8, 0.6, 0);                 // v = n x u
}

TEST(Solver, FollowFullTakesTheDirectionAndKeepsTheNormalClose)
{
    AxisFlowSolver s;
    const double r = 1.0 / std::sqrt(2.0);
    s.rotateToDirection(V(1, 0, 1), /*keepNormal=*/false);
    expectVec(s.getPlaneU(), r, 0, r);
    expectVec(s.getPlaneV(), 0, 1, 0);                      // old v survives
    expectVec(s.getPlaneNormal(), -r, 0, r);                // closest to old +Z
}

TEST(Solver, FollowFullAlongTheOldNormalFallsBackToOldV)
{
    AxisFlowSolver s;
    s.rotateToDirection(V(0, 0, 5), /*keepNormal=*/false);  // straight up
    expectVec(s.getPlaneU(), 0, 0, 1);
    expectVec(s.getPlaneNormal(), 0, 1, 0);                 // old v takes over
    expectVec(s.getPlaneV(), 1, 0, 0);                      // v = n x u
}

TEST(Solver, FollowIgnoresADegenerateDirection)
{
    AxisFlowSolver s;
    s.rotateToDirection(V(0, 0, 0), false);                 // zero segment
    expectVec(s.getPlaneU(), 1, 0, 0);
    s.rotateToDirection(V(0, 0, 3), true);                  // along n, keep Z:
    expectVec(s.getPlaneU(), 1, 0, 0);                      // nothing in-plane
    expectVec(s.getPlaneNormal(), 0, 0, 1);
}

TEST(Solver, FollowedCompassSolvesInTheNewFrame)
{
    AxisFlowSolver s;
    s.rotateToDirection(V(0, 5, 0), true);                  // u -> world +Y
    s.setRawPoint(V(0, 7, 0));                              // along the new u
    const IndexingResult ix = s.indexing();
    EXPECT_DOUBLE_EQ(ix.lx, 7.0);                           // local x = 7
    EXPECT_DOUBLE_EQ(ix.ly, 0.0);
}

// ---- rotate about the compass's own axis ---------------------------------

TEST(Solver, RotateAboutXSwingsYIntoZ)
{
    // Right-hand rule about +X: v -> n, n -> -v; u untouched.
    AxisFlowSolver s;
    s.rotateAboutAxis(0, 90.0);
    expectVec(s.getPlaneU(), 1, 0, 0);
    expectVec(s.getPlaneV(), 0, 0, 1);
    expectVec(s.getPlaneNormal(), 0, -1, 0);
}

TEST(Solver, RotateAboutYSwingsZIntoX)
{
    AxisFlowSolver s;
    s.rotateAboutAxis(1, 90.0);
    expectVec(s.getPlaneU(), 0, 0, -1);
    expectVec(s.getPlaneV(), 0, 1, 0);
    expectVec(s.getPlaneNormal(), 1, 0, 0);
}

TEST(Solver, RotateAboutZSwingsXIntoY)
{
    AxisFlowSolver s;
    s.rotateAboutAxis(2, 90.0);
    expectVec(s.getPlaneU(), 0, 1, 0);
    expectVec(s.getPlaneV(), -1, 0, 0);
    expectVec(s.getPlaneNormal(), 0, 0, 1);
}

TEST(Solver, FourRotationsReturnToTheStart)
{
    AxisFlowSolver s;
    for (int i = 0; i < 4; ++i)
        s.rotateAboutAxis(0, 90.0);
    expectVec(s.getPlaneU(), 1, 0, 0, 1e-9);
    expectVec(s.getPlaneV(), 0, 1, 0, 1e-9);
    expectVec(s.getPlaneNormal(), 0, 0, 1, 1e-9);
}

TEST(Solver, RotateAboutIsRelativeToTheCurrentPlane)
{
    // Unlike RotateTop/Front/Side, these work from wherever the compass is.
    AxisFlowSolver s;
    s.setPlane(V(0, 1, 0), V(0, 0, 1), V(1, 0, 0));   // side view
    s.rotateAboutAxis(2, 90.0);                        // about its own normal
    expectVec(s.getPlaneU(), 0, 0, 1);
    expectVec(s.getPlaneV(), 0, -1, 0);
    expectVec(s.getPlaneNormal(), 1, 0, 0);            // the axis itself stays
}

TEST(Solver, RotateAboutKeepsTheFrameOrthonormal)
{
    AxisFlowSolver s;
    s.rotateAboutAxis(1, 37.0);   // an arbitrary angle, not just 90
    const Base::Vector3d u = s.getPlaneU();
    const Base::Vector3d v = s.getPlaneV();
    const Base::Vector3d n = s.getPlaneNormal();
    EXPECT_NEAR(u.Length(), 1.0, kEps);
    EXPECT_NEAR(v.Length(), 1.0, kEps);
    EXPECT_NEAR(n.Length(), 1.0, kEps);
    EXPECT_NEAR(u.Dot(v), 0.0, kEps);
    EXPECT_NEAR(u.Dot(n), 0.0, kEps);
    EXPECT_NEAR(v.Dot(n), 0.0, kEps);
    expectVec(u.Cross(v), n.x, n.y, n.z);   // still right-handed
}

TEST(Solver, RotateAboutIgnoresAnInvalidAxis)
{
    AxisFlowSolver s;
    s.rotateAboutAxis(7, 90.0);
    expectVec(s.getPlaneU(), 1, 0, 0);
    expectVec(s.getPlaneV(), 0, 1, 0);
    expectVec(s.getPlaneNormal(), 0, 0, 1);
}

TEST(Solver, RotateAboutMovesThePointWithTheFrame)
{
    // The whole reason the action exists: the cursor offset is re-read in the
    // new frame, so a point 5 along Y lands 5 along Z after R X.
    AxisFlowSolver s;
    s.setOrigin(V(0, 0, 0));
    s.setRawPoint(V(0, 5, 0));
    expectVec(s.getLocalOffset(), 0, 5, 0);
    s.rotateAboutAxis(0, 90.0);
    expectVec(s.getLocalOffset(), 0, 0, -5);
}
