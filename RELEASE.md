# Releasing AxisFlow

AxisFlow is a **compiled C++ workbench** (`AxisFlow.pyd`). That shapes how it can
be distributed.

## Why not the Addon Manager (auto-install)

FreeCAD's Addon Manager installs external addons by cloning their git repo into
the `Mod` folder — it does **not** compile anything. Per the FreeCAD wiki,
*external workbenches distributed via the Addon Manager must be Python-only*. For
a C++ workbench the documented options are:

1. **host precompiled binaries yourself** (per OS / FreeCAD version), or
2. get the code merged into the FreeCAD source tree (LGPL2+, forum review).

For an alpha, option 1 is the way: publish prebuilt ZIPs on **GitHub Releases**.

## The ABI rule (read this first)

A `.pyd` only loads in a FreeCAD build whose **Python + Qt + Coin ABI matches**
the one it was compiled against. A binary built in your pixi/conda-forge
environment may fail to load in the official freecad.org installer build, and
vice-versa. Therefore:

- Build the release against the **same FreeCAD distribution your users run**
  (state exactly which one), and
- **test the ZIP on a clean install** of that FreeCAD before publishing.

Name releases accordingly, e.g. `AxisFlow-0.1.1-win64-FC1.1.zip`.

## Cutting a release (maintainer)

1. Bump `<version>`/`<date>` in `package.xml` (and `VERSION` in `make_release.bat`).
2. Build + run the tests once (`run_tests.bat`).
3. Build the module and package it:
   ```bat
   cmake --build build --config RelWithDebInfo --target AxisFlow
   make_release.bat
   ```
   This produces `AxisFlow-<version>-win64-FC1.1.zip` containing a single
   `AxisFlow\` folder with the runtime files only (the `.pyd`, the `.py` glue,
   `Resources\`, `package.xml`) — no sources, tests, or build artifacts.
4. Test the ZIP on a clean FreeCAD (unzip into `Mod`, restart, use it).
5. On GitHub: create a **tag** (e.g. `v0.1.1`) and a **Release**, attach the ZIP,
   paste install instructions + which FreeCAD build it targets.

## Installing (end users)

1. Download `AxisFlow-<version>-win64-FC1.1.zip` from the Releases page.
2. Unzip so that the `AxisFlow` folder lands in your FreeCAD `Mod` directory:
   - Windows: `%APPDATA%\FreeCAD\Mod\`
   (final path: `...\FreeCAD\Mod\AxisFlow\InitGui.py`)
   - The exact folder is shown by, in FreeCAD's Python console:
     `App.getUserAppDataDir()`
3. Fully restart FreeCAD.
4. Pick **AxisFlow** from the workbench selector.

If it doesn't appear, this build likely doesn't match your FreeCAD's ABI — use a
release built for your FreeCAD version.

## Optional distribution niceties

- **Addon Manager custom repository**: users can add your repo URL under
  *Tools → Addon Manager → gear → Custom repositories*. This still won't compile
  C++, but if you **commit a prebuilt `AxisFlow.pyd`** (for one platform/ABI)
  into the repo alongside the `.py`/`Resources`, matching users get a one-click
  install. It only works for that one platform/ABI, so keep the ZIP as the main
  channel.
- **Windows installer** (Inno Setup / NSIS): nicer UX than a raw ZIP; drops the
  files into `Mod\AxisFlow` and can detect the FreeCAD path. More setup work.
- **LICENSE**: `package.xml` references a `LICENSE` file. Easiest is to let
  GitHub add it when creating the repo (choose *LGPL-2.1*), or drop the official
  LGPL-2.1 text into `LICENSE`.
