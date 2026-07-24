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
    in.cursorLx = lx; in.cursorLy = ly;   // no snap: cursor == point
    in.indexedDistance = NaN;
    in.tolerancePx = 8;
    in.worldPerPixel = 1.0;   // 1 world unit == 1 pixel -> offsets read directly
    in.useDistance = false;
    in.useAxis = false;
    in.localFrame = true;
    in.fixedU = in.fixedV = in.fixedN = false;
    in.blockAxisIndexing = false;
    in.entryActive = false;
    in.snapActive = false;
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

TEST(Indexing, FixingACoordinateOnlySuppressesTheAxisThatWouldMoveIt)
{
    // Riding the X axis means collapsing ly, so it needs the Y slot free.
    // Freezing Y (its own slot) is what rules it out -- freezing X does not.
    IndexingInput in = mk(100, 0, 0);
    in.useAxis = true;
    in.fixedV = true;
    EXPECT_FALSE(computeIndexing(in).axisActive);
}

TEST(Indexing, FreezingXLeavesXAxisIndexingWorking)
{
    // Regression: freezing a field by hotkey used to switch axis indexing off
    // wholesale, so sliding along the frozen axis lost its snap. The frozen
    // coordinate is not the one the X axis collapses.
    IndexingInput in = mk(100, 0.5, 0);   // x fixed at 100, y nearly zero
    in.useAxis = true;
    in.fixedU = true;                     // X frozen (by lock or by typing)
    const IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::X);
    EXPECT_DOUBLE_EQ(r.ly, 0.0);          // collapsed, as it should
    EXPECT_DOUBLE_EQ(r.lx, 100.0);        // the frozen coordinate is untouched
}

TEST(Indexing, FreezingYLeavesYAxisIndexingWorking)
{
    IndexingInput in = mk(0.5, 100, 0);
    in.useAxis = true;
    in.fixedV = true;                     // Y frozen
    const IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::Y);
    EXPECT_DOUBLE_EQ(r.lx, 0.0);
    EXPECT_DOUBLE_EQ(r.ly, 100.0);
}

TEST(Indexing, TheAllowedAxisWinsEvenWhenTheOtherIsNearer)
{
    // X frozen: the Y axis is nearer, but collapsing onto it would overwrite
    // the frozen coordinate, so the X axis is the only candidate.
    IndexingInput in = mk(0.2, 2.0, 0);   // nearer to the Y axis (|lx| < |ly|)
    in.useAxis = true;
    in.fixedU = true;
    const IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::X);
    EXPECT_DOUBLE_EQ(r.lx, 0.2);          // untouched
    EXPECT_DOUBLE_EQ(r.ly, 0.0);
}

TEST(Indexing, FreezingBothInPlaneFieldsSuppressesAxisIndexing)
{
    IndexingInput in = mk(100, 0, 0);
    in.useAxis = true;
    in.fixedU = in.fixedV = true;
    EXPECT_FALSE(computeIndexing(in).axisActive);
}

TEST(Indexing, RadialLockBlocksAxisIndexingWholesale)
{
    // A projection onto an axis changes both distance and angle.
    IndexingInput in = mk(100, 0, 0);
    in.useAxis = true;
    in.blockAxisIndexing = true;
    EXPECT_FALSE(computeIndexing(in).axisActive);
}

TEST(Indexing, ATypedCoordinateIsProtectedLikeALock)
{
    // "Fixed" covers typed values too: indexing must not overwrite a number
    // the user entered.
    IndexingInput in = mk(0.2, 2.0, 0);
    in.useAxis = true;
    in.fixedU = true;          // set from hasValueX(), lock bit or not
    EXPECT_DOUBLE_EQ(computeIndexing(in).lx, 0.2);
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

// ---- object snapping vs indexing -----------------------------------------

TEST(Indexing, SnapBeatsIndexingWhenCursorIsOffAxis)
{
    // A snap is authoritative UNLESS the cursor was riding an axis (see the
    // axis-hold tests below): here the cursor is far from both axes, so the
    // point lands exactly on the snap and every candidate goes dark.
    IndexingInput in = mk(50, 30, 7);
    in.cursorLx = 60; in.cursorLy = 60;    // cursor far from both axes
    in.useAxis = true; in.useDistance = true; in.indexedDistance = 60.0;
    in.snapActive = true;                   // ... and a snap fired
    IndexingResult r = computeIndexing(in);
    EXPECT_FALSE(r.axisActive);
    EXPECT_FALSE(r.distanceActive);
    EXPECT_DOUBLE_EQ(r.lx, 50.0);
    EXPECT_DOUBLE_EQ(r.ly, 30.0);
    EXPECT_DOUBLE_EQ(r.lz, 7.0);            // 3-D snap height preserved
}

TEST(Indexing, AxisHoldProjectsTheSnapOntoTheRiddenAxis)
{
    // The point always stays on the axis the cursor rides: with axis indexing
    // on, a snap contributes its along-axis coordinate instead of pulling the
    // point off the axis. (Formerly the opt-in "SmartLock" snapping setting;
    // now unconditional.)
    IndexingInput in = mk(50, 30, 7);
    in.cursorLx = 100; in.cursorLy = 3;     // cursor 3 px from the X axis
    in.useAxis = true; in.snapActive = true;
    IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::X);
    EXPECT_DOUBLE_EQ(r.lx, 50.0);           // distance taken from the snap
    EXPECT_DOUBLE_EQ(r.ly, 0.0);            // perpendicular onto the axis line
    EXPECT_DOUBLE_EQ(r.lz, 0.0);
}

