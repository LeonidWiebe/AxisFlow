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

bool AxisFlowEventFilter::focusInView() const
{
    if (!_view) return false;

    QWidget* fw = QApplication::focusWidget();
    if (!fw) return false;

    auto* vw = qobject_cast<QWidget*>(_view);
    if (!vw) return false;

    return fw == vw || vw->isAncestorOf(fw);
}

bool AxisFlowEventFilter::claims(const QKeyEvent* ke) const
{
    // Modified keys (Ctrl+S, Alt+F4, ...) always belong to the application.
    if (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;

    if (AxisFlowService::instance().actionForKey(ke->key()) != AxisAction::None)
        return true;

    return _ownsInput && isValueChar(ke);
}

// ---------------------------------------------------------------------------

bool AxisFlowEventFilter::eventFilter(QObject* watched, QEvent* ev)
{
    const QEvent::Type t = ev->type();
    if (t != QEvent::KeyPress && t != QEvent::ShortcutOverride)
        return QObject::eventFilter(watched, ev);

    if (!AxisFlowService::instance().isActive())
        return QObject::eventFilter(watched, ev);

    // Only while the 3-D view holds focus. With focus in the panel, its own
    // widgets handle typing and its event filter handles the action keys.
    if (!focusInView())
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
    if (a != AxisAction::None) {
        svc.performAction(a);
        return true;
    }

    // "Just start typing": seed the panel's primary field. Focus moves there, so
    // the rest of the number is typed normally and never reaches this filter.
    if (_ownsInput && isValueChar(ke)) {
        if (AxisFlowPanel::instance()->startTyping(ke->text().at(0)))
            return true;
    }

    return QObject::eventFilter(watched, ev);
}

} // namespace AxisFlowGui
