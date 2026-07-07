import os
import sys
import FreeCAD
import FreeCADGui

_mod_dir = os.path.join(FreeCAD.getUserAppDataDir(), "Mod", "AxisFlow")
if _mod_dir not in sys.path:
    sys.path.insert(0, _mod_dir)

# ---------------------------------------------------------------------------
# Make the module's dependent DLLs (Qt6, Coin4, fmt, ...) resolvable.
#
# Since Python 3.8, an extension module's dependent DLLs are NOT looked up on
# PATH. Only these are searched: the .pyd's own directory, system directories,
# already-loaded modules, and directories registered with add_dll_directory().
# In a pixi source build the Qt6/Coin4/fmt DLLs live under Library\bin and the
# FreeCAD DLLs under build\release\bin, so we register them explicitly.
#
# NOTE: these are dev-machine paths (same layout as BUILD_WINDOWS.md); adjust
# if your FreeCAD/pixi lives elsewhere. Missing paths are skipped silently.
# ---------------------------------------------------------------------------
_dll_dirs = [
    r"C:\projects\FreeCAD111\.pixi\envs\default\Library\bin",
    r"C:\projects\FreeCAD111\.pixi\envs\default",
    r"C:\projects\FreeCAD111\build\release\bin",
]
for _d in _dll_dirs:
    if os.path.isdir(_d):
        try:
            os.add_dll_directory(_d)
        except (OSError, AttributeError):
            pass

import AxisFlow


class AxisFlowWorkbenchPy(FreeCADGui.Workbench):
    MenuText = "AxisFlow"
    ToolTip  = "Precise coordinate input (AxisFlow-style)"
    Icon     = ""

    def Initialize(self):
        self.list = ["AxisFlow_Activate"]
        self.appendToolbar("AxisFlow", self.list)
        self.appendMenu("AxisFlow", self.list)

    def Activated(self):
        pass

    def Deactivated(self):
        pass


FreeCADGui.addWorkbench(AxisFlowWorkbenchPy)
FreeCAD.Console.PrintLog("AxisFlow module loaded.\n")
