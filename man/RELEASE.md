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

Name releases accordingly, e.g. `AxisFlow-0.2.0-win64-FC1.1.zip`.

## Cutting a release (maintainer)

1. Bump the version in ALL of these -- they drift apart otherwise:
   `package.xml` (`<version>` and `<date>`), `CMakeLists.txt` (`project(...)`),
   `make_release.bat`, `build_installer.bat`, `installer/AxisFlow.nsi`
   (`APPVERSION`) and the line under the title in `README.md`. Move the
   `[Unreleased]` notes in `CHANGELOG.md` under a new dated version heading and
   update the compare links.
2. Refresh the translations (`python update_translations.py`) and translate
   anything it leaves marked `type="unfinished"`; the `.qm` files are what
   actually ship.
3. Build + run the tests once (`run_tests.bat`), and the offline demo harness
   (`python3 tests/demo_arc_harness.py`).
4. Build the module and package it:
   ```bat
   cmake --build build --config RelWithDebInfo --target AxisFlow
   make_release.bat
   ```
   This produces `AxisFlow-<version>-win64-FC1.1.zip` containing a single
   `AxisFlow\` folder with the runtime files only (the `.pyd`, the `.py` glue
   including `axisflow_snapping.py`, `Resources\`, `translations\*.qm`,
   `hotkeys.ini`, `package.xml`) — no sources, tests, or build artifacts.
   `stage_dist.bat` / `stage_dist.sh` are the single source of truth for that
   list: **every newly shipped file must be added there and to the CMake
   `install()` rules**, or it will be missing from installs (this has bitten us
   with the translations and with `package.xml`).
5. Test the ZIP on a clean FreeCAD (unzip into `Mod`, restart, use it).
6. On GitHub: create a **tag** (e.g. `v0.2.0`) and a **Release**, attach the ZIP,
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
- **Windows installer (NSIS)**: nicer UX than a raw ZIP. Build it with
  `build_installer.bat` (needs [NSIS](https://nsis.sourceforge.io) on PATH); it
  stages the runtime files via `stage_dist.bat` and compiles
  `installer\AxisFlow.nsi` into `AxisFlow-<version>-win64-FC1.1-setup.exe`. The
  installer is **per-user** (no admin): it installs into the FreeCAD `Mod`
  folder (auto-detecting the versioned `...\FreeCAD\v1-1\Mod` when present, with
  a directory page to override), registers an uninstaller in *Apps & features*
  under `HKCU`, and ships **only** the workbench files (Qt/Coin/Python come from
  FreeCAD). The same ABI rule applies -- build against the target FreeCAD and
  test the `setup.exe` on a clean install.
- **Linux (prebuilt tarball + one-liner)**: `stage_dist.sh` builds the module and
  packages `AxisFlow-1_1-linux-<arch>-FC1.1.tar.gz` plus `SHA256SUMS.txt`. Upload
  both to the GitHub Release (tag `v<version>`). Users install with one command,
  `curl -fsSL …/install.sh | bash`, which detects their FreeCAD, **refuses** to
  install on a non-1.1.x FreeCAD (unless `--force`), verifies the checksum and
  drops the workbench into their Mod folder. The tarball asset name is stable (no
  app version) so `install.sh` can fetch it from the *latest* release; the version
  lives in the tag and in `package.xml`.
  Prerequisite: the current `CMakeLists.txt` is Windows-only — it must be made
  cross-platform (find FreeCAD/Qt/Coin, emit a prefix-less `AxisFlow.so`) before
  `stage_dist.sh` can build the Linux binary. Flatpak/Snap FreeCAD are out of
  scope for a prebuilt (their `.so` must be built inside their SDK/runtime).
- **LICENSE**: `package.xml` references a `LICENSE` file. Easiest is to let
  GitHub add it when creating the repo (choose *LGPL-2.1*), or drop the official
  LGPL-2.1 text into `LICENSE`.
