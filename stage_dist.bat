@echo off
REM ===========================================================================
REM  stage_dist.bat -- build AxisFlow and assemble ONLY the runtime files into
REM  dist\AxisFlow\. Single source of truth for "what ships", shared by:
REM     make_release.bat    (zips dist\AxisFlow  -> release ZIP)
REM     build_installer.bat (feeds dist\AxisFlow -> NSIS setup.exe)
REM
REM  A compiled .pyd only loads in a FreeCAD whose Python/Qt/Coin ABI matches
REM  the one it was built against. Qt/Coin/Python DLLs are NOT shipped -- they
REM  come from the user's FreeCAD. Override the build config with e.g.
REM     set CONFIG=Release && call stage_dist.bat
REM ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
if not defined CONFIG set "CONFIG=RelWithDebInfo"

echo === Build (%CONFIG%) ===
cmake --build build --config %CONFIG% --target AxisFlow || exit /b 1

REM locate the freshly built module. CMake sends it to build\bin
REM (RUNTIME_OUTPUT_DIRECTORY); multi-config generators like Visual Studio add
REM the configuration as one more level. Check those two directly.
set "PYD="
if exist "%~dp0build\bin\%CONFIG%\AxisFlow.pyd" set "PYD=%~dp0build\bin\%CONFIG%\AxisFlow.pyd"
if not defined PYD if exist "%~dp0build\bin\AxisFlow.pyd" set "PYD=%~dp0build\bin\AxisFlow.pyd"

REM Fallback for an unusual generator layout. Note the `if exist`, which is not
REM optional: `for /r` with a set that contains NO wildcard does not search the
REM tree at all -- it just prepends every directory under it to the name.
REM Without the test this "finds" AxisFlow.pyd inside whichever directory the
REM filesystem happens to enumerate last (ZERO_CHECK.dir, for instance), and
REM the copy below then fails on a path that never existed. It used to work
REM only because the real directory happened to come last.
if not defined PYD (
    for /r "%~dp0build" %%F in (AxisFlow.pyd) do if exist "%%F" set "PYD=%%F"
)
if not defined PYD (
    echo *** AxisFlow.pyd not found under build\ ***
    echo     Expected: build\bin\%CONFIG%\AxisFlow.pyd
    echo     If build\ does not exist yet, run run_tests.bat once -- it is the
    echo     only script that configures CMake; this one only builds.
    exit /b 1
)
echo Found module: %PYD%

echo === Stage dist\AxisFlow ===
if exist dist rmdir /s /q dist
mkdir dist\AxisFlow || exit /b 1
copy /y "%PYD%"               dist\AxisFlow\ >nul || exit /b 1
copy /y InitGui.py           dist\AxisFlow\ >nul || exit /b 1
copy /y axisflow_snap.py     dist\AxisFlow\ >nul || exit /b 1
copy /y axisflow_demo.py     dist\AxisFlow\ >nul || exit /b 1
copy /y axisflow_settings.py dist\AxisFlow\ >nul || exit /b 1
copy /y axisflow_snapping.py dist\AxisFlow\ >nul || exit /b 1
copy /y axisflow_transform.py dist\AxisFlow\ >nul || exit /b 1
copy /y axisflow_extrude.py  dist\AxisFlow\ >nul || exit /b 1
copy /y package.xml          dist\AxisFlow\ >nul || exit /b 1
copy /y hotkeys.ini          dist\AxisFlow\ >nul || exit /b 1
if exist README.md    copy /y README.md    dist\AxisFlow\ >nul
if exist man\CHANGELOG.md copy /y man\CHANGELOG.md dist\AxisFlow\ >nul
if exist LICENSE      copy /y LICENSE      dist\AxisFlow\ >nul
xcopy /e /i /y Resources dist\AxisFlow\Resources >nul || exit /b 1
if exist translations (
    mkdir dist\AxisFlow\translations >nul 2>nul
    copy /y translations\*.qm dist\AxisFlow\translations\ >nul 2>nul
)

echo === Staged: dist\AxisFlow ===
exit /b 0
