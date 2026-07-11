#include "PreCompiled.h"

#include "AxisFlowTool.h"
#include "AxisFlowPanel.h"
#include "AxisFlowEventFilter.h"
#include "AxisFlowSettings.h"

#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>

#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/sensors/SoNodeSensor.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbLine.h>

#include <Base/Vector3D.h>

#include <QtGui/QKeyEvent>   // Qt::Key_*

#include <cmath>

namespace AxisFlowGui {

// ---------------------------------------------------------------------------

AxisFlowTool::AxisFlowTool(Gui::View3DInventor* view, QObject* parent)
    : QObject(parent)
    , _view(view)
{
    // Signal connections are scoped to the active window (see activate()), not
    // the object lifetime: with deferred destruction a just-deactivated tool
    // can briefly outlive its replacement and must not react in the meantime.
    _compass = new AxisFlowCompass;
}

AxisFlowTool::~AxisFlowTool()
{
    deactivate();
    delete _compass;
}

// ---------------------------------------------------------------------------

void AxisFlowTool::activate(bool withPositionSource)
{
    if (_active) return;
    _active = true;
    _hasPositionSource = withPositionSource;

    // Listen to the service only while active. The guard above rules out a
    // double-connect, and deactivate() tears these down again.
    AxisFlowService& svc = AxisFlowService::instance();
    connect(&svc, &AxisFlowService::cursorMoved,      this, &AxisFlowTool::onCursorMoved);
    connect(&svc, &AxisFlowService::stateChanged,     this, &AxisFlowTool::onServiceStateChanged);
    connect(&svc, &AxisFlowService::activeChanged,    this, &AxisFlowTool::onActiveChanged);
    connect(&svc, &AxisFlowService::cancelRequested,  this, &AxisFlowTool::cancelInput);
    connect(&svc, &AxisFlowService::finishRequested,  this, &AxisFlowTool::onFinishLine);
    connect(&svc, &AxisFlowService::confirmRequested, this, &AxisFlowTool::confirmPoint);
    connect(&AxisFlowSettings::instance(), &AxisFlowSettings::changed,
            this, &AxisFlowTool::onSettingsChanged);

    Gui::View3DInventorViewer* viewer = _view->getViewer();
    SoSeparator* root = static_cast<SoSeparator*>(viewer->getSceneGraph());

    if (withPositionSource) {
        // Own tool: AxisFlow owns the keyboard and the constraint. A previous
        // adapter session may have left constraining off.
        AxisFlowService::instance().setConstrainEnabled(true);
        _eventCB = new SoEventCallback;
        _eventCB->ref();
        _eventCB->addEventCallback(SoEvent::getClassTypeId(), coinEventCB, this);
        root->insertChild(_eventCB, 0);
    }

    root->addChild(_compass->getRootNode());
    applyModeToUi();

    // Shared, autonomous panel.
    AxisFlowPanel* panel = AxisFlowPanel::instance();
    panel->setOwnsInput(withPositionSource);   // adapter never grabs the caret
    QPoint pos = _view->mapToGlobal(QPoint(20, 20));
    panel->move(pos);
    panel->show();

    // Keys must be claimed at the Qt level, not in the Coin callback: FreeCAD
    // binds bare digits to global QActions (2 -> Std_ViewTop), and Qt resolves
    // shortcuts before the key ever reaches the 3-D view widget.
    //   own tool -> also claim digits ("just start typing")
    //   adapter  -> claim only bound action keys; digits belong to Draft
    _keyFilter = new AxisFlowEventFilter(_view, withPositionSource, this);
    _keyFilter->install();

    AxisFlowService::instance().activate();

    // First paint: orient to the working plane, size to the pixel setting and
    // apply "always show compass" before anything moves.
    syncCompassPlane();
    syncCompassSize();
    applyModeToUi();
    updateCompassVisibility();
    attachCameraSensor();

    // AccuDraw model: the caret lives in the active coordinate field from the
    // moment the tool starts, so typing just works and global shortcuts do not.
    if (withPositionSource) {
        beginInput(/*continueMode*/ true);   // Idle, ready for the first point
        panel->focusActiveField();
    }
}

void AxisFlowTool::deactivate()
{
    if (!_active) return;
    _active = false;

    // Stop listening immediately. With deferred destruction this object may
    // outlive the current teardown; it must not react to the service signals
    // emitted below (or to a new session's signals) once it is being retired.
    disconnect(&AxisFlowService::instance(),  nullptr, this, nullptr);
    disconnect(&AxisFlowSettings::instance(), nullptr, this, nullptr);

    AxisFlowPanel::instance()->setOwnsInput(false);

    AxisFlowService::instance().deactivate();

    detachCameraSensor();

    if (_keyFilter) {
        // deactivate() can run from *inside* the filter's own eventFilter()
        // (backtick -> CancelCommand -> cancelRequested -> endSession). Deleting
        // the filter synchronously here would free an object whose method is
        // still on the call stack. Unregister it from qApp now (safe mid-
        // dispatch), detach it from this tool so ~AxisFlowTool won't touch it,
        // and defer the actual destruction to the event loop.
        _keyFilter->remove();
        _keyFilter->setParent(nullptr);
        _keyFilter->deleteLater();
        _keyFilter = nullptr;
    }

    Gui::View3DInventorViewer* viewer = _view->getViewer();
    SoSeparator* root = static_cast<SoSeparator*>(viewer->getSceneGraph());

    if (_eventCB) {
        root->removeChild(_eventCB);
        _eventCB->unref();
        _eventCB = nullptr;
    }
    root->removeChild(_compass->getRootNode());

    AxisFlowPanel::instance()->hide();
}

// ---------------------------------------------------------------------------

void AxisFlowTool::coinEventCB(void* userData, SoEventCallback* node)
{
    static_cast<AxisFlowTool*>(userData)->handleCoinEvent(node);
}

void AxisFlowTool::handleCoinEvent(SoEventCallback* node)
{
    const SoEvent* event = node->getEvent();

    if (event->isOfType(SoLocation2Event::getClassTypeId())) {
        const auto* me = static_cast<const SoLocation2Event*>(event);
        SbVec3f worldPt = unprojectToCompassPlane(me->getPosition());
        Base::Vector3d raw(worldPt[0], worldPt[1], worldPt[2]);
        AxisFlowService::instance().updateCursor(raw, nullptr);   // -> cursorMoved
        node->setHandled();
        return;
    }

    if (event->isOfType(SoMouseButtonEvent::getClassTypeId())) {
        const auto* be = static_cast<const SoMouseButtonEvent*>(event);
        if (be->getButton() == SoMouseButtonEvent::BUTTON1 &&
            be->getState()  == SoButtonEvent::DOWN)
        {
            confirmPoint();
            node->setHandled();
        }
        return;
    }

    if (event->isOfType(SoKeyboardEvent::getClassTypeId())) {
        const auto* ke = static_cast<const SoKeyboardEvent*>(event);
        if (ke->getState() != SoButtonEvent::DOWN) return;

        // Value entry is NOT handled here. Two reasons: Qt resolves global
        // shortcuts before the key ever reaches this callback (see
        // AxisFlowEventFilter), and SoKeyboardEvent::getPrintableCharacter()
        // returns '.' as a placeholder for keys with no printable form -- so
        // Backspace used to type a dot. Typing lives in the Qt filter alone.
        AxisFlowService& svc = AxisFlowService::instance();
        int qtKey = coinKeyToQt(ke->getKey());
        if (qtKey != 0) {
            AxisAction a = svc.actionForKey(qtKey);
            if (a != AxisAction::None) {
                svc.performAction(a);
                node->setHandled();
            }
        }
    }
}

// ---------------------------------------------------------------------------

SbVec3f AxisFlowTool::unprojectToCompassPlane(const SbVec2s& pixel)
{
    Gui::View3DInventorViewer* viewer = _view->getViewer();

    SoCamera* cam = viewer->getSoRenderManager()->getCamera();
    if (!cam) return SbVec3f(0, 0, 0);

    SbViewportRegion vp = viewer->getSoRenderManager()->getViewportRegion();
    SbViewVolume     vv = cam->getViewVolume(vp.getViewportAspectRatio());

    SbVec2s vpSize = vp.getViewportSizePixels();
    float nx = float(pixel[0]) / float(vpSize[0]);
    float ny = float(pixel[1]) / float(vpSize[1]);

    SbLine ray;
    vv.projectPointToLine(SbVec2f(nx, ny), ray);

    AxisFlowService& svc = AxisFlowService::instance();
    Base::Vector3d n = svc.planeNormal();
    Base::Vector3d o = svc.origin();

    SbPlane plane(SbVec3f(float(n.x), float(n.y), float(n.z)),
                  SbVec3f(float(o.x), float(o.y), float(o.z)));

    SbVec3f intersection;
    if (!plane.intersect(ray, intersection)) {
        intersection = ray.getPosition() + ray.getDirection() * 100.f;
    }
    return intersection;
}

// ---------------------------------------------------------------------------

void AxisFlowTool::confirmPoint()
{
    AxisFlowService& svc = AxisFlowService::instance();
    placePoint(svc.currentSolved());
}

void AxisFlowTool::beginInput(bool continueMode)
{
    _continueMode = continueMode;
    _hasBasePoint = false;
    _state = InputState::Idle;
    AxisFlowService::instance().clearLocks();
    AxisFlowService::instance().clearKeyValues();
}

void AxisFlowTool::setBasePoint(const Base::Vector3d& world)
{
    // Seed the anchor (e.g. getPoint's 'last') without confirming a point, so no
    // pointConfirmed fires and no consumer callback is triggered for the seed.
    _lastPoint = world;
    _hasBasePoint = true;
    _state = InputState::Tracking;

    AxisFlowService::instance().setOrigin(world);
    _compass->setOrigin(SbVec3f(
        float(world.x), float(world.y), float(world.z)));
}

void AxisFlowTool::placePoint(const Base::Vector3d& world)
{
    if (_state == InputState::Finished)
        return;

    AxisFlowService& svc = AxisFlowService::instance();
    _lastPoint = world;

    // Anchor the compass at the new point and reset per-point state (an AxisFlow
    // lock and any typed values are per-point in AccuDraw).
    svc.confirm(world);   // moves origin, clears overrides + lock, emits signals
    _hasBasePoint = true;

    _compass->setOrigin(SbVec3f(
        float(world.x), float(world.y), float(world.z)));

    // Clicking in the 3-D view hands keyboard focus to the viewport. Take it
    // straight back, or the next digit becomes a FreeCAD view shortcut.
    if (_hasPositionSource)
        AxisFlowPanel::instance()->focusActiveField();

    // Idle -> Tracking on the first point; stay Tracking while continuing.
    _state = _continueMode ? InputState::Tracking : InputState::Finished;

    Q_EMIT pointConfirmed(world);
}

void AxisFlowTool::handleEscape()
{
    // Layered cancel lives in the panel (it owns the entry state).
    AxisFlowPanel::instance()->cancelInput();
}

void AxisFlowTool::cancelInput()
{
    _state = InputState::Finished;
    AxisFlowService::instance().clearLocks();
    AxisFlowService::instance().clearKeyValues();
    // Note: the tool stays activated (compass/panel remain) -- ending the DRAW
    // command is the consumer's business. This just stops point collection.
}

void AxisFlowTool::onFinishLine()
{
    // FinishLine (Escape): the consumer commits the current polyline; the tool
    // re-arms so the next click starts a fresh line. The session (compass,
    // panel) stays up throughout.
    if (!_active) return;
    beginInput(_continueMode);
}

// ---------------------------------------------------------------------------

void AxisFlowTool::applyModeToUi()
{
    bool polar = (AxisFlowService::instance().mode() == CompassMode::Polar);
    _compass->setMode(polar ? CompassMode::Polar : CompassMode::Rectangular);
}

void AxisFlowTool::syncCompassPlane()
{
    AxisFlowService& svc = AxisFlowService::instance();
    const Base::Vector3d u = svc.planeU();
    const Base::Vector3d v = svc.planeV();
    const Base::Vector3d n = svc.planeNormal();

    _compass->setPlane(SbVec3f(float(u.x), float(u.y), float(u.z)),
                       SbVec3f(float(v.x), float(v.y), float(v.z)),
                       SbVec3f(float(n.x), float(n.y), float(n.z)));
}

void AxisFlowTool::syncCompassSize()
{
    // The setting is in PIXELS, the scene graph works in world units, so the
    // conversion needs the camera. Perspective cameras additionally depend on
    // the distance to the compass, hence the recompute on every cursor move.
    if (!_view) return;

    Gui::View3DInventorViewer* viewer = _view->getViewer();
    if (!viewer) return;

    SoRenderManager* rm = viewer->getSoRenderManager();
    if (!rm) return;

    SoCamera* cam = rm->getCamera();
    if (!cam) return;

    const SbVec2s vpPix = rm->getViewportRegion().getViewportSizePixels();
    const int hPix = vpPix[1];
    if (hPix <= 0) return;

    float worldHeight = 0.0f;
    if (cam->isOfType(SoOrthographicCamera::getClassTypeId())) {
        worldHeight = static_cast<SoOrthographicCamera*>(cam)->height.getValue();
    }
    else if (cam->isOfType(SoPerspectiveCamera::getClassTypeId())) {
        auto* pc = static_cast<SoPerspectiveCamera*>(cam);
        const Base::Vector3d o = AxisFlowService::instance().origin();
        const SbVec3f worldOrigin(float(o.x), float(o.y), float(o.z));
        const float dist = (worldOrigin - cam->position.getValue()).length();
        worldHeight = 2.0f * dist * std::tan(pc->heightAngle.getValue() * 0.5f);
    }
    else {
        return;   // unknown camera type: leave the last radius alone
    }

    const float worldPerPixel = worldHeight / float(hPix);
    const int sizePx = AxisFlowSettings::instance().compassSizePx();
    _compass->setRadius(0.5f * float(sizePx) * worldPerPixel);
}

void AxisFlowTool::onSettingsChanged()
{
    if (!_compass) return;
    _compass->applySettings();
    syncCompassSize();
    updateCompassVisibility();
}

void AxisFlowTool::updateCompassVisibility()
{
    if (!_compass) return;
    // "Always show compass" keeps it on screen while the module is loaded, even
    // when AxisFlow is toggled off; otherwise the compass follows the service.
    const bool always = AxisFlowSettings::instance().alwaysShowCompass();
    _compass->setVisible(AxisFlowService::instance().isActive() || always);
}

void AxisFlowTool::cameraChangedCB(void* userData, SoSensor*)
{
    static_cast<AxisFlowTool*>(userData)->syncCompassSize();
}

void AxisFlowTool::attachCameraSensor()
{
    if (_camSensor || !_view) return;

    Gui::View3DInventorViewer* viewer = _view->getViewer();
    if (!viewer) return;
    SoRenderManager* rm = viewer->getSoRenderManager();
    if (!rm) return;
    SoCamera* cam = rm->getCamera();
    if (!cam) return;

    // Without this the compass keeps the size it had when the session started:
    // in adapter mode nothing drives cursorMoved, and zooming never resizes it.
    _camSensor = new SoNodeSensor(&AxisFlowTool::cameraChangedCB, this);
    _camSensor->attach(cam);
}

void AxisFlowTool::detachCameraSensor()
{
    if (!_camSensor) return;
    _camSensor->detach();
    delete _camSensor;
    _camSensor = nullptr;
}

int AxisFlowTool::coinKeyToQt(int coinKey)
{
    switch (coinKey) {
    case SoKeyboardEvent::RETURN: return Qt::Key_Return;  // Coin aliases ENTER to RETURN
    case SoKeyboardEvent::SPACE:  return Qt::Key_Space;
    case SoKeyboardEvent::X:      return Qt::Key_X;
    case SoKeyboardEvent::Y:      return Qt::Key_Y;
    case SoKeyboardEvent::Z:      return Qt::Key_Z;
    case SoKeyboardEvent::ESCAPE: return Qt::Key_Escape;
    default: return 0;
    }
}

// ---------------------------------------------------------------------------

void AxisFlowTool::onCursorMoved(Base::Vector3d solved)
{
    // Keep the compass anchored at the service origin (the last data point).
    // In adapter mode confirmPoint() is never called, so the origin is set
    // here from the service (updateCursor sets it to 'lastpoint').
    Base::Vector3d o = AxisFlowService::instance().origin();
    _compass->setOrigin(SbVec3f(float(o.x), float(o.y), float(o.z)));
    _compass->setCursorPoint(SbVec3f(
        float(solved.x), float(solved.y), float(solved.z)));

    // Keeps the compass a constant size on screen through zoom and, for a
    // perspective camera, through changes of distance.
    syncCompassSize();
}

void AxisFlowTool::onServiceStateChanged()
{
    // Mode drives the contour shape (square vs circle); the plane may have been
    // pushed by an adapter or by setPlane() from Python.
    applyModeToUi();
    syncCompassPlane();
}

void AxisFlowTool::onActiveChanged(bool active)
{
    // The session (compass in scene, key filter, panel instance) stays alive;
    // activation just shows or hides the UI. This lets the global ToggleActive
    // key -- or the panel's power button -- flip AxisFlow on/off without tearing
    // the whole session down.
    updateCompassVisibility();

    AxisFlowPanel* panel = AxisFlowPanel::instance();
    if (active) panel->show();
    else        panel->hide();
}

} // namespace AxisFlowGui
