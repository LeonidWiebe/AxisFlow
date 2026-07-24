// Unit tests for key routing -- the single decision point for "is this key
// ours, and what does it mean?" (AxisFlowKeyRouting.h).
// Pure header policy: no Qt, no Coin, no FreeCAD runtime.

#include <gtest/gtest.h>

#include "AxisFlowKeyRouting.h"

using namespace AxisFlowGui;

namespace {

// A key arriving in the 3-D view while AxisFlow's own tool is running: the
// common case. Tests override only what they exercise.
KeyContext own(AxisAction bound = AxisAction::None)
{
    KeyContext c;
    c.bound         = bound;
    c.serviceActive = true;
    c.ownsInput     = true;
    return c;
}

// The same key arriving inside one of the panel's coordinate fields.
KeyContext inField(AxisAction bound = AxisAction::None)
{
    KeyContext c = own(bound);
    c.inField = true;
    return c;
}

// A bare digit ("5"): entry character, and the line edit handles it too.
void makeDigit(KeyContext& c)
{
    c.entryStart = true;
    c.editKey    = true;
}

} // namespace

// ---- modifiers ------------------------------------------------------------

TEST(KeyRouting, ModifiedKeysAlwaysBelongToTheApplication)
{
    KeyContext c = own(AxisAction::LockZ);
    c.modified = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

TEST(KeyRouting, CtrlZInAFieldIsUndoNotTheZLock)
{
    // Regression: the field filter compared bare key codes, so Ctrl+Z fired
    // LockZ and Undo never reached FreeCAD.
    KeyContext c = inField(AxisAction::LockZ);
    c.modified = true;
    c.letter   = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

TEST(KeyRouting, ModifiedDigitIsNotAnEntry)
{
    KeyContext c = own();
    makeDigit(c);
    c.modified = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

// ---- the off state --------------------------------------------------------

TEST(KeyRouting, WhileOffOnlyToggleActiveIsClaimed)
{
    KeyContext c = own(AxisAction::ToggleActive);
    c.serviceActive = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Action);
}

TEST(KeyRouting, WhileOffEveryOtherActionIsIgnored)
{
    KeyContext c = own(AxisAction::LockX);
    c.serviceActive = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

TEST(KeyRouting, WhileOffDigitsAreNotClaimed)
{
    KeyContext c = own();
    makeDigit(c);
    c.serviceActive = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

// ---- Tab ------------------------------------------------------------------

TEST(KeyRouting, TabCyclesTheActiveFieldFromTheView)
{
    KeyContext c = own();
    c.tab = true;
    EXPECT_EQ(routeKey(c), KeyRoute::TabCycle);
}

TEST(KeyRouting, TabCyclesTheActiveFieldFromAField)
{
    KeyContext c = inField();
    c.tab = true;
    c.ownsInput = false;          // even when the adapter owns the input
    EXPECT_EQ(routeKey(c), KeyRoute::TabCycle);
}

TEST(KeyRouting, TabOutsideOurInputIsLeftAlone)
{
    KeyContext c = own();
    c.tab = true;
    c.ownsInput = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

// ---- auto-repeat ----------------------------------------------------------

TEST(KeyRouting, HeldActionKeyDoesNotRefire)
{
    KeyContext c = own(AxisAction::LockX);
    c.autoRepeat = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Swallow);
}

TEST(KeyRouting, HeldDigitStillTypesInAField)
{
    KeyContext c = inField();
    makeDigit(c);
    c.autoRepeat = true;
    EXPECT_EQ(routeKey(c), KeyRoute::BeginEntry);
}

TEST(KeyRouting, HeldBackspaceStillDeletes)
{
    KeyContext c = own();
    c.backspace = true;
    c.editKey   = true;
    c.autoRepeat = true;
    c.entryInProgress = true;   // holding it down to erase what was typed
    EXPECT_EQ(routeKey(c), KeyRoute::Backspace);
}

// ---- layered cancel -------------------------------------------------------

TEST(KeyRouting, CancelEntryIsRoutedToThePanelEverywhere)
{
    EXPECT_EQ(routeKey(own(AxisAction::CancelEntry)), KeyRoute::CancelEntry);
    EXPECT_EQ(routeKey(inField(AxisAction::CancelEntry)), KeyRoute::CancelEntry);
}

TEST(KeyRouting, CancelEntryOutranksTheFieldEditor)
{
    // Bound to a key that also edits (e.g. Delete): the cancel wins.
    KeyContext c = inField(AxisAction::CancelEntry);
    c.editKey = true;
    c.del     = true;
    EXPECT_EQ(routeKey(c), KeyRoute::CancelEntry);
}

// ---- inside a coordinate field --------------------------------------------

TEST(KeyRouting, FirstDigitInAFreshFieldReplacesTheReadout)
{
    KeyContext c = inField();
    makeDigit(c);
    c.sameEntryField = false;
    EXPECT_EQ(routeKey(c), KeyRoute::BeginEntry);
}

TEST(KeyRouting, FurtherDigitsAppendToTheEntry)
{
    KeyContext c = inField();
    makeDigit(c);
    c.sameEntryField = true;
    EXPECT_EQ(routeKey(c), KeyRoute::PassToWidget);
}

TEST(KeyRouting, EditingKeysGoToTheLineEdit)
{
    KeyContext c = inField();
    c.editKey   = true;
    c.backspace = true;
    c.entryInProgress = true;   // there IS something to edit
    EXPECT_EQ(routeKey(c), KeyRoute::PassToWidget);
}

TEST(KeyRouting, ADigitBoundToAnActionStillTypes)
{
    // The number always wins inside a field, whatever the key map says.
    KeyContext c = inField(AxisAction::RotateTop);
    makeDigit(c);
    EXPECT_EQ(routeKey(c), KeyRoute::BeginEntry);
}

TEST(KeyRouting, BoundLetterActsMidEntry)
{
    // After typing "1500" the X key must still lock X.
    KeyContext c = inField(AxisAction::LockX);
    c.letter = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Action);
}

TEST(KeyRouting, UnboundLetterIsSwallowedInAField)
{
    KeyContext c = inField();
    c.letter = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Swallow);
}

TEST(KeyRouting, UnknownNonLetterInAFieldIsLeftToQt)
{
    KeyContext c = inField();   // e.g. F5, PageDown
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

// ---- outside the fields (3-D view) ----------------------------------------

TEST(KeyRouting, BoundKeyRunsItsAction)
{
    EXPECT_EQ(routeKey(own(AxisAction::SmartLock)), KeyRoute::Action);
}

TEST(KeyRouting, DigitStartsTypingInTheView)
{
    KeyContext c = own();
    makeDigit(c);
    EXPECT_EQ(routeKey(c), KeyRoute::StartTyping);
}

TEST(KeyRouting, SignStartsTypingToo)
{
    KeyContext c = own();
    c.entryStart = true;   // '+' or '-'
    c.editKey    = true;
    EXPECT_EQ(routeKey(c), KeyRoute::StartTyping);
}

TEST(KeyRouting, BackspaceTrimsTheEntry)
{
    KeyContext c = own();
    c.backspace = true;
    c.editKey   = true;
    c.entryInProgress = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Backspace);
}

TEST(KeyRouting, DeleteClearsTheEntry)
{
    KeyContext c = own();
    c.del     = true;
    c.editKey = true;
    c.entryInProgress = true;
    EXPECT_EQ(routeKey(c), KeyRoute::ClearEntry);
}

TEST(KeyRouting, UnboundLetterInTheViewGoesToFreeCAD)
{
    KeyContext c = own();
    c.letter = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

// ---- the Draft adapter ----------------------------------------------------

TEST(KeyRouting, AdapterLeavesDigitsToDraft)
{
    KeyContext c = own();
    makeDigit(c);
    c.ownsInput = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

TEST(KeyRouting, AdapterLeavesBackspaceAndDeleteToDraft)
{
    KeyContext c = own();
    c.ownsInput = false;
    c.backspace = true;
    c.editKey   = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);

    KeyContext d = own();
    d.ownsInput = false;
    d.del       = true;
    d.editKey   = true;
    EXPECT_EQ(routeKey(d), KeyRoute::Ignore);
}

TEST(KeyRouting, AdapterStillRunsBoundActions)
{
    // The adapter unbinds what Draft needs; whatever stays bound is still ours.
    KeyContext c = own(AxisAction::ToggleMode);
    c.ownsInput = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Action);
}

// ---- the claim predicate --------------------------------------------------

TEST(KeyRouting, EveryRouteButIgnoreClaimsTheKey)
{
    EXPECT_FALSE(routeClaimsKey(KeyRoute::Ignore));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::PassToWidget));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::BeginEntry));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::Action));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::CancelEntry));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::Backspace));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::ClearEntry));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::TabCycle));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::StartTyping));
    EXPECT_TRUE(routeClaimsKey(KeyRoute::Swallow));
}

