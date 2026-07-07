#pragma once

#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/SbVec3f.h>

#include "AxisFlowService.h"   // service + solver enums
#include "AxisFlowCompass.h"

namespace AxisFlowGui { class AxisFlowEventFilter; }

namespace Gui {
    class View3DInventor;
}

namespace AxisFlowGui {

/**
 * AxisFlowTool
 *
 * Owns the compass (Coin scene node) for one 3-D view and, optionally, a Coin
 * event callback that acts as the *cursor position source* in standalone mode.
 * The panel is the shared autonomous AxisFlowPanel (not owned here).
 *
 * Two modes, chosen at activate():
 *   - withPositionSource = true  (standalone): install the Coin callback; mouse
 *     moves feed AxisFlowService::updateCursor, clicks confirm points.
 *   - withPositionSource = false (Snapper adapter): NO Coin callback; the cursor
 *     is driven externally (the Python adapter calls AD.updateCursor), and this
 *     tool just reflects it on the compass via the cursorMoved signal.
 */
class AxisFlowTool : public QObject
{
    Q_OBJECT

public:
    explicit AxisFlowTool(Gui::View3DInventor* view, QObject* parent = nullptr);
    ~AxisFlowTool();

    void activate(bool withPositionSource = true);
    void deactivate();
    bool isActive() const { return _active; }

    Base::Vector3d lastPoint() const { return _lastPoint; }

Q_SIGNALS:
    void pointConfirmed(Base::Vector3d worldPt);

private Q_SLOTS:
    void onCursorMoved(Base::Vector3d solved);
    void onServiceStateChanged();

private:
    static void coinEventCB(void* userData, SoEventCallback* node);
    void        handleCoinEvent(SoEventCallback* node);

    SbVec3f  unprojectToCompassPlane(const SbVec2s& pixel);
    void     confirmPoint();
    void     applyModeToUi();

    static int coinKeyToQt(int coinKey);

    Gui::View3DInventor* _view    { nullptr };
    SoEventCallback*     _eventCB { nullptr };
    AxisFlowCompass*     _compass { nullptr };
    AxisFlowEventFilter* _keyFilter { nullptr };   // adapter mode only

    Base::Vector3d       _lastPoint;
    bool                 _active { false };
    bool                 _hasPositionSource { false };
};

} // namespace AxisFlowGui
