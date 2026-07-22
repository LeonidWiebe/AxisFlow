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

REM locate the freshly built module wherever the generator put it
set "PYD="
for /r "%~dp0build" %%F in (AxisFlow.pyd) do set "PYD=%%F"
if not defined PYD (
    echo *** AxisFlow.pyd not found under build\ -- configure/build first. ***
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
