#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>

// Complete type required by QPointer (its data() static_casts QObject* to T*).
#include <Gui/View3DInventor.h>

class QEvent;

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
 * This class decides only WHERE it may act (inScope: never while the panel, a
 * dialog or the Python console has focus). WHAT a key means is routeKey() in
 * AxisFlowKeyRouting.h -- one policy, shared with the panel, covered by Tier-1
 * tests.
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
    /**
     * Whether AxisFlow may claim keys right now.
     *
     * Not simply "focus is in the 3-D view": focus is often nowhere at all (we
     * clear it when an entry ends, and Qt leaves it null after a dialog closes).
     * With a null focus widget the key would fall through to FreeCAD's global
     * shortcuts, so a digit becomes Std_ViewTop mid-typing. Anything that is a
     * real text input elsewhere -- the Python console, a spin box, a modal
     * dialog -- keeps its keys.
     */
    bool inScope() const;

    /** Held weakly, like the tool's own pointer: the filter is a child of the
     *  tool and is removed when the view dies, but inScope() must not be able
     *  to read a destroyed view even for one stray event. */
    QPointer<Gui::View3DInventor> _view;
    bool                 _ownsInput { false };
    bool                 _installed { false };
};

} // namespace AxisFlowGui
