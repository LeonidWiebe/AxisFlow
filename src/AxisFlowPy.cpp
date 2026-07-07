#include "PreCompiled.h"

#include <string>

#include <Base/GeometryPyCXX.h>   // Py::Vector <-> Base::Vector3d
#include <Base/Vector3D.h>
#include <Base/Exception.h>

#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/View3DInventor.h>

#include "AxisFlowService.h"
#include "AxisFlowTool.h"

using namespace AxisFlowGui;

// ---------------------------------------------------------------------------
// Adapter-mode UI session (compass + shared panel, no Coin position source).
// Created on the active 3-D view; driven externally via updateCursor().
// ---------------------------------------------------------------------------

static AxisFlowTool* g_session = nullptr;

static Gui::View3DInventor* activeView3D()
{
    if (!Gui::Application::Instance)
        return nullptr;
    Gui::Document* doc = Gui::Application::Instance->activeDocument();
    if (!doc)
        return nullptr;
    return qobject_cast<Gui::View3DInventor*>(doc->getActiveView());
}

static AxisAction actionFromString(const std::string& s)
{
    if (s == "togglemode") return AxisAction::ToggleMode;
    if (s == "smartlock")  return AxisAction::SmartLock;
    if (s == "lockx")      return AxisAction::LockX;
    if (s == "locky")      return AxisAction::LockY;
    if (s == "lockz")      return AxisAction::LockZ;
    if (s == "clearlocks") return AxisAction::ClearLocks;
    return AxisAction::None;
}

// ===========================================================================
// Thin Python surface over AxisFlowService.
//
// This is the "open API" other workbenches / adapters call, e.g. from the
// Draft/Arch Snapper adapter:
//
//     import AxisFlow as AD
//     if AD.isActive():
//         pt = AD.solve(rawPoint, lastPoint)
//
// The C++ singleton remains the single source of truth; Python is only glue.
// ===========================================================================

