#include "PreCompiled.h"

#include "AxisFlowEventFilter.h"
#include "AxisFlowService.h"
#include "AxisFlowPanel.h"

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

bool AxisFlowEventFilter::isValueChar(const QKeyEvent* ke)
{
    const QString& t = ke->text();
    if (t.isEmpty()) return false;
    const QChar c = t.at(0);
    return c.isDigit()
        || c == QLatin1Char('.')
        || c == QLatin1Char(',')
        || c == QLatin1Char('-');
}

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
        if (auto* vw = qobject_cast<QWidget*>(_view)) {
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

bool AxisFlowEventFilter::claims(const QKeyEvent* ke) const
{
    // Modified keys (Ctrl+S, Alt+F4, ...) always belong to the application.
    if (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;

    if (AxisFlowService::instance().actionForKey(ke->key()) != AxisAction::None)
        return true;

    if (!_ownsInput)
        return false;

    // Editing keys must be claimed too: with focus back in the 3-D view (which
    // happens as soon as the mouse moves) they would otherwise be lost, and
    // Backspace would silently do nothing.
    switch (ke->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return true;
    default:
        break;
    }

    return isValueChar(ke);
}

// ---------------------------------------------------------------------------

bool AxisFlowEventFilter::eventFilter(QObject* watched, QEvent* ev)
{
    const QEvent::Type t = ev->type();
    if (t != QEvent::KeyPress && t != QEvent::ShortcutOverride)
        return QObject::eventFilter(watched, ev);

    if (!AxisFlowService::instance().isActive()) {
        // While OFF the filter stays out of the way -- except for the one key
        // whose whole job is to turn AxisFlow back ON. Without this the toggle
        // was a one-way switch: the key deactivated, then never came back.
        auto* ke = static_cast<QKeyEvent*>(ev);
        if ((ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) == 0
            && AxisFlowService::instance().actionForKey(ke->key()) == AxisAction::ToggleActive
            && inScope()) {
            if (t == QEvent::ShortcutOverride) { ev->accept(); return true; }
            if (!ke->isAutoRepeat())
                AxisFlowService::instance().performAction(AxisAction::ToggleActive);
            return true;
        }
        return QObject::eventFilter(watched, ev);
    }

    if (!inScope())
        return QObject::eventFilter(watched, ev);

    auto* ke = static_cast<QKeyEvent*>(ev);
    if (!claims(ke))
        return QObject::eventFilter(watched, ev);

    // Stop Qt from resolving this key as a global QAction shortcut (digits are
    // bound to Std_ViewTop and friends). Accepting the override makes Qt deliver
    // an ordinary KeyPress next, which is where we do the work.
    if (t == QEvent::ShortcutOverride) {
        ev->accept();
        return true;
    }

    if (ke->isAutoRepeat())
        return true;   // claimed, but do not repeat-fire a lock toggle

    AxisFlowService& svc = AxisFlowService::instance();
    AxisAction a = svc.actionForKey(ke->key());
    if (a == AxisAction::CancelEntry) {
        AxisFlowPanel::instance()->cancelInput();   // layered: entry -> locks
        return true;
    }
    if (a != AxisAction::None) {
        svc.performAction(a);
        return true;
    }

    if (_ownsInput) {
        AxisFlowPanel* panel = AxisFlowPanel::instance();

        if (ke->key() == Qt::Key_Backspace) { panel->backspaceEntry(); return true; }
        if (ke->key() == Qt::Key_Delete)    { panel->clearEntry();     return true; }
        if (ke->key() == Qt::Key_Tab ||
            ke->key() == Qt::Key_Backtab)   { panel->cycleActiveField(); return true; }

        // "Just start typing": seed the panel's primary field. The panel window
        // is activated, so the caret shows and the rest is typed natively.
        if (isValueChar(ke) && panel->startTyping(ke->text().at(0)))
            return true;
    }

    return QObject::eventFilter(watched, ev);
}

} // namespace AxisFlowGui
