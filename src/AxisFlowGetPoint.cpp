#include "PreCompiled.h"

#include "AxisFlowGetPoint.h"
#include "AxisFlowTool.h"
#include "AxisFlowService.h"

#include <Base/GeometryPyCXX.h>   // Py::Vector <-> Base::Vector3d
#include <Base/Interpreter.h>     // Base::PyGILStateLocker

namespace AxisFlowGui {

GetPointBridge* GetPointBridge::s_active = nullptr;

GetPointBridge::GetPointBridge(AxisFlowTool* tool,
                               PyObject* callback,
                               PyObject* moveCallback,
                               PyObject* finishCallback,
                               PyObject* modeCallback,
                               bool continuous,
                               QObject* parent)
    : QObject(parent)
    , _tool(tool)
    , _cb(callback)
    , _moveCb(moveCallback)
    , _finishCb(finishCallback)
    , _modeCb(modeCallback)
    , _continuous(continuous)
{
    // Own a reference to each callable for our lifetime.
    Base::PyGILStateLocker lock;
    Py_XINCREF(_cb);
    Py_XINCREF(_moveCb);
    Py_XINCREF(_finishCb);
    Py_XINCREF(_modeCb);
}

GetPointBridge::~GetPointBridge()
{
    Base::PyGILStateLocker lock;
    Py_XDECREF(_cb);
    Py_XDECREF(_moveCb);
    Py_XDECREF(_finishCb);
    Py_XDECREF(_modeCb);
}

void GetPointBridge::discardActive()
{
    if (s_active) {
        GetPointBridge* b = s_active;
        s_active = nullptr;             // stop it firing anything further
        b->disconnect();
        b->deleteLater();
    }
}

void GetPointBridge::start()
{
    discardActive();                    // only one at a time
    s_active = this;

    AxisFlowService& svc = AxisFlowService::instance();
    connect(_tool, &AxisFlowTool::pointConfirmed, this, &GetPointBridge::onPoint);
    // The tool is deleteLater'd whenever the session is replaced (a new
    // document brings a new view, so ensureOwnSession() builds a new tool).
    // The bridge can outlive that by an event-loop turn, and finish() would
    // then call stopCollecting() through a dangling pointer.
    connect(_tool, &QObject::destroyed, this, [this] { _tool = nullptr; });
    connect(&svc,  &AxisFlowService::cancelRequested, this, &GetPointBridge::onCancel);
    connect(&svc,  &AxisFlowService::finishRequested, this, &GetPointBridge::onFinish);
    connect(&svc,  &AxisFlowService::segmentModeRequested,
            this, &GetPointBridge::onModeToggle);
    if (_moveCb)
        connect(&svc, &AxisFlowService::cursorMoved, this, &GetPointBridge::onMove);
}

// ---------------------------------------------------------------------------

void GetPointBridge::onPoint(Base::Vector3d p)
{
    const bool cont = _continuous;
    callPoint(&p, true);

    // The callback may have started a NEW getPoint (the classic "line" pattern:
    // request the second point from inside the first point's callback). If so we
    // were already disconnected and scheduled for deletion by discardActive();
    // touching the tool now would clobber the new session's state.
    if (s_active != this)
        return;

    if (!cont)
        finish();
}

void GetPointBridge::onCancel()
{
    callPoint(nullptr, false);
    if (s_active != this)
        return;
    finish();
}

void GetPointBridge::onFinish()
{
    // Finish the current line but DO NOT finish the bridge: the session stays
    // active and keeps collecting points for the next line.
    callFinish();
}

void GetPointBridge::onMove(Base::Vector3d p)
{
    callMove(p);
}

// ---------------------------------------------------------------------------

void GetPointBridge::callPoint(const Base::Vector3d* p, bool ok)
{
    if (!_cb) return;

    Base::PyGILStateLocker lock;
    try {
        Py::Object pt = p ? Py::Object(Py::Vector(*p)) : Py::None();
        PyObject* res = PyObject_CallFunction(
            _cb, "Oi", pt.ptr(), ok ? 1 : 0);
        if (res) Py_DECREF(res);
        else     PyErr_Print();   // never let a consumer error kill the tool
    }
    catch (...) {
        PyErr_Print();
    }
}

void GetPointBridge::callMove(const Base::Vector3d& p)
{
    if (!_moveCb) return;

    Base::PyGILStateLocker lock;
    try {
        Py::Object pt = Py::Object(Py::Vector(p));
        PyObject* res = PyObject_CallFunction(_moveCb, "O", pt.ptr());
        if (res) Py_DECREF(res);
        else     PyErr_Print();
    }
    catch (...) {
        PyErr_Print();
    }
}

void GetPointBridge::onModeToggle()
{
    // Pure relay: the mode belongs to the consumer, not to AxisFlow. The input
    // state machine is untouched, so a half-placed point survives the toggle.
    callMode();
}

void GetPointBridge::callMode()
{
    if (!_modeCb) return;
    Base::PyGILStateLocker lock;
    PyObject* res = PyObject_CallFunction(_modeCb, nullptr);
    if (res) Py_DECREF(res);
    else     PyErr_Print();
}

void GetPointBridge::callFinish()
{
    if (!_finishCb) return;

    Base::PyGILStateLocker lock;
    try {
        PyObject* res = PyObject_CallFunction(_finishCb, nullptr);
        if (res) Py_DECREF(res);
        else     PyErr_Print();
    }
    catch (...) {
        PyErr_Print();
    }
}

void GetPointBridge::finish()
{
    if (s_active == this)
        s_active = nullptr;

    disconnect();                 // no more signals reach us
    if (_tool)
        _tool->stopCollecting();  // clicks stop placing points; UI stays up

    deleteLater();                // safe: plain QObject, not in the scene graph
}

} // namespace AxisFlowGui
