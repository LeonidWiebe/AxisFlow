#include "PreCompiled.h"

#include <QtCore/QTimer>

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
#include <Inventor/SbViewVolume.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbLine.h>

#include <Base/Vector3D.h>
#include <Base/Interpreter.h>   // PyGILStateLocker + Python.h

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

    // The one connection that IS tied to this object's lifetime: closing a
    // document destroys its view, and everything we hang off that view -- the
    // scenegraph nodes, the camera, the viewer -- goes with it. Tear down here,
    // while the tool is still alive, instead of discovering the corpse later.
    if (_view) {
        connect(_view.data(), &QObject::destroyed,
                this, &AxisFlowTool::onViewDestroyed);
    }
}

AxisFlowTool::~AxisFlowTool()
{
    deactivate();
    delete _compass;
}

// ---------------------------------------------------------------------------

AxisFlowTool* AxisFlowTool::s_current = nullptr;

void AxisFlowTool::activate(bool withPositionSource)
{
    if (_active) return;

    // One session at a time, whoever owns it: the palette command and the
    // Python API (getPoint) each hold a tool of their own. Standing the other
    // one down FIRST matters -- its deactivate() clears s_current, so claiming
    // it before this point would make the check see itself and skip.
    if (s_current && s_current != this)
        s_current->deactivate();

    _active = true;
    s_current = this;
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
    connect(&svc, &AxisFlowService::alignToViewRequested, this, &AxisFlowTool::alignPlaneToView);
    connect(&AxisFlowSettings::instance(), &AxisFlowSettings::changed,
            this, &AxisFlowTool::onSettingsChanged);

    // The view can already be gone: the tool may have been parked while its
    // document was closed. Without a view there is no scenegraph to hang the
    // compass on, so there is nothing to activate.
    if (!_view) {
        _active = false;
        if (s_current == this)
            s_current = nullptr;
        Base::Console().warning("AxisFlow: the 3D view is gone; not activating.\n");
        return;
    }

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
    _keyFilter = new AxisFlowEventFilter(_view.data(), withPositionSource, this);
    _keyFilter->install();

    AxisFlowService::instance().activate();

    // Pull indexing enable flags + tolerance from settings into the solver.
    AxisFlowService::instance().applyIndexingSettings();

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
    // Called from inside the Coin callback (a consumer ending the session from
    // its move/point handler)? Then the node we are about to unref is the one
    // being dispatched. Record it and let handleCoinEvent run the teardown
    // once Coin is done with the node.
    if (_inCoinCallback) {
        _deactivateDeferred = true;
        return;
    }

    if (!_active) return;
    if (s_current == this)
        s_current = nullptr;
    _active = false;

    // Stop listening immediately. With deferred destruction this object may
    // outlive the current teardown; it must not react to the service signals
    // emitted below (or to a new session's signals) once it is being retired.
    disconnect(&AxisFlowService::instance(),  nullptr, this, nullptr);
    disconnect(&AxisFlowSettings::instance(), nullptr, this, nullptr);

    AxisFlowPanel::instance()->setOwnsInput(false);

    // Drop any live object snap and take Draft's snap markers off the screen.
    AxisFlowService::instance().clearSnap();
    hideSnapMarkers();

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

    // Scenegraph teardown only makes sense while the view is alive. When the
    // document was closed the viewer, its scenegraph and our nodes in it are
    // already gone -- touching them here was the crash. The Coin callback still
    // has to be unref'd: we own that reference regardless of the view.
    if (_view) {
        Gui::View3DInventorViewer* viewer = _view->getViewer();
        SoSeparator* root = static_cast<SoSeparator*>(viewer->getSceneGraph());
        if (_eventCB)
            root->removeChild(_eventCB);
        root->removeChild(_compass->getRootNode());
    }
    if (_eventCB) {
        _eventCB->unref();
        _eventCB = nullptr;
    }

    AxisFlowPanel::instance()->hide();
}

