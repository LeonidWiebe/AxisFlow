#ifndef AXISFLOW_KEYROUTING_H
#define AXISFLOW_KEYROUTING_H

#include "AxisFlowHotkeys.h"   // AxisAction

namespace AxisFlowGui {

/**
 * Key routing -- pure, header-only policy (no Qt/Coin/FreeCAD), so it can be
 * unit-tested in isolation (Tier 1) while every key-handling site just asks it
 * what to do.
 *
 * Why this exists: the same decision ("is this key ours, and what does it
 * mean?") used to be re-implemented in the panel's line-edit filter, in the
 * panel's keyPressEvent, in the application-wide AxisFlowEventFilter, and --
 * partially and incorrectly -- in the Coin event callback. Four copies of one
 * policy drift apart: the Coin copy never learned the keys added after it was
 * written and ignored modifiers entirely, and the panel copy would happily read
 * Ctrl+Z as the Z lock. The policy now lives here once and is covered by tests;
 * the call sites only translate Qt events into a KeyContext and execute the
 * verdict.
 *
 * Deliberately NOT decided here (it is Qt topology, not policy):
 *   - whether keyboard focus is somewhere AxisFlow may act at all
 *     (AxisFlowEventFilter::inScope);
 *   - the QEvent::ShortcutOverride dance -- the call site accepts the override
 *     for any key that routes to something other than Ignore, which is exactly
 *     what stops FreeCAD's global QActions from eating it.
 */

/** What the caller should do with a key. */
enum class KeyRoute {
    Ignore,        //!< Not ours: let Qt / the focused widget have it.
    PassToWidget,  //!< A coordinate field must edit itself; deliver normally.
    BeginEntry,    //!< First character of a FRESH entry: clear the live
                   //!< read-out in the target field, then deliver the key.
    Action,        //!< Run KeyContext::bound through the service.
    CancelEntry,   //!< Layered cancel; the panel owns the entry state.
    Backspace,     //!< Drop the last character of the entry (outside a field).
    ClearEntry,    //!< Drop the whole entry and release its override.
    TabCycle,      //!< Move the active field; never Qt's focus chain.
    StartTyping,   //!< Seed the panel's active field with this character.
    BeginChord,    //!< This key opens a two-key chord: start waiting.
    ChordHold,     //!< Auto-repeat of the held prefix: swallow, KEEP waiting.
    Swallow        //!< Claimed on purpose, but nothing to do.
};

/**
 * Everything the policy needs about one key press, in plain bools so the rule
 * stays testable without a QKeyEvent (or a running FreeCAD).
 */
struct KeyContext {
    /** Action bound to this key, or None. */
    AxisAction bound { AxisAction::None };
    /** AxisFlow is switched on. While off, only ToggleActive is ours. */
    bool serviceActive { true };
    /** Ctrl / Alt / Meta held: the key belongs to the application. */
    bool modified { false };
    /** Qt auto-repeat (the key is being held down). */
    bool autoRepeat { false };
    /** AxisFlow drives the input (its own tool), as opposed to the adapter. */
    bool ownsInput { false };
    /** The key arrived inside one of the panel's coordinate fields. */
    bool inField { false };
    /** That field is the one already being typed into (no read-out to clear). */
    bool sameEntryField { false };
    /** A character that begins or continues a number: digit, sign, separator. */
    bool entryStart { false };
    /** A key the line edit handles itself (entryStart, Backspace, Delete,
     *  arrows, Home, End). */
    bool editKey { false };
    bool tab { false };        //!< Tab or Backtab.
    bool backspace { false };
    bool del { false };
    /** The key produced a letter -- never part of a coordinate. */
    bool letter { false };

    /** A chord prefix was pressed earlier and we are waiting for the second
     *  key. While this is set, `bound` is the action of {prefix, this key}. */
    bool prefixPending { false };
    /** This key is the first key of at least one chord (and nothing is
     *  pending). By the prefix rule it can carry no single binding. */
    bool opensChord { false };

