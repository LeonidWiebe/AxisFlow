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

## Tier 1 -- C++ unit tests (solver, indexing, hotkeys, entry policy, key routing)

Pure logic, no Coin, no FreeCADGui, no display. Fast and deterministic. Uses
GoogleTest (FreeCAD's own C++ test framework), fetched at configure time.

Six suites live in `tests/`: `test_solver.cpp` (constraints, locks, working-plane
maths), `test_indexing.cpp` (the live-line indexing policy, plus how object snaps
and Smart lock interact with it), `test_hotkeys.cpp` (the key map),
`test_entry.cpp` (typed-value entry policy), `test_keyrouting.cpp` (which key
goes where: modifiers, the off state, Tab, auto-repeat, field vs view, adapter
mode, two-key chords, and the rule that Backspace/Delete are only ours while a
value is actually being typed) and `test_path.cpp` (the compass-path poly-line:
its shape under each combination of frozen in-plane fields, the snap leg, and
the dropping of zero-length legs). `AxisFlowIndexing.h` is deliberately
a header-only pure function, so the whole indexing policy is testable without any
FreeCAD object graph -- keep it that way when extending it.

One trap worth knowing: `IndexingInput` is filled field by field in the test
helper `mk()`. When you add a field to that struct, add it to `mk()` too --
otherwise the tests run on uninitialised memory and fail in ways that look like
logic errors.

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
Covered: compass basis and world<->local transform, field locks (a frozen slot
holds its value while free ones follow the cursor; the SmartLock pair; polar
distance/angle locks; the Z lock pinning a snapped point's height; the
typed-vs-locked distinction that gates distance indexing; the candidate axis
staying lit while a lock pins the point onto an axis), keyboard overrides,
`nearestAxis`, polar distance/angle, and the full hotkey map (defaults incl.
`LockD`, set/remove/bind, name round-trip). The former `DISABLED_` polar-lock
placeholder is gone: the unified fixed-slot model made the polar tests live.

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
pickup, Escape/backtick behaviour, live-line toggle, Local/World readout,
object snapping against real geometry (markers, the snap palette, Smart lock,
3-D coordinates in the fields) and "the created line is not selected" are
covered by the running manual checklist in the handoff notes. These need synthetic mouse/key events, a live scene graph
and a visible display; automating them (QTest key injection + a framebuffer)
is possible later but is high-friction on Windows and low-ROI for now.

## Running the tests locally

Tests are run on your own machine (no CI service required).

### Tier 1 -- one command

`run_tests.bat` (at the repo root) configures with `-DAXISFLOW_BUILD_TESTS=ON`,
builds `AxisFlow_tests_run`, puts the pixi Python/Qt DLLs on `PATH`, and runs
`ctest`. Double-click it, or from a shell:

```bat
run_tests.bat            REM configure + build + run
run_tests.bat clean      REM wipe build\ first (fresh gtest + full rebuild)
run_tests.bat nopause    REM no keypress at the end (for scripting)
```

Edit the `PIXI_ROOT` / `GENERATOR` / `CONFIG` lines at the top of the script if
your paths differ. A green run ends with `ALL TESTS PASSED`; a failure returns a
non-zero exit code and prints the failing tests.

Equivalent manual commands:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DAXISFLOW_BUILD_TESTS=ON
cmake --build build --config RelWithDebInfo --target AxisFlow_tests_run
set PATH=C:\projects\FreeCAD111\.pixi\envs\default\Library\bin;C:\projects\FreeCAD111\.pixi\envs\default;%PATH%
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

### Offline: the line demo's own harness

`tests/demo_arc_harness.py` runs `axisflow_demo.py` against stubbed
FreeCAD/Part modules, so the consumer-side bookkeeping -- which segment carries
an arc, what a mode toggle abandons, where the compass is parked while an arc is
drawn, what happens when the document changes underneath -- can be checked with
plain `python3`, with no FreeCAD at all:

```bash
python3 tests/demo_arc_harness.py
```

### Tier 2 -- by hand in a GUI FreeCAD

Needs a live FreeCAD (its `.pyd` links Gui), so run it from the Python console:

```python
import Test, TestAxisFlow
Test.runTestsFromModule(TestAxisFlow)
```

(Install the module first so FreeCAD finds `TestAxisFlow.py`.)

### Optional: GitHub Actions

A ready-made self-hosted-runner workflow is included at
`.github/workflows/ci.yml` if you ever want CI. It is not required for local
testing and can be ignored or deleted; see the comments inside it for setup.
