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

/**
 * Focus advance on movement ("the X is typed, now the Y").
 *
 * After the user types a value into an in-plane field, a DELIBERATE cursor
 * move signals that the entry is done and the next coordinate is wanted, so
 * the caret advances to the adjacent field. This predicate decides whether the
 * cursor has moved far enough since the last keystroke: the anchor-to-now
 * offset must exceed a pixel threshold, so mouse jitter while typing never
 * steals the entry.
 *
 * @param dx, dy          local-plane offset from the keystroke anchor to now
 * @param worldPerPixel   camera scale; <= 0 (no camera / headless) never fires
 * @param thresholdPx     deliberate-move threshold in screen pixels
 * @return                true when the move is deliberate and focus may advance
 */
inline bool focusAdvanceExceeded(double dx, double dy, double worldPerPixel,
                                 int thresholdPx = 6)
{
    if (worldPerPixel <= 0.0)
        return false;
    const double px = std::sqrt(dx * dx + dy * dy) / worldPerPixel;
    return px > static_cast<double>(thresholdPx);
}

}  // namespace AxisFlowGui

#endif  // AXISFLOW_ENTRY_H
