// Unit tests for the Hotkeys map -- pure std, no Qt runtime (only the
// compile-time Qt::Key_* enum constants).

#include <gtest/gtest.h>
#include <QtCore/qnamespace.h>

#include "AxisFlowHotkeys.h"

using namespace AxisFlowGui;

TEST(Hotkeys, DefaultsMatchTheDocumentedLayout)
{
    Hotkeys h;   // ctor installs defaults (mirrors hotkeys.ini)
    EXPECT_EQ(h.action(Qt::Key_Insert),    AxisAction::None);           // unbound now
    EXPECT_EQ(h.action(Qt::Key_Space),     AxisAction::ToggleMode);
    EXPECT_EQ(h.action(Qt::Key_P),         AxisAction::ConfirmPoint);
    EXPECT_EQ(h.action(Qt::Key_Return),    AxisAction::SmartLock);
    EXPECT_EQ(h.action(Qt::Key_Enter),     AxisAction::SmartLock);      // numpad
    EXPECT_EQ(h.action(Qt::Key_X),         AxisAction::LockX);
    EXPECT_EQ(h.action(Qt::Key_Y),         AxisAction::LockY);
    EXPECT_EQ(h.action(Qt::Key_Z),         AxisAction::LockZ);
    EXPECT_EQ(h.action(Qt::Key_D),         AxisAction::LockD);          // polar distance
    EXPECT_EQ(h.action(Qt::Key_O),         AxisAction::SetOrigin);
    EXPECT_EQ(h.action(Qt::Key_T),         AxisAction::RotateTop);
    EXPECT_EQ(h.action(Qt::Key_F),         AxisAction::RotateFront);
    EXPECT_EQ(h.action(Qt::Key_S),         AxisAction::RotateSide);
    EXPECT_EQ(h.action(Qt::Key_V),         AxisAction::RotateByView);
    EXPECT_EQ(h.action(Qt::Key_E),         AxisAction::RotateCycle);
    EXPECT_EQ(h.action(Qt::Key_Escape),    AxisAction::FinishLine);
    EXPECT_EQ(h.action(Qt::Key_QuoteLeft), AxisAction::CancelCommand);  // backtick
    EXPECT_EQ(h.action(Qt::Key_R),         AxisAction::None);           // chord prefix
}

TEST(Hotkeys, SetAndRemove)
{
    Hotkeys h;
    h.set(KeyChord{Qt::Key_F2, 0}, AxisAction::LockZ);
    EXPECT_EQ(h.action(Qt::Key_F2), AxisAction::LockZ);
    h.remove(KeyChord{Qt::Key_F2, 0});
    EXPECT_EQ(h.action(Qt::Key_F2), AxisAction::None);
}

TEST(Hotkeys, SetNoneUnbinds)
{
    Hotkeys h;
    h.set(KeyChord{Qt::Key_X, 0}, AxisAction::None);   // documented: None erases
    EXPECT_EQ(h.action(Qt::Key_X), AxisAction::None);
}

TEST(Hotkeys, SmartLockHasBothReturnAndEnterByDefault)
{
    Hotkeys h;
    EXPECT_EQ(h.chordsFor(AxisAction::SmartLock).size(), 2u);
}

TEST(Hotkeys, BindReplacesEveryPreviousKeyOfTheAction)
{
    Hotkeys h;
    ASSERT_TRUE(h.bind(AxisAction::SmartLock, { KeyChord{Qt::Key_F5, 0} }));
    const std::vector<KeyChord> keys = h.chordsFor(AxisAction::SmartLock);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0].first, static_cast<int>(Qt::Key_F5));
    EXPECT_FALSE(keys[0].isChord());
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

// ---- two-key chords -------------------------------------------------------

TEST(Hotkeys, DefaultChordsRotateAboutEachAxis)
{
    Hotkeys hk;
    EXPECT_EQ(hk.action(KeyChord{Qt::Key_R, Qt::Key_X}), AxisAction::RotateAboutX);
    EXPECT_EQ(hk.action(KeyChord{Qt::Key_R, Qt::Key_Y}), AxisAction::RotateAboutY);
    EXPECT_EQ(hk.action(KeyChord{Qt::Key_R, Qt::Key_Z}), AxisAction::RotateAboutZ);
}

TEST(Hotkeys, TheChordPrefixIsNotASingleKey)
{
    Hotkeys hk;
    EXPECT_TRUE(hk.isPrefix(Qt::Key_R));
    EXPECT_EQ(hk.action(Qt::Key_R), AxisAction::None);
}

