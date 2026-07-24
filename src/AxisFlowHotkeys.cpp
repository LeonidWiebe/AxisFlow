#include "PreCompiled.h"

#include "AxisFlowHotkeys.h"

#include <QtGui/QKeyEvent>   // for Qt::Key_* codes

#include <set>

namespace AxisFlowGui {

Hotkeys::Hotkeys()
{
    setDefaults();
}

void Hotkeys::setDefaults()
{
    _map.clear();

    // AxisFlow owns the keyboard inside its OWN tools, so the full
    // AccuDraw-flavoured layout is the default. The optional Draft adapter
    // unbinds what Draft needs (X/Y/Z are Draft's RestrictX/Y/Z, Enter/Escape
    // are its commit/abort) by calling setHotkey(code, 'none') at install time.
    // Mirrors hotkeys.ini (that file is layer 2 on top of this fallback).
    auto single = [this](int k, AxisAction a) { _map[KeyChord{k, 0}] = a; };

    single(Qt::Key_Space,  AxisAction::ToggleMode);    // ortho <-> radial
    single(Qt::Key_P,      AxisAction::ConfirmPoint);  // place the point
    single(Qt::Key_Return, AxisAction::SmartLock);     // nearest-axis constraint
    single(Qt::Key_Enter,  AxisAction::SmartLock);     // numpad Enter
    single(Qt::Key_X,      AxisAction::LockX);
    single(Qt::Key_Y,      AxisAction::LockY);
    single(Qt::Key_Z,      AxisAction::LockZ);
    single(Qt::Key_D,      AxisAction::LockD);   // polar: freeze the distance
    single(Qt::Key_O,      AxisAction::SetOrigin);
    single(Qt::Key_T,      AxisAction::RotateTop);
    single(Qt::Key_F,      AxisAction::RotateFront);
    single(Qt::Key_S,      AxisAction::RotateSide);
    single(Qt::Key_V,      AxisAction::RotateByView);
    single(Qt::Key_E,      AxisAction::RotateCycle);
    // M, as in Sketcher's polyline tool, where the same key cycles the mode of
    // the next segment.
    single(Qt::Key_M,      AxisAction::ToggleSegmentMode);
    single(Qt::Key_Escape,     AxisAction::FinishLine);     // finish the line, keep going
    single(Qt::Key_QuoteLeft,  AxisAction::CancelCommand);  // backtick ends the command

    // Two-key chords. R is therefore a PREFIX and must not carry a single
    // binding of its own (see the prefix rule in the header).
    _map[KeyChord{Qt::Key_R, Qt::Key_X}] = AxisAction::RotateAboutX;
    _map[KeyChord{Qt::Key_R, Qt::Key_Y}] = AxisAction::RotateAboutY;
    _map[KeyChord{Qt::Key_R, Qt::Key_Z}] = AxisAction::RotateAboutZ;

    // ToggleActive / CancelEntry / ClearLocks have no default key: bind via the
    // dialog, hotkeys.ini or setHotkey. Clear a typed value with Backspace, a
    // lock by pressing its key again.
}

void Hotkeys::set(KeyChord c, AxisAction a)
{
    if (!c.valid())
        return;
    if (a == AxisAction::None)
        _map.erase(c);
    else
        _map[c] = a;
}

void Hotkeys::remove(KeyChord c)
{
    _map.erase(c);
}

void Hotkeys::clear()
{
    _map.clear();
}

AxisAction Hotkeys::action(KeyChord c) const
{
    auto it = _map.find(c);
    return (it == _map.end()) ? AxisAction::None : it->second;
}

bool Hotkeys::isPrefix(int qtKey) const
{
    if (qtKey == 0)
        return false;
    for (const auto& kv : _map) {
        if (kv.first.isChord() && kv.first.first == qtKey)
            return true;
    }
    return false;
}

std::vector<std::pair<int, AxisAction>> Hotkeys::continuations(int prefix) const
{
    std::vector<std::pair<int, AxisAction>> out;
    if (prefix == 0)
        return out;
    // std::map order already sorts by {first, second}, so the seconds come out
    // ascending -- the panel shows them in a stable order.
    for (const auto& kv : _map) {
        if (kv.first.isChord() && kv.first.first == prefix)
            out.emplace_back(kv.first.second, kv.second);
    }
    return out;
}

KeyChord Hotkeys::chordFor(AxisAction a) const
{
    for (const auto& kv : _map) {
        if (kv.second == a)
            return kv.first;
    }
    return KeyChord{};
}

std::vector<KeyChord> Hotkeys::chordsFor(AxisAction a) const
{
    std::vector<KeyChord> out;
    for (const auto& kv : _map) {
        if (kv.second == a)
            out.push_back(kv.first);
    }
    return out;
}

bool Hotkeys::consistent(const std::map<KeyChord, AxisAction>& m)
{
    // The prefix rule: no key may be both a single binding and the first key of
    // a chord. Collect both sets, then intersect.
    std::set<int> singles;
    std::set<int> prefixes;
    for (const auto& kv : m) {
        if (kv.first.isChord()) prefixes.insert(kv.first.first);
        else                    singles.insert(kv.first.first);
    }
    for (int k : prefixes) {
        if (singles.count(k))
            return false;
    }
    return true;
}

bool Hotkeys::canBind(AxisAction a, const std::vector<KeyChord>& keys) const
{
    if (a == AxisAction::None)
        return false;

    // Build the map this binding would produce and check the rule on it.
    std::map<KeyChord, AxisAction> candidate = _map;
    for (auto it = candidate.begin(); it != candidate.end(); ) {
        if (it->second == a) it = candidate.erase(it);
        else                 ++it;
    }
    for (const KeyChord& c : keys) {
        if (!c.valid())
            return false;
        candidate[c] = a;      // assigning a key held by another action moves it
    }
    return consistent(candidate);
}

bool Hotkeys::bind(AxisAction a, const std::vector<KeyChord>& keys)
{
    if (!canBind(a, keys))
        return false;

    // Drop the action's previous keys first: this is a REPLACE, which is what
    // keeps the live map in step with the settings (they store one key list per
    // action).
    for (auto it = _map.begin(); it != _map.end(); ) {
        if (it->second == a) it = _map.erase(it);
        else                 ++it;
    }
    for (const KeyChord& c : keys)
        _map[c] = a;
    return true;
}

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
    case AxisAction::RotateAboutX: return "RotateAboutX";
    case AxisAction::RotateAboutY: return "RotateAboutY";
    case AxisAction::RotateAboutZ: return "RotateAboutZ";
    case AxisAction::ToggleSegmentMode: return "ToggleSegmentMode";
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
        AxisAction::RotateAboutX,
        AxisAction::RotateAboutY,
        AxisAction::RotateAboutZ,
        AxisAction::ToggleSegmentMode,
        AxisAction::ClearLocks,
        AxisAction::CancelEntry,
        AxisAction::CancelCommand,
        AxisAction::FinishLine,
        AxisAction::ConfirmPoint,
    };
    return v;
}

} // namespace AxisFlowGui
