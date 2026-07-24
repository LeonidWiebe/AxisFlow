#pragma once

#include <QtGui/QKeyEvent>

#include "AxisFlowKeyRouting.h"
#include "AxisFlowHotkeys.h"
#include "AxisFlowService.h"
#include "AxisFlowPanel.h"   // entryInProgress()

namespace AxisFlowGui {

/**
 * Qt-side adapter for AxisFlowKeyRouting.h.
 *
 * The routing policy is deliberately Qt-free so it can be unit-tested; this
 * header is the one place that turns a QKeyEvent into a KeyContext. Keeping the
 * two apart means the key-classification predicates (what counts as a value
 * character, what the line edit must handle itself) also exist only once --
 * they used to be duplicated, and subtly different, in the panel and in the
 * application-wide filter.
 */

/** A character that begins or continues a number: digit, separator or sign. */
inline bool isEntryChar(const QKeyEvent* ke)
{
    const QString t = ke->text();
    if (t.isEmpty())
        return false;
    const QChar c = t.at(0);
    return c.isDigit()
        || c == QLatin1Char('.') || c == QLatin1Char(',')
        || c == QLatin1Char('-') || c == QLatin1Char('+');
}

/** A key a focused QLineEdit must handle itself (editing and caret movement). */
inline bool isEditKey(const QKeyEvent* ke)
{
    switch (ke->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Left: case Qt::Key_Right:
    case Qt::Key_Home: case Qt::Key_End:
        return true;
    default:
        return isEntryChar(ke);
    }
}

/**
 * Build the routing context for a key press.
 *
 * @param inField    the event came from one of the panel's coordinate fields
 * @param ownsInput  AxisFlow drives the input (its own tool, not the adapter)
 *
 * KeyContext::sameEntryField is left false: only the panel knows which field is
 * mid-entry, so it fills that in before routing.
 */
inline KeyContext keyContextFromEvent(const QKeyEvent* ke, bool inField, bool ownsInput)
{
    const AxisFlowService& svc = AxisFlowService::instance();
    const Hotkeys& hk = svc.hotkeys();
    const int pending = svc.pendingPrefix();

    KeyContext c;
    // With a prefix pending, the binding to look up is the CHORD {prefix, this
    // key}; otherwise the plain single key. Resolving it here keeps routeKey()
    // free of the key map (and therefore free of Qt).
    c.bound = pending ? hk.action(KeyChord{pending, ke->key()})
                      : hk.action(KeyChord{ke->key(), 0});
    c.prefixPending = (pending != 0);
    c.opensChord    = !c.prefixPending && hk.isPrefix(ke->key());

    c.serviceActive = svc.isActive();
    c.modified      = (ke->modifiers()
                       & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) != 0;
    c.autoRepeat    = ke->isAutoRepeat();
    c.ownsInput     = ownsInput;
    c.inField       = inField;
    c.entryStart    = isEntryChar(ke);
    c.editKey       = isEditKey(ke);
    c.tab           = (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab);
    c.backspace     = (ke->key() == Qt::Key_Backspace);
    c.del           = (ke->key() == Qt::Key_Delete);

    const QString t = ke->text();
    c.letter        = !t.isEmpty() && t.at(0).isLetter();

    // Only the panel knows whether a number is actually being typed.
    if (const AxisFlowPanel* panel = AxisFlowPanel::instance())
        c.entryInProgress = panel->entryInProgress();

    return c;
}

/**
 * Apply a routing verdict's effect on the chord state, then report whether the
 * caller still has work to do. Every key-handling site calls this immediately
 * after routeKey(), so the "one press ends the wait" rule exists once.
 */
inline void applyChordState(const QKeyEvent* ke, KeyRoute route)
{
    AxisFlowService& svc = AxisFlowService::instance();
    if (route == KeyRoute::BeginChord) {
        svc.setPendingPrefix(ke->key());
        return;
    }
    if (svc.pendingPrefix() != 0 && routeConsumesPrefix(route))
        svc.clearPendingPrefix();
}

}  // namespace AxisFlowGui
