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
# NOTE: these are dev-machine paths (same layout as man/BUILD_WINDOWS.md); adjust
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

_icons_dir = os.path.join(_mod_dir, "Resources", "icons")
if os.path.isdir(_icons_dir):
    FreeCADGui.addIconPath(_icons_dir)

# ---------------------------------------------------------------------------
# Default hotkey layout. Kept in an editable hotkeys.ini next to this file so
# the defaults can be changed without rebuilding the C++ module. Layering, later
# wins: built-in layout -> hotkeys.ini -> keys the user rebound themselves, so
# editing the file never overrides a deliberate user choice.
# Missing or damaged file: the built-in layout stands (nothing breaks).
# ---------------------------------------------------------------------------
_hotkeys_ini = os.path.join(_mod_dir, "hotkeys.ini")
if os.path.isfile(_hotkeys_ini):
    try:
        AxisFlow.loadHotkeyDefaults(_hotkeys_ini)
    except Exception as _exc:                       # pragma: no cover
        FreeCAD.Console.PrintLog(
            "AxisFlow: could not read hotkeys.ini: {0}\n".format(_exc))

# ---------------------------------------------------------------------------
# Interface translation. Per FreeCAD policy the workbench follows FreeCAD's own
# UI language (Preferences -> General -> Language) rather than offering its own
# switcher -- so it also matches FreeCAD's language on first start. We only
# register where our compiled .qm files live; FreeCAD's translator picks the one
# matching the current language. Community translations: see man/TRANSLATING.md.
# ---------------------------------------------------------------------------
_tr_dir = os.path.join(_mod_dir, "translations")
if os.path.isdir(_tr_dir):
    try:
        FreeCADGui.addLanguagePath(_tr_dir)
        FreeCADGui.updateLocale()
    except Exception as _exc:                       # pragma: no cover
        FreeCAD.Console.PrintLog(
            "AxisFlow: could not register translations: {0}\n".format(_exc))


class AxisFlowWorkbenchPy(FreeCADGui.Workbench):
    # NOTE: FreeCAD runs InitGui.py with separate globals/locals, so names
    # assigned at module top level (like _icons_dir) are NOT visible inside a
    # class/def body. Keep the body literal-only and set Icon below at top level.
    MenuText = "AxisFlow"
    ToolTip  = "Precise coordinate input (AxisFlow-style)"
    Icon     = ""

    def Initialize(self):
        self.list = [
            "AxisFlow_Activate",
            "AxisFlow_DrawLine",
            "AxisFlow_ToggleLiveLine",
            "AxisFlow_Settings",
        ]
        self.appendToolbar("AxisFlow", self.list)
        self.appendMenu("AxisFlow", self.list)

        # Snap palette. AxisFlow borrows FreeCAD's own object snapping instead
        # of reimplementing it, so it can simply reuse Draft's snap commands:
        # commands are global, the toolbar looks and behaves like everywhere
        # else, and the on/off state stays shared with Draft and BIM.
        try:
            import axisflow_snapping
            snaps = axisflow_snapping.snap_commands()
            if snaps:
                self.appendToolbar("AxisFlow snapping", snaps)
                self.appendMenu([("AxisFlow"), ("Snapping")], snaps)
        except Exception as exc:
            FreeCAD.Console.PrintLog(
                "AxisFlow: snap palette unavailable: {0}\n".format(exc))

    def Activated(self):
        pass

    def Deactivated(self):
        pass


# Top level: names ARE visible here, so resolve the icon path now.
AxisFlowWorkbenchPy.Icon = os.path.join(_icons_dir, "AxisFlow.svg")


FreeCADGui.addWorkbench(AxisFlowWorkbenchPy)
FreeCAD.Console.PrintLog("AxisFlow module loaded.\n")
