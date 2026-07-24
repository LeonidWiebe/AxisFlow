@echo off
REM ===========================================================================
REM  build_installer.bat -- build the AxisFlow NSIS installer (setup.exe).
REM
REM  Steps: (1) build + stage the runtime files into dist\AxisFlow via the
REM  shared stage_dist.bat, (2) run makensis on installer\AxisFlow.nsi.
REM  Output: AxisFlow-<VERSION>-win64-FC1.1-setup.exe in the repo root.
REM
REM  Requires NSIS (https://nsis.sourceforge.io) -- makensis.exe on PATH or in
REM  the default Program Files\NSIS location.
REM ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"

REM keep in sync with package.xml / make_release.bat
set "VERSION=0.2.0"
if not defined CONFIG set "CONFIG=RelWithDebInfo"

REM 1) build + stage runtime files
call "%~dp0stage_dist.bat" || goto fail

REM 2) locate makensis.exe
set "MAKENSIS="
for %%P in (makensis.exe) do if not defined MAKENSIS set "MAKENSIS=%%~$PATH:P"
if not defined MAKENSIS if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe"      set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
if not defined MAKENSIS (
    echo *** makensis.exe not found. Install NSIS and/or add it to PATH. ***
    echo     https://nsis.sourceforge.io
    goto fail
)
echo Using NSIS: %MAKENSIS%

REM 3) compile (paths inside the .nsi are relative to this repo root)
"%MAKENSIS%" /DAPPVERSION=%VERSION% installer\AxisFlow.nsi || goto fail

echo.
echo ==================================================
echo   INSTALLER READY: AxisFlow-%VERSION%-win64-FC1.1-setup.exe
echo   Test it on a clean FreeCAD 1.1 before publishing.
echo ==================================================
pause
exit /b 0

:fail
echo.
echo *** INSTALLER BUILD FAILED ***
pause
exit /b 1
