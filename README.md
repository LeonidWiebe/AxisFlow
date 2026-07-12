# AxisFlow Workbench for FreeCAD

version 0.1.1 (alpha)

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

Consequently AxisFlow ships no Line, Rectangle, Arc or Circle commands, and none are planned. A single demo command (`axisflow_demo.line()`) exists purely as an end-to-end test of the input cycle -- it lives outside the core and builds its geometry with `Draft.make_wire`, drawing its own live preview with a small self-owned Coin node.

## Why not build on Draft's input

Early versions hooked `FreeCADGui.Snapper.snap` to constrain points inside Draft, Arch and BIM tools. Geometrically that worked, but it fought Draft for the keyboard.

Reading Draft 1.1.1 made the reason plain: `FreeCADGui.Snapper` and `FreeCADGui.draftToolBar` are not one tool's widgets, they are the canonical coordinate input shared by the whole Draft family (Draft, Arch/BIM, CAM's point picker, and third-party addons built on Draft). Draft already implements axis locking (`DraftToolBar.constrain` sets `Snapper.mask`; `Snapper.constrain` projects onto the working-plane axis), in-command shortcuts (`RestrictX`/`Y`/`Z` default to X/Y/Z, `Global` to G), a typed-value latch (`setMouseMode` plus the `MouseDelay` preference), and a global/working-plane coordinate toggle. Placing a second input panel beside it means competing for focus, lock state, relativity and units — a structural conflict, not a bug to polish.

So AxisFlow owns the input inside **its own** tools and does not try to take it away from anyone else's. Sketcher is untouched by this: it has its own C++ `DrawSketchHandler` and no Draft toolbar.

An optional Draft adapter (`axisflow_snap.py`) still exists, but only as a cooperative visualiser: it shows the compass over a running Draft/BIM command and mirrors Draft's own lock. It does not constrain and it does not claim keys.

## Status

Working today: the constraint solver (ortho and radial, working-plane relative, axis locks, smart lock); a singleton service with signals; a Coin3D compass that orients to the working plane (contour + centre point, square in ortho / circle in radial, no Z axis, size and colours from settings); a configurable **live guide line** from the compass centre to the cursor (on/off, style and colour from settings); a floating panel with editable unit-aware fields, an active field chosen by the nearest axis with a highlight and Tab cycling, lock checkboxes, "just start typing" with the caret always in the active field, AccuDraw-style sign-from-direction and round-on-blur, and a **Local/World coordinate-frame** button; a reconfigurable key map persisted to FreeCAD's parameter tree; an input state machine (Idle/Tracking/Finished, continue mode, finish-line vs cancel-command, Enter commits a point); the public **`getPoint(callback, movecallback, last, continuous, finishcallback)`** callback API; a demo Line command that draws multiple lines with a live preview and picks up the active working plane; a live two-column **settings dialog** that applies every option on the fly; palette commands with icons and a workbench icon; automated tests (C++ unit tests plus a Python API suite); and the optional Draft adapter.

Not there yet: the polar (radial) lock in the solver; multi-axis lock; typed **absolute/relative** entry beyond the per-axis world conversion; a dockable coordinate panel; Sketcher.

## Keys and their configuration

AxisFlow is keyboard-first. Inside its own tools the caret always sits in the active coordinate field, so digits edit the field while bound letters act as commands. Every binding is a **Qt key code → action** map with sensible defaults, stored in FreeCAD's parameter tree and reconfigurable at runtime.

### Default keys (inside AxisFlow's own tools)

**Global actions** — resolved wherever the session has focus (the coordinate window or the 3-D view). Combinations with Ctrl / Alt / Meta are never claimed and pass through to FreeCAD.

