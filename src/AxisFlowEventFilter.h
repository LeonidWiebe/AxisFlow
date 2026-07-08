#pragma once

#include <QtCore/QObject>

class QEvent;

namespace AxisFlowGui {

/**
 * AxisFlowEventFilter
 *
 * Application-wide Qt event filter installed for the lifetime of an adapter
 * session. In the Draft adapter (variant A) AxisFlow owns its geometric keys
 * globally — enable/disable, axis locks and mode toggle — because they are
 * letters/special keys that never collide with Draft's numeric entry.
 *
 * Deliberately reserved for Draft and NEVER consumed here: digits and the
 * decimal/sign chars (they are simply left unbound), Enter/Return (Draft
 * commits the typed point), Escape (Draft aborts) and Tab (field navigation).
 * This is what makes the "type the length into Draft's field" flow work while
 * AxisFlow still constrains the point through the snap hook.
 */
class AxisFlowEventFilter : public QObject
{
public:
    explicit AxisFlowEventFilter(QObject* parent = nullptr);

    void install();
    void remove();
    bool installed() const { return _installed; }

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;

private:
    bool _installed { false };
};

} // namespace AxisFlowGui
