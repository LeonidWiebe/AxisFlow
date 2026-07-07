#include "PreCompiled.h"

#include "AxisFlowTool.h"
#include "AxisFlowPanel.h"
#include "AxisFlowEventFilter.h"

#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>

#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbLine.h>

#include <Base/Vector3D.h>

#include <QtGui/QKeyEvent>   // Qt::Key_*

namespace AxisFlowGui {

// ---------------------------------------------------------------------------

AxisFlowTool::AxisFlowTool(Gui::View3DInventor* view, QObject* parent)
    : QObject(parent)
    , _view(view)
{
    AxisFlowService& svc = AxisFlowService::instance();
    connect(&svc, &AxisFlowService::cursorMoved,  this, &AxisFlowTool::onCursorMoved);
    connect(&svc, &AxisFlowService::stateChanged, this, &AxisFlowTool::onServiceStateChanged);

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
        _keyFilter = new AxisFlowEventFilter(_view, this);
        _keyFilter->install();
    }

    AxisFlowService::instance().activate();
}

void AxisFlowTool::deactivate()
{
    if (!_active) return;
    _active = false;

    AxisFlowService::instance().deactivate();

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
}

void AxisFlowTool::onServiceStateChanged()
{
    // Keep the compass mode in sync (the panel manages its own display).
    applyModeToUi();
}

} // namespace AxisFlowGui