// ---- two-key chords -------------------------------------------------------

namespace {
// A prefix key press (nothing pending yet).
KeyContext prefix()
{
    KeyContext c = own();
    c.opensChord = true;
    c.letter     = true;      // R
    return c;
}
// The second key of a chord: something IS pending, and the caller has already
// resolved {prefix, this key} into `bound`.
KeyContext second(AxisAction resolved)
{
    KeyContext c = own(resolved);
    c.prefixPending = true;
    c.letter        = true;
    return c;
}
} // namespace

TEST(KeyRouting, PrefixKeyOpensTheChord)
{
    EXPECT_EQ(routeKey(prefix()), KeyRoute::BeginChord);
}

TEST(KeyRouting, PrefixOpensTheChordInsideAFieldToo)
{
    // Chords work mid-entry, like every other action key.
    KeyContext c = prefix();
    c.inField = true;
    EXPECT_EQ(routeKey(c), KeyRoute::BeginChord);
}

TEST(KeyRouting, DigitStillTypesEvenIfItCouldOpenAChord)
{
    // The number always wins inside a field.
    KeyContext c = prefix();
    c.inField = true;
    c.letter  = false;
    makeDigit(c);
    EXPECT_EQ(routeKey(c), KeyRoute::BeginEntry);
}

TEST(KeyRouting, SecondKeyRunsTheChordAction)
{
    EXPECT_EQ(routeKey(second(AxisAction::RotateAboutX)), KeyRoute::Action);
}

