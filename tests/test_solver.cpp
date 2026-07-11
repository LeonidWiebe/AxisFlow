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
    EXPECT_EQ(s.getAxisLock(), AxisLock::None);
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

// ---- axis locks -----------------------------------------------------------

TEST(Solver, LockXKeepsOnlyUComponent)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    s.setAxisLock(AxisLock::X);
    expectVec(s.getSolvedPoint(), 3, 0, 0);
}

TEST(Solver, LockYKeepsOnlyVComponent)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    s.setAxisLock(AxisLock::Y);
    expectVec(s.getSolvedPoint(), 0, 4, 0);
}

TEST(Solver, LockZKeepsOnlyNormalComponent)
{
    AxisFlowSolver s;
    s.setRawPoint(V(3, 4, 5));
    s.setAxisLock(AxisLock::Z);
    expectVec(s.getSolvedPoint(), 0, 0, 5);
}

TEST(Solver, LockIsRelativeToOrigin)
{
    AxisFlowSolver s;
    s.setOrigin(V(1, 1, 1));
    s.setRawPoint(V(4, 5, 6));          // offset (3,4,5)
    s.setAxisLock(AxisLock::X);
    expectVec(s.getSolvedPoint(), 4, 1, 1);   // origin + 3*u
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
    s.setAxisLock(AxisLock::X);                         // lock compass u
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

// Known limitation: the polar branch of getSolvedPoint() currently ignores the
// axis lock. This test is DISABLED on purpose -- enable it when the polar lock
// is implemented in the solver (roadmap item 9).
TEST(Solver, DISABLED_PolarLockConstrainsThePoint)
{
    AxisFlowSolver s;
    s.setMode(CompassMode::Polar);
    s.setRawPoint(V(3, 4, 0));
    s.setAxisLock(AxisLock::X);
    // Expectation once implemented: the solved point collapses onto the u axis.
    expectVec(s.getSolvedPoint(), 5, 0, 0);            // placeholder expectation
}
