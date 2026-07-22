@echo off
REM ===========================================================================
REM  AxisFlow -- build a release ZIP (prebuilt Windows binary workbench).
REM
REM  Produces  AxisFlow-<VERSION>-win64-FC1.1.zip  containing a single folder
REM  "AxisFlow\" with exactly the runtime files. The user unzips it into
REM      %APPDATA%\FreeCAD\Mod\   (or the versioned ...\FreeCAD\v1-1\Mod\)
REM  and restarts FreeCAD. NOTE: a compiled .pyd only loads in a FreeCAD whose
REM  Python/Qt/Coin ABI matches the one you built against -- build against the
REM  same FreeCAD distribution your users run, and test on a clean install.
REM
REM  For a GUI installer instead of a ZIP, see build_installer.bat (NSIS).
REM ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"

REM ---- edit per release ----
set "VERSION=0.1.3"
set "CONFIG=RelWithDebInfo"
set "ZIPNAME=AxisFlow-%VERSION%-win64-FC1.1.zip"
REM --------------------------

REM build + assemble runtime files into dist\AxisFlow (shared staging)
call "%~dp0stage_dist.bat" || goto fail

echo === Zip -^> %ZIPNAME% ===
if exist "%ZIPNAME%" del /q "%ZIPNAME%"
powershell -NoProfile -Command "Compress-Archive -Path 'dist\AxisFlow' -DestinationPath '%ZIPNAME%' -Force" || goto fail

echo.
echo ==================================================
echo   RELEASE READY: %ZIPNAME%
echo   Unzip into  %%APPDATA%%\FreeCAD\Mod\  then restart FreeCAD.
echo   (built %CONFIG%; verify it loads in the TARGET FreeCAD.)
echo ==================================================
pause
exit /b 0

:fail
echo.
echo *** RELEASE PACKAGING FAILED ***
pause
exit /b 1