| Key | Action | What it does |
|---|---|---|
| `Insert` | ToggleActive | turn AxisFlow on / off |
| `Space` | ToggleMode | switch ortho ↔ radial (rectangular ↔ polar) |
| `Enter`, numpad `Enter` | ConfirmPoint | place the point; commits the typed coordinates |
| `X` | LockX | lock the X (compass-u) axis; press again to release |
| `Y` | LockY | lock the Y (compass-v) axis; press again to release |
| `Z` | LockZ | lock the Z (compass-normal) axis; press again to release |
| `Escape` | FinishLine | finish the **current** line (it is saved) and start a new one; the compass and coordinate window stay up |
| `` ` `` (backtick) | CancelCommand | finish the current line **and** end the whole command |

**Coordinate-window editing keys** — active while a field has the caret (own-tool mode):

| Key | What it does |
|---|---|
| `0`–`9`, `.`, `,`, `-`, `+` | start / continue typing a value into the active field |
| `Backspace` | delete the last character of the entry |
| `Delete` | clear the entry and release that field's override |
| `Tab` / `Shift`+`Tab` | move the active field to the next / previous visible field |
| the global keys above | still act as actions even while a field is focused |

Note on the coordinate-frame button: the button below the fields is **not** a mode toggle. It switches the read-out (and typed entry) between **Local** — offset from the compass origin along the compass axes — and **World** — the same axes measured from the world origin. Ortho/radial stays on `Space`, shown by the field labels (`X`/`Y`/`Z` vs `D`/`A`).

Entry behaviour (AccuDraw-style): the first character typed replaces the field's live read-out rather than appending to it. A bare magnitude takes the **sign of the cursor's current direction** along that axis — point down −X, type `5`, and X locks at −5 (the live line follows immediately); an explicitly typed sign always wins. On focus loss a committed value is rounded to the standard precision (4 decimals; 2 for the polar angle) and normalised (`5cm` → `50.0000`).

Actions with **no default key** but available through the API: `SmartLock` (lock the axis nearest the cursor, toggle), `ClearLocks` (drop all axis locks), `CancelEntry` (layered: abort a value entry, then release locks).

### Reconfiguring keys

Bindings live under `User parameter:BaseApp/Preferences/Mod/AxisFlow` as `Hotkey.<Action>` entries: a comma-separated list of Qt key codes (an empty string means "unbound"). Change them from Python:

```python
import AxisFlow as AF
from PySide import QtCore

# Rebind "finish line" from Escape to F2 (persisted to settings):
AF.setHotkey(int(QtCore.Qt.Key_F2), 'finishline')

# Unbind a key (e.g. free Space so it does nothing):
AF.setHotkey(int(QtCore.Qt.Key_Space), 'none')

# Session-scoped change that must NOT touch the stored layout (persist=False):
AF.setHotkey(int(QtCore.Qt.Key_X), 'none', False)

