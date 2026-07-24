#include "PreCompiled.h"

#include "AxisFlowEventFilter.h"
#include "AxisFlowService.h"
#include "AxisFlowPanel.h"
#include "AxisFlowKeyContext.h"

#include <Gui/View3DInventor.h>
#include <Gui/MainWindow.h>

#include <QtCore/QEvent>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>
#include <QtWidgets/QLineEdit>

namespace AxisFlowGui {

AxisFlowEventFilter::AxisFlowEventFilter(Gui::View3DInventor* view,
                                         bool ownsInput,
                                         QObject* parent)
    : QObject(parent)
    , _view(view)
    , _ownsInput(ownsInput)
{
}

void AxisFlowEventFilter::install()
{
    if (_installed) return;
    if (qApp) {
        qApp->installEventFilter(this);
        _installed = true;
    }
}

void AxisFlowEventFilter::remove()
{
    if (!_installed) return;
    if (qApp)
        qApp->removeEventFilter(this);
    _installed = false;
}

// ---------------------------------------------------------------------------

bool AxisFlowEventFilter::inScope() const
{
    // A modal dialog owns the keyboard, full stop.
    if (QApplication::activeModalWidget())
        return false;

    QWidget* fw = QApplication::focusWidget();

    // No focus widget: this is the state right after an entry ends (clearFocus)
    // or after a dialog closes. Claim it -- otherwise the next digit is resolved
    // as a global QAction shortcut and the camera jumps.
    if (!fw)
        return true;

    // The panel handles its own keys through its own event filter.
    if (QWidget* panel = AxisFlowPanel::instance()) {
        if (fw == panel || panel->isAncestorOf(fw))
            return false;
    }

    if (_view) {
        if (auto* vw = qobject_cast<QWidget*>(_view.data())) {
            if (fw == vw || vw->isAncestorOf(fw))
                return true;
        }
    }

    // Someone else is typing: Python console, spin box, combo, text editor.
    if (fw->inherits("QLineEdit") || fw->inherits("QAbstractSpinBox") ||
        fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit") ||
        fw->inherits("QComboBox"))
        return false;

    // A non-text widget of the main window (tree, viewport chrome): the session
    // is active, so the few keys we own are still ours.
    return fw->window() == Gui::getMainWindow();
}

// ---------------------------------------------------------------------------

bool AxisFlowEventFilter::eventFilter(QObject* watched, QEvent* ev)
{
    const QEvent::Type t = ev->type();
    if (t != QEvent::KeyPress && t != QEvent::ShortcutOverride)
        return QObject::eventFilter(watched, ev);

    // Focus topology is this filter's business; what a key MEANS is not. The
    // panel runs its own filter, so anything focused there is out of scope and
    // never reaches the routing below with inField=true.
    if (!inScope())
        return QObject::eventFilter(watched, ev);

    auto* ke = static_cast<QKeyEvent*>(ev);
    const KeyContext ctx = keyContextFromEvent(ke, /*inField*/ false, _ownsInput);
    const KeyRoute route = routeKey(ctx);

    if (!routeClaimsKey(route))
        return QObject::eventFilter(watched, ev);

    // Stop Qt from resolving this key as a global QAction shortcut (digits are
    // bound to Std_ViewTop and friends). Accepting the override makes Qt deliver
    // an ordinary KeyPress next, which is where we do the work.
    //
    // This matters doubly while a chord is pending: every plain key then routes
    // to something claimed, so the second key of R X cannot leak out and fire
    // the X lock (or a camera shortcut) before we resolve the chord.
    if (t == QEvent::ShortcutOverride) {
        ev->accept();
        return true;
    }

    applyChordState(ke, route);   // start / end the two-key chord wait

    AxisFlowService& svc = AxisFlowService::instance();
    AxisFlowPanel*   panel = AxisFlowPanel::instance();

    switch (route) {
    case KeyRoute::BeginChord:
    case KeyRoute::ChordHold:   return true;   // waiting for the second key
    case KeyRoute::Action:      svc.performAction(ctx.bound); return true;
    case KeyRoute::CancelEntry: panel->cancelInput();         return true;   // entry -> locks
    case KeyRoute::Backspace:   panel->backspaceEntry();      return true;
    case KeyRoute::ClearEntry:  panel->clearEntry();          return true;
    case KeyRoute::TabCycle:    panel->cycleActiveField();    return true;
    case KeyRoute::StartTyping:
        // "Just start typing": seed the panel's active field. The panel window
        // is activated, so the caret shows and the rest is typed natively.
        if (!ke->text().isEmpty() && panel->startTyping(ke->text().at(0)))
            return true;
        break;
    case KeyRoute::Swallow:     return true;   // claimed, e.g. a held lock key
    default:                    break;
    }

    return QObject::eventFilter(watched, ev);
}

} // namespace AxisFlowGui
