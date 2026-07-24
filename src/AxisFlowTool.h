#pragma once

#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/SbVec3f.h>

#include <QtCore/QPointer>

// Must be the complete type, not a forward declaration: QPointer<T>::data()
// static_casts a QObject* to T*, which only compiles once the compiler can see
// that T really does derive from QObject.
#include <Gui/View3DInventor.h>

#include "AxisFlowService.h"   // service + solver enums
#include "AxisFlowCompass.h"

namespace AxisFlowGui { class AxisFlowEventFilter; }

class SoCamera;
class SoNodeSensor;
class SoSensor;

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
 *     moves feed AxisFlowService::updateCursor, clicks confirm points. Keys are
 *     NOT read here -- they are claimed at the qApp level by
 *     AxisFlowEventFilter and routed by routeKey() (AxisFlowKeyRouting.h).
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

    /**
     * The session that is live right now, whoever started it, or null.
     *
     * There are two independent owners of tools: the palette command
     * (CmdAxisFlowActivate) and the Python API (g_session, behind getPoint).
     * Each used to look only at its own pointer, so pressing "Activate" while a
     * getPoint consumer was running built a SECOND tool on the same view --
     * the button then appeared to do nothing on the first press and to switch
     * AxisFlow off on the second. One live session at a time, and both owners
     * ask here.
     */
    static AxisFlowTool* current() { return s_current; }

    /** The view this tool is bound to, or null once that view has been
     *  destroyed (its document was closed). Callers that cache a session must
     *  check this before reusing it in another document. */
    Gui::View3DInventor* view() const { return _view.data(); }

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
     * Escape defaults to FinishLine (finish this polyline, keep the session);
     * the backtick ends the whole command. The layered cancel (entry, then
     * locks) is the CancelEntry action, which ships unbound.
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
    /** RotateByView: align the compass plane with the current camera. */
    void alignPlaneToView();
    void onActiveChanged(bool active);
    void onSettingsChanged();
    void onFinishLine();   // service->finishRequested: re-arm for the next line

private:
    static void coinEventCB(void* userData, SoEventCallback* node);
    void        handleCoinEvent(SoEventCallback* node);
    /** The actual work; handleCoinEvent only wraps it in the re-entrancy
     *  guard. Kept apart so the guard cannot be escaped by an early return. */
    void        handleCoinEventBody(SoEventCallback* node);

    /** Cancel key: finish the command outright. */
    void cancelInput();
    /** A point was placed (click or typed+confirm): advance the machine. */
    void placePoint(const Base::Vector3d& world);

    /** Orient the compass to the service's working plane. */
    void syncCompassPlane();
    /** Ask Draft's Snapper for a snapped point at a screen position.
     *  Returns true and fills @p out on a hit; false means "no snap". */
    bool querySnap(const SbVec2s& pixel, Base::Vector3d& out);
    /** Hide Draft's snap markers when the tool ends. */
    static void hideSnapMarkers();

    /** Convert the pixel size from the settings into a world radius. */
    void syncCompassSize();
    /** World units per screen pixel from the current camera (0 if unavailable). */
    float computeWorldPerPixel();
    /** Compass follows the service, unless "always show compass" is set. */
    void updateCompassVisibility();
    /** Push the current indexing candidate state onto the compass. */
    void refreshIndexingCandidates();

    /** Recompute the compass size whenever the camera changes (zoom, orbit). */
    static void cameraChangedCB(void* userData, SoSensor* sensor);
    /** The view is being destroyed (its document is closing): tear everything
     *  down while we still can, WITHOUT touching the view. */
    void onViewDestroyed();

    void attachCameraSensor();
    void detachCameraSensor();

    SbVec3f  unprojectToCompassPlane(const SbVec2s& pixel);
    void     confirmPoint();
    void     applyModeToUi();

    /**
     * The 3-D view, held WEAKLY.
     *
     * A view dies whenever its document is closed, and it takes its viewer,
     * scenegraph and camera with it. A raw pointer here used to outlive all of
     * that, so the tool dereferenced freed memory the next time anything
     * touched it -- and ~AxisFlowTool did exactly that while the view was
     * being destroyed, because Qt deletes children only after the parent's own
     * destructor has run. QPointer goes null by itself, so every use below is
     * a plain null check.
     */
    QPointer<Gui::View3DInventor> _view;

    /** The single live session; see current(). */
    static AxisFlowTool* s_current;
    SoEventCallback*     _eventCB { nullptr };
    AxisFlowCompass*     _compass { nullptr };
    AxisFlowEventFilter* _keyFilter { nullptr };   // adapter mode only
    SoNodeSensor*        _camSensor { nullptr };   // recompute size on zoom/orbit
    /** The camera the sensor watches, ref'd for as long as we watch it, so it
     *  cannot be freed under the sensor when the view goes away. */
    SoCamera*            _camWatched { nullptr };

    /**
     * Re-entrancy guard for the Coin callback.
     *
     * Handling a mouse move calls into Python (object snapping, and
     * cursorMoved -> the getPoint consumer), and the consumer is free to end
     * the session from there -- the line demo does exactly that when it notices
     * that the document changed under it. Tearing down synchronously would
     * remove and unref the very SoEventCallback that Coin is dispatching
     * through, and whose setHandled() is called moments later: an access
     * violation. While the guard is up, deactivate() only records the request
     * and the teardown runs from the event loop instead.
     *
     * The keyboard path never hit this, which is why ending with the backtick
     * always looked fine: keys arrive through the Qt filter, not through Coin.
     */
    bool                 _inCoinCallback     { false };
    bool                 _deactivateDeferred { false };

    Base::Vector3d       _lastPoint;
    InputState           _state { InputState::Idle };
    bool                 _continueMode { true };
    bool                 _hasBasePoint { false };
    bool                 _active { false };
    bool                 _hasPositionSource { false };
};

} // namespace AxisFlowGui