# Inspect the live map, and restore the built-in layout:
AF.getSettings()['hotkeys']       # -> {'FinishLine': [16777216], 'LockX': [88], ...}
AF.loadHotkeysFromSettings()      # re-apply defaults + stored overrides
AF.resetSettings()                # restore ALL defaults (options + hotkeys)
```

Action names accepted by `setHotkey` (case-insensitive-friendly lowercase): `toggleactive`, `togglemode`, `smartlock`, `lockx`, `locky`, `lockz`, `clearlocks`, `cancelentry`, `cancelcommand`, `finishline`, `confirmpoint`, `none`.

An action may hold several keys (e.g. `ConfirmPoint` is bound to both `Return` and the numpad `Enter`); `getSettings()['hotkeys']` lists each action's full key list.

The optional Draft adapter deliberately unbinds `X`/`Y`/`Z`/`Enter`/`Escape` for the duration of its session (with `persist=False`, so your stored layout is untouched) because those belong to Draft while its command runs.

## Settings

All options are stored in FreeCAD's own parameter tree under `User parameter:BaseApp/Preferences/Mod/AxisFlow`. That is an XML file (`user.cfg`) in the user's config directory: always writable, surviving a module reinstall, readable identically from C++ and Python. Every option is also reachable from Python via `AF.getSettings()` / `AF.setSetting(name, value)`.

The **Settings** command (`AxisFlow_Settings`, gear icon) opens a live, non-modal editor: a two-column grid of name → value where each value widget matches the option's type (check box, combo, number spin, colour picker). Every change applies **on the fly** — there is no OK button — so the compass and panel update as you edit. It is a thin Python/PySide dialog (`axisflow_settings.py`); adding a future option is one row there plus a getter/setter in `AxisFlowSettings`.

| Key | `setSetting` name | Type | Default | Meaning |
|---|---|---|---|---|
| `CompassSize` | `compassSize` | Int | 120 | compass size, pixels |
| `AxisThickness` | `axisThickness` | Int | 2 | axis line thickness, pixels |
| `AxisStartPct` | `axisStartPct` | Float | 0.15 | where an axis starts, as a fraction of the centre-to-contour distance |
| `AxisLengthPct` | `axisLengthPct` | Float | 0.70 | axis length, same fraction |
| `AxisColorX` | `axisColorX` | Unsigned | `0xFF0000FF` | X axis colour (`0xRRGGBBAA`), red by default |
| `AxisColorY` | `axisColorY` | Unsigned | `0x00FF00FF` | Y axis colour, green by default |
| `ActiveFieldColor` | `activeFieldColor` | Unsigned | `0xDCDCDCFF` | background of the active coordinate field |
| `FillTransparency` | `fillTransparency` | Float | 0.65 | transparency of the compass fill (0..1) |
| `AlwaysShowCompass` | `alwaysShowCompass` | Bool | false | show the compass whenever loaded, not only during a command |
| `LiveLineEnabled` | `liveLineEnabled` | Bool | true | draw the live guide line from the compass centre to the cursor |
| `LiveLineStyle` | `liveLineStyle` | Int | 1 | 0 = solid, 1 = dashed, 2 = dotted, 3 = dash-dot |
| `LiveLineColor` | `liveLineColor` | Unsigned | `0xFFE633FF` | live guide line colour (`0xRRGGBBAA`) |
| `Hotkey.<Action>` | — | ASCII | see above | comma-separated Qt key codes; empty = unbound |

Actions used as `Hotkey.<Action>` keys: `ToggleActive`, `ToggleMode`, `SmartLock`, `LockX`, `LockY`, `LockZ`, `ClearLocks`, `CancelEntry`, `CancelCommand`, `FinishLine`, `ConfirmPoint`.

```python
import AxisFlow as AF
AF.setSetting('liveLineEnabled', False)   # hide the live guide line
AF.setSetting('liveLineStyle', 0)         # solid
AF.setSetting('liveLineColor', 0x33CCFFFF)# light blue
AF.setSetting('compassSize', 160)
AF.getSettings()                          # -> dict, including the 'hotkeys' map
AF.resetSettings()                        # restore built-in defaults
```

## Commands

The workbench menu / toolbar exposes:

* **Activate AxisFlow** (`AxisFlow_Activate`) — toggle the standalone compass tool on the active 3-D view.
* **Draw line** (`AxisFlow_DrawLine`) — draw a polyline through AxisFlow's `getPoint()` API (Escape starts a new line, backtick ends the command). The geometry lives in the demo consumer, `axisflow_demo.py`.
* **Toggle live line** (`AxisFlow_ToggleLiveLine`) — show / hide the live guide line (flips `liveLineEnabled`; the change is persisted).
* **Settings** (`AxisFlow_Settings`) — open the live settings dialog described above.

## Installation

AxisFlow is a **compiled C++ workbench**, so it is not installed through FreeCAD's
Addon Manager (which only handles Python-only addons). Install a prebuilt release:

1. Download `AxisFlow-<version>-win64-FC1.1.zip` from the project's **Releases** page.
2. Unzip it so the `AxisFlow` folder lands in your FreeCAD `Mod` directory — on
   Windows `%APPDATA%\FreeCAD\Mod\`, giving `...\FreeCAD\Mod\AxisFlow\InitGui.py`.
   (The exact path is printed by `App.getUserAppDataDir()` in FreeCAD's Python console.)
3. Fully restart FreeCAD and pick **AxisFlow** from the workbench selector.

A compiled `.pyd` only loads in a FreeCAD build whose **Python/Qt/Coin ABI matches**
the one it was built against, so use a release built for your FreeCAD version. If the
workbench doesn't appear after a restart, the binary likely doesn't match your build.
Maintainers: see `RELEASE.md` for how to package and publish a release.

## Build (Windows)

Reference environment: Windows, MSVC (Visual Studio 17 2022, x64), FreeCAD 1.1.1 with Qt 6 and Coin 4 from a **pixi** environment. See `BUILD_WINDOWS.md` for setup and `HANDOFF.md` for the CMake pinning and the build pitfalls already solved.

```bat
cd C:\projects\AxisFlow
rmdir /s /q build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
cmake --install build
REM the .py glue and Resources are installed too; the Mod path is queried from FreeCAD
```

The module installs into FreeCAD's own user Mod directory (`%APPDATA%\FreeCAD\Mod\AxisFlow\`, queried from FreeCAD rather than hard-coded) and builds as `AxisFlow.pyd`. Adding or renaming sources requires a clean reconfigure — delete `build` first. Pure-Python files (`axisflow_demo.py`, `axisflow_snap.py`, `axisflow_settings.py`) can be dropped into the Mod directory without rebuilding.

## Testing

Automated tests come in two tiers (details in `TESTING.md`):

* **Tier 1 — C++ unit tests** (solver + hotkeys + entry policy): pure logic, no GUI. Build with `-DAXISFLOW_BUILD_TESTS=ON` and run `ctest`, or just run `run_tests.bat`.
* **Tier 2 — Python API tests** (`TestAxisFlow.py`): run inside a GUI FreeCAD with `import Test, TestAxisFlow; Test.runTestsFromModule(TestAxisFlow)`.

## Python API

```python
import AxisFlow as AF

