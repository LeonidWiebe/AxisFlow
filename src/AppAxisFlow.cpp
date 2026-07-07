#include "PreCompiled.h"

#ifdef FC_OS_WIN32
# include <windows.h>
#endif

#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/View3DInventor.h>

#include "Workbench.h"
#include "AxisFlowTool.h"
#include "AxisFlowService.h"

// Forward declarations — defined in other TUs
void CreateAxisFlowCommands();        // Command.cpp
PyMethodDef* getAxisFlowMethods();    // AxisFlowPy.cpp

extern "C" __declspec(dllexport) PyObject* PyInit_AxisFlow()
{
    if (!Gui::Application::Instance) {
        PyErr_SetString(PyExc_ImportError, "Cannot load Gui module in console application.");
        return nullptr;
    }

    CreateAxisFlowCommands();
    AxisFlowGui::AxisFlowWorkbench::init();

    // Touch the singleton so the service exists as soon as the module loads.
    AxisFlowGui::AxisFlowService::instance();

    static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT, "AxisFlow", nullptr, -1, nullptr
    };
    // Attach the API method table at runtime (can't be a constant initializer).
    moduledef.m_methods = getAxisFlowMethods();

    PyObject* mod = PyModule_Create(&moduledef);
    Base::Console().message("AxisFlow module loaded.\n");
    return mod;
}
