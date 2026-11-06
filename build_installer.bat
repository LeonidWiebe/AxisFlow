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
set "VERSION=0.2.1"
if not defined CONFIG set "CONFIG=RelWithDebInfo"

REM 1) build + stage runtime files
call "%~dp0stage_dist.bat" || goto fail

REM 2) locate makensis.exe
REM    NSIS does not have to be on PATH: the default install locations are
REM    checked too. For an install somewhere else, point at it explicitly:
REM        set "MAKENSIS=D:\Tools\NSIS\makensis.exe" && build_installer.bat
if defined MAKENSIS goto have_nsis
for %%P in (makensis.exe) do if not defined MAKENSIS set "MAKENSIS=%%~$PATH:P"
if not defined MAKENSIS if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe"      set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
:have_nsis
if not defined MAKENSIS (
    echo *** makensis.exe not found. ***
    echo     Looked on PATH, in "%%ProgramFiles(x86)%%\NSIS\" and "%%ProgramFiles%%\NSIS\".
    echo     If NSIS is installed elsewhere, say where and re-run:
    echo         set "MAKENSIS=D:\path\to\NSIS\makensis.exe" ^&^& build_installer.bat
    echo     https://nsis.sourceforge.io
    goto fail
)
if not exist "%MAKENSIS%" (
    echo *** MAKENSIS points at "%MAKENSIS%" but there is no such file. ***
    goto fail
)
echo Using NSIS: %MAKENSIS%

REM 3) compile.
REM    /NOCD is REQUIRED, not decoration. By default makensis changes the
REM    current directory to the one holding the .nsi -- so every relative path
REM    in the script would resolve against installer\ instead of the repo root:
REM    "LICENSE" became installer\LICENSE (open failed), dist\AxisFlow became
REM    installer\dist\AxisFlow, and the setup.exe would have been written into
REM    installer\ too. /NOCD keeps us in the repo root, which is where the
REM    script's paths are written to point.
"%MAKENSIS%" /NOCD /DAPPVERSION=%VERSION% installer\AxisFlow.nsi || goto fail

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
