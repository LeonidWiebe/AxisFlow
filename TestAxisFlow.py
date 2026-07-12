# SPDX-License-Identifier: LGPL-2.1-or-later
"""
FreeCAD unit tests for the AxisFlow module (Tier 2 -- Python API).

AxisFlow is a GUI-coupled extension (AxisFlow.pyd links FreeCADGui), so it does
NOT import under `freecadcmd`. Run these in a GUI FreeCAD:

    FreeCAD --run-test TestAxisFlow

or from the Python console:

    import Test, TestAxisFlow
    Test.runTestsFromModule(TestAxisFlow)

The API tests need no 3-D view (solve / settings / hotkeys go through the
service singleton). The session smoke test needs a view and skips if none.

The tests are non-destructive: persisted options touched here are snapshotted
and restored, and hotkey rebinds use persist=False so your stored layout is
never overwritten.
"""

import unittest
import FreeCAD as App

try:
    import AxisFlow as AF
except Exception:            # console mode / module not built
    AF = None


def _v(x, y, z):
    return App.Vector(x, y, z)


@unittest.skipIf(AF is None, "AxisFlow not importable (needs a GUI FreeCAD build)")
class TestAxisFlowApi(unittest.TestCase):
    """Pure, view-independent API: solve, locks, overrides, plane, settings."""

    def setUp(self):
        self._settings = AF.getSettings()
        AF.setConstrainEnabled(True)      # standalone semantics (adapter turns it off)
        AF.resetPlane()
        AF.setMode('rect')
        AF.clearLocks()
        AF.clearKeyValues()
        AF.confirm(_v(0, 0, 0))           # deterministic origin at world zero
        AF.clearLocks()
        AF.clearKeyValues()

    def tearDown(self):
        AF.clearLocks()
        AF.clearKeyValues()
        AF.resetPlane()
        AF.setMode('rect')
        for k in ('liveLineEnabled', 'liveLineStyle', 'liveLineColor', 'compassSize'):
            if k in self._settings:
                try:
                    AF.setSetting(k, self._settings[k])
                except Exception:
                    pass
        AF.loadHotkeysFromSettings()      # discard any session-scoped rebind

    # -- solve / locks / overrides ----------------------------------------
    def test_mode_roundtrip(self):
        AF.setMode('polar')
        self.assertEqual(AF.getMode(), 'polar')
        AF.setMode('rect')
        self.assertEqual(AF.getMode(), 'rect')

    def test_no_lock_passes_through(self):
        p = AF.solve(_v(3, 4, 5))
        self.assertAlmostEqual(p.x, 3)
        self.assertAlmostEqual(p.y, 4)
        self.assertAlmostEqual(p.z, 5)

    def test_lock_x_constrains(self):
        AF.lockAxis('x')
        p = AF.solve(_v(3, 4, 5))
        self.assertAlmostEqual(p.x, 3)
        self.assertAlmostEqual(p.y, 0)
        self.assertAlmostEqual(p.z, 0)

    def test_keyvalue_override(self):
        AF.setKeyValue(0, 10.0)
        p = AF.solve(_v(3, 4, 5))
        self.assertAlmostEqual(p.x, 10.0)
        self.assertAlmostEqual(p.y, 4.0)
        AF.clearKeyValues()
        p = AF.solve(_v(3, 4, 5))
        self.assertAlmostEqual(p.x, 3.0)

    def test_setplane_rotated(self):
        AF.setPlane(_v(0, 1, 0), _v(-1, 0, 0), _v(0, 0, 1))   # u == world +Y
        AF.lockAxis('x')
        p = AF.solve(_v(3, 4, 5))
        self.assertAlmostEqual(p.x, 0)
        self.assertAlmostEqual(p.y, 4)
        self.assertAlmostEqual(p.z, 0)

    # -- hotkeys / settings ------------------------------------------------
    # Qt key codes are stable ABI constants; use them directly to avoid the
    # PySide6 scoped-enum question (Qt.Key_F7 vs Qt.Key.Key_F7).
    def test_hotkeys_roundtrip(self):
        code = 0x01000036          # Qt::Key_F7
        AF.setHotkey(code, 'lockx', False)             # persist=False
        self.assertIn(code, AF.getSettings()['hotkeys'].get('LockX', []))

    def test_finishline_is_bindable(self):
        code = 0x01000037          # Qt::Key_F8
        AF.setHotkey(code, 'finishline', False)
        self.assertIn(code, AF.getSettings()['hotkeys'].get('FinishLine', []))

    def test_liveline_settings_roundtrip(self):
        AF.setSetting('liveLineEnabled', False)
        AF.setSetting('liveLineStyle', 2)
        AF.setSetting('liveLineColor', 0x33CCFFFF)
        s = AF.getSettings()
        self.assertFalse(bool(s['liveLineEnabled']))
        self.assertEqual(int(s['liveLineStyle']), 2)
        self.assertEqual(int(s['liveLineColor']), 0x33CCFFFF)


@unittest.skipIf(AF is None, "AxisFlow not importable (needs a GUI FreeCAD build)")
class TestAxisFlowSessionGui(unittest.TestCase):
    """Session lifecycle smoke test. Needs a GUI and a 3-D view; skips otherwise."""

    def test_session_lifecycle(self):
        if not getattr(App, "GuiUp", False):
            self.skipTest("no GUI")
        import FreeCADGui as Gui
        doc = App.newDocument("axisflow_selftest")
        try:
            try:
                Gui.activateWorkbench("AxisFlowWorkbenchPy")
            except Exception:
                pass
            view = None
            try:
                if Gui.ActiveDocument is not None:
                    view = Gui.ActiveDocument.ActiveView
            except Exception:
                view = None
            if view is None:
                self.skipTest("no active 3D view")
            AF.beginSession()
            self.assertTrue(AF.isActive())
            self.assertIsInstance(AF.getCompassInfo(), dict)
            AF.endSession()
            self.assertFalse(AF.isActive())
        finally:
            App.closeDocument(doc.Name)
