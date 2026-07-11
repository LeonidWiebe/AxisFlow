// Unit tests for the Hotkeys map -- pure std, no Qt runtime (only the
// compile-time Qt::Key_* enum constants).

#include <gtest/gtest.h>
#include <QtCore/qnamespace.h>

#include "AxisFlowHotkeys.h"

using namespace AxisFlowGui;

TEST(Hotkeys, DefaultsMatchTheDocumentedLayout)
{
    Hotkeys h;   // ctor installs defaults
    EXPECT_EQ(h.action(Qt::Key_Insert),    AxisAction::ToggleActive);
    EXPECT_EQ(h.action(Qt::Key_Space),     AxisAction::ToggleMode);
    EXPECT_EQ(h.action(Qt::Key_Return),    AxisAction::ConfirmPoint);
    EXPECT_EQ(h.action(Qt::Key_Enter),     AxisAction::ConfirmPoint);   // numpad
    EXPECT_EQ(h.action(Qt::Key_X),         AxisAction::LockX);
    EXPECT_EQ(h.action(Qt::Key_Y),         AxisAction::LockY);
    EXPECT_EQ(h.action(Qt::Key_Z),         AxisAction::LockZ);
    EXPECT_EQ(h.action(Qt::Key_Escape),    AxisAction::FinishLine);     // new default
    EXPECT_EQ(h.action(Qt::Key_QuoteLeft), AxisAction::CancelCommand);  // backtick
}

TEST(Hotkeys, SetAndRemove)
{
    Hotkeys h;
    h.set(Qt::Key_F2, AxisAction::LockZ);
    EXPECT_EQ(h.action(Qt::Key_F2), AxisAction::LockZ);
    h.remove(Qt::Key_F2);
    EXPECT_EQ(h.action(Qt::Key_F2), AxisAction::None);
}

TEST(Hotkeys, SetNoneUnbinds)
{
    Hotkeys h;
    h.set(Qt::Key_X, AxisAction::None);       // documented: None erases
    EXPECT_EQ(h.action(Qt::Key_X), AxisAction::None);
}

TEST(Hotkeys, ConfirmPointHasBothReturnAndEnterByDefault)
{
    Hotkeys h;
    const std::vector<int> keys = h.keysFor(AxisAction::ConfirmPoint);
    EXPECT_EQ(keys.size(), 2u);
}

TEST(Hotkeys, BindReplacesEveryPreviousKeyOfTheAction)
{
    Hotkeys h;
    h.bind(AxisAction::ConfirmPoint, {Qt::Key_F5});
    const std::vector<int> keys = h.keysFor(AxisAction::ConfirmPoint);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], static_cast<int>(Qt::Key_F5));
    // The old Return / Enter bindings are gone.
    EXPECT_EQ(h.action(Qt::Key_Return), AxisAction::None);
    EXPECT_EQ(h.action(Qt::Key_Enter),  AxisAction::None);
}

TEST(Hotkeys, NameRoundTripsForEveryAction)
{
    for (AxisAction a : Hotkeys::allActions()) {
        EXPECT_EQ(Hotkeys::fromName(Hotkeys::name(a)), a) << "action " << Hotkeys::name(a);
    }
}

TEST(Hotkeys, FinishLineIsAKnownName)
{
    EXPECT_EQ(Hotkeys::fromName("FinishLine"), AxisAction::FinishLine);
    EXPECT_STREQ(Hotkeys::name(AxisAction::FinishLine), "FinishLine");
}

TEST(Hotkeys, SetDefaultsRestoresLayout)
{
    Hotkeys h;
    h.clear();
    EXPECT_EQ(h.action(Qt::Key_X), AxisAction::None);
    h.setDefaults();
    EXPECT_EQ(h.action(Qt::Key_X), AxisAction::LockX);
}
