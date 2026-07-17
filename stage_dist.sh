#!/usr/bin/env bash
# ===========================================================================
#  stage_dist.sh -- build AxisFlow (Linux) and package the release tarball that
#  install.sh downloads. Mirror of stage_dist.bat / make_release.bat.
#
#  Output (in repo root):
#     dist/AxisFlow/                                  staged runtime files
#     AxisFlow-1_1-linux-<arch>-FC1.1.tar.gz          the release asset
#     SHA256SUMS.txt                                  checksum for the asset
#
#  The asset name is STABLE (no app version) on purpose: install.sh resolves the
#  latest tag from GitHub and downloads this fixed filename. The app version
#  lives in the git tag and in package.xml.
#
#  PREREQUISITE: a Linux-capable CMake build. The current CMakeLists.txt is
#  Windows-only (hardcoded pixi .lib paths, .pyd, WIN32 defines); it must be made
#  cross-platform (find FreeCAD/Qt/Coin, emit a prefix-less AxisFlow.so) before
#  this script can build. Once that exists, `cmake -B build && bash stage_dist.sh`
#  produces the asset.
# ===========================================================================
set -euo pipefail
cd "$(dirname "$0")"

ARCH="$(uname -m)"
TARGET_MM="1.1"
APPNAME="AxisFlow"
BUILD_DIR="${BUILD_DIR:-build}"
ASSET="${APPNAME}-${TARGET_MM/./_}-linux-${ARCH}-FC${TARGET_MM}.tar.gz"

echo "=== Build (${BUILD_DIR}) ==="
cmake --build "$BUILD_DIR" --target AxisFlow

# locate the freshly built module (plain AxisFlow.so or the cpython-suffixed one)
SO="$(find "$BUILD_DIR" -name 'AxisFlow*.so' -print -quit || true)"
[ -n "$SO" ] || { echo "*** AxisFlow*.so not found under ${BUILD_DIR}/ -- configure/build first. ***" >&2; exit 1; }
echo "Found module: $SO"

echo "=== Stage dist/AxisFlow ==="
rm -rf dist
mkdir -p "dist/${APPNAME}/Resources"
install -m 0755 "$SO"                 "dist/${APPNAME}/${APPNAME}.so"
install -m 0644 InitGui.py            "dist/${APPNAME}/"
install -m 0644 axisflow_snap.py      "dist/${APPNAME}/"
install -m 0644 axisflow_demo.py      "dist/${APPNAME}/"
install -m 0644 axisflow_settings.py  "dist/${APPNAME}/"
install -m 0644 package.xml           "dist/${APPNAME}/"
[ -f README.md ]    && install -m 0644 README.md    "dist/${APPNAME}/"
[ -f CHANGELOG.md ] && install -m 0644 CHANGELOG.md "dist/${APPNAME}/"
[ -f LICENSE ]      && install -m 0644 LICENSE      "dist/${APPNAME}/"
cp -r Resources/. "dist/${APPNAME}/Resources/" 2>/dev/null || cp -r resources/. "dist/${APPNAME}/Resources/"

echo "=== Tar -> ${ASSET} ==="
rm -f "$ASSET" SHA256SUMS.txt
tar -C dist -czf "$ASSET" "${APPNAME}"
sha256sum "$ASSET" > SHA256SUMS.txt

echo
echo "=================================================="
echo "  ASSET READY: ${ASSET}"
echo "  $(cat SHA256SUMS.txt)"
echo "  Upload both to the GitHub Release (tag = v<version>)."
echo "  Built for FreeCAD ${TARGET_MM}.x / ${ARCH}; test on that FreeCAD."
echo "=================================================="