namespace {

PyObject* axisflow_isActive(PyObject*, PyObject*)
{
    return PyBool_FromLong(AxisFlowService::instance().isActive() ? 1 : 0);
}

PyObject* axisflow_activate(PyObject*, PyObject*)
{
    AxisFlowService::instance().activate();
    Py_RETURN_NONE;
}

PyObject* axisflow_deactivate(PyObject*, PyObject*)
{
    AxisFlowService::instance().deactivate();
    Py_RETURN_NONE;
}

PyObject* axisflow_solve(PyObject*, PyObject* args)
{
    PyObject* rawO = nullptr;
    PyObject* lastO = nullptr;
    if (!PyArg_ParseTuple(args, "O|O", &rawO, &lastO))
        return nullptr;
    try {
        Base::Vector3d raw = Py::Vector(Py::Object(rawO)).toVector();
        Base::Vector3d out;
        if (lastO && lastO != Py_None) {
            Base::Vector3d last = Py::Vector(Py::Object(lastO)).toVector();
            out = AxisFlowService::instance().solve(raw, &last);
        }
        else {
            out = AxisFlowService::instance().solve(raw, nullptr);
        }
        return Py::new_reference_to(Py::Vector(out));
    }
    catch (Py::Exception&) {
        return nullptr;
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

PyObject* axisflow_confirm(PyObject*, PyObject* args)
{
    PyObject* pO = nullptr;
    if (!PyArg_ParseTuple(args, "O", &pO))
        return nullptr;
    try {
        Base::Vector3d p = Py::Vector(Py::Object(pO)).toVector();
        AxisFlowService::instance().confirm(p);
        Py_RETURN_NONE;
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject* axisflow_setPlane(PyObject*, PyObject* args)
{
    PyObject *uO = nullptr, *vO = nullptr, *nO = nullptr;
    if (!PyArg_ParseTuple(args, "OOO", &uO, &vO, &nO))
        return nullptr;
    try {
        Base::Vector3d u = Py::Vector(Py::Object(uO)).toVector();
        Base::Vector3d v = Py::Vector(Py::Object(vO)).toVector();
        Base::Vector3d n = Py::Vector(Py::Object(nO)).toVector();
        AxisFlowService::instance().setPlane(u, v, n);
        Py_RETURN_NONE;
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject* axisflow_resetPlane(PyObject*, PyObject*)
{
    AxisFlowService::instance().resetPlane();
    Py_RETURN_NONE;
}

PyObject* axisflow_setMode(PyObject*, PyObject* args)
{
    const char* m = nullptr;
    if (!PyArg_ParseTuple(args, "s", &m))
        return nullptr;
    std::string s(m ? m : "");
    AxisFlowService::instance().setMode(
        (s == "polar" || s == "Polar") ? CompassMode::Polar
                                        : CompassMode::Rectangular);
    Py_RETURN_NONE;
}

PyObject* axisflow_getMode(PyObject*, PyObject*)
{
    bool polar = AxisFlowService::instance().mode() == CompassMode::Polar;
    return PyUnicode_FromString(polar ? "polar" : "rect");
}

PyObject* axisflow_lockAxis(PyObject*, PyObject* args)
{
    const char* a = nullptr;
    if (!PyArg_ParseTuple(args, "s", &a))
        return nullptr;
    std::string s(a ? a : "");
    AxisLock lk = AxisLock::None;
    if      (s == "x" || s == "X") lk = AxisLock::X;
    else if (s == "y" || s == "Y") lk = AxisLock::Y;
    else if (s == "z" || s == "Z") lk = AxisLock::Z;
    AxisFlowService::instance().lockAxis(lk);
    Py_RETURN_NONE;
}

PyObject* axisflow_clearLocks(PyObject*, PyObject*)
{
    AxisFlowService::instance().clearLocks();
    Py_RETURN_NONE;
}

PyObject* axisflow_setKeyValue(PyObject*, PyObject* args)
{
    int field = 0;
    double val = 0.0;
    if (!PyArg_ParseTuple(args, "id", &field, &val))
        return nullptr;
    AxisFlowService::instance().setKeyValue(field, val);
    Py_RETURN_NONE;
}

PyObject* axisflow_clearKeyValues(PyObject*, PyObject*)
{
    AxisFlowService::instance().clearKeyValues();
    Py_RETURN_NONE;
}

PyObject* axisflow_getState(PyObject*, PyObject*)
{
    AxisFlowService& svc = AxisFlowService::instance();
    try {
        Py::Dict d;
        d.setItem("active", Py::Boolean(svc.isActive()));
        d.setItem("mode",   Py::String(svc.mode() == CompassMode::Polar ? "polar" : "rect"));

        const char* lockStr = "none";
        switch (svc.lock()) {
        case AxisLock::X: lockStr = "x"; break;
        case AxisLock::Y: lockStr = "y"; break;
        case AxisLock::Z: lockStr = "z"; break;
        default: break;
        }
        d.setItem("lock",        Py::String(lockStr));
        d.setItem("origin",      Py::Vector(svc.origin()));
        d.setItem("planeNormal", Py::Vector(svc.planeNormal()));
        d.setItem("distance",    Py::Float(svc.distance()));
        d.setItem("angle",       Py::Float(svc.angleDeg()));
        return Py::new_reference_to(d);
    }
    catch (Py::Exception&) {
        return nullptr;
    }
}

PyObject* axisflow_updateCursor(PyObject*, PyObject* args)
{
    PyObject* rawO = nullptr;
    PyObject* lastO = nullptr;
    if (!PyArg_ParseTuple(args, "O|O", &rawO, &lastO))
        return nullptr;
    try {
        Base::Vector3d raw = Py::Vector(Py::Object(rawO)).toVector();
        Base::Vector3d out;
        if (lastO && lastO != Py_None) {
            Base::Vector3d last = Py::Vector(Py::Object(lastO)).toVector();
            out = AxisFlowService::instance().updateCursor(raw, &last);
        }
        else {
            out = AxisFlowService::instance().updateCursor(raw, nullptr);
        }
        return Py::new_reference_to(Py::Vector(out));
    }
    catch (Py::Exception&) { return nullptr; }
    catch (const Base::Exception& e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

PyObject* axisflow_beginSession(PyObject*, PyObject*)
{
    // Always activate the service so constraints apply even without a view.
    AxisFlowService::instance().activate();

    if (g_session) {
        g_session->deactivate();
        delete g_session;
        g_session = nullptr;
    }

    Gui::View3DInventor* v = activeView3D();
    if (v) {
        g_session = new AxisFlowTool(v);
        g_session->activate(false);   // adapter mode: no Coin position source
        return PyBool_FromLong(1);
    }
    // No 3-D view: service is active, but there's no compass/panel UI.
    return PyBool_FromLong(0);
}

PyObject* axisflow_endSession(PyObject*, PyObject*)
{
    if (g_session) {
        g_session->deactivate();
        delete g_session;
        g_session = nullptr;
    }
    AxisFlowService::instance().deactivate();
    Py_RETURN_NONE;
}

PyObject* axisflow_setHotkey(PyObject*, PyObject* args)
{
    int qtKey = 0;
    const char* action = nullptr;
    if (!PyArg_ParseTuple(args, "is", &qtKey, &action))
        return nullptr;
    std::string s(action ? action : "");
    if (s == "none")
        AxisFlowService::instance().hotkeys().remove(qtKey);
    else
        AxisFlowService::instance().hotkeys().set(qtKey, actionFromString(s));
    Py_RETURN_NONE;
}

PyMethodDef AxisFlowMethods[] = {
    {"isActive",       axisflow_isActive,       METH_NOARGS,
     "isActive() -> bool. True if the AxisFlow constraint service is active."},
    {"activate",       axisflow_activate,       METH_NOARGS,
     "activate(). Enable the AxisFlow constraint service."},
    {"deactivate",     axisflow_deactivate,     METH_NOARGS,
     "deactivate(). Disable the AxisFlow constraint service."},
    {"solve",          axisflow_solve,          METH_VARARGS,
     "solve(raw[, last]) -> Vector. Constrain a raw world-space point."},
    {"updateCursor",   axisflow_updateCursor,   METH_VARARGS,
     "updateCursor(raw[, last]) -> Vector. Like solve(), but also updates the UI."},
    {"beginSession",   axisflow_beginSession,   METH_NOARGS,
     "beginSession() -> bool. Activate AxisFlow and show compass+panel on the active view."},
    {"endSession",     axisflow_endSession,     METH_NOARGS,
     "endSession(). Tear down the UI session and deactivate the service."},
    {"setHotkey",      axisflow_setHotkey,      METH_VARARGS,
     "setHotkey(qtKeyCode, 'togglemode'|'smartlock'|'lockx'|'locky'|'lockz'|'clearlocks'|'none')."},
    {"confirm",        axisflow_confirm,        METH_VARARGS,
     "confirm(point). Commit a point as the new compass origin."},
    {"setPlane",       axisflow_setPlane,       METH_VARARGS,
     "setPlane(u, v, normal). Set compass/working-plane orientation."},
    {"resetPlane",     axisflow_resetPlane,     METH_NOARGS,
     "resetPlane(). Reset compass orientation to world axes."},
    {"setMode",        axisflow_setMode,        METH_VARARGS,
     "setMode('rect'|'polar')."},
    {"getMode",        axisflow_getMode,        METH_NOARGS,
     "getMode() -> 'rect'|'polar'."},
    {"lockAxis",       axisflow_lockAxis,       METH_VARARGS,
     "lockAxis('x'|'y'|'z'|'none')."},
    {"clearLocks",     axisflow_clearLocks,     METH_NOARGS,
     "clearLocks(). Remove any axis lock."},
    {"setKeyValue",    axisflow_setKeyValue,    METH_VARARGS,
     "setKeyValue(field, value). rect: 0=X 1=Y 2=Z ; polar: 0=dist 1=angle 2=Z."},
    {"clearKeyValues", axisflow_clearKeyValues, METH_NOARGS,
     "clearKeyValues(). Clear keyboard override values."},
    {"getState",       axisflow_getState,       METH_NOARGS,
     "getState() -> dict. Snapshot of the current AxisFlow state."},
    {nullptr, nullptr, 0, nullptr}
};

} // anonymous namespace

// Exposed to AppAxisFlow.cpp so it can attach the table to the module def.
PyMethodDef* getAxisFlowMethods()
{
    return AxisFlowMethods;
}
