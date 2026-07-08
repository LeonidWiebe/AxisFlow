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
    AxisFlowService& svc = AxisFlowService::instance();
    connect(&svc, &AxisFlowService::cursorMoved,  this, &AxisFlowTool::onCursorMoved);
    connect(&svc, &AxisFlowService::stateChanged, this, &AxisFlowTool::onServiceStateChanged);
    connect(&svc, &AxisFlowService::activeChanged, this, &AxisFlowTool::onActiveChanged);
    connect(&AxisFlowSettings::instance(), &AxisFlowSettings::changed,
            this, &AxisFlowTool::onSettingsChanged);

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

    Gui::View3DInventorViewer* viewer = _view->getViewer();
    SoSeparator* root = static_cast<SoSeparator*>(viewer->getSceneGraph());

    if (withPositionSource) {
        _eventCB = new SoEventCallback;
        _eventCB->ref();
        _eventCB->addEventCallback(SoEvent::getClassTypeId(), coinEventCB, this);
        root->insertChild(_eventCB, 0);
    }

    root->addChild(_compass->getRootNode());
    applyModeToUi();

    // Shared, autonomous panel.
    AxisFlowPanel* panel = AxisFlowPanel::instance();
    QPoint pos = _view->mapToGlobal(QPoint(20, 20));
    panel->move(pos);
    panel->show();

    // Adapter mode has no Coin key source: the 3-D view keeps focus while a
    // Draft/Arch tool draws. Install an app-wide filter so keys reach AxisFlow
    // without first clicking the panel. Standalone uses the Coin callback.
    if (!withPositionSource) {
        _keyFilter = new AxisFlowEventFilter(this);
        _keyFilter->install();
    }

    AxisFlowService::instance().activate();

    // First paint: orient to the working plane, size to the pixel setting and
    // apply "always show compass" before anything moves.
    syncCompassPlane();
    syncCompassSize();
    applyModeToUi();
    updateCompassVisibility();
    attachCameraSensor();
}

void AxisFlowTool::deactivate()
{
    if (!_active) return;
    _active = false;

    AxisFlowService::instance().deactivate();

    detachCameraSensor();

    if (_keyFilter) {
        _keyFilter->remove();
        delete _keyFilter;
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

        int qtKey = coinKeyToQt(ke->getKey());
        if (qtKey != 0) {
            AxisFlowService& svc = AxisFlowService::instance();
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

    Base::Vector3d solved = svc.currentSolved();
    _lastPoint = solved;

    svc.confirm(solved);   // moves origin, clears overrides + lock, emits signals

    _compass->setOrigin(SbVec3f(
        float(solved.x), float(solved.y), float(solved.z)));

    Q_EMIT pointConfirmed(solved);
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
