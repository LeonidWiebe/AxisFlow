@echo off
REM ===========================================================================
REM  AxisFlow -- local Tier 1 test runner (C++ unit tests, headless).
REM
REM  Usage:
REM    run_tests.bat            configure + build + run tests
REM    run_tests.bat clean      wipe build\ first (fresh gtest + full rebuild)
REM    run_tests.bat nopause    don't wait for a keypress at the end (scripting)
REM  (arguments can be combined, e.g.  run_tests.bat clean nopause)
REM
REM  Tier 2 (Python API) needs a GUI FreeCAD and is run by hand from its
REM  Python console:   import Test, TestAxisFlow; Test.runTestsFromModule(TestAxisFlow)
REM ===========================================================================

setlocal EnableExtensions
cd /d "%~dp0"

REM ---- edit these if your paths differ --------------------------------------
set "PIXI_ROOT=C:\projects\FreeCAD111\.pixi\envs\default"
set "GENERATOR=Visual Studio 17 2022"
set "CONFIG=RelWithDebInfo"
REM --------------------------------------------------------------------------

set "DO_CLEAN=0"
set "DO_PAUSE=1"
:parse
if "%~1"=="" goto after_parse
if /I "%~1"=="clean"   set "DO_CLEAN=1"
if /I "%~1"=="nopause" set "DO_PAUSE=0"
shift
goto parse
:after_parse

if "%DO_CLEAN%"=="1" (
    echo [clean] removing build\ ...
    if exist build rmdir /s /q build
)

echo.
echo === Configure ^(tests ON^) ===
cmake -B build -G "%GENERATOR%" -A x64 -DAXISFLOW_BUILD_TESTS=ON || goto fail

echo.
echo === Build test runner ===
cmake --build build --config %CONFIG% --target AxisFlow_tests_run || goto fail

if not exist "%PIXI_ROOT%\Library\bin" (
    echo.
    echo [warn] pixi bin not found: %PIXI_ROOT%\Library\bin
    echo        The test exe needs the pixi Python/Qt DLLs; edit PIXI_ROOT above
    echo        if the run step fails with "DLL load failed".
)

echo.
echo === Run tests ===
set "PATH=%PIXI_ROOT%\Library\bin;%PIXI_ROOT%;%PATH%"
ctest --test-dir build -C %CONFIG% --output-on-failure || goto fail

echo.
echo ==============================
echo    ALL TESTS PASSED
echo ==============================
set "RC=0"
goto end

:fail
echo.
echo ******************************
echo    TESTS FAILED - see above
echo ******************************
set "RC=1"

:end
if "%DO_PAUSE%"=="1" pause
exit /b %RC%