// ---------------------------------------------------------------------------

void AxisFlowTool::coinEventCB(void* userData, SoEventCallback* node)
{
    static_cast<AxisFlowTool*>(userData)->handleCoinEvent(node);
}

void AxisFlowTool::handleCoinEvent(SoEventCallback* node)
{
    // Everything below can call into Python -- object snapping, and
    // cursorMoved reaching the getPoint consumer -- and the consumer may end
    // the session from there. Deactivating synchronously would destroy this
    // very SoEventCallback mid-dispatch, so the guard defers it to the event
    // loop instead.
    _inCoinCallback = true;
    handleCoinEventBody(node);
    _inCoinCallback = false;

    if (_deactivateDeferred) {
        _deactivateDeferred = false;
        // Context object `this`: if the tool is destroyed first (endSession
        // also calls deleteLater), Qt drops the call instead of running it on
        // a corpse -- and the destructor deactivates anyway.
        QTimer::singleShot(0, this, [this] { deactivate(); });
    }
}

void AxisFlowTool::handleCoinEventBody(SoEventCallback* node)
{
    const SoEvent* event = node->getEvent();

    if (event->isOfType(SoLocation2Event::getClassTypeId())) {
        const auto* me = static_cast<const SoLocation2Event*>(event);
        AxisFlowService& svc = AxisFlowService::instance();

        // Push the current pixel scale first, so indexing's pixel tolerances are
        // evaluated against THIS frame's zoom during the solve below.
        svc.setViewScale(computeWorldPerPixel());

        // Object snapping (Draft's Snapper) takes precedence over the plain
        // projection: a snapped point is an explicit geometric target, and it
        // may legitimately sit off the compass plane (its Z then shows up in
        // the panel). Falls back to the plane when nothing is snapped.
        Base::Vector3d snapped;
        if (AxisFlowSettings::instance().useSnapping()
            && querySnap(me->getPosition(), snapped)) {
            svc.setSnapPoint(snapped);
        }
        else {
            svc.clearSnap();
        }

        // The raw cursor is still needed even while snapped: the axis hold
        // decides which axis to ride from where the CURSOR is, not the snap.
        SbVec3f worldPt = unprojectToCompassPlane(me->getPosition());
        Base::Vector3d raw(worldPt[0], worldPt[1], worldPt[2]);
        svc.updateCursor(raw, nullptr);   // -> cursorMoved
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

    // NOTE: keyboard events are deliberately NOT handled here. Qt resolves
    // global shortcuts (bare digits are bound to Std_ViewTop and friends)
    // before the key ever reaches this callback, so AxisFlowEventFilter claims
    // every key we care about at the qApp level and this branch could only ever
    // see keys nobody wants. It used to carry a second, always-incomplete key
    // map (it never learned P/O/T/F/S/V/E or the backtick) that also ignored
    // modifiers, so Ctrl+Y could fire the Y lock. One key policy now:
    // routeKey() in AxisFlowKeyRouting.h.
}

// ---------------------------------------------------------------------------

bool AxisFlowTool::querySnap(const SbVec2s& pixel, Base::Vector3d& out)
{
    // The Snapper is Python, so the query goes through a one-function bridge
    // (axisflow_snapping.snap). Marshalling plain floats keeps this side free
    // of App.Vector construction. The module object is cached: importing on
    // every mouse move would be needlessly expensive.
    Base::PyGILStateLocker lock;

    static PyObject* s_fn = nullptr;
    static bool s_tried = false;
    if (!s_tried) {
        s_tried = true;
        PyObject* mod = PyImport_ImportModule("axisflow_snapping");
        if (mod) {
            s_fn = PyObject_GetAttrString(mod, "snap");   // new ref, kept
            Py_DECREF(mod);
        }
        PyErr_Clear();
    }
    if (!s_fn)
        return false;

    // Previous point: Draft needs it for extension / parallel / perpendicular.
    const Base::Vector3d o = AxisFlowService::instance().origin();
    // During a polyline the origin IS the previous point, which is exactly what
    // Draft wants for extension / parallel / perpendicular snaps.
    const bool hasOrigin = AxisFlowService::instance().isActive();

    PyObject* res = PyObject_CallFunction(s_fn, "iidddi",
                                          int(pixel[0]), int(pixel[1]),
                                          o.x, o.y, o.z, hasOrigin ? 1 : 0);
    if (!res) {
        PyErr_Clear();       // never let a Python error break the tool
        return false;
    }

    double px = 0.0, py = 0.0, pz = 0.0;
    int hit = 0;
    const bool ok = (PyArg_ParseTuple(res, "dddi", &px, &py, &pz, &hit) != 0);
    if (!ok)
        PyErr_Clear();
    Py_DECREF(res);

    if (!ok || hit == 0)
        return false;

    out = Base::Vector3d(px, py, pz);
    return true;
}

void AxisFlowTool::hideSnapMarkers()
{
    Base::PyGILStateLocker lock;
    PyObject* mod = PyImport_ImportModule("axisflow_snapping");
    if (mod) {
        PyObject* r = PyObject_CallMethod(mod, "hide", nullptr);
        Py_XDECREF(r);
        Py_DECREF(mod);
    }
    PyErr_Clear();
}

SbVec3f AxisFlowTool::unprojectToCompassPlane(const SbVec2s& pixel)
{
    if (!_view) return SbVec3f(0, 0, 0);
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
    // The origin still points at the LAST session's point: the first confirm
    // of this session must not rotate the compass along that stale segment.
    AxisFlowService::instance().clearFollowReference();
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

float AxisFlowTool::computeWorldPerPixel()
{
    // The setting is in PIXELS, the scene graph works in world units, so the
    // conversion needs the camera. Perspective cameras additionally depend on
    // the distance to the compass, hence the recompute on every cursor move.
    if (!_view) return 0.0f;

    Gui::View3DInventorViewer* viewer = _view->getViewer();
    if (!viewer) return 0.0f;

    SoRenderManager* rm = viewer->getSoRenderManager();
    if (!rm) return 0.0f;

    SoCamera* cam = rm->getCamera();
    if (!cam) return 0.0f;

    const SbVec2s vpPix = rm->getViewportRegion().getViewportSizePixels();
    const int hPix = vpPix[1];
    if (hPix <= 0) return 0.0f;

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
        return 0.0f;   // unknown camera type
    }

    return worldHeight / float(hPix);
}

void AxisFlowTool::syncCompassSize()
{
    const float worldPerPixel = computeWorldPerPixel();
    if (worldPerPixel <= 0.0f) return;   // no camera yet: leave the last radius

    // Feed the pixel scale to the service so indexing tolerances (also in
    // pixels) track zoom. Silent setter -- no re-emit, so this cannot recurse.
    AxisFlowService::instance().setViewScale(worldPerPixel);

    const int sizePx = AxisFlowSettings::instance().compassSizePx();
    _compass->setRadius(0.5f * float(sizePx) * worldPerPixel);
}

void AxisFlowTool::refreshIndexingCandidates()
{
    if (!_compass) return;
    AxisFlowService& svc = AxisFlowService::instance();
    const IndexingResult r = svc.currentIndexing();
    _compass->setIndexingCandidates(r.distanceActive, r.axisActive);

    // Compass path: drawn exactly when the point stops being "wherever the
    // cursor is" -- an object snap has claimed it, or a coordinate field is
    // frozen and holding one of its components.
    //
    // The in-plane lock flags only make sense in ortho mode, where fields 0
    // and 1 ARE the plane's two axes. In radial mode they are distance and
    // angle, which do not map to an axis leg, so the path falls back to the
    // plain "in-plane leg, then normal leg" shape.
    const bool ortho = (svc.mode() == CompassMode::Rectangular);
    const Base::Vector3d sp = svc.snapPoint();
    _compass->setPathState(
        svc.snapActive() || svc.anyFieldFixed(),
        ortho && svc.hasKeyValue(0),
        ortho && svc.hasKeyValue(1),
        svc.snapActive(),
        SbVec3f(float(sp.x), float(sp.y), float(sp.z)));
}

void AxisFlowTool::onSettingsChanged()
{
    if (!_compass) return;
    _compass->applySettings();
    // Enable flags / tolerance drive the solver; push them, then re-evaluate
    // the candidate state at the current cursor.
    AxisFlowService::instance().applyIndexingSettings();
    syncCompassSize();
    updateCompassVisibility();
    refreshIndexingCandidates();
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
    auto* self = static_cast<AxisFlowTool*>(userData);
    self->syncCompassSize();
    // Zoom changes the pixel value of a fixed world offset, so the indexing
    // decision can flip with no mouse motion. Re-solve + re-broadcast; the
    // silent setViewScale above means this does not recurse.
    AxisFlowService::instance().refreshCursor();
}

void AxisFlowTool::onViewDestroyed()
{
    // Drop the view FIRST, then tear down. deactivate() skips every
    // view-dependent step when _view is null, so this is a teardown that
    // touches only what we own: the panel, the service, the key filter and our
    // Coin reference. Clearing it by hand rather than trusting QPointer to have
    // been cleared already keeps the ordering independent of Qt's internals
    // (for widgets, destroyed() is emitted from ~QWidget).
    _view = nullptr;
    deactivate();
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
    //
    // The camera is ref'd for as long as the sensor watches it. Otherwise the
    // view's destruction frees the camera under a still-attached sensor, and
    // detaching afterwards writes into freed memory.
    _camSensor = new SoNodeSensor(&AxisFlowTool::cameraChangedCB, this);
    _camWatched = cam;
    _camWatched->ref();
    _camSensor->attach(cam);
}

void AxisFlowTool::detachCameraSensor()
{
    if (!_camSensor) return;
    // Safe even after the view died: our own reference has kept the camera
    // alive precisely so that this detach has something to detach from.
    _camSensor->detach();
    delete _camSensor;
    _camSensor = nullptr;

    if (_camWatched) {
        _camWatched->unref();     // may well be the last reference now
        _camWatched = nullptr;
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

    // Indexing candidate markers (candidate point / candidate axis) follow the
    // solved point; the solver already snapped 'solved' when either is active.
    refreshIndexingCandidates();
}

void AxisFlowTool::onServiceStateChanged()
{
    // Mode drives the contour shape (square vs circle); the plane may have been
    // pushed by an adapter or by setPlane() from Python.
    applyModeToUi();
    syncCompassPlane();

    // The origin can move WITHOUT a mouse move (SetOrigin, confirm from the
    // API): re-anchor the compass now instead of waiting for the next cursor
    // event.
    const Base::Vector3d o = AxisFlowService::instance().origin();
    _compass->setOrigin(SbVec3f(float(o.x), float(o.y), float(o.z)));
}

void AxisFlowTool::alignPlaneToView()
{
    // Camera basis in world space: right / up / back (Coin cameras look down
    // their local -Z, so +Z points back at the viewer -- that is our normal).
    if (!_view) return;
    Gui::View3DInventorViewer* viewer = _view->getViewer();
    if (!viewer) return;
    SoRenderManager* rm = viewer->getSoRenderManager();
    if (!rm) return;
    SoCamera* cam = rm->getCamera();
    if (!cam) return;

    const SbRotation rot = cam->orientation.getValue();
    SbVec3f right(1, 0, 0), up(0, 1, 0), back(0, 0, 1);
    rot.multVec(right, right);
    rot.multVec(up, up);
    rot.multVec(back, back);
    AxisFlowService::instance().setPlane(
        Base::Vector3d(right[0], right[1], right[2]),
        Base::Vector3d(up[0],    up[1],    up[2]),
        Base::Vector3d(back[0],  back[1],  back[2]));
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
