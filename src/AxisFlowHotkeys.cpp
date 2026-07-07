#include "PreCompiled.h"

#include "AxisFlowHotkeys.h"

#include <QtGui/QKeyEvent>   // for Qt::Key_* codes

namespace AxisFlowGui {

Hotkeys::Hotkeys()
{
    // Defaults (MicroStation-flavoured). All reconfigurable at runtime.
    _map[Qt::Key_Return] = AxisAction::SmartLock;   // Enter: smart lock toggle
    _map[Qt::Key_Enter]  = AxisAction::SmartLock;   // numpad Enter
    _map[Qt::Key_Space]  = AxisAction::ToggleMode;  // Rect <-> Polar
    _map[Qt::Key_X]      = AxisAction::LockX;
    _map[Qt::Key_Y]      = AxisAction::LockY;
    _map[Qt::Key_Z]      = AxisAction::LockZ;
    _map[Qt::Key_Escape] = AxisAction::ClearLocks;
}

void Hotkeys::set(int qtKey, AxisAction a)
{
    _map[qtKey] = a;
}

void Hotkeys::remove(int qtKey)
{
    _map.erase(qtKey);
}

void Hotkeys::clear()
{
    _map.clear();
}

AxisAction Hotkeys::action(int qtKey) const
{
    auto it = _map.find(qtKey);
    return (it == _map.end()) ? AxisAction::None : it->second;
}

} // namespace AxisFlowGui
