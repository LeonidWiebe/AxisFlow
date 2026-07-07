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

## Features (this prototype)

| Feature | Status |
|---|---|
| 3-axis Coin3D compass (X red, Y green, Z blue) | ✅ |
| Guide line from origin to cursor | ✅ |
| Floating Qt input panel (X / Y / Z) | ✅ |
| Rectangular ↔ Polar toggle (`Space`) | ✅ |
| Axis lock (press `X` / `Y` / `Z`) | ✅ |
| Keyboard value override + Enter to confirm | ✅ |
| Auto-orient compass to edge | 🔲 next step |
| Snap to geometry | 🔲 next step |
| 3-D (off-plane) support | 🔲 next step |

---

## Build

### Prerequisites

```
FreeCAD built from source (or dev package with headers)
CMake ≥ 3.14
Qt 5
Coin3D  (libCoin-dev / coin3 / SIM Coin)
```

### Steps

```bash
git clone <this repo>
cd AxisFlow

cmake -B build \
  -DFREECAD_ROOT=/path/to/freecad/build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build -j$(nproc)

# Install into FreeCAD Mod directory
cmake --install build
```

### Manual install (without CMake install target)

Copy these two files into `~/.local/share/FreeCAD/Mod/AxisFlow/`:

```
build/libAxisFlow.so   (or .dll on Windows)
InitGui.py
```

Then start FreeCAD, switch to the **AxisFlow** workbench.

---

## Usage

1. Switch to the **AxisFlow** workbench (or press the shortcut `A D`).
2. Click **Activate AxisFlow** in the toolbar.
3. Move the mouse over the 3D view — the compass appears at the last confirmed point.
4. Type a number → it goes directly into the focused field.
5. `Tab` — cycle between fields.
6. `Enter` — confirm the point, compass moves to the new location.
7. `Space` — toggle Rectangular / Polar mode.
8. `X` / `Y` / `Z` — lock to that axis.

---

## Architecture

```
AppAxisFlow.cpp       ← module entry, registers workbench + command
Workbench.h/cpp       ← FreeCAD workbench (menu + toolbar)
AxisFlowTool.h/cpp    ← central coordinator, Coin3D event loop
AxisFlowSolver.h/cpp  ← pure math: axis locks, polar/rect, keyboard overrides
AxisFlowCompass.h/cpp ← Coin3D scenegraph: 3 axes + guide line
AxisFlowPanel.h/cpp   ← Qt floating widget: X/Y/Z input fields
```

---

## Next Steps

- **Edge snap / auto-orient**: detect nearest edge under cursor via `SoRayPickAction`, call `_solver.alignCompassX(edgeDir)` + `_compass->setRotation(...)`.
- **Off-plane input**: replace the fixed Z-plane projection with a proper "compass plane follows camera" approach.
- **Snap indicators**: draw a small circle on the compass axes when cursor is near them.
- **Persistent settings**: save last mode (rect/polar) via FreeCAD parameter system.
