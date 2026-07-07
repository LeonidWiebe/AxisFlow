#pragma once

#include <QtCore/QObject>

namespace Gui { class View3DInventor; }

class QKeyEvent;
class QEvent;

namespace AxisFlowGui {

/**
 * AxisFlowEventFilter
 *
 * Application-wide Qt event filter used ONLY in adapter mode (Snapper session),
 * where the 3-D view keeps keyboard focus while a Draft/Arch tool is drawing and
 * the autonomous panel therefore never receives key presses on its own.
 *
 * Installed on qApp for the lifetime of the session. On every KeyPress it acts
 * only when:
 *   - the AxisFlow service is active, AND
 *   - the widget holding keyboard focus belongs to *our* 3-D view
 *     (not the panel, the Python console, a dialog, the sketcher editor, ...).
 *
 * Behaviour (MicroStation "just start typing"):
 *   - A numeric char (digit / '.' / '-') focuses the panel's primary field and
 *     is forwarded there; the field then keeps focus so the rest is typed
 *     normally.
 *   - Keys bound in the reconfigurable keymap (Enter/X/Y/Z/Space/...) execute
 *     the mapped AxisAction and are consumed.
 *   - Escape is never consumed, so the host tool can still abort/cancel.
 *   - Everything else passes through untouched.
 *
 * Standalone mode does NOT use this filter: there the Coin SoEventCallback is
 * the key source, and installing this too would double-handle.
 */
class AxisFlowEventFilter : public QObject
{
public:
    explicit AxisFlowEventFilter(Gui::View3DInventor* view, QObject* parent = nullptr);

    void install();
    void remove();
    bool installed() const { return _installed; }

protected:
    bool eventFilter(QObject* watched, QEvent* ev) override;

private:
    /** True if the widget with keyboard focus is inside our 3-D view. */
    bool focusInView() const;

    /** True if the key text starts a numeric value entry. */
    static bool isValueChar(const QKeyEvent* ke);

    Gui::View3DInventor* _view      { nullptr };
    bool                 _installed { false };
};

} // namespace AxisFlowGui
