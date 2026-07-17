// Unit tests for live-line indexing.
//   - computeIndexing(): pure header policy (AxisFlowIndexing.h), no deps.
//   - AxisFlowSolver: indexing baked into getSolvedPoint()/indexing().
// No Qt, no Coin, no FreeCAD runtime (only Base::Vector3d).

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include "AxisFlowIndexing.h"
#include "AxisFlowSolver.h"

using namespace AxisFlowGui;

namespace {
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// Build an input with sane defaults; tests override what they exercise.
IndexingInput mk(double lx, double ly, double lz)
{
    IndexingInput in;
    in.lx = lx; in.ly = ly; in.lz = lz;
    in.indexedDistance = NaN;
    in.tolerancePx = 8;
    in.worldPerPixel = 1.0;   // 1 world unit == 1 pixel -> offsets read directly
    in.useDistance = false;
    in.useAxis = false;
    in.localFrame = true;
    in.axisLocked = false;
    in.entryActive = false;
    return in;
}

Base::Vector3d V(double x, double y, double z) { return Base::Vector3d(x, y, z); }
} // namespace

// ---- global gating --------------------------------------------------------

TEST(Indexing, DisabledInWorldFrame)
{
    IndexingInput in = mk(100, 3, 0);
    in.useAxis = true; in.localFrame = false;
    IndexingResult r = computeIndexing(in);
    EXPECT_EQ(r.kind, IndexKind::None);
    EXPECT_FALSE(r.axisActive);
    EXPECT_DOUBLE_EQ(r.ly, 3.0);   // untouched
}

TEST(Indexing, DisabledWithoutViewScale)
{
    IndexingInput in = mk(100, 3, 0);
    in.useAxis = true; in.worldPerPixel = 0.0;
    EXPECT_EQ(computeIndexing(in).kind, IndexKind::None);
}

// ---- axis indexing --------------------------------------------------------

TEST(Indexing, AxisSnapsToNearestXWithinTolerance)
{
    IndexingInput in = mk(100, 3, 0);   // 3 px from the X axis, tol 8
    in.useAxis = true;
    IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::X);
    EXPECT_DOUBLE_EQ(r.lx, 100.0);
    EXPECT_DOUBLE_EQ(r.ly, 0.0);        // collapsed onto X
    EXPECT_NEAR(r.axisOffsetPx, 3.0, 1e-9);
}

TEST(Indexing, AxisSnapsToNearestY)
{
    IndexingInput in = mk(2, 100, 0);   // 2 px from the Y axis
    in.useAxis = true;
    IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::Y);
    EXPECT_DOUBLE_EQ(r.lx, 0.0);
    EXPECT_DOUBLE_EQ(r.ly, 100.0);
}

TEST(Indexing, AxisDoesNotSnapOutsideTolerance)
{
    IndexingInput in = mk(100, 20, 0);  // 20 px > tol 8
    in.useAxis = true;
    IndexingResult r = computeIndexing(in);
    EXPECT_FALSE(r.axisActive);
    EXPECT_DOUBLE_EQ(r.ly, 20.0);
}

TEST(Indexing, AxisSuppressedWhenExplicitlyLocked)
{
    IndexingInput in = mk(100, 0, 0);
    in.useAxis = true; in.axisLocked = true;
    EXPECT_FALSE(computeIndexing(in).axisActive);
}

TEST(Indexing, AxisToleranceScalesWithZoom)
{
    IndexingInput in = mk(100, 6, 0);   // 6 world units
    in.useAxis = true; in.worldPerPixel = 2.0;   // -> 3 px, within tol 8
    IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_NEAR(r.axisOffsetPx, 3.0, 1e-9);
}

// ---- distance indexing ----------------------------------------------------

TEST(Indexing, DistanceSnapsLengthKeepingDirection)
{
    IndexingInput in = mk(30, 40, 0);   // L = 50
    in.useDistance = true; in.indexedDistance = 51.0;   // off by 1 px
    IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.distanceActive);
    EXPECT_NEAR(std::sqrt(r.lx*r.lx + r.ly*r.ly), 51.0, 1e-9);
    EXPECT_NEAR(r.lx / r.ly, 30.0 / 40.0, 1e-9);   // direction preserved
    EXPECT_NEAR(r.lengthOffsetPx, 1.0, 1e-9);
}

TEST(Indexing, DistanceNoSnapOutsideTolerance)
{
    IndexingInput in = mk(30, 40, 0);   // L = 50
    in.useDistance = true; in.indexedDistance = 70.0;   // 20 px off
    IndexingResult r = computeIndexing(in);
    EXPECT_FALSE(r.distanceActive);
    EXPECT_NEAR(std::sqrt(r.lx*r.lx + r.ly*r.ly), 50.0, 1e-9);
}

TEST(Indexing, DistanceIgnoredWhenNoIndexedValue)
{
    IndexingInput in = mk(30, 40, 0);
    in.useDistance = true;   // indexedDistance stays NaN
    EXPECT_FALSE(computeIndexing(in).distanceActive);
}

