#include "PreCompiled.h"

#include <string>
#include <vector>

#include <Base/GeometryPyCXX.h>   // Py::Vector <-> Base::Vector3d
#include <Base/Vector3D.h>
#include <Base/Exception.h>

#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/View3DInventor.h>

#include "AxisFlowService.h"
#include "AxisFlowSettings.h"
#include "AxisFlowCompass.h"
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
    if (s == "toggleactive") return AxisAction::ToggleActive;
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

PyObject* axisflow_setConstrainEnabled(PyObject*, PyObject* args)
{
    int on = 1;
    if (!PyArg_ParseTuple(args, "p", &on))
        return nullptr;
    AxisFlowService::instance().setConstrainEnabled(on != 0);
    Py_RETURN_NONE;
}

PyObject* axisflow_constrainEnabled(PyObject*, PyObject*)
{
    return PyBool_FromLong(AxisFlowService::instance().constrainEnabled() ? 1 : 0);
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
    int persist = 1;
    if (!PyArg_ParseTuple(args, "is|p", &qtKey, &action, &persist))
        return nullptr;
    std::string s(action ? action : "");
    // 'none' unbinds. persist=False keeps the change session-scoped (used by
    // the Draft adapter, which must not overwrite the user's stored layout).
    AxisFlowService::instance().setHotkey(qtKey, actionFromString(s), persist != 0);
    Py_RETURN_NONE;
}

// ---------------------------------------------------------------------------
// Settings (block C): ParameterGrp-backed, shared by every consumer.
// ---------------------------------------------------------------------------

PyObject* axisflow_getSettings(PyObject*, PyObject*)
{
    AxisFlowSettings& s = AxisFlowSettings::instance();
    PyObject* d = PyDict_New();
    if (!d) return nullptr;

    auto setItem = [d](const char* k, PyObject* v) {
        if (v) { PyDict_SetItemString(d, k, v); Py_DECREF(v); }
    };
    setItem("compassSize",      PyLong_FromLong(s.compassSizePx()));
    setItem("axisThickness",    PyLong_FromLong(s.axisThicknessPx()));
    setItem("axisStartPct",     PyFloat_FromDouble(s.axisStartPct()));
    setItem("axisLengthPct",    PyFloat_FromDouble(s.axisLengthPct()));
    setItem("axisColorX",       PyLong_FromUnsignedLong(s.axisColorX()));
    setItem("axisColorY",       PyLong_FromUnsignedLong(s.axisColorY()));
    setItem("activeFieldColor", PyLong_FromUnsignedLong(s.activeFieldColor()));
    setItem("fillTransparency", PyFloat_FromDouble(s.fillTransparency()));
    setItem("alwaysShowCompass", PyBool_FromLong(s.alwaysShowCompass() ? 1 : 0));

    // Hotkeys as {'SmartLock': [16777220, 16777221], ...}; [] means unbound.
    PyObject* hk = PyDict_New();
    if (hk) {
        const Hotkeys& h = AxisFlowService::instance().hotkeys();
        for (AxisAction a : Hotkeys::allActions()) {
            const std::vector<int> keys = h.keysFor(a);
            PyObject* lst = PyList_New(0);
            if (!lst) continue;
            for (int k : keys) {
                PyObject* v = PyLong_FromLong(k);
                if (v) { PyList_Append(lst, v); Py_DECREF(v); }
            }
            PyDict_SetItemString(hk, Hotkeys::name(a), lst);
            Py_DECREF(lst);
        }
        PyDict_SetItemString(d, "hotkeys", hk);
        Py_DECREF(hk);
    }
    return d;
}

PyObject* axisflow_setSetting(PyObject*, PyObject* args)
{
    const char* name = nullptr;
    PyObject*   val  = nullptr;
    if (!PyArg_ParseTuple(args, "sO", &name, &val))
        return nullptr;

    AxisFlowSettings& s = AxisFlowSettings::instance();
    const std::string k(name ? name : "");

    if (k == "compassSize")            s.setCompassSizePx(static_cast<int>(PyLong_AsLong(val)));
    else if (k == "axisThickness")     s.setAxisThicknessPx(static_cast<int>(PyLong_AsLong(val)));
    else if (k == "axisStartPct")      s.setAxisStartPct(PyFloat_AsDouble(val));
    else if (k == "axisLengthPct")     s.setAxisLengthPct(PyFloat_AsDouble(val));
    else if (k == "axisColorX")        s.setAxisColorX(PyLong_AsUnsignedLong(val));
    else if (k == "axisColorY")        s.setAxisColorY(PyLong_AsUnsignedLong(val));
    else if (k == "activeFieldColor")  s.setActiveFieldColor(PyLong_AsUnsignedLong(val));
    else if (k == "fillTransparency")  s.setFillTransparency(PyFloat_AsDouble(val));
    else if (k == "alwaysShowCompass") s.setAlwaysShowCompass(PyObject_IsTrue(val) == 1);
    else {
        PyErr_Format(PyExc_KeyError, "unknown AxisFlow setting: %s", k.c_str());
        return nullptr;
    }

    if (PyErr_Occurred())   // a bad value type surfaced from PyLong/PyFloat
        return nullptr;
    Py_RETURN_NONE;
}

