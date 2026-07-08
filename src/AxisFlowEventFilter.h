#pragma once

#include <QtCore/QObject>

namespace Gui { class View3DInventor; }

class QEvent;
class QKeyEvent;

namespace AxisFlowGui {

/**
 * AxisFlowEventFilter
 *
 * Application-wide Qt key filter, alive for the duration of a session.
 *
 * Why Qt and not the Coin event callback: FreeCAD binds plain digits to global
 * QActions (2 -> Std_ViewTop, and so on). Qt resolves shortcuts BEFORE the key
 * reaches the 3-D view widget, so a Coin SoEventCallback never sees them and
 * "just start typing" silently changes the camera instead.
 *
 * The fix is the mechanism Qt provides for exactly this: accept
 * QEvent::ShortcutOverride and the QAction does not fire; the key is then
 * delivered as an ordinary KeyPress, which we handle here.
 *
 * Two modes:
 *   ownsInput = true  (AxisFlow's own tool) -- bound action keys AND printable
 *       value characters are claimed; a digit starts a value entry in the panel.
 *   ownsInput = false (Draft adapter) -- only bound action keys are claimed, so
 *       digits, Enter and Escape stay with Draft's coordinate fields.
 *
 * In both modes nothing is touched unless the service is active and keyboard
 * focus is inside our 3-D view (never while the panel, a dialog or the Python
 * console has focus).
 */
class AxisFlowEventFilter : public QObject
{
public:
    explicit AxisFlowEventFilter(Gui::View3DInventor* view,
                                 bool ownsInput,
                                 QObject* parent = nullptr);

    void install();
    void remove();
    bool installed() const { return _installed; }

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;

private:
    /** Keyboard focus is inside our 3-D view. */
    bool focusInView() const;
    /** A printable character that starts a numeric entry. */
    static bool isValueChar(const QKeyEvent* ke);
    /** This key is one we intend to consume (action, or value char if we own input). */
    bool claims(const QKeyEvent* ke) const;

    Gui::View3DInventor* _view      { nullptr };
    bool                 _ownsInput { false };
    bool                 _installed { false };
};

} // namespace AxisFlowGui
