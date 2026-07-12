#ifndef AXISFLOW_ENTRY_H
#define AXISFLOW_ENTRY_H

#include <cmath>

namespace AxisFlowGui {

/**
 * Entry-policy helpers -- pure, header-only, no Qt/Coin/FreeCAD dependencies, so
 * they can be unit-tested in isolation (Tier 1) while the panel just calls them.
 */

/**
 * AccuDraw sign-from-direction.
 *
 * A bare magnitude typed into a rectangular axis field takes the sign of the
 * cursor's current direction along that axis: pointing down -X and typing "5"
 * locks X = -5. A value the user signed explicitly ("-5", "+5") is used as-is.
 *
 * @param value          the parsed value (already signed if the user typed a sign)
 * @param dirComponent   the cursor's raw offset along this axis (sign is what matters)
 * @param explicitSign   true if the user typed a leading '-' or '+'
 * @return               the value to store as the axis override
 */
inline double signedByDirection(double value, double dirComponent, bool explicitSign)
{
    if (explicitSign)
        return value;                       // explicit sign always wins
    return (dirComponent < 0.0) ? -std::abs(value) : std::abs(value);
}

}  // namespace AxisFlowGui

#endif  // AXISFLOW_ENTRY_H
