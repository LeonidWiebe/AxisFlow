# AxisFlow Workbench for FreeCAD

version 0.1 alpha

**AxisFlow** is a precision drawing assistant for FreeCAD, inspired by the workflow concept of **MicroStation AccuDraw**.

The goal of AxisFlow is to provide a more intuitive and efficient way of creating 2D and 3D geometry by introducing a dynamic local coordinate system, contextual axes, and guided input directly at the cursor position.

Instead of switching between tools, panels, and coordinate dialogs, AxisFlow helps users maintain focus on the modeling process by providing:

* dynamic X/Y/Z axis guidance;
* contextual local coordinate systems;
* fast distance and angle input;
* improved control of drawing direction;
* a workflow based on user intent rather than repetitive manual constraints.

AxisFlow extends the FreeCAD modeling experience with an interactive drawing approach designed for users who value speed, accuracy, and a more natural connection between mouse movement and geometric creation.

The project is inspired by the principles behind MicroStation AccuDraw, adapted for the FreeCAD environment and its parametric modeling workflow.

---

## What AxisFlow is (and is not)

AxisFlow is a reusable **coordinate-input module**, not a drawing workbench.

It supplies the input half of a drawing tool: the compass anchored at a reference point, ortho and radial modes, axis locks, smart lock to the nearest axis, unit-aware value entry, a configurable key map, and a confirmed point. **AxisFlow never creates document objects.** No lines, no shapes. What to build from the points it hands back is entirely the consumer's decision.

That separation is the whole design. Any workbench, command, or macro can use AxisFlow through its Python API without AxisFlow knowing anything about it — the dependency runs one way only. A consumer is free to build its geometry with whatever it likes, including FreeCAD's own `Draft.make_line` / `make_wire` / `make_circle`, which live in `draftmake/*` and touch neither `Snapper` nor `DraftToolBar`.

Consequently AxisFlow ships no Line, Rectangle, Arc or Circle commands, and none are planned. A single demo command exists purely as an end-to-end test of the input cycle and lives outside the core.

## Why not build on Draft's input

Early versions hooked `FreeCADGui.Snapper.snap` to constrain points inside Draft, Arch and BIM tools. Geometrically that worked, but it fought Draft for the keyboard.

Reading Draft 1.1.1 made the reason plain: `FreeCADGui.Snapper` and `FreeCADGui.draftToolBar` are not one tool's widgets, they are the canonical coordinate input shared by the whole Draft family (Draft, Arch/BIM, CAM's point picker, and third-party addons built on Draft). Draft already implements axis locking (`DraftToolBar.constrain` sets `Snapper.mask`; `Snapper.constrain` projects onto the working-plane axis), in-command shortcuts (`RestrictX`/`Y`/`Z` default to X/Y/Z, `Global` to G), a typed-value latch (`setMouseMode` plus the `MouseDelay` preference), and a global/working-plane coordinate toggle. Placing a second input panel beside it means competing for focus, lock state, relativity and units — a structural conflict, not a bug to polish.

So AxisFlow owns the input inside **its own** tools and does not try to take it away from anyone else's. Sketcher is untouched by this: it has its own C++ `DrawSketchHandler` and no Draft toolbar.

An optional Draft adapter (`axisflow_snap.py`) still exists, but only as a cooperative visualiser: it shows the compass over a running Draft/BIM command and mirrors Draft's own lock. It does not constrain and it does not claim keys.

## Status

Working today: the constraint solver (ortho and radial, working-plane relative, axis locks, smart lock); a singleton service with signals; a Coin3D compass; a floating panel with title bar, live read-out and lock checkboxes; a reconfigurable key map persisted to FreeCAD's parameter tree; the settings layer below; a standalone tool that owns cursor, clicks and keys in the 3D view; and the optional Draft adapter.

Not there yet: the compass rework (contour, no Z axis, working-plane orientation); the full own-input loop ("just start typing", `Base::Quantity` parsing, relative/absolute coordinates); the public `getPoint` callback API; the polar lock in the solver; multi-axis lock; Sketcher.

## Settings

All options are stored in FreeCAD's own parameter tree under `User parameter:BaseApp/Preferences/Mod/AxisFlow`. That is an XML file (`user.cfg`) in the user's config directory: always writable, surviving a module reinstall, readable identically from C++ and Python.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `CompassSize` | Int | 120 | compass size, pixels |
| `AxisThickness` | Int | 2 | axis line thickness, pixels |
| `AxisStartPct` | Float | 0.15 | where an axis starts, as a fraction of the centre-to-contour distance |
| `AxisLengthPct` | Float | 0.70 | axis length, same fraction |
| `AxisColorX` | Unsigned | `0xFF0000FF` | X axis colour (`0xRRGGBBAA`), red by default |
| `AxisColorY` | Unsigned | `0x00FF00FF` | Y axis colour, green by default |
| `ActiveFieldColor` | Unsigned | `0xDCDCDCFF` | background of the active coordinate field |
| `FillTransparency` | Float | 0.65 | transparency of the compass fill (0..1) |
| `AlwaysShowCompass` | Bool | false | show the compass whenever loaded, not only during a command |
| `Hotkey.<Action>` | ASCII | see below | comma-separated Qt key codes; empty = unbound |

