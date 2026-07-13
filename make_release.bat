@echo off
REM ===========================================================================
REM  AxisFlow -- build a release ZIP (prebuilt Windows binary workbench).
REM
REM  Produces  AxisFlow-<VERSION>-win64-FC1.1.zip  containing a single folder
REM  "AxisFlow\" with exactly the runtime files. The user unzips it into
REM      %APPDATA%\FreeCAD\Mod\
REM  and restarts FreeCAD. NOTE: a compiled .pyd only loads in a FreeCAD whose
REM  Python/Qt/Coin ABI matches the one you built against -- build against the
REM  same FreeCAD distribution your users run, and test on a clean install.
REM ===========================================================================

setlocal EnableExtensions
cd /d "%~dp0"

REM ---- edit per release ----
set "VERSION=0.1.1"
set "CONFIG=RelWithDebInfo"
set "ZIPNAME=AxisFlow-%VERSION%-win64-FC1.1.zip"
REM --------------------------

echo === Build (%CONFIG%) ===
cmake --build build --config %CONFIG% --target AxisFlow || goto fail

REM locate the freshly built module wherever the generator put it
set "PYD="
for /r "%~dp0build" %%F in (AxisFlow.pyd) do set "PYD=%%F"
if not defined PYD (
    echo *** AxisFlow.pyd not found under build\ -- configure/build first. ***
    goto fail
)
echo Found module: %PYD%

echo === Stage dist\AxisFlow ===
if exist dist rmdir /s /q dist
mkdir dist\AxisFlow || goto fail
copy /y "%PYD%"               dist\AxisFlow\ >nul || goto fail
copy /y InitGui.py           dist\AxisFlow\ >nul || goto fail
copy /y axisflow_snap.py     dist\AxisFlow\ >nul || goto fail
copy /y axisflow_demo.py     dist\AxisFlow\ >nul || goto fail
copy /y axisflow_settings.py dist\AxisFlow\ >nul || goto fail
copy /y package.xml          dist\AxisFlow\ >nul || goto fail
if exist README.md copy /y README.md dist\AxisFlow\ >nul
if exist LICENSE   copy /y LICENSE   dist\AxisFlow\ >nul
xcopy /e /i /y Resources     dist\AxisFlow\Resources >nul || goto fail

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
