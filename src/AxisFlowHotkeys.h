#pragma once

#include <map>

namespace AxisFlowGui {

/**
 * Semantic AxisFlow actions that keys can be bound to. Keeping keys mapped to
 * actions (instead of hard-coding key handling) is what makes the hotkeys
 * reconfigurable: the UI just reports a key, the service resolves the action.
 */
enum class AxisAction {
    None,
    ToggleMode,
    SmartLock,
    LockX,
    LockY,
    LockZ,
    ClearLocks
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

private:
    std::map<int, AxisAction> _map;
};

} // namespace AxisFlowGui
