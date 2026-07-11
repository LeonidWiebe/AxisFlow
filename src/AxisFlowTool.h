#pragma once

#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/SbVec3f.h>

#include "AxisFlowService.h"   // service + solver enums
#include "AxisFlowCompass.h"

namespace AxisFlowGui { class AxisFlowEventFilter; }

class SoNodeSensor;
class SoSensor;

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

    /** Diagnostics: the live compass (may be null before activate()). */
    AxisFlowCompass* compass() const { return _compass; }

    /**
     * Input state machine for AxisFlow's own tool.
     *
     *   Idle       - session up, no base point yet; the compass sits at the WP
     *                origin and the first click sets the anchor.
     *   Tracking   - a base point exists; the cursor drives a live point and a
     *                click confirms the next one (this is where entry happens).
     *   Finished   - the command ended (Cancel key, or a consumer stopped it).
     *
     * Escape is layered, per the agreed behaviour: it first cancels an active
     * value entry, then releases locks; it does NOT end the command (a separate
     * Cancel key does).
     */
    enum class InputState { Idle, Tracking, Finished };
    InputState inputState() const { return _state; }

    /** Begin a fresh point sequence (Idle). Optional continue-mode keeps the
     *  tool tracking after each confirmed point instead of finishing. */
    void beginInput(bool continueMode = true);
    /** True while the tool is collecting points. */
    bool isCollecting() const { return _state != InputState::Finished; }
    /** Stop collecting without clearing state (used when getPoint ends). */
    void stopCollecting() { _state = InputState::Finished; }
    /** Seed the base point (compass origin) without emitting pointConfirmed. */
    void setBasePoint(const Base::Vector3d& world);
    /** True in AxisFlow's own tool (owns keyboard); false in the Draft adapter. */
    bool hasPositionSource() const { return _hasPositionSource; }

Q_SIGNALS:
    void pointConfirmed(Base::Vector3d worldPt);

private Q_SLOTS:
    void onCursorMoved(Base::Vector3d solved);
    void onServiceStateChanged();
    void onActiveChanged(bool active);
    void onSettingsChanged();
    void onFinishLine();   // service->finishRequested: re-arm for the next line

private:
    static void coinEventCB(void* userData, SoEventCallback* node);
    void        handleCoinEvent(SoEventCallback* node);

    /** Escape: cancel entry, then release locks (never ends the command). */
    void handleEscape();
    /** Cancel key: finish the command outright. */
    void cancelInput();
    /** A point was placed (click or typed+confirm): advance the machine. */
    void placePoint(const Base::Vector3d& world);

    /** Orient the compass to the service's working plane. */
    void syncCompassPlane();
    /** Convert the pixel size from the settings into a world radius. */
    void syncCompassSize();
    /** Compass follows the service, unless "always show compass" is set. */
    void updateCompassVisibility();

    /** Recompute the compass size whenever the camera changes (zoom, orbit). */
    static void cameraChangedCB(void* userData, SoSensor* sensor);
    void attachCameraSensor();
    void detachCameraSensor();

    SbVec3f  unprojectToCompassPlane(const SbVec2s& pixel);
    void     confirmPoint();
    void     applyModeToUi();

    static int coinKeyToQt(int coinKey);

    Gui::View3DInventor* _view    { nullptr };
    SoEventCallback*     _eventCB { nullptr };
    AxisFlowCompass*     _compass { nullptr };
    AxisFlowEventFilter* _keyFilter { nullptr };   // adapter mode only
    SoNodeSensor*        _camSensor { nullptr };   // recompute size on zoom/orbit

    Base::Vector3d       _lastPoint;
    InputState           _state { InputState::Idle };
    bool                 _continueMode { true };
    bool                 _hasBasePoint { false };
    bool                 _active { false };
    bool                 _hasPositionSource { false };
};

} // namespace AxisFlowGui
