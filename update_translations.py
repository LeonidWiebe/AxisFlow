#!/usr/bin/env python3
"""
update_translations.py -- regenerate the AxisFlow translation source and compile
all translations, following the FreeCAD "translating an external workbench"
procedure (https://wiki.freecad.org/Translating_an_external_workbench).

What it does:
  1. Extract user-visible strings from the Python UI (pylupdate) and the C++
     sources (lupdate) and merge them into  translations/AxisFlow.ts  (the base
     source template, context-grouped).
  2. Compile every  translations/AxisFlow_<lang>.ts  into a  .qm  (lrelease),
     which is what FreeCAD loads at runtime.

Requires Qt Linguist tools on PATH. On Qt5:  pylupdate5, lupdate, lconvert,
lrelease  (Debian/Ubuntu: pyqt5-dev-tools + qttools5-dev-tools). On Qt6 a single
lupdate handles both .py and C++; pyside6-lupdate / pyside6-lrelease also work.

Usage:
    python update_translations.py            # extract + compile
    python update_translations.py --compile  # only compile existing *_<lang>.ts
"""

import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
TRDIR = os.path.join(ROOT, "translations")
BASE = os.path.join(TRDIR, "AxisFlow.ts")

PY_FILES = ["InitGui.py", "axisflow_settings.py", "axisflow_demo.py"]
CPP_DIRS = ["src"]


def _find(*names):
    for n in names:
        p = shutil.which(n)
        if p:
            return p
    return None


def _run(cmd):
    print("  $", " ".join(os.path.basename(c) if i == 0 else c
                           for i, c in enumerate(cmd)))
    subprocess.run(cmd, check=True, cwd=ROOT)


def extract():
    os.makedirs(TRDIR, exist_ok=True)
    pylupdate = _find("pylupdate5", "pylupdate6", "pyside6-lupdate", "pyside2-lupdate", "pylupdate")
    lupdate = _find("lupdate", "lupdate-qt5", "lupdate6", "pyside6-lupdate")
    lconvert = _find("lconvert", "lconvert-qt5")
    if not (lupdate and lconvert):
        sys.exit("error: need 'lupdate' and 'lconvert' on PATH (Qt Linguist tools).")

    py_ts = os.path.join(TRDIR, "_py.ts")
    cpp_ts = os.path.join(TRDIR, "_cpp.ts")

    print("Extracting Python strings...")
    if pylupdate and "pyside" not in os.path.basename(pylupdate):
        _run([pylupdate] + PY_FILES + ["-ts", py_ts])
    else:
        # Qt6 lupdate / pyside lupdate parse .py directly.
        _run([lupdate] + PY_FILES + ["-ts", py_ts])

    print("Extracting C++ strings...")
    _run([lupdate, "-recursive"] + CPP_DIRS + ["-ts", cpp_ts])

    print("Merging -> translations/AxisFlow.ts")
    _run([lconvert, "-i", py_ts, cpp_ts, "-o", BASE])
    for f in (py_ts, cpp_ts):
        try:
            os.remove(f)
        except OSError:
            pass
    print("  base source updated. Translate copies named AxisFlow_<lang>.ts.")


def compile_qm():
    lrelease = _find("lrelease", "lrelease-qt5", "lrelease6", "pyside6-lrelease")
    if not lrelease:
        sys.exit("error: need 'lrelease' on PATH.")
    made = 0
    for name in sorted(os.listdir(TRDIR)):
        # only language files (AxisFlow_xx.ts), not the base AxisFlow.ts
        if name.endswith(".ts") and "_" in name:
            _run([lrelease, os.path.join(TRDIR, name)])
            made += 1
    print("Compiled %d translation(s)." % made)
    if made == 0:
        print("  (no AxisFlow_<lang>.ts yet -- translate translations/AxisFlow.ts first.)")


def main():
    if not os.path.isdir(ROOT) or not os.path.exists(os.path.join(ROOT, "package.xml")):
        sys.exit("run this from the AxisFlow repo root.")
    only_compile = "--compile" in sys.argv[1:]
    if not only_compile:
        extract()
    compile_qm()
    print("Done.")


if __name__ == "__main__":
    main()
