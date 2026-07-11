# Testing AxisFlow

AxisFlow's layering makes some parts easy to test automatically and some
inherently manual. The split below matches that reality.

## Why the module can't be tested in `freecadcmd`

`AxisFlow.pyd` is a single GUI-coupled module (its Python bindings reference
`Gui::View3DInventor` / `Gui::Application`), so `import AxisFlow` requires
FreeCADGui to be loaded. Under `freecadcmd` the import fails with a DLL-load
error -- the same limitation every FreeCAD GUI workbench has. Consequences:

* pure **math/logic** is tested as a standalone C++ runner (no FreeCAD runtime);
* the **Python API** is tested inside a GUI FreeCAD (`FreeCAD --run-test`);
* the **compass/panel rendering and live input** stay on the manual checklist.

## Tier 1 -- C++ unit tests (Solver + Hotkeys)

Pure logic, no Coin, no FreeCADGui, no display. Fast and deterministic. Uses
GoogleTest (FreeCAD's own C++ test framework), fetched at configure time.

Enable once by adding a single line at the end of the root `CMakeLists.txt`
(after the `AxisFlow` target and the Qt6/Python3/FreeCAD paths):

```cmake
add_subdirectory(tests)
```

Then:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DAXISFLOW_BUILD_TESTS=ON
cmake --build build --config RelWithDebInfo --target AxisFlow_tests_run
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

The runner links only `FreeCADBase.lib` (+ Python + Qt headers) -- the pixi
env's Python/Qt DLLs must be on `PATH` at run time, as for the module itself.
Covered: compass basis and world<->local transform, axis-lock projection,
keyboard overrides, `nearestAxis`, polar distance/angle, and the full hotkey
map (defaults, set/remove/bind, name round-trip). One `DISABLED_` test records
the not-yet-implemented polar lock (roadmap item 9) -- enable it when it lands.

## Tier 2 -- Python API tests (GUI FreeCAD)

`TestAxisFlow.py` exercises the public API that needs no 3-D view (solve,
locks, overrides, plane, settings, hotkeys) plus a session-lifecycle smoke test
that skips when there is no view. Tests are non-destructive: touched options are
snapshotted and restored, and hotkey rebinds use `persist=False`.

```bat
FreeCAD --run-test TestAxisFlow
```

or in the Python console:

```python
import Test, TestAxisFlow
Test.runTestsFromModule(TestAxisFlow)
```

Install `TestAxisFlow.py` alongside `InitGui.py` in the Mod dir (add it to the
CMake `install(FILES ...)` list, or copy it) so FreeCAD's discovery finds it.

## Tier 3 -- manual checklist (interactive / rendering)

Input flow, compass/panel visuals, teardown under real focus, working-plane
pickup, Escape/backtick behaviour, live-line toggle, Local/World readout, and
"the created line is not selected" are covered by the running manual checklist
in the handoff notes. These need synthetic mouse/key events, a live scene graph
and a visible display; automating them (QTest key injection + a framebuffer)
is possible later but is high-friction on Windows and low-ROI for now.

## Suggested CI shape

* per-commit: Tier 1 (needs only FreeCAD Base + Qt/Python from the pixi env);
* nightly / on-demand on a machine with a desktop session: Tier 2;
* Tier 3 stays manual until a headless GUI harness is worth the cost.
