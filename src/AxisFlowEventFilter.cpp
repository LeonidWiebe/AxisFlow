#include "PreCompiled.h"

#include "AxisFlowEventFilter.h"
#include "AxisFlowService.h"
#include "AxisFlowPanel.h"

#include <Gui/View3DInventor.h>

#include <QtCore/QEvent>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace AxisFlowGui {

AxisFlowEventFilter::AxisFlowEventFilter(Gui::View3DInventor* view, QObject* parent)
    : QObject(parent)
    , _view(view)
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
        || c == QLatin1Char('-');
}

bool AxisFlowEventFilter::focusInView() const
{
    if (!_view) return false;

    QWidget* fw = QApplication::focusWidget();
    if (!fw) return false;

    QWidget* vw = qobject_cast<QWidget*>(_view);
    if (!vw) return false;

    return fw == vw || vw->isAncestorOf(fw);
}

// ---------------------------------------------------------------------------

bool AxisFlowEventFilter::eventFilter(QObject* watched, QEvent* ev)
{
    if (ev->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, ev);

    AxisFlowService& svc = AxisFlowService::instance();
    if (!svc.isActive())
        return QObject::eventFilter(watched, ev);

    // Only take over keys while the 3-D view holds focus. When the panel itself
    // is focused it already handles its own keys; when focus is elsewhere
    // (console, dialog, sketcher edit) we must not interfere.
    if (!focusInView())
        return QObject::eventFilter(watched, ev);

    auto* ke = static_cast<QKeyEvent*>(ev);

    // "Just start typing": the first numeric char jumps into the primary field;
    // from then on the field has focus, so focusInView() is false and the rest
    // of the number is typed normally.
    if (isValueChar(ke)) {
        if (AxisFlowPanel::instance()->startTyping(ke))
            return true;
    }

    // Never swallow Escape — the host Draft/Arch tool needs it to abort.
    if (ke->key() == Qt::Key_Escape)
        return QObject::eventFilter(watched, ev);

    // Reconfigurable action keys (Enter/X/Y/Z/Space/...). Ignore auto-repeats so
    // held keys don't rapidly toggle a lock on and off.
    if (!ke->isAutoRepeat()) {
        AxisAction a = svc.actionForKey(ke->key());
        if (a != AxisAction::None) {
            svc.performAction(a);
            return true;
        }
    }

    return QObject::eventFilter(watched, ev);
}

} // namespace AxisFlowGui
