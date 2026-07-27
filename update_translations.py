#!/usr/bin/env python3
"""
update_translations.py -- regenerate the AxisFlow translation source and compile
all translations, following the FreeCAD "translating an external workbench"
procedure (https://wiki.freecad.org/Translating_an_external_workbench).

What it does:
  1. Extract user-visible strings from the Python UI and the C++ sources in ONE
     lupdate pass (Qt6 lupdate reads .py too) into translations/AxisFlow.ts,
     then merge the same set into every translations/AxisFlow_<lang>.ts --
     keeping existing translations and flagging new strings as unfinished.
  2. Compile every  translations/AxisFlow_<lang>.ts  into a  .qm  (lrelease),
     which is what FreeCAD loads at runtime.

Requires Qt Linguist tools on PATH: lupdate and lrelease. Debian/Ubuntu:
qt6-l10n-tools (or qttools5-dev-tools). Anywhere with pip:
    pip install PySide6-Essentials      # gives pyside6-lupdate / pyside6-lrelease
Note that PySide6 does NOT ship lconvert, which is why this script no longer
uses it.

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

PY_FILES = ["InitGui.py", "axisflow_settings.py", "axisflow_demo.py",
            "axisflow_transform.py", "axisflow_extrude.py"]
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
    """Refresh the base template AND merge into every AxisFlow_<lang>.ts.

    One lupdate pass over both the Python and the C++ sources: Qt6's lupdate
    parses .py directly, so the old two-pass + lconvert merge is unnecessary --
    and lconvert is not shipped by PySide6 at all, which used to stop this
    script dead. Running lupdate against a language file MERGES: existing
    translations are kept, new strings arrive as `type="unfinished"`, and
    strings that disappeared from the sources are marked obsolete rather than
    dropped.
    """
    os.makedirs(TRDIR, exist_ok=True)
    lupdate = _find("lupdate", "lupdate-qt5", "lupdate6", "pyside6-lupdate")
    if not lupdate:
        sys.exit("error: need 'lupdate' on PATH (Qt Linguist tools, or "
                 "pip install PySide6-Essentials).")

    sources = list(PY_FILES)
    for d in CPP_DIRS:
        for name in sorted(os.listdir(os.path.join(ROOT, d))):
            if name.endswith((".cpp", ".h")):
                sources.append(os.path.join(d, name))

    print("Updating the base template...")
    _run([lupdate] + sources + ["-ts", os.path.relpath(BASE, ROOT)])

    langs = sorted(n for n in os.listdir(TRDIR)
                   if n.endswith(".ts") and n != os.path.basename(BASE))
    for name in langs:
        print("Merging into %s..." % name)
        _run([lupdate] + sources + ["-ts", os.path.join("translations", name)])
    print("  %d language file(s) merged. Translate anything left "
          "type=\"unfinished\"." % len(langs))


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
