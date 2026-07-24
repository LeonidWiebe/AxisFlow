#pragma once

#include <map>
#include <string>
#include <vector>

namespace AxisFlowGui {

/**
 * Semantic AxisFlow actions that keys can be bound to. Keeping keys mapped to
 * actions (instead of hard-coding key handling) is what makes the hotkeys
 * reconfigurable: the UI just reports a key, the service resolves the action.
 */
enum class AxisAction {
    None,
    ToggleActive,   // enable/disable AxisFlow (the only globally-handled action)
    ToggleMode,
    SmartLock,
    LockX,
    LockY,
    LockZ,
    LockD,          // polar mode: freeze the Distance field (rect: no-op)
    SetOrigin,      // move the compass origin to the cursor -- NOT a point
    RotateTop,      // compass = global axes
    RotateFront,    // compass Z -> global -Y, compass Y -> global +Z
    RotateSide,     // compass Z -> global +X, compass Y -> global +Z
    RotateByView,   // compass follows the current camera orientation
    RotateCycle,    // Top -> Front -> Side -> Top ...
    RotateAboutX,   // turn the compass 90 deg about its own X axis (chord: R X)
    RotateAboutY,   // ... about its own Y axis (chord: R Y)
    RotateAboutZ,   // ... about its own Z axis (chord: R Z)
    /** Consumer-defined: AxisFlow only forwards it. The line demo uses it to
     *  switch the next segment between a straight line and an arc. The core
     *  knows nothing about arcs -- it just relays the keypress. */
    ToggleSegmentMode,
    ClearLocks,
    CancelEntry,    // Escape: cancel value entry, then (next press) release locks
    CancelCommand,  // separate key: end the drawing command
    FinishLine,     // finish the current polyline but keep the session running
    ConfirmPoint    // Enter: place the current point (commit typed coords)
};

/**
 * A key binding: one key, or a two-key chord (MicroStation style, e.g. R then
 * X). second == 0 means a plain single key.
 *
 * Chords are limited to TWO keys on purpose: three-key sequences would need a
 * timeout to stay unambiguous, and a timeout makes behaviour depend on typing
 * speed. Two keys resolve deterministically -- see the prefix rule below.
 */
struct KeyChord {
    int first  { 0 };
    int second { 0 };

    bool isChord() const { return second != 0; }
    bool valid()   const { return first != 0; }

    bool operator==(const KeyChord& o) const
    { return first == o.first && second == o.second; }
    bool operator<(const KeyChord& o) const
    { return (first != o.first) ? (first < o.first) : (second < o.second); }
};

/**
 * Hotkeys: a replaceable KeyChord -> AxisAction map with sensible defaults.
 * Stored in AxisFlowService; can be edited at runtime (e.g. from Python via
 * AxisFlow.setHotkeys(...)).
 *
 * THE PREFIX RULE. A key is either a single binding or the first key of a
 * chord -- never both. If R both did something on its own and opened a chord,
 * pressing R could only be resolved by waiting to see whether a second key
 * arrives, i.e. by a timeout. Forbidding the overlap keeps every press
 * decidable at the moment it happens: R always waits, X always locks. bind()
 * enforces this and refuses a violating binding.
 */
class Hotkeys
{
public:
    Hotkeys();                        // installs defaults

    void       set(KeyChord c, AxisAction a);
    void       remove(KeyChord c);
    void       clear();

    AxisAction action(KeyChord c) const;
    /** Convenience for the common single-key lookup. */
    AxisAction action(int qtKey) const { return action(KeyChord{qtKey, 0}); }

    /** True if @p qtKey opens a chord (is the first key of at least one). By
     *  the prefix rule such a key never carries a single binding. */
    bool isPrefix(int qtKey) const;

    /** Every second key that can follow @p prefix, with the action it runs,
     *  ordered by key code. This is what the panel shows while waiting. */
    std::vector<std::pair<int, AxisAction>> continuations(int prefix) const;

    /** First binding of @p a, or an invalid chord when unbound. */
    KeyChord chordFor(AxisAction a) const;

    /** Every binding of @p a (an action may own several, e.g. ConfirmPoint on
     *  both Return and the numpad Enter). Empty if unbound. */
    std::vector<KeyChord> chordsFor(AxisAction a) const;

    /**
     * Bind @p a to exactly @p keys, dropping its previous bindings.
     *
     * @return false (and changes nothing) when the result would break the
     *         prefix rule -- e.g. binding R while R X exists, or binding R X
     *         while R is a single key elsewhere.
     */
    bool bind(AxisAction a, const std::vector<KeyChord>& keys);

    /** Would bind() succeed? Same check, without touching the map, so the UI
     *  can warn before asking the user to confirm. */
    bool canBind(AxisAction a, const std::vector<KeyChord>& keys) const;

    /** Read-only view of the map (used when persisting to settings). */
    const std::map<KeyChord, AxisAction>& map() const { return _map; }

    /** Restore the built-in defaults (used by "reset settings"). */
    void setDefaults();

    /** Stable names used as settings keys, e.g. "LockX". */
    static const char* name(AxisAction a);
    static AxisAction  fromName(const std::string& s);

    /** All actions that can be bound, in a stable order (for persistence/UI). */
    static const std::vector<AxisAction>& allActions();

private:
    /** The prefix rule, checked against an arbitrary candidate map. */
    static bool consistent(const std::map<KeyChord, AxisAction>& m);

    std::map<KeyChord, AxisAction> _map;
};

} // namespace AxisFlowGui
