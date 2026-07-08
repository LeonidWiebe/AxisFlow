cmake_minimum_required(VERSION 3.14)
project(AxisFlow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===========================================================================
# PATHS — всё найдено автоматически, менять не нужно
# ===========================================================================
set(PIXI_ENV      "C:/projects/FreeCAD111/.pixi/envs/default/Library"
    CACHE PATH "pixi environment Library dir")
set(FREECAD_SRC   "C:/projects/FreeCAD111/src"
    CACHE PATH "FreeCAD source directory")
set(FREECAD_BUILD "C:/projects/FreeCAD111/build/release"
    CACHE PATH "FreeCAD build/release directory")
# ===========================================================================

# ---------------------------------------------------------------------------
# Qt6
# ---------------------------------------------------------------------------
set(Qt6_DIR "${PIXI_ENV}/lib/cmake/Qt6")
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGL)

# ---------------------------------------------------------------------------
# Coin4
# ---------------------------------------------------------------------------
set(COIN4_INCLUDE_DIR "${PIXI_ENV}/include")
set(COIN4_LIB         "${PIXI_ENV}/lib/Coin4.lib")

# ---------------------------------------------------------------------------
# FreeCAD headers (из исходников)
# ---------------------------------------------------------------------------
set(FREECAD_INCLUDE_DIRS
    "${FREECAD_SRC}"
    "${FREECAD_SRC}/Gui"
    "${FREECAD_SRC}/Base"
    "${FREECAD_SRC}/App"
    "${FREECAD_BUILD}"
    "${FREECAD_BUILD}/src"
    "${PIXI_ENV}/include"
    "${PIXI_ENV}/include/qt6"
    "${PIXI_ENV}/include/qt6/QtCore"
    "${PIXI_ENV}/include/qt6/QtGui"
    "${PIXI_ENV}/include/qt6/QtWidgets"
    "${PIXI_ENV}/include/qt6/QtOpenGL"
    "${FREECAD_SRC}/3rdParty/PyCXX"
    "${FREECAD_BUILD}/src/3rdParty/PyCXX"
    "${PIXI_ENV}/include"
    "${PIXI_ENV}/../include"
)

# Python headers/libs — pin to the SAME interpreter FreeCAD embeds (the pixi
# env). Otherwise find_package may grab a system Python of a different version,
# and the resulting .pyd will depend on the wrong pythonXX.dll → import fails
# with "DLL load failed: module not found" (or crashes on load).
set(PYTHON_ROOT "C:/projects/FreeCAD111/.pixi/envs/default")
set(Python3_ROOT_DIR       "${PYTHON_ROOT}")
set(Python3_FIND_STRATEGY  LOCATION)   # trust the given root, don't grab "newest"
set(Python3_FIND_REGISTRY  NEVER)      # ignore Windows-registry Python installs
set(Python3_FIND_VIRTUALENV FIRST)
find_package(Python3 REQUIRED COMPONENTS Development)
message(STATUS "  Python3     : ${Python3_VERSION}")
message(STATUS "  Python3 exe : ${Python3_EXECUTABLE}")
message(STATUS "  Python3 inc : ${Python3_INCLUDE_DIRS}")
message(STATUS "  Python3 lib : ${Python3_LIBRARIES}")

# ---------------------------------------------------------------------------
# FreeCAD import libs (из build/release)
# ---------------------------------------------------------------------------
set(FREECAD_LIBRARIES
    "${FREECAD_BUILD}/src/Base/FreeCADBase.lib"
    "${FREECAD_BUILD}/src/App/FreeCADApp.lib"
    "${FREECAD_BUILD}/src/Gui/FreeCADGui.lib"
)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SOURCES
    src/AppAxisFlow.cpp
    src/AxisFlowPy.cpp
    src/Command.cpp
    src/Workbench.cpp
    src/PreCompiled.h
    src/AxisFlowTool.cpp
    src/AxisFlowService.cpp
    src/AxisFlowHotkeys.cpp
    src/AxisFlowSolver.cpp
    src/AxisFlowCompass.cpp
    src/AxisFlowPanel.cpp
    src/AxisFlowEventFilter.cpp
    src/AxisFlowSettings.cpp
)

set(HEADERS
    src/Workbench.h
    src/AxisFlowTool.h
    src/AxisFlowService.h
    src/AxisFlowHotkeys.h
    src/AxisFlowSolver.h
    src/AxisFlowCompass.h
    src/AxisFlowPanel.h
    src/AxisFlowEventFilter.h
    src/AxisFlowSettings.h
)

qt6_wrap_cpp(MOC_SOURCES
    src/AxisFlowTool.h
    src/AxisFlowService.h
    src/AxisFlowPanel.h
    src/AxisFlowSettings.h
)