TEST(KeyRouting, UnknownSecondKeyIsSwallowedWithTheWait)
{
    // Decision: swallow rather than let the key do its normal job -- a mistyped
    // R,Y must not silently freeze the Y field.
    KeyContext c = second(AxisAction::None);
    EXPECT_EQ(routeKey(c), KeyRoute::Swallow);
}

TEST(KeyRouting, PendingChordSwallowsAKeyThatIsNormallyBound)
{
    // While waiting, `bound` is the CHORD's action; the single-key meaning of
    // the second key is deliberately not consulted by the router.
    KeyContext c = second(AxisAction::None);
    c.opensChord = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Swallow);
    EXPECT_TRUE(routeClaimsKey(routeKey(c)));   // never leaks to FreeCAD
}

TEST(KeyRouting, PendingChordClaimsDigitsToo)
{
    // Otherwise the second key would be resolved as a camera shortcut.
    KeyContext c = second(AxisAction::None);
    c.letter = false;
    makeDigit(c);
    EXPECT_TRUE(routeClaimsKey(routeKey(c)));
}

TEST(KeyRouting, EscapeCancelsAPendingChordInsteadOfFinishingTheLine)
{
    // Escape defaults to FinishLine, but there is no R+Escape chord, so the
    // press is consumed by the cancelled wait.
    KeyContext c = second(AxisAction::None);
    c.letter = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Swallow);
}

