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
    ClearLocks,
    CancelEntry,    // Escape: cancel value entry, then (next press) release locks
    CancelCommand,  // separate key: end the drawing command
    FinishLine,     // finish the current polyline but keep the session running
    ConfirmPoint    // Enter: place the current point (commit typed coords)
};

/**
 * Hotkeys: a replaceable Qt-key-code -> AxisAction map with sensible defaults.
 * Stored in AxisFlowService; can be edited at runtime (e.g. from Python via
 * AxisFlow.setHotkey(...)).
 */
class Hotkeys
{
public:
    Hotkeys();                        // installs defaults

    void       set(int qtKey, AxisAction a);
    void       remove(int qtKey);
    void       clear();
    AxisAction action(int qtKey) const;

    /** First Qt key code bound to @p a, or 0 if the action is unbound. */
    int keyFor(AxisAction a) const;

    /** Every Qt key code bound to @p a (an action may have several, e.g. the
     *  main Return and the numpad Enter). Empty if unbound. */
    std::vector<int> keysFor(AxisAction a) const;

    /** Bind @p a to exactly @p keys, dropping any previous binding of @p a.
     *  This is what keeps the live map and the stored settings in sync. */
    void bind(AxisAction a, const std::vector<int>& keys);

    /** Read-only view of the map (used when persisting to settings). */
    const std::map<int, AxisAction>& map() const { return _map; }

    /** Restore the built-in defaults (used by "reset settings"). */
    void setDefaults();

    /** Stable names used as settings keys, e.g. "LockX". */
    static const char* name(AxisAction a);
    static AxisAction  fromName(const std::string& s);

    /** All actions that can be bound, in a stable order (for persistence/UI). */
    static const std::vector<AxisAction>& allActions();

private:
    std::map<int, AxisAction> _map;
};

} // namespace AxisFlowGui