qt6_add_resources(QRC_SOURCES resources/AxisFlow.qrc)

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------
add_library(AxisFlow SHARED
    ${SOURCES}
    ${HEADERS}
    ${MOC_SOURCES}
    ${QRC_SOURCES}
)

target_include_directories(AxisFlow PRIVATE
    ${FREECAD_INCLUDE_DIRS}
    ${COIN4_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
    "C:/projects/FreeCAD111/.pixi/envs/default/Library/include/boost"
    src
)

target_link_libraries(AxisFlow PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGL
    ${COIN4_LIB}
    ${FREECAD_LIBRARIES}
    ${Python3_LIBRARIES}
    "${PIXI_ENV}/lib/fmt.lib"
)

# ---------------------------------------------------------------------------
# Python import library directory
# Python.h on MSVC injects  #pragma comment(lib, "python311.lib")  which the
# linker resolves by bare name against /LIBPATH. conda/pixi place that .lib in
# <env>/libs, which is NOT added automatically → add it here (fixes LNK1104).
# ---------------------------------------------------------------------------
target_link_directories(AxisFlow PRIVATE
    "${PYTHON_ROOT}/libs"
)

target_compile_definitions(AxisFlow PRIVATE
    AXISFLOW_EXPORTS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _USE_MATH_DEFINES
    HAVE_ROUND          # prevents python object.h conflict on MSVC
)

if(MSVC)
    # /FI forces include of PreCompiled.h before every TU including MOC-generated files
    target_compile_options(AxisFlow PRIVATE
        /MD /W3 /wd4251 /wd4275 /wd4996 /wd4005
    )
endif()

set_target_properties(AxisFlow PROPERTIES
    # Python on Windows imports extension modules only as .pyd, never .dll.
    # (This is how FreeCAD's own modules — Part.pyd, Sketcher.pyd — are built.)
    PREFIX ""
    SUFFIX ".pyd"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# ---------------------------------------------------------------------------
# Install -> FreeCAD user Mod dir
#
# FreeCAD 1.1 versions its user data directory: %APPDATA%/FreeCAD/v1-1/Mod,
# not %APPDATA%/FreeCAD/Mod. Installing into the unversioned path means FreeCAD
# never scans the module: no workbench, no InitGui.py, `import AxisFlow` fails.
#
# Detected from the filesystem (no Python needed -- find_package(Python3) is
# requested with Development only, so Python3_EXECUTABLE is empty here).
# Override explicitly with:
#   cmake -B build -DFREECAD_MOD_DIR="D:/somewhere/Mod/AxisFlow"
#
# NOTE: this is a CACHE variable. After changing FreeCAD versions, delete the
# build directory (or re-pass -DFREECAD_MOD_DIR), or the stale cached path wins.
# ---------------------------------------------------------------------------
set(FREECAD_MOD_DIR "" CACHE PATH "Target FreeCAD user Mod dir (empty = autodetect)")

if(NOT FREECAD_MOD_DIR)
    set(_fc_user "$ENV{APPDATA}/FreeCAD")
    set(_fc_base "${_fc_user}")

    # Pick the highest versioned directory (v1-1, v1-2, ...) if any exist.
    file(GLOB _fc_versioned LIST_DIRECTORIES true "${_fc_user}/v*")
    list(SORT _fc_versioned)
    foreach(_d IN LISTS _fc_versioned)
        if(IS_DIRECTORY "${_d}")
            set(_fc_base "${_d}")
        endif()
    endforeach()

    set(_fc_auto "${_fc_base}/Mod/AxisFlow")
    file(TO_CMAKE_PATH "${_fc_auto}" _fc_auto)
    set(FREECAD_MOD_DIR "${_fc_auto}" CACHE PATH
        "Target FreeCAD user Mod dir (empty = autodetect)" FORCE)
endif()

install(TARGETS AxisFlow
    RUNTIME DESTINATION "${FREECAD_MOD_DIR}"
)
install(FILES InitGui.py        DESTINATION "${FREECAD_MOD_DIR}")
install(FILES axisflow_snap.py  DESTINATION "${FREECAD_MOD_DIR}")

message(STATUS "=== AxisFlow build config ===")
message(STATUS "  Qt6 dir     : ${Qt6_DIR}")
message(STATUS "  Coin4 lib   : ${COIN4_LIB}")
message(STATUS "  FreeCAD src : ${FREECAD_SRC}")
message(STATUS "  FreeCAD libs: ${FREECAD_LIBRARIES}")
message(STATUS "  Install to  : ${FREECAD_MOD_DIR}")