TEST(KeyRouting, HoldingThePrefixKeepsTheWait)
{
    KeyContext c = second(AxisAction::None);
    c.autoRepeat = true;
    EXPECT_EQ(routeKey(c), KeyRoute::ChordHold);
    EXPECT_FALSE(routeConsumesPrefix(KeyRoute::ChordHold));
}

TEST(KeyRouting, EveryOtherRouteEndsTheWait)
{
    EXPECT_TRUE(routeConsumesPrefix(KeyRoute::Action));
    EXPECT_TRUE(routeConsumesPrefix(KeyRoute::Swallow));
    EXPECT_TRUE(routeConsumesPrefix(KeyRoute::Ignore));
    EXPECT_TRUE(routeConsumesPrefix(KeyRoute::BeginEntry));
}

TEST(KeyRouting, ModifiedKeyDuringAChordGoesToTheApplicationAndEndsTheWait)
{
    KeyContext c = second(AxisAction::None);
    c.modified = true;
    const KeyRoute r = routeKey(c);
    EXPECT_EQ(r, KeyRoute::Ignore);          // Ctrl+S still saves
    EXPECT_TRUE(routeConsumesPrefix(r));     // ... and the chord is dropped
}

TEST(KeyRouting, ChordsDoNotOpenWhileAxisFlowIsOff)
{
    KeyContext c = prefix();
    c.serviceActive = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

// ---- Delete / Backspace belong to AxisFlow only while typing ---------------
// Regression: they were claimed unconditionally, so Del could never reach
// FreeCAD and deleting the selected object stopped working with the compass on.
// Focus is NOT the test -- AxisFlow parks the caret in a field after every
// click, so a field almost always has it.

TEST(KeyRouting, DeleteWithNothingTypedGoesToFreeCAD)
{
    KeyContext c = own();
    c.del     = true;
    c.editKey = true;
    c.entryInProgress = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
    EXPECT_FALSE(routeClaimsKey(routeKey(c)));
}

TEST(KeyRouting, DeleteWhileTypingClearsTheEntry)
{
    KeyContext c = own();
    c.del     = true;
    c.editKey = true;
    c.entryInProgress = true;
    EXPECT_EQ(routeKey(c), KeyRoute::ClearEntry);
}

TEST(KeyRouting, BackspaceWithNothingTypedGoesToFreeCAD)
{
    KeyContext c = own();
    c.backspace = true;
    c.editKey   = true;
    c.entryInProgress = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

TEST(KeyRouting, BackspaceWhileTypingTrimsTheEntry)
{
    KeyContext c = own();
    c.backspace = true;
    c.editKey   = true;
    c.entryInProgress = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Backspace);
}

TEST(KeyRouting, DeleteInAFieldWithNothingTypedIsNotOurs)
{
    // The caret is in the field, but the field is only showing the read-out.
    KeyContext c = inField();
    c.del     = true;
    c.editKey = true;
    c.entryInProgress = false;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}

TEST(KeyRouting, DeleteInAFieldWhileTypingEditsTheField)
{
    KeyContext c = inField();
    c.del     = true;
    c.editKey = true;
    c.entryInProgress = true;
    EXPECT_EQ(routeKey(c), KeyRoute::PassToWidget);
}

TEST(KeyRouting, CaretMovementStaysWithTheFieldEitherWay)
{
    // Arrows / Home / End are harmless and belong to the line edit whether or
    // not anything is being typed.
    KeyContext c = inField();
    c.editKey = true;              // e.g. Home
    c.entryInProgress = false;
    EXPECT_EQ(routeKey(c), KeyRoute::PassToWidget);
}

TEST(KeyRouting, AdapterStillLeavesEraserKeysToDraft)
{
    // Unchanged: in adapter mode they were never ours to begin with.
    KeyContext c = own();
    c.ownsInput = false;
    c.del = true; c.editKey = true;
    c.entryInProgress = true;
    EXPECT_EQ(routeKey(c), KeyRoute::Ignore);
}