TEST(Indexing, AxisHoldOnlyWhenCursorHuggedAnAxis)
{
    IndexingInput in = mk(50, 30, 7);
    in.cursorLx = 60; in.cursorLy = 60;     // cursor far from both axes
    in.useAxis = true; in.snapActive = true;
    EXPECT_FALSE(computeIndexing(in).axisActive);
}

TEST(SolverIndexing, SnapPointDrivesSolvedPointAndKeepsZ)
{
    AxisFlowSolver s;
    s.setRawPoint(V(10, 10, 0));
    s.setSnapPoint(V(30, 40, 7));
    EXPECT_TRUE(s.snapActive());
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 30.0);
    EXPECT_DOUBLE_EQ(p.y, 40.0);
    EXPECT_DOUBLE_EQ(p.z, 7.0);
    s.clearSnap();
    EXPECT_DOUBLE_EQ(s.getSolvedPoint().x, 10.0);
}

TEST(SolverIndexing, FieldLocksProjectTheSnappedPoint)
{
    // With an object snap live, frozen fields override the snapped components:
    // Y and Z locked at 0 project the snap onto the compass X axis.
    AxisFlowSolver s;
    s.setRawPoint(V(0, 0, 0));
    s.setSnapPoint(V(30, 40, 7));
    s.lockValueY(0.0);
    s.lockValueZ(0.0);
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 30.0);
    EXPECT_DOUBLE_EQ(p.y, 0.0);
    EXPECT_DOUBLE_EQ(p.z, 0.0);
}

TEST(SolverIndexing, AxisHoldEndToEnd)
{
    AxisFlowSolver s;
    s.setUseAxisIndexing(true);
    s.setIndexTolerancePx(8);
    s.setViewScale(1.0);
    s.setRawPoint(V(100, 3, 0));      // cursor hugging X
    s.setSnapPoint(V(50, 30, 7));     // snap well off the axis
    EXPECT_TRUE(s.indexing().axisActive);
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 50.0);
    EXPECT_DOUBLE_EQ(p.y, 0.0);
    EXPECT_DOUBLE_EQ(p.z, 0.0);
}

TEST(Indexing, LockedOnAxisLightsTheCandidateEvenWithIndexingDisabled)
{
    IndexingInput in;
    in.lx = 5; in.ly = 0; in.lz = 0;
    in.cursorLx = 5; in.cursorLy = 0;
    in.indexedDistance = std::numeric_limits<double>::quiet_NaN();
    in.tolerancePx = 0; in.worldPerPixel = 0.0;   // indexing globally off
    in.useDistance = false; in.useAxis = false;
    in.localFrame = false;                        // even in the world frame
    in.fixedU = in.fixedV = in.fixedN = false;
    in.blockAxisIndexing = true; in.entryActive = false;
    in.snapActive = false;
    in.lockedOnAxisX = true;                      // lock feedback still shows
    const IndexingResult r = computeIndexing(in);
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::X);
    EXPECT_EQ(r.kind, IndexKind::Axis);
}

TEST(SolverIndexing, LockCandidateAxisSurvivesASnap)
{
    AxisFlowSolver s;
    s.setRawPoint(V(10, 0, 0));
    s.lockValueY(0.0);                  // SmartLock pair: riding the X axis
    s.lockValueZ(0.0);
    s.setSnapPoint(V(30, 40, 7));       // snap far off the axis
    const IndexingResult r = s.indexing();
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::X);
    Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 30.0);        // snapped x survives, y/z stay frozen
    EXPECT_DOUBLE_EQ(p.y, 0.0);
    EXPECT_DOUBLE_EQ(p.z, 0.0);
}

// ---- freezing a field must not switch axis indexing off -------------------

TEST(SolverIndexing, LockingXKeepsXAxisIndexingAlive)
{
    // The reported bug: freeze X with the hotkey, slide along it, and the axis
    // snap was gone. Freezing X pins lx; riding the X axis collapses ly, which
    // is a different slot, so it must still fire.
    AxisFlowSolver s;
    s.setViewScale(0.1);                // 0.1 world units per pixel
    s.setIndexTolerancePx(15);
    s.setUseAxisIndexing(true);
    s.setLocalFrame(true);
    s.setRawPoint(V(100, 0.5, 0));      // 5 px off the X axis
    s.lockValueX(100.0);                // frozen by hotkey

    const IndexingResult r = s.indexing();
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::X);

    const Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 100.0);       // the frozen coordinate is untouched
    EXPECT_DOUBLE_EQ(p.y, 0.0);         // ... and the free one snapped
}

TEST(SolverIndexing, LockingYKeepsYAxisIndexingAlive)
{
    AxisFlowSolver s;
    s.setViewScale(0.1);
    s.setIndexTolerancePx(15);
    s.setUseAxisIndexing(true);
    s.setLocalFrame(true);
    s.setRawPoint(V(0.5, 100, 0));
    s.lockValueY(100.0);

    const IndexingResult r = s.indexing();
    EXPECT_TRUE(r.axisActive);
    EXPECT_EQ(r.axis, IndexAxis::Y);

    const Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 0.0);
    EXPECT_DOUBLE_EQ(p.y, 100.0);
}

TEST(SolverIndexing, ATypedXIsNotOverwrittenByAxisIndexing)
{
    // Same protection for a typed value: indexing may not zero it.
    AxisFlowSolver s;
    s.setViewScale(0.1);
    s.setIndexTolerancePx(15);
    s.setUseAxisIndexing(true);
    s.setLocalFrame(true);
    s.setRawPoint(V(0.2, 2.0, 0));      // nearer the Y axis
    s.setValueX(0.2);                   // typed, not locked

    const Base::Vector3d p = s.getSolvedPoint();
    EXPECT_DOUBLE_EQ(p.x, 0.2);         // survives
}