TEST(Hotkeys, PlainXKeepsItsOwnMeaningDespiteTheChord)
{
    // R X exists, but X on its own is still the X lock: only the FIRST key of a
    // chord is reserved.
    Hotkeys hk;
    EXPECT_EQ(hk.action(Qt::Key_X), AxisAction::LockX);
    EXPECT_FALSE(hk.isPrefix(Qt::Key_X));
}

TEST(Hotkeys, ContinuationsListEverySecondKeyOfThePrefix)
{
    Hotkeys hk;
    const auto opts = hk.continuations(Qt::Key_R);
    ASSERT_EQ(opts.size(), 3u);
    EXPECT_EQ(opts[0].first, Qt::Key_X);
    EXPECT_EQ(opts[0].second, AxisAction::RotateAboutX);
    EXPECT_EQ(opts[2].second, AxisAction::RotateAboutZ);
    EXPECT_TRUE(hk.continuations(Qt::Key_T).empty());
}

TEST(Hotkeys, BindingThePrefixAsASingleKeyIsRefused)
{
    // The rule that makes chords resolvable without a timeout.
    Hotkeys hk;
    EXPECT_FALSE(hk.canBind(AxisAction::SetOrigin, { KeyChord{Qt::Key_R, 0} }));
    EXPECT_FALSE(hk.bind(AxisAction::SetOrigin, { KeyChord{Qt::Key_R, 0} }));
    EXPECT_EQ(hk.action(Qt::Key_R), AxisAction::None);          // unchanged
    EXPECT_EQ(hk.chordFor(AxisAction::SetOrigin).first, Qt::Key_O);
}

TEST(Hotkeys, ChordOnAKeyThatIsAlreadyASingleBindingIsRefused)
{
    // T is RotateTop, so T-something cannot be created without freeing T first.
    Hotkeys hk;
    EXPECT_FALSE(hk.bind(AxisAction::ClearLocks, { KeyChord{Qt::Key_T, Qt::Key_X} }));
    EXPECT_EQ(hk.action(Qt::Key_T), AxisAction::RotateTop);
}

TEST(Hotkeys, FreeingTheSingleKeyFirstThenTheChordIsAccepted)
{
    Hotkeys hk;
    EXPECT_TRUE(hk.bind(AxisAction::RotateTop, {}));            // release T
    EXPECT_TRUE(hk.bind(AxisAction::ClearLocks, { KeyChord{Qt::Key_T, Qt::Key_X} }));
    EXPECT_TRUE(hk.isPrefix(Qt::Key_T));
    EXPECT_EQ(hk.action(KeyChord{Qt::Key_T, Qt::Key_X}), AxisAction::ClearLocks);
}

TEST(Hotkeys, RebindingAnActionOntoItsOwnPrefixKeyIsRefused)
{
    // RotateAboutX owns R X; giving it plain R would make R both.
    Hotkeys hk;
    EXPECT_FALSE(hk.bind(AxisAction::RotateAboutX, { KeyChord{Qt::Key_R, 0} }));
}

TEST(Hotkeys, ReplacingAllChordsFreesThePrefix)
{
    Hotkeys hk;
    EXPECT_TRUE(hk.bind(AxisAction::RotateAboutX, {}));
    EXPECT_TRUE(hk.bind(AxisAction::RotateAboutY, {}));
    EXPECT_TRUE(hk.bind(AxisAction::RotateAboutZ, {}));
    EXPECT_FALSE(hk.isPrefix(Qt::Key_R));
    EXPECT_TRUE(hk.bind(AxisAction::SetOrigin, { KeyChord{Qt::Key_R, 0} }));
    EXPECT_EQ(hk.action(Qt::Key_R), AxisAction::SetOrigin);
}

TEST(Hotkeys, ChordNamesRoundTrip)
{
    for (AxisAction a : { AxisAction::RotateAboutX, AxisAction::RotateAboutY,
                          AxisAction::RotateAboutZ }) {
        EXPECT_EQ(Hotkeys::fromName(Hotkeys::name(a)), a);
    }
}

// ---- consumer-defined mode toggle -----------------------------------------

TEST(Hotkeys, SegmentModeToggleShipsOnM)
{
    // M, like Sketcher's polyline tool, where the same key cycles the mode of
    // the next segment.
    Hotkeys h;
    EXPECT_EQ(h.action(Qt::Key_M), AxisAction::ToggleSegmentMode);
}

TEST(Hotkeys, SegmentModeToggleNameRoundTrips)
{
    EXPECT_EQ(Hotkeys::fromName(Hotkeys::name(AxisAction::ToggleSegmentMode)),
              AxisAction::ToggleSegmentMode);
}