    /**
     * A value is actually being TYPED right now (the panel's _entryField).
     *
     * Not the same as "a field has focus": AxisFlow parks the caret in a
     * coordinate field after every click, so focus is almost always there and
     * would be useless as a test. Until a character is typed the field only
     * shows the live read-out, and there is nothing to erase -- which is why
     * Backspace and Delete only belong to AxisFlow while this is set. They used
     * to be claimed unconditionally, so Del could never reach FreeCAD and
     * deleting the selected object simply stopped working with the compass on.
     */
    bool entryInProgress { false };
};

/** Decide what happens to a key. Total, side-effect free, never throws. */
inline KeyRoute routeKey(const KeyContext& c)
{
    // Modified keys (Ctrl+S, Ctrl+Z, Alt+F4, ...) are always the application's.
    // Without this, Ctrl+Z inside a coordinate field resolved to the Z lock
    // instead of Undo, because only the bare key code was ever compared.
    if (c.modified)
        return KeyRoute::Ignore;

    // While AxisFlow is off exactly one key is ours: the one whose whole job is
    // to turn it back on. Otherwise the toggle is a one-way switch.
    if (!c.serviceActive)
        return (c.bound == AxisAction::ToggleActive) ? KeyRoute::Action
                                                     : KeyRoute::Ignore;

    // ---- waiting for the second key of a chord ----------------------------
    // Exactly one key press resolves the wait, whatever it is. No timeout: the
    // prefix rule makes every press decidable the moment it arrives, so nothing
    // depends on typing speed. The caller clears the pending prefix for every
    // route except ChordHold.
    if (c.prefixPending) {
        // Holding the prefix down repeats it; that is not "the second key".
        if (c.autoRepeat)
            return KeyRoute::ChordHold;
        if (c.bound != AxisAction::None)
            return KeyRoute::Action;
        // No chord for this combination: swallow the key along with the wait.
        // Letting it through instead would silently run whatever it normally
        // does -- a mistyped R,Y would freeze the Y field, which nobody asked
        // for. The user simply presses the key again.
        return KeyRoute::Swallow;
    }

    // Tab moves the ACTIVE FIELD. Letting Qt walk its focus chain instead would
    // drop the caret out of the panel, and the next digit would land in a
    // global shortcut.
    if (c.tab)
        return (c.inField || c.ownsInput) ? KeyRoute::TabCycle : KeyRoute::Ignore;

    // Holding a bound key must not re-fire it: a held X would flip its lock
    // dozens of times a second. Editing keys keep repeating normally.
    if (c.autoRepeat && c.bound != AxisAction::None)
        return KeyRoute::Swallow;

    // The layered cancel is the panel's business (it owns the entry state), so
    // it is routed apart from the plain service actions.
    if (c.bound == AxisAction::CancelEntry)
        return KeyRoute::CancelEntry;

    if (c.inField) {
        // Inside a coordinate field the NUMBER wins over any binding: a digit
        // someone bound to an action must still be typeable.
        if (c.entryStart)
            return c.sameEntryField ? KeyRoute::PassToWidget : KeyRoute::BeginEntry;
        if (c.editKey) {
            // Backspace / Delete with nothing being typed are not editing: the
            // field is showing the read-out. Hand them back so FreeCAD's Del
            // deletes the selected object. Arrows / Home / End stay with the
            // line edit -- moving a caret is harmless and expected.
            if ((c.backspace || c.del) && !c.entryInProgress)
                return KeyRoute::Ignore;
            return KeyRoute::PassToWidget;
        }
        // Chords work mid-entry, like every other action key: after typing
        // "1500" the user can still press R X.
        if (c.opensChord)
            return KeyRoute::BeginChord;
        if (c.bound != AxisAction::None)
            return KeyRoute::Action;
        // A letter cannot be part of a coordinate (the field validator rejects
        // it), so swallow it rather than let it flash and be refused.
        return c.letter ? KeyRoute::Swallow : KeyRoute::Ignore;
    }

    if (c.opensChord)
        return KeyRoute::BeginChord;

    // Outside the fields a binding is the whole point of the key map.
    if (c.bound != AxisAction::None)
        return KeyRoute::Action;

    // Raw editing keys are claimed only by AxisFlow's own tool. In the Draft
    // adapter they belong to Draft's own coordinate fields.
    if (!c.ownsInput)
        return KeyRoute::Ignore;

    // Same rule outside the fields: only claim them while there is an entry to
    // trim or clear. clearEntry()/backspaceEntry() already refuse when there is
    // none -- but by then the key has been swallowed, which is the bug.
    if (c.backspace)  return c.entryInProgress ? KeyRoute::Backspace : KeyRoute::Ignore;
    if (c.del)        return c.entryInProgress ? KeyRoute::ClearEntry : KeyRoute::Ignore;
    if (c.entryStart) return KeyRoute::StartTyping;

    return KeyRoute::Ignore;
}

/**
 * Does this route consume a pending chord prefix?
 *
 * Every route but ChordHold does: the wait lasts exactly one key press, so the
 * caller clears the prefix unless the user is merely holding it down.
 */
inline bool routeConsumesPrefix(KeyRoute r)
{
    return r != KeyRoute::ChordHold;
}

/**
 * True when AxisFlow consumes the key. This is what the application-wide filter
 * accepts QEvent::ShortcutOverride for, so FreeCAD's global QAction never fires
 * and the key comes back as an ordinary KeyPress.
 */
inline bool routeClaimsKey(KeyRoute r)
{
    return r != KeyRoute::Ignore;
}

}  // namespace AxisFlowGui

#endif  // AXISFLOW_KEYROUTING_H
