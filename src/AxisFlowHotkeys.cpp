#include "PreCompiled.h"

#include "AxisFlowHotkeys.h"

#include <QtGui/QKeyEvent>   // for Qt::Key_* codes

namespace AxisFlowGui {

Hotkeys::Hotkeys()
{
    setDefaults();
}

void Hotkeys::setDefaults()
{
    _map.clear();

    // AxisFlow owns the keyboard inside its OWN tools (the Coin event callback
    // is the key source there), so the full AccuDraw-flavoured layout is the
    // default. The optional Draft adapter unbinds what Draft needs (X/Y/Z are
    // Draft's RestrictX/Y/Z, Enter/Escape are its commit/abort) by calling
    // setHotkey(code, 'none') at install time.
    // Mirrors hotkeys.ini (that file is layer 2 on top of this fallback).
    _map[Qt::Key_Space]  = AxisAction::ToggleMode;   // ortho <-> radial
    _map[Qt::Key_P]      = AxisAction::ConfirmPoint; // place the point
    _map[Qt::Key_Return] = AxisAction::SmartLock;    // constrain to the nearest axis
    _map[Qt::Key_Enter]  = AxisAction::SmartLock;    // numpad Enter
    _map[Qt::Key_X]      = AxisAction::LockX;
    _map[Qt::Key_Y]      = AxisAction::LockY;
    _map[Qt::Key_Z]      = AxisAction::LockZ;
    _map[Qt::Key_D]      = AxisAction::LockD;   // polar: freeze the distance
    _map[Qt::Key_O]      = AxisAction::SetOrigin;
    _map[Qt::Key_T]      = AxisAction::RotateTop;
    _map[Qt::Key_F]      = AxisAction::RotateFront;
    _map[Qt::Key_S]      = AxisAction::RotateSide;
    _map[Qt::Key_V]      = AxisAction::RotateByView;
    _map[Qt::Key_E]      = AxisAction::RotateCycle;
    _map[Qt::Key_Escape]   = AxisAction::FinishLine;     // finish the current line, keep going
    _map[Qt::Key_QuoteLeft] = AxisAction::CancelCommand; // backtick ` ends the whole command
    // ToggleActive / CancelEntry / ClearLocks have no default key: bind via the
    // dialog, hotkeys.ini or setHotkey. Clear a typed value with Backspace, a
    // lock by pressing its key again.
}

void Hotkeys::set(int qtKey, AxisAction a)
{
    if (a == AxisAction::None)
        _map.erase(qtKey);
    else
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

int Hotkeys::keyFor(AxisAction a) const
{
    for (const auto& kv : _map) {
        if (kv.second == a)
            return kv.first;
    }
    return 0;   // unbound
}

std::vector<int> Hotkeys::keysFor(AxisAction a) const
{
    std::vector<int> keys;
    for (const auto& kv : _map) {
        if (kv.second == a)
            keys.push_back(kv.first);
    }
    return keys;
}

void Hotkeys::bind(AxisAction a, const std::vector<int>& keys)
{
    if (a == AxisAction::None)
        return;

    // Drop every previous key of this action, so an action never ends up bound
    // to a stale key that the settings no longer know about.
    for (auto it = _map.begin(); it != _map.end(); ) {
        if (it->second == a) it = _map.erase(it);
        else                 ++it;
    }
    for (int k : keys) {
        if (k > 0) _map[k] = a;
    }
}

// ---------------------------------------------------------------------------
// Names: stable identifiers used as settings keys ("Hotkey.LockX").
// ---------------------------------------------------------------------------

const char* Hotkeys::name(AxisAction a)
{
    switch (a) {
    case AxisAction::ToggleActive: return "ToggleActive";
    case AxisAction::ToggleMode:   return "ToggleMode";
    case AxisAction::SmartLock:    return "SmartLock";
    case AxisAction::LockX:        return "LockX";
    case AxisAction::LockY:        return "LockY";
    case AxisAction::LockZ:        return "LockZ";
    case AxisAction::LockD:        return "LockD";
    case AxisAction::SetOrigin:    return "SetOrigin";
    case AxisAction::RotateTop:    return "RotateTop";
    case AxisAction::RotateFront:  return "RotateFront";
    case AxisAction::RotateSide:   return "RotateSide";
    case AxisAction::RotateByView: return "RotateByView";
    case AxisAction::RotateCycle:  return "RotateCycle";
    case AxisAction::ClearLocks:   return "ClearLocks";
    case AxisAction::CancelEntry:  return "CancelEntry";
    case AxisAction::CancelCommand:return "CancelCommand";
    case AxisAction::FinishLine:   return "FinishLine";
    case AxisAction::ConfirmPoint: return "ConfirmPoint";
    default:                       return "None";
    }
}

AxisAction Hotkeys::fromName(const std::string& s)
{
    for (AxisAction a : allActions()) {
        if (s == name(a))
            return a;
    }
    return AxisAction::None;
}

const std::vector<AxisAction>& Hotkeys::allActions()
{
    static const std::vector<AxisAction> v {
        AxisAction::ToggleActive,
        AxisAction::ToggleMode,
        AxisAction::SmartLock,
        AxisAction::LockX,
        AxisAction::LockY,
        AxisAction::LockZ,
        AxisAction::LockD,
        AxisAction::SetOrigin,
        AxisAction::RotateTop,
        AxisAction::RotateFront,
        AxisAction::RotateSide,
        AxisAction::RotateByView,
        AxisAction::RotateCycle,
        AxisAction::ClearLocks,
        AxisAction::CancelEntry,
        AxisAction::CancelCommand,
        AxisAction::FinishLine,
        AxisAction::ConfirmPoint,
    };
    return v;
}

} // namespace AxisFlowGui