AF.isActive()                     # -> bool
AF.activate() / AF.deactivate()
AF.beginSession() / AF.endSession()   # raise / tear down compass + panel

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

### getPoint — the consumer entry point

```python
AF.getPoint(callback=None, movecallback=None, last=None,
            continuous=False, finishcallback=None)
```

* `callback(point, ok)` — a point was confirmed (`ok=True`, `point` is a `Base.Vector`) or the command was cancelled (`ok=False`, `point=None`).
* `movecallback(point)` — the live solved point on every cursor move; draw your own preview here.
* `finishcallback()` — the user pressed FinishLine (Escape): finish the current line and keep drawing; the session stays up.
* `last` — an optional base point (relative-input anchor).
* `continuous` — `False` fires `callback` once, then finishes; `True` fires per point until cancel.

**Working plane.** AxisFlow constrains points to the plane through the anchor with the compass orientation. A consumer that wants to draw on FreeCAD's active working plane should sync it once at command start:

```python
import WorkingPlane, FreeCAD as App
wp = WorkingPlane.get_working_plane()
AF.setPlane(App.Vector(wp.u), App.Vector(wp.v), App.Vector(wp.axis))
```

`axisflow_demo.py` does exactly this. A minimal multi-line consumer:

```python
import AxisFlow as AF, FreeCAD as App, Draft

class Line:
    def __init__(self): self.pts = []
    def on_point(self, p, ok):
        if not ok:                       # backtick: end command
            self.finish(); AF.endSession(); return
        self.pts.append(App.Vector(p))
    def on_finish(self):                 # Escape: save this line, keep going
        self.finish(); self.pts = []
    def finish(self):
        if len(self.pts) >= 2:
            Draft.make_wire(self.pts, closed=False)
            App.ActiveDocument.recompute()

d = Line()
AF.getPoint(callback=d.on_point, finishcallback=d.on_finish, continuous=True)
```

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
  Command.cpp                Activate / Draw line / Toggle live line / Settings
  AxisFlowSolver.{h,cpp}     pure math: plane, origin, axis lock, key overrides
  AxisFlowService.{h,cpp}    singleton brain: state, signals, hotkeys, actions
  AxisFlowSettings.{h,cpp}   persistent options on FreeCAD's ParameterGrp
  AxisFlowHotkeys.{h,cpp}    reconfigurable Qt-key -> action map
  AxisFlowEntry.h            pure entry policy (sign-from-direction); unit-tested
  AxisFlowEventFilter.{h,cpp}  global key filter (app-wide + adapter)
  AxisFlowCompass.{h,cpp}    Coin3D scene graph (compass + live guide line)
  AxisFlowPanel.{h,cpp}      floating Qt panel (title/power, read-out, locks, Local/World)
  AxisFlowTool.{h,cpp}       per-view coordinator (standalone / adapter)
  AxisFlowGetPoint.{h,cpp}   getPoint() bridge: Python callbacks <- state machine
  AxisFlowPy.cpp             Python bindings (module `AxisFlow`)
tests/                       C++ unit tests (GoogleTest): solver, hotkeys, entry
axisflow_snap.py             optional Draft adapter
axisflow_demo.py             demo Line command (reference consumer, outside core)
axisflow_settings.py         live settings dialog (Python/PySide)
TestAxisFlow.py              Python API tests (run in a GUI FreeCAD)
```

Only one input session may be active at a time — the service is a singleton by deliberate choice. Session teardown is deferred (`deleteLater`) so it is safe to end a command from inside the very event handler that owns the tool.

## License

AxisFlow is licensed under the **GNU Lesser General Public License v2.1 or later**
(LGPL-2.1-or-later); the full text is in the `LICENSE` file.

Early alpha (0.1.x). Interfaces and behaviour are still changing. Contributions and issue reports are welcome.