Actions: `ToggleActive`, `ToggleMode`, `SmartLock`, `LockX`, `LockY`, `LockZ`, `ClearLocks`.

## Build (Windows)

Reference environment: Windows, MSVC (Visual Studio 17 2022, x64), FreeCAD 1.1.1 with Qt 6 and Coin 4 from a **pixi** environment. See `BUILD_WINDOWS.md` for setup and `HANDOFF.md` for the CMake pinning and the build pitfalls already solved.

```bat
cd C:\projects\AxisFlowFC
rmdir /s /q build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
cmake --install build
REM axisflow_snap.py is installed too; the Mod path is queried from FreeCAD
```

The module installs into FreeCAD's own user Mod directory (`%APPDATA%\FreeCAD\v1-1\Mod\AxisFlow\` on 1.1, queried from FreeCAD rather than hard-coded) and builds as `AxisFlow.pyd`. Adding or renaming sources requires a clean reconfigure — delete `build` first.

## Python API

```python
import AxisFlow as AF

AF.isActive()                     # -> bool
AF.activate() / AF.deactivate()
AF.beginSession() / AF.endSession()   # raise/tear down compass + panel

AF.setPlane(u, v, normal)         # orient the compass (App.Vector each)
AF.resetPlane()
AF.setMode('rect')                # 'rect' (ortho) | 'polar' (radial)
AF.getMode()

AF.lockAxis('x')                  # 'x' | 'y' | 'z' | 'none'
AF.clearLocks()
AF.setKeyValue(0, 1500.0)         # rect: 0=X 1=Y 2=Z ; polar: 0=dist 1=angle 2=Z
AF.clearKeyValues()

AF.solve(raw[, last])             # -> Vector, constrain without touching the UI
AF.updateCursor(raw[, last])      # same, plus move the compass / panel
AF.confirm(pt)                    # commit a point as the new origin
AF.getState()                     # -> dict

AF.setConstrainEnabled(bool)      # off only in the Draft adapter
AF.setHotkey(0x58, 'lockx')       # persist a binding; add False to keep it session-scoped
AF.loadHotkeysFromSettings()

AF.getSettings()                  # -> dict, including the 'hotkeys' map
AF.setSetting('compassSize', 160)
AF.resetSettings()
```

Default keys inside AxisFlow's own tools: `X` / `Y` / `Z` lock an axis (press again to release), `Space` toggles ortho/radial, `Enter` smart-locks the nearest axis, `Escape` clears locks, `Insert` turns AxisFlow on and off.

A `getPoint(last=None, callback=..., movecallback=...)` entry point, mirroring the idiom consumers already know from `Snapper.getPoint`, is the next milestone — it is what will let a consumer write a drawing command in a dozen lines.

## Optional Draft adapter

```python
import axisflow_snap
axisflow_snap.install()      # compass over a running Draft/BIM command
axisflow_snap.uninstall()
```

The adapter constrains nothing and binds no keys. It mirrors Draft's own lock into the compass, extends Draft's typed-value hold while you type, and unbinds AxisFlow's X/Y/Z/Enter/Escape for the duration of the session so Draft's native shortcuts keep working.

## Architecture

```
src/
  AppAxisFlow.cpp            module entry; attaches the Python method table
  Workbench.{h,cpp}          thin workbench (menu + toolbar)
  Command.cpp                "Activate AxisFlow" (standalone tool)
  AxisFlowSolver.{h,cpp}     pure math: plane, origin, axis lock, key overrides
  AxisFlowService.{h,cpp}    singleton brain: state, signals, hotkeys, actions
  AxisFlowSettings.{h,cpp}   persistent options on FreeCAD's ParameterGrp
  AxisFlowHotkeys.{h,cpp}    reconfigurable Qt-key -> action map
  AxisFlowEventFilter.{h,cpp}  global key filter, used by the adapter only
  AxisFlowCompass.{h,cpp}    Coin3D scene graph (compass + guide line)
  AxisFlowPanel.{h,cpp}      floating Qt panel (title/power, read-out, locks)
  AxisFlowTool.{h,cpp}       per-view coordinator (standalone / adapter)
  AxisFlowPy.cpp             Python bindings (module `AxisFlow`)
axisflow_snap.py             optional Draft adapter
```

Only one input session may be active at a time — the service is a singleton by deliberate choice.

## License / status

Early alpha (0.1). Interfaces and behaviour are still changing. Contributions and issue reports are welcome.
