# ---------------------------------------------------------------------------
# AxisFlow C++ unit tests (pure-logic layer: Solver + Hotkeys).
#
# Off by default. Enable from the top-level configure:
#   cmake -B build -G "Visual Studio 17 2022" -A x64 -DAXISFLOW_BUILD_TESTS=ON
#
# Add ONE line at the very end of the root CMakeLists.txt (after Qt6/Python3/
# FreeCAD paths and the AxisFlow target):
#   add_subdirectory(tests)
#
# GoogleTest is compiled straight into the runner from source (gtest-all.cc +
# gtest_main.cc) -- NOT linked as a separate library. That removes every
# static/shared/ABI/link-order pitfall we hit: the gtest headers and their
# definitions come from one place, built with the same flags as the tests.
#
# The units under test link only FreeCAD Base (Vector3d/Matrix4d) + Python + Qt
# headers -- no Coin, no FreeCADGui, no running FreeCAD. Python/Qt DLLs from the
# pixi env must be on PATH when the test exe runs.
# ---------------------------------------------------------------------------

option(AXISFLOW_BUILD_TESTS "Build the AxisFlow C++ unit tests" OFF)
if(NOT AXISFLOW_BUILD_TESTS)
    return()
endif()

enable_testing()

message(STATUS "AxisFlow tests: configuring AxisFlow_tests_run (gtest compiled from source)")

# Download GoogleTest sources only (we compile them ourselves; the gtest/
# gtest_main library targets are defined but EXCLUDE_FROM_ALL, so they are never
# built or linked).
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_executable(AxisFlow_tests_run
    test_solver.cpp
    test_hotkeys.cpp
    # Units under test:
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/AxisFlowSolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/AxisFlowHotkeys.cpp
    # GoogleTest, compiled in directly:
    ${googletest_SOURCE_DIR}/googletest/src/gtest-all.cc
    ${googletest_SOURCE_DIR}/googletest/src/gtest_main.cc
)

target_include_directories(AxisFlow_tests_run PRIVATE
    # GoogleTest FIRST so <gtest/gtest.h> can never resolve to a stray copy on
    # the FreeCAD/pixi include paths. The googletest root is needed by
    # gtest-all.cc for its internal "src/..." includes.
    ${googletest_SOURCE_DIR}/googletest/include
    ${googletest_SOURCE_DIR}/googletest
    ${CMAKE_CURRENT_SOURCE_DIR}/../src      # PreCompiled.h, AxisFlow*.h
    ${FREECAD_INCLUDE_DIRS}                 # FCConfig.h, Base/*
    ${Python3_INCLUDE_DIRS}
    "${PIXI_ENV}/include/boost"
)

find_package(Threads REQUIRED)

target_link_libraries(AxisFlow_tests_run PRIVATE
    Threads::Threads
    Qt6::Core
    Qt6::Widgets                            # PreCompiled.h pulls QtWidgets
    "${FREECAD_BUILD}/src/Base/FreeCADBase.lib"
    ${Python3_LIBRARIES}
    # If the linker reports unresolved App symbols, also add:
    #   "${FREECAD_BUILD}/src/App/FreeCADApp.lib"
)

# Register each TEST() with CTest. PRE_TEST defers enumeration to `ctest` time,
# so the build itself does not need the runner's Python/Qt DLLs on PATH.
include(GoogleTest)
gtest_discover_tests(AxisFlow_tests_run DISCOVERY_MODE PRE_TEST)
