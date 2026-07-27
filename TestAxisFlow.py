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
        for k in ('liveLineEnabled', 'liveLineStyle', 'liveLineColor', 'compassSize',
                  'liveLineTransparency', 'useDistanceIndexing', 'useAxesIndexing',
                  'indexingTolerance', 'indexingAxisWeight', 'indexingAxisTransparency',
                  'useSnapping', 'smartNegativeDirection', 'focusAdvanceOnMove',
                  'compassFollowDirection', 'compassRotateAnimation'):
            if k in self._settings:
                try:
                    AF.setSetting(k, self._settings[k])
                except Exception:
                    pass
        try:
            AF.clearIndexedDistance()
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

    def test_lock_x_freezes_the_field(self):
        # New semantics: a lock FREEZES that coordinate at its current value;
        # the free fields keep following the cursor.
        AF.solve(_v(3, 4, 5))            # cursor at local x = 3
        AF.lockAxis('x')                 # freeze X there
        self.assertTrue(AF.getState()['locks']['x'])
        p = AF.solve(_v(7, 8, 9))        # cursor moved
        self.assertAlmostEqual(p.x, 3)
        self.assertAlmostEqual(p.y, 8)
        self.assertAlmostEqual(p.z, 9)
        AF.clearLocks()
        self.assertFalse(AF.getState()['locks']['x'])
        p = AF.solve(_v(7, 8, 9))
        self.assertAlmostEqual(p.x, 7)

    def test_smartlock_pair_constrains_to_axis(self):
        # Movement along one axis == the OTHER two fields locked at 0.
        AF.solve(_v(3, 0, 0))            # cursor exactly on the compass X axis
        AF.lockAxis('y')                 # the SmartLock pair, set via the API
        AF.lockAxis('z')
        st = AF.getState()['locks']
        self.assertTrue(st['y'])
        self.assertTrue(st['z'])
        p = AF.solve(_v(7, 8, 9))
        self.assertAlmostEqual(p.x, 7)
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
        AF.solve(_v(3, 4, 5))            # local: x=4 (along u), y=-3, z=5
        AF.lockAxis('x')                 # freeze the compass-u coordinate at 4
        p = AF.solve(_v(7, 8, 9))        # local of (7,8,9): x=8, y=-7, z=9
        # x frozen at 4 -> world = 4*u + (-7)*v + 9*n = (7, 4, 9)
        self.assertAlmostEqual(p.x, 7)
        self.assertAlmostEqual(p.y, 4)
        self.assertAlmostEqual(p.z, 9)

    def test_align_compass_x_aims_at_a_direction(self):
        """alignCompassX is what the extrude tool stands the compass up with.

        setPlane needs a ready-made orthonormal triple; this one takes a single
        direction (a wire's normal, in practice) and derives the rest.
        """
        AF.alignCompassX(_v(0, 0, 5))                 # length must not matter
        u = AF.getCompassInfo()['planeU']
        self.assertAlmostEqual(u[0], 0.0)
        self.assertAlmostEqual(u[1], 0.0)
        self.assertAlmostEqual(u[2], 1.0)
        AF.resetPlane()

    def test_align_compass_x_makes_the_drag_read_as_x(self):
        """What the extrude tool actually depends on: with the compass aimed
        along the sweep, freezing the other two fields leaves the cursor's
        distance along that direction as the X field -- so the height can be
        typed instead of dragged."""
        AF.confirm(_v(0, 0, 0))
        AF.alignCompassX(_v(0, 0, 1))
        AF.setKeyValue(1, 0.0)                        # freeze v
        AF.setKeyValue(2, 0.0)                        # freeze the normal
        p = AF.solve(_v(7, 9, 40))                    # cursor wanders off axis
        # Only the component along u survives, and it is the world Z here.
        self.assertAlmostEqual(p.x, 0.0)
        self.assertAlmostEqual(p.y, 0.0)
        self.assertAlmostEqual(p.z, 40.0)
        AF.clearKeyValues()
        AF.resetPlane()

    def test_align_compass_x_builds_an_orthonormal_frame(self):
        """A skewed frame would be silently wrong: the solver inverts the
        compass matrix by transposing it, which only holds when it is
        orthonormal. Checked at the binding as well as in Tier 1."""
        def dot(a, b):
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

        AF.alignCompassX(_v(1, 2, 3))
        info = AF.getCompassInfo()
        u, v, n = info['planeU'], info['planeV'], info['planeN']
        for axis in (u, v, n):
            self.assertAlmostEqual(dot(axis, axis), 1.0, places=6)
        self.assertAlmostEqual(dot(u, v), 0.0, places=6)
        self.assertAlmostEqual(dot(u, n), 0.0, places=6)
        self.assertAlmostEqual(dot(v, n), 0.0, places=6)
        AF.resetPlane()

    def test_align_compass_x_survives_a_direction_along_world_z(self):
        """The degenerate case the helper exists for: 'up' defaults to world Z,
        so a direction parallel to it would collapse the cross product. That is
        not exotic -- it is the normal of any wire drawn on the XY plane."""
        def dot(a, b):
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]

        AF.alignCompassX(_v(0, 0, 1))
        info = AF.getCompassInfo()
        u, v, n = info['planeU'], info['planeV'], info['planeN']
        self.assertAlmostEqual(dot(v, v), 1.0, places=6)
        self.assertAlmostEqual(dot(n, n), 1.0, places=6)
        self.assertAlmostEqual(dot(u, v), 0.0, places=6)
        self.assertAlmostEqual(dot(u, n), 0.0, places=6)
        AF.resetPlane()

    def test_align_compass_x_ignores_a_zero_direction(self):
        """A degenerate wire must not wipe the compass orientation."""
        AF.setPlane(_v(0, 1, 0), _v(-1, 0, 0), _v(0, 0, 1))
        before = AF.getCompassInfo()['planeU']
        AF.alignCompassX(_v(0, 0, 0))
        after = AF.getCompassInfo()['planeU']
        for i in range(3):
            self.assertAlmostEqual(before[i], after[i])
        AF.resetPlane()

    def test_set_compass_visible_is_display_state_only(self):
        """Hiding the compass must not touch the service.

        The distinction is the whole reason the call exists: deactivate() also
        hides the panel and, because AxisFlowTool::activate() activates the
        service itself, was undone by the next getPoint().
        """
        self.assertTrue(AF.isActive())
        AF.setCompassVisible(False)
        self.assertTrue(AF.isActive(), "hiding the compass deactivated AxisFlow")
        p = AF.solve(_v(5, 0, 0))
        self.assertAlmostEqual(p.x, 5.0)      # still solving
        AF.setCompassVisible(True)
        self.assertTrue(AF.isActive())

    def test_set_compass_visible_survives_a_double_call(self):
        for _ in range(2):
            AF.setCompassVisible(False)
        for _ in range(2):
            AF.setCompassVisible(True)
        self.assertTrue(AF.isActive())

    # -- hotkeys / settings ------------------------------------------------
    # Qt key codes are stable ABI constants; use them directly to avoid the
    # PySide6 scoped-enum question (Qt.Key_F7 vs Qt.Key.Key_F7).
    def test_lockd_is_bindable(self):
        code = 0x01000035          # Qt::Key_F6
        AF.setHotkey(code, 'lockd', False)
        self.assertIn(code, AF.getSettings()['hotkeys'].get('LockD', []))

    def test_view_actions_are_bindable(self):
        code = 0x01000038          # Qt::Key_F9
        AF.setHotkey(code, 'rotatecycle', False)
        self.assertIn(code, AF.getSettings()['hotkeys'].get('RotateCycle', []))
        code2 = 0x01000039         # Qt::Key_F10
        AF.setHotkey(code2, 'setorigin', False)
        self.assertIn(code2, AF.getSettings()['hotkeys'].get('SetOrigin', []))

    def test_getstate_reports_lock_slots(self):
        locks = AF.getState()['locks']
        for k in ('x', 'y', 'z', 'd', 'a'):
            self.assertIn(k, locks)
            self.assertFalse(locks[k])

    def test_hotkeys_roundtrip(self):
        code = 0x01000036          # Qt::Key_F7
        AF.setHotkey(code, 'lockx', False)             # persist=False
        self.assertIn(code, AF.getSettings()['hotkeys'].get('LockX', []))

    def test_finishline_is_bindable(self):
        code = 0x01000037          # Qt::Key_F8
        AF.setHotkey(code, 'finishline', False)
        self.assertIn(code, AF.getSettings()['hotkeys'].get('FinishLine', []))

    def test_smart_negative_setting_roundtrip(self):
        # Default is on (the AccuDraw sign-from-direction behaviour).
        self.assertTrue(bool(AF.getSettings()['smartNegativeDirection']))
        AF.setSetting('smartNegativeDirection', False)
        self.assertFalse(bool(AF.getSettings()['smartNegativeDirection']))

    def test_compass_follow_direction(self):
        # Default off; 1 = keep Z (in-plane), 2 = full. The setUp confirm at
        # the origin armed the reference, so the next confirm rotates.
        self.assertEqual(int(AF.getSettings()['compassFollowDirection']), 0)
        AF.setSetting('compassFollowDirection', 1)
        AF.confirm(_v(3, 4, 0))          # segment (3,4,0) -> u = (0.6, 0.8, 0)
        u = AF.getCompassInfo()['planeU']
        self.assertAlmostEqual(u[0], 0.6)
        self.assertAlmostEqual(u[1], 0.8)
        self.assertAlmostEqual(u[2], 0.0)
        n = AF.getCompassInfo()['planeN']
        self.assertAlmostEqual(n[2], 1.0)   # keep-Z: the normal is untouched
        AF.resetPlane()

    def test_rotate_animation_setting_roundtrip(self):
        # Display-only easing of the compass; 0 disables, values clamp to 0..2.
        self.assertAlmostEqual(float(AF.getSettings()['compassRotateAnimation']), 0.5)
        AF.setSetting('compassRotateAnimation', 1.0)
        self.assertAlmostEqual(float(AF.getSettings()['compassRotateAnimation']), 1.0)
        AF.setSetting('compassRotateAnimation', 9.0)   # clamped
        self.assertAlmostEqual(float(AF.getSettings()['compassRotateAnimation']), 2.0)

    def test_focus_advance_setting_roundtrip(self):
        # Default is on: a deliberate cursor move after typing X hands the
        # caret to Y (and back), so a coordinate pair is typed in one flow.
        self.assertTrue(bool(AF.getSettings()['focusAdvanceOnMove']))
        AF.setSetting('focusAdvanceOnMove', False)
        self.assertFalse(bool(AF.getSettings()['focusAdvanceOnMove']))

    def test_liveline_settings_roundtrip(self):
        AF.setSetting('liveLineEnabled', False)
        AF.setSetting('liveLineStyle', 2)
        AF.setSetting('liveLineColor', 0x33CCFFFF)
        s = AF.getSettings()
        self.assertFalse(bool(s['liveLineEnabled']))
        self.assertEqual(int(s['liveLineStyle']), 2)
        self.assertEqual(int(s['liveLineColor']), 0x33CCFFFF)

    # -- indexing ----------------------------------------------------------
    def test_indexing_settings_roundtrip(self):
        AF.setSetting('useDistanceIndexing', True)
        AF.setSetting('useAxesIndexing', True)
        AF.setSetting('indexingTolerance', 12)
        AF.setSetting('indexingAxisWeight', 3)
        AF.setSetting('indexingAxisTransparency', 0.5)
        AF.setSetting('liveLineTransparency', 0.25)
        s = AF.getSettings()
        self.assertTrue(bool(s['useDistanceIndexing']))
        self.assertTrue(bool(s['useAxesIndexing']))
        self.assertEqual(int(s['indexingTolerance']), 12)
        self.assertEqual(int(s['indexingAxisWeight']), 3)
        self.assertAlmostEqual(float(s['indexingAxisTransparency']), 0.5, places=3)
        self.assertAlmostEqual(float(s['liveLineTransparency']), 0.25, places=3)

    def test_indexing_tolerance_is_clamped(self):
        AF.setSetting('indexingTolerance', 999)
        self.assertLessEqual(int(AF.getSettings()['indexingTolerance']), 50)
        AF.setSetting('indexingTolerance', -5)
        self.assertGreaterEqual(int(AF.getSettings()['indexingTolerance']), 0)

    def test_indexed_distance_api_and_state(self):
        AF.setIndexedDistance(42.0)
        self.assertAlmostEqual(float(AF.getState()['indexedDistance']), 42.0)
        AF.clearIndexedDistance()
        self.assertIsNone(AF.getState()['indexedDistance'])

    def test_getstate_exposes_candidate_flags(self):
        st = AF.getState()
        self.assertIn('indexCandidatePoint', st)
        self.assertIn('indexCandidateAxis', st)

    def test_indexing_inert_without_view_scale(self):
        # No 3-D view here -> no pixel scale pushed -> indexing cannot fire, so
        # solve() must pass the raw point through even with indexing enabled.
        AF.setSetting('useAxesIndexing', True)
        AF.setSetting('indexingTolerance', 50)
        p = AF.solve(_v(100, 3, 5))
        self.assertAlmostEqual(p.y, 3.0)   # not snapped onto the X axis

    def test_default_settings_match_the_live_keys(self):
        """getDefaultSettings() must cover exactly what getSettings() exposes,
        or the dialog's per-section restore would silently skip options."""
        live = set(AF.getSettings().keys()) - {'hotkeys'}
        self.assertEqual(set(AF.getDefaultSettings().keys()), live)

    def test_default_settings_are_accepted_by_set_setting(self):
        """Every default must round-trip: the restore button writes them back."""
        defaults = AF.getDefaultSettings()
        before = AF.getSettings()
        try:
            for key, value in defaults.items():
                AF.setSetting(key, value)
            after = AF.getSettings()
            for key, value in defaults.items():
                if isinstance(value, float):
                    self.assertAlmostEqual(float(after[key]), float(value), places=3)
                else:
                    self.assertEqual(type(value)(after[key]), value, key)
        finally:
            for key, value in before.items():
                if key != 'hotkeys':
                    AF.setSetting(key, value)

    def test_readme_settings_table_matches_the_defaults(self):
        """The Settings table in README.md is the user-facing contract, and it
        drifted twice before this test existed: three CompassPath* options were
        never documented, and a whole column of defaults in the handoff notes
        went stale after the compass was retuned. Parse the table and compare it
        against the code so a changed default or a new option cannot ship with
        the docs quietly disagreeing.

        Skipped when README.md is not next to this file: the installed module
        ships without it, and a missing doc must not fail an install-tree run.
        """
        import os
        import re

        readme = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'README.md')
        if not os.path.isfile(readme):
            self.skipTest('README.md not present (installed tree)')

        with open(readme, encoding='utf-8') as handle:
            text = handle.read()

        # Rows look like:  | `Key` | `camelName` | Type | default | meaning |
        row = re.compile(
            r'^\|\s*`(\w+)`\s*\|\s*`(\w+)`\s*\|\s*(\w+)\s*\|\s*`?([^`|]+?)`?\s*\|',
            re.MULTILINE)
        documented = {m.group(2): (m.group(3), m.group(4).strip())
                      for m in row.finditer(text)}
        self.assertTrue(documented, 'no settings rows parsed out of README.md')

        defaults = AF.getDefaultSettings()
        self.assertEqual(set(documented), set(defaults),
                         'README settings table and getDefaultSettings() differ')

        for name, (kind, written) in documented.items():
            actual = defaults[name]
            if kind == 'Bool':
                self.assertEqual(written.lower(), str(bool(actual)).lower(), name)
            elif kind == 'Unsigned':
                self.assertEqual(int(written, 16), int(actual), name)
            elif kind == 'Float':
                self.assertAlmostEqual(float(written), float(actual), places=3,
                                       msg=name)
            elif kind == 'Int':
                self.assertEqual(int(written), int(actual), name)
            else:
                self.fail('unknown type column {0!r} for {1}'.format(kind, name))

    def test_set_base_point_moves_the_origin(self):
        """setBasePoint moves the compass anchor without confirming a point."""
        AF.beginSession()
        try:
            AF.setBasePoint(_v(3, 4, 5))
            o = AF.getState()['origin']
            self.assertAlmostEqual(o.x, 3.0)
            self.assertAlmostEqual(o.y, 4.0)
            self.assertAlmostEqual(o.z, 5.0)
        finally:
            AF.endSession()

    def test_set_base_point_without_a_session_raises(self):
        AF.endSession()
        with self.assertRaises(RuntimeError):
            AF.setBasePoint(_v(1, 2, 3))

    # -- two-key chords ----------------------------------------------------
    # Qt key codes spelled out: QtCore is not imported in this module.
    _KEY_G = 0x47
    _KEY_L = 0x4C

    @staticmethod
    def _as_list(codes):
        """Normalise a hotkey list: chords arrive as nested pairs."""
        return [list(c) if isinstance(c, (list, tuple)) else c for c in (codes or [])]

    def test_chord_defaults_are_pairs(self):
        hk = AF.getSettings()['hotkeys']
        for name in ('RotateAboutX', 'RotateAboutY', 'RotateAboutZ'):
            binding = self._as_list(hk.get(name))
            self.assertEqual(len(binding), 1, name)
            self.assertEqual(len(binding[0]), 2, name)

    def test_chord_prefix_is_never_also_a_single_key(self):
        # The rule that makes a chord resolvable without a timeout.
        hk = AF.getSettings()['hotkeys']
        prefixes, singles = set(), set()
        for codes in hk.values():
            for c in self._as_list(codes):
                if isinstance(c, list):
                    prefixes.add(c[0])
                else:
                    singles.add(c)
        self.assertEqual(prefixes & singles, set())

    def test_binding_a_prefix_as_a_single_key_is_refused(self):
        hk = AF.getSettings()['hotkeys']
        prefix = self._as_list(hk['RotateAboutX'])[0][0]
        with self.assertRaises(ValueError):
            AF.setHotkeys('ClearLocks', [prefix], False)

    def test_chord_roundtrips_through_the_api(self):
        before = self._as_list(AF.getSettings()['hotkeys'].get('ClearLocks'))
        try:
            AF.setHotkeys('ClearLocks', [[self._KEY_G, self._KEY_L]], False)
            got = self._as_list(AF.getSettings()['hotkeys']['ClearLocks'])
            self.assertEqual(got, [[self._KEY_G, self._KEY_L]])
        finally:
            AF.setHotkeys('ClearLocks', before, False)


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
