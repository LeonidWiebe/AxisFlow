// Unit tests for the entry-policy helpers (AccuDraw sign-from-direction).
// Pure header, no Qt/Coin/FreeCAD -- this is exactly the logic that used to be
// buried in the panel and is now testable in isolation.

#include <gtest/gtest.h>
#include "AxisFlowEntry.h"

using namespace AxisFlowGui;

// --- bare magnitude follows the cursor direction --------------------------

TEST(Entry, BareMagnitudeTakesNegativeDirection)
{
    // Pointing down -X, typing "5" -> lock -5.
    EXPECT_DOUBLE_EQ(signedByDirection(5.0, -3.2, /*explicitSign=*/false), -5.0);
}

TEST(Entry, BareMagnitudeTakesPositiveDirection)
{
    EXPECT_DOUBLE_EQ(signedByDirection(5.0, 3.2, false), 5.0);
}

TEST(Entry, MagnitudeIsAlwaysAbsedWhenNoExplicitSign)
{
    // Even if a stray negative magnitude arrives without an explicit sign, the
    // direction decides: +dir -> +, -dir -> -.
    EXPECT_DOUBLE_EQ(signedByDirection(-5.0, 4.0, false), 5.0);
    EXPECT_DOUBLE_EQ(signedByDirection(-5.0, -4.0, false), -5.0);
}

TEST(Entry, ZeroOrPositiveDirectionYieldsPositive)
{
    // dir == 0 (cursor exactly on the perpendicular) defaults to positive.
    EXPECT_DOUBLE_EQ(signedByDirection(5.0, 0.0, false), 5.0);
}

// --- explicit sign always wins --------------------------------------------

TEST(Entry, ExplicitNegativeIsKeptAgainstPositiveDirection)
{
    // User typed "-5" while pointing +X: absolute wins -> -5.
    EXPECT_DOUBLE_EQ(signedByDirection(-5.0, 3.0, /*explicitSign=*/true), -5.0);
}

TEST(Entry, ExplicitPositiveIsKeptAgainstNegativeDirection)
{
    EXPECT_DOUBLE_EQ(signedByDirection(5.0, -3.0, true), 5.0);
}

// ---- focus advance on movement ---------------------------------------------
// After typing into an in-plane field, a DELIBERATE cursor move (beyond a
// pixel threshold) hands the caret to the adjacent field; jitter never does.

TEST(Entry, FocusAdvanceIgnoresJitter)
{
    // 1 world unit == 1 px; 4 px of drift is below the 6 px default threshold.
    EXPECT_FALSE(focusAdvanceExceeded(3.0, 2.0, 1.0));
}

TEST(Entry, FocusAdvanceFiresOnADeliberateMove)
{
    EXPECT_TRUE(focusAdvanceExceeded(5.0, 5.0, 1.0));          // ~7 px
    // The same world offset is only ~0.7 px when zoomed out 10x: no advance.
    EXPECT_FALSE(focusAdvanceExceeded(5.0, 5.0, 10.0));
}

TEST(Entry, FocusAdvanceNeedsACamera)
{
    // Headless / no camera yet (worldPerPixel 0): never fires.
    EXPECT_FALSE(focusAdvanceExceeded(1000.0, 1000.0, 0.0));
}
