#include "PreCompiled.h"

#include "AxisFlowEventFilter.h"
#include "AxisFlowService.h"

#include <QtCore/QEvent>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>

namespace AxisFlowGui {

AxisFlowEventFilter::AxisFlowEventFilter(QObject* parent)
    : QObject(parent)
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

bool AxisFlowEventFilter::eventFilter(QObject* watched, QEvent* ev)
{
    if (ev->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, ev);

    auto* ke = static_cast<QKeyEvent*>(ev);
    if (ke->isAutoRepeat())
        return QObject::eventFilter(watched, ev);

    // Reserved for the host tool (Draft): never consume these so numeric entry,
    // point commit and command-abort keep working in Draft's own fields.
    //   digits / '.' / '-'  -> not bound, fall through naturally
    //   Enter / Return       -> Draft commits the typed point
    //   Escape               -> Draft aborts the command
    //   Tab                  -> Draft field navigation
    switch (ke->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Escape:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return QObject::eventFilter(watched, ev);
    default:
        break;
    }

    // AxisFlow owns its geometric lock/mode keys globally (they are letters and
    // never collide with Draft's numeric input). Any key bound in the keymap is
    // executed and consumed; everything else falls through untouched.
    AxisFlowService& svc = AxisFlowService::instance();
    AxisAction a = svc.actionForKey(ke->key());
    if (a != AxisAction::None) {
        svc.performAction(a);
        return true;
    }

    return QObject::eventFilter(watched, ev);
}

} // namespace AxisFlowGui