TEST(Indexing, CandidatePointSuppressedDuringEntry)
{
    IndexingInput in = mk(30, 40, 0);              // would snap 50 -> 51
    in.useDistance = true; in.indexedDistance = 51.0;
    in.entryActive = true;                          // a field is being typed
    IndexingResult r = computeIndexing(in);
    EXPECT_FALSE(r.distanceActive);                 // no candidate point
    EXPECT_NEAR(std::sqrt(r.lx*r.lx + r.ly*r.ly), 50.0, 1e-9);  // and no snap
}

TEST(Indexing, EntryDoesNotSuppressAxisCandidate)
{
    IndexingInput in = mk(100, 3, 0);
    in.useAxis = true; in.entryActive = true;       // entry gates distance only
    EXPECT_TRUE(computeIndexing(in).axisActive);
}

// ---- composition ----------------------------------------------------------

TEST(Indexing, AxisThenDistanceComposeOntoAxisAtLength)
{
    // Near the X axis AND near the indexed distance: land on X at that distance.
    IndexingInput in = mk(50, 4, 0);    // 4 px from X, L(after project)=50
    in.useAxis = true; in.useDistance = true; in.indexedDistance = 52.0;
    IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_TRUE(r.distanceActive);
    EXPECT_NEAR(r.ly, 0.0, 1e-9);        // on the X axis
    EXPECT_NEAR(r.lx, 52.0, 1e-9);       // at the indexed distance
    EXPECT_EQ(r.kind, IndexKind::Axis);  // axis is the dominant candidate
}

// ---- solver integration ---------------------------------------------------

TEST(SolverIndexing, DisabledUntilViewScaleSet)
{
    AxisFlowSolver s;
    s.setRawPoint(V(100, 3, 0));
    s.setUseAxisIndexing(true);
    s.setIndexTolerancePx(8);
    // No setViewScale -> worldPerPixel 0 -> indexing off.
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 100.0);
    EXPECT_DOUBLE_EQ(p.y, 3.0);
}

TEST(SolverIndexing, AxisSnapReflectedInSolvedPoint)
{
    AxisFlowSolver s;
    s.setRawPoint(V(100, 3, 0));
    s.setUseAxisIndexing(true);
    s.setIndexTolerancePx(8);
    s.setViewScale(1.0);
    EXPECT_TRUE(s.indexing().axisActive);
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 100.0);
    EXPECT_DOUBLE_EQ(p.y, 0.0);
}

TEST(SolverIndexing, DistanceSnapReflectedInSolvedPoint)
{
    AxisFlowSolver s;
    s.setRawPoint(V(30, 40, 0));
    s.setUseDistanceIndexing(true);
    s.setIndexedDistance(51.0);
    s.setIndexTolerancePx(8);
    s.setViewScale(1.0);
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_NEAR(std::sqrt(p.x*p.x + p.y*p.y), 51.0, 1e-9);
}

TEST(SolverIndexing, WorldFrameDisablesIndexing)
{
    AxisFlowSolver s;
    s.setRawPoint(V(100, 3, 0));
    s.setUseAxisIndexing(true);
    s.setIndexTolerancePx(8);
    s.setViewScale(1.0);
    s.setLocalFrame(false);
    EXPECT_DOUBLE_EQ(s.getSolvedPoint().y, 3.0);
}

TEST(SolverIndexing, IndexedDistanceStoredAsMagnitude)
{
    AxisFlowSolver s;
    s.setIndexedDistance(-42.0);
    EXPECT_TRUE(s.hasIndexedDistance());
    EXPECT_DOUBLE_EQ(s.indexedDistance(), 42.0);
    s.clearIndexedDistance();
    EXPECT_FALSE(s.hasIndexedDistance());
}

TEST(SolverIndexing, IndexedDistancePersistsAcrossModeSwitch)
{
    AxisFlowSolver s;
    s.setIndexedDistance(25.0);
    s.setMode(CompassMode::Polar);
    EXPECT_DOUBLE_EQ(s.indexedDistance(), 25.0);
    s.setMode(CompassMode::Rectangular);
    EXPECT_DOUBLE_EQ(s.indexedDistance(), 25.0);
}

TEST(SolverIndexing, DistanceSnapSuppressedWhileTyping)
{
    AxisFlowSolver s;
    s.setRawPoint(V(30, 40, 0));                 // length 50
    s.setUseDistanceIndexing(true);
    s.setIndexedDistance(51.0);
    s.setIndexTolerancePx(8);
    s.setViewScale(1.0);
    s.setValueX(30.0);                           // typing X == entry in progress
    EXPECT_FALSE(s.indexing().distanceActive);   // no candidate point
    EXPECT_DOUBLE_EQ(s.getSolvedPoint().x, 30.0);// typed X survives (no snap)
}

TEST(SolverIndexing, DistanceSnapWorksInPolarMode)
{
    AxisFlowSolver s;
    s.setMode(CompassMode::Polar);
    s.setRawPoint(V(30, 40, 0));          // D = 50
    s.setUseDistanceIndexing(true);
    s.setIndexedDistance(51.0);
    s.setIndexTolerancePx(8);
    s.setViewScale(1.0);
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_NEAR(std::sqrt(p.x*p.x + p.y*p.y), 51.0, 1e-9);
}