PyObject* axisflow_resetSettings(PyObject*, PyObject*)
{
    AxisFlowSettings::instance().resetDefaults();
    // Rebuild the live key map from the (now clean) settings.
    AxisFlowService::instance().hotkeys().setDefaults();
    AxisFlowService::instance().loadHotkeysFromSettings();
    Py_RETURN_NONE;
}

PyObject* axisflow_loadHotkeysFromSettings(PyObject*, PyObject*)
{
    // Built-in defaults first, then the user's stored overrides on top. Used to
    // undo session-scoped rebinds (see setHotkey persist=False).
    AxisFlowService::instance().hotkeys().setDefaults();
    AxisFlowService::instance().loadHotkeysFromSettings();
    Py_RETURN_NONE;
}

PyObject* axisflow_getCompassInfo(PyObject*, PyObject*)
{
    // Diagnostics: the compass lives in C++, so when it "disappears" there is no
    // way to tell a degenerate radius from a hidden switch without this.
    PyObject* d = PyDict_New();
    if (!d) return nullptr;

    auto put = [d](const char* k, PyObject* v) {
        if (v) { PyDict_SetItemString(d, k, v); Py_DECREF(v); }
    };

    AxisFlowCompass* c = g_session ? g_session->compass() : nullptr;
    put("hasSession", PyBool_FromLong(g_session ? 1 : 0));
    put("hasCompass", PyBool_FromLong(c ? 1 : 0));
    if (c) {
        put("radius",  PyFloat_FromDouble(double(c->radius())));
        put("visible", PyBool_FromLong(c->visible() ? 1 : 0));
    }

    AxisFlowService& svc = AxisFlowService::instance();
    put("active", PyBool_FromLong(svc.isActive() ? 1 : 0));
    put("mode",   PyUnicode_FromString(svc.mode() == CompassMode::Polar ? "polar" : "rect"));

    const Base::Vector3d o = svc.origin();
    PyObject* org = Py_BuildValue("(ddd)", o.x, o.y, o.z);
    put("origin", org);

    const Base::Vector3d u = svc.planeU(), v = svc.planeV(), n = svc.planeNormal();
    put("planeU", Py_BuildValue("(ddd)", u.x, u.y, u.z));
    put("planeV", Py_BuildValue("(ddd)", v.x, v.y, v.z));
    put("planeN", Py_BuildValue("(ddd)", n.x, n.y, n.z));
    return d;
}

PyMethodDef AxisFlowMethods[] = {
    {"isActive",       axisflow_isActive,       METH_NOARGS,
     "isActive() -> bool. True if the AxisFlow constraint service is active."},
    {"setConstrainEnabled", axisflow_setConstrainEnabled, METH_VARARGS,
     "setConstrainEnabled(bool). Off in the Draft adapter: keep the lock as UI "
     "state only and let Draft do the constraining (avoids a double lock)."},
    {"constrainEnabled",    axisflow_constrainEnabled,    METH_NOARGS,
     "constrainEnabled() -> bool."},
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
     "setHotkey(qtKeyCode, 'toggleactive'|'togglemode'|'smartlock'|'lockx'|'locky'|'lockz'"
     "|'clearlocks'|'none'[, persist=True]). persist=False = session-scoped rebind."},
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
    {"getSettings",    axisflow_getSettings,    METH_NOARGS,
     "getSettings() -> dict. All persistent options plus the 'hotkeys' map."},
    {"setSetting",     axisflow_setSetting,     METH_VARARGS,
     "setSetting(name, value). name in {compassSize, axisThickness, axisStartPct, "
     "axisLengthPct, axisColorX, axisColorY, activeFieldColor, fillTransparency, "
     "alwaysShowCompass}."},
    {"resetSettings",  axisflow_resetSettings,  METH_NOARGS,
     "resetSettings(). Restore built-in defaults (options and hotkeys)."},
    {"loadHotkeysFromSettings", axisflow_loadHotkeysFromSettings, METH_NOARGS,
     "loadHotkeysFromSettings(). Re-apply defaults + stored overrides."},
    {"getCompassInfo", axisflow_getCompassInfo, METH_NOARGS,
     "getCompassInfo() -> dict. Live compass radius/visibility/mode/plane."},
    {nullptr, nullptr, 0, nullptr}
};

} // anonymous namespace

// Exposed to AppAxisFlow.cpp so it can attach the table to the module def.
PyMethodDef* getAxisFlowMethods()
{
    return AxisFlowMethods;
}
