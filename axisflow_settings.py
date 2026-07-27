"""
axisflow_settings -- a live settings dialog for AxisFlow.

A sectioned settings editor: sections are listed on the left (horizontal text),
each showing a two-column grid (name / value) whose value editor matches the
setting's type: checkbox for booleans, spin box for numbers, a combo for
enumerations, a colour swatch for colours. Changes apply immediately (no OK
button): every editor calls AF.setSetting(...), which persists and emits the C++
changed() signal, so the compass/panel update on the fly.

Adding a new option later = add one row to the relevant section in _SECTIONS.

Opened by the "AxisFlow settings" command / toolbar button, or:
    import axisflow_settings; axisflow_settings.show()
"""

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtGui, QtWidgets
from PySide.QtCore import QT_TRANSLATE_NOOP

try:
    import AxisFlow as AF
except Exception as exc:      # pragma: no cover
    AF = None
    App.Console.PrintError("axisflow_settings: AxisFlow not available: {0}\n".format(exc))


# Translation: the function MUST be named exactly `translate` (the string
# extractor keys on that name). Strings are shown in FreeCAD's current language;
# see man/TRANSLATING.md. Context "AxisFlow" groups all of the addon's strings.
try:
    translate = App.Qt.translate
except Exception:             # pragma: no cover - very old FreeCAD
    def translate(context, text):
        return QtCore.QCoreApplication.translate(context, text)


def _tr(text):
    return translate("AxisFlow", text)


# --- hotkey actions -------------------------------------------------------
# Canonical action names as reported by getSettings()['hotkeys'], each with a
# label explaining what it does. Order = display order on the hotkey pages.
# Two pages: everyday drawing control, and view/orientation keys.
_CONTROL_ACTIONS = [
    ("ToggleActive",  QT_TRANSLATE_NOOP("AxisFlow", "Turn AxisFlow on/off")),
    ("ToggleMode",    QT_TRANSLATE_NOOP("AxisFlow", "Switch orthogonal / radial")),
    ("SmartLock",     QT_TRANSLATE_NOOP("AxisFlow", "Constrain to the nearest axis")),
    ("LockX",         QT_TRANSLATE_NOOP("AxisFlow", "Freeze the X field")),
    ("LockY",         QT_TRANSLATE_NOOP("AxisFlow", "Freeze the Y field")),
    ("LockZ",         QT_TRANSLATE_NOOP("AxisFlow", "Freeze the Z field")),
    ("LockD",         QT_TRANSLATE_NOOP("AxisFlow", "Freeze the distance (radial mode)")),
    ("SetOrigin",     QT_TRANSLATE_NOOP("AxisFlow", "Move the compass origin to the cursor")),
    ("FinishLine",    QT_TRANSLATE_NOOP("AxisFlow", "Finish the current line")),
    ("CancelCommand", QT_TRANSLATE_NOOP("AxisFlow", "End the command")),
]
_VIEW_ACTIONS = [
    ("RotateTop",     QT_TRANSLATE_NOOP("AxisFlow", "Align the compass with the global axes")),
    ("RotateFront",   QT_TRANSLATE_NOOP("AxisFlow", "Front (compass Z along global -Y)")),
    ("RotateSide",    QT_TRANSLATE_NOOP("AxisFlow", "Side (compass Z along global +X)")),
    ("RotateByView",  QT_TRANSLATE_NOOP("AxisFlow", "Align the compass with the current view")),
    ("RotateCycle",   QT_TRANSLATE_NOOP("AxisFlow", "Cycle Top / Front / Side")),
    ("RotateAboutX",  QT_TRANSLATE_NOOP("AxisFlow", "Turn 90 deg about the compass X")),
    ("RotateAboutY",  QT_TRANSLATE_NOOP("AxisFlow", "Turn 90 deg about the compass Y")),
    ("RotateAboutZ",  QT_TRANSLATE_NOOP("AxisFlow", "Turn 90 deg about the compass Z")),
    ("ToggleSegmentMode",
        QT_TRANSLATE_NOOP("AxisFlow", "Toggle segment: line / arc (line command)")),
]
_ACTIONS = _CONTROL_ACTIONS + _VIEW_ACTIONS
# Bindable through the API / hotkeys.ini but not shown in the dialog (keeps the
# window short); still need labels for the "key already used" prompt.
_HIDDEN_ACTION_LABELS = {
    "ConfirmPoint": QT_TRANSLATE_NOOP("AxisFlow", "Place the current point"),
    "ClearLocks":   QT_TRANSLATE_NOOP("AxisFlow", "Release all locks"),
    "CancelEntry":  QT_TRANSLATE_NOOP("AxisFlow", "Cancel the typed value"),
}


def _key_name(code):
    """16777220 -> 'Return'. PortableText on purpose: these are the names Qt
    also parses, so a name shown here can be typed straight into hotkeys.ini."""
    name = QtGui.QKeySequence(int(code)).toString(QtGui.QKeySequence.PortableText)
    return name if name else "0x%X" % int(code)


def _keys_text(codes):
    """[16777220, 16777221] -> 'Return, Enter'; [[82, 88]] -> 'R X' (a two-key
    chord); [] -> 'not assigned'."""
    if not codes:
        return translate("AxisFlow", "not assigned")
    names = []
    for c in codes:
        if isinstance(c, (list, tuple)):
            names.append(" ".join(_key_name(k) for k in c if k))
        else:
            names.append(_key_name(c))
    return ", ".join(names)


class _KeyButton(QtWidgets.QPushButton):
    """Shows an action's key(s); click, then press a key to rebind it.

    AxisFlow deliberately never claims Ctrl/Alt/Meta combinations (they belong
    to FreeCAD), so those are refused here rather than silently stored as a
    binding that could never fire.
    """

    def __init__(self, action, on_capture, parent=None):
        super(_KeyButton, self).__init__(parent)
        self._action = action
        self._on_capture = on_capture
        self._listening = False
        self._codes = []
        self._chord_first = None      # first key of a chord being captured
        self.setFocusPolicy(QtCore.Qt.StrongFocus)
        self.clicked.connect(self._toggle_listen)

    def set_codes(self, codes):
        self._codes = list(codes or [])
        if not self._listening:
            self.setText(_keys_text(self._codes))

    def _toggle_listen(self):
        if self._listening:
            self._stop()
        else:
            self._listening = True
            self._chord_first = None
            self.setText(translate("AxisFlow", "Press a key (hold Shift for a two-key chord)..."))

    def _stop(self):
        self._listening = False
        self._chord_first = None
        self.setText(_keys_text(getattr(self, "_codes", [])))

    def focusOutEvent(self, event):
        if self._listening:
            self._stop()
        super(_KeyButton, self).focusOutEvent(event)

    def keyPressEvent(self, event):
        if not self._listening:
            super(_KeyButton, self).keyPressEvent(event)
            return

        key = event.key()
        # Ignore bare modifier presses: wait for a real key.
        if key in (QtCore.Qt.Key_Shift, QtCore.Qt.Key_Control,
                   QtCore.Qt.Key_Alt, QtCore.Qt.Key_Meta,
                   QtCore.Qt.Key_AltGr, QtCore.Qt.Key_unknown):
            return

        mods = event.modifiers()
        blocked = (QtCore.Qt.ControlModifier | QtCore.Qt.AltModifier | QtCore.Qt.MetaModifier)
        if mods & blocked:
            QtWidgets.QToolTip.showText(
                self.mapToGlobal(QtCore.QPoint(0, self.height())),
                translate("AxisFlow", "AxisFlow uses plain keys only, without Ctrl / Alt."), self)
            return

        # Second key of a chord: the first was captured with Shift held.
        if self._chord_first is not None:
            first, self._chord_first = self._chord_first, None
            self._listening = False
            self._on_capture(self._action, [first, int(key)])
            return

        # Shift held on the FIRST key means "this opens a chord": keep
        # listening for the key that completes it. Chords are two keys, so the
        # next press always finishes the capture.
        if mods & QtCore.Qt.ShiftModifier:
            self._chord_first = int(key)
            self.setText(translate("AxisFlow", "{key} then...").format(key=_key_name(key)))
            return

        self._listening = False
        self._on_capture(self._action, int(key))


# --- setting descriptors --------------------------------------------------
# Settings are grouped into sections; each section becomes a page, selected
# from the list on the left. A row is (key, label, type, extra):
#   type 'bool'  -> checkbox                       ; extra None
#   type 'int'   -> spin box                       ; extra (min, max, step)
#   type 'float' -> double spin box                ; extra (min, max, step, decimals)
#   type 'enum'  -> combo                          ; extra [labels...]  (index == value)
#   type 'color' -> colour swatch (0xRRGGBBAA)     ; extra None
# Adding a new option later = add one row to the right section below.
_SECTIONS = [
    (QT_TRANSLATE_NOOP("AxisFlow", "Compass"), [
        ("alwaysShowCompass", QT_TRANSLATE_NOOP("AxisFlow", "Always show compass"),   "bool",  None),
        ("compassAnimation",  QT_TRANSLATE_NOOP("AxisFlow", "Animate the compass"),   "bool",  None),
        ("compassSize",       QT_TRANSLATE_NOOP("AxisFlow", "Compass size (px)"),     "int",   (20, 1000, 5)),
        ("axisThickness",     QT_TRANSLATE_NOOP("AxisFlow", "Axis thickness (px)"),   "int",   (1, 20, 1)),
        ("axisStartPct",      QT_TRANSLATE_NOOP("AxisFlow", "Axis start (fraction)"), "float", (0.0, 1.0, 0.05, 2)),
        ("axisLengthPct",     QT_TRANSLATE_NOOP("AxisFlow", "Axis length (fraction)"),"float", (0.0, 1.0, 0.05, 2)),
        ("fillTransparency",  QT_TRANSLATE_NOOP("AxisFlow", "Fill transparency"),     "float", (0.0, 1.0, 0.05, 2)),
        ("axisColorX",        QT_TRANSLATE_NOOP("AxisFlow", "X axis colour"),         "color", None),
        ("axisColorY",        QT_TRANSLATE_NOOP("AxisFlow", "Y axis colour"),         "color", None),
        ("activeFieldColor",  QT_TRANSLATE_NOOP("AxisFlow", "Active field colour"),   "color", None),
    ]),
    (QT_TRANSLATE_NOOP("AxisFlow", "Live Line"), [
        ("liveLineEnabled",   QT_TRANSLATE_NOOP("AxisFlow", "Live line"),             "bool",  None),
        ("liveLineStyle",     QT_TRANSLATE_NOOP("AxisFlow", "Live line style"),       "enum",
            [QT_TRANSLATE_NOOP("AxisFlow", "Solid"), QT_TRANSLATE_NOOP("AxisFlow", "Dashed"),
             QT_TRANSLATE_NOOP("AxisFlow", "Dotted"), QT_TRANSLATE_NOOP("AxisFlow", "Dash-dot")]),
        ("liveLineColor",     QT_TRANSLATE_NOOP("AxisFlow", "Live line colour"),      "color", None),
        ("liveLineTransparency", QT_TRANSLATE_NOOP("AxisFlow", "Live line transparency"), "float", (0.0, 1.0, 0.05, 2)),
        ("compassPathEnabled", QT_TRANSLATE_NOOP("AxisFlow", "Draw path to the point"), "bool", None),
        ("compassPathStyle",  QT_TRANSLATE_NOOP("AxisFlow", "Path style"),            "enum",
            [QT_TRANSLATE_NOOP("AxisFlow", "Solid"), QT_TRANSLATE_NOOP("AxisFlow", "Dashed"),
             QT_TRANSLATE_NOOP("AxisFlow", "Dotted"), QT_TRANSLATE_NOOP("AxisFlow", "Dash-dot")]),
        ("compassPathColor",  QT_TRANSLATE_NOOP("AxisFlow", "Path colour"),           "color", None),
        ("useDistanceIndexing", QT_TRANSLATE_NOOP("AxisFlow", "Use distance indexing"), "bool", None),
        ("useAxesIndexing",     QT_TRANSLATE_NOOP("AxisFlow", "Use axes indexing"),   "bool",  None),
        ("indexingTolerance",   QT_TRANSLATE_NOOP("AxisFlow", "Indexing tolerance (px)"), "int", (0, 50, 1)),
        ("indexingAxisWeight",  QT_TRANSLATE_NOOP("AxisFlow", "Indexing axis weight (px)"), "int", (1, 20, 1)),
        ("indexingAxisTransparency", QT_TRANSLATE_NOOP("AxisFlow", "Indexing axis transparency"), "float", (0.0, 1.0, 0.05, 2)),
    ]),
    (QT_TRANSLATE_NOOP("AxisFlow", "Behaviour"), [
        ("useSnapping", QT_TRANSLATE_NOOP("AxisFlow", "Use object snapping"), "bool", None),
        ("smartNegativeDirection", QT_TRANSLATE_NOOP("AxisFlow", "Smart negative direction"), "bool", None),
        ("focusAdvanceOnMove", QT_TRANSLATE_NOOP("AxisFlow", "Focus advance on movement"), "bool", None),
        ("compassFollowDirection", QT_TRANSLATE_NOOP("AxisFlow", "Compass follows direction"), "enum",
            [QT_TRANSLATE_NOOP("AxisFlow", "Off"), QT_TRANSLATE_NOOP("AxisFlow", "On (keep Z)"),
             QT_TRANSLATE_NOOP("AxisFlow", "On (full)")]),
        ("compassRotateAnimation", QT_TRANSLATE_NOOP("AxisFlow", "Compass animation (s)"), "float", (0.0, 2.0, 0.1, 2)),
    ]),
]


def _unpack(rgba):
    v = int(rgba) & 0xFFFFFFFF
    return (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF


def _pack(r, g, b, a):
    return ((int(r) & 0xFF) << 24) | ((int(g) & 0xFF) << 16) | ((int(b) & 0xFF) << 8) | (int(a) & 0xFF)


class _ColorButton(QtWidgets.QPushButton):
    """A swatch button that opens a colour picker and reports 0xRRGGBBAA."""

    def __init__(self, packed, on_change, parent=None):
        super(_ColorButton, self).__init__(parent)
        self._packed = int(packed) & 0xFFFFFFFF
        self._on_change = on_change
        self.setFixedWidth(90)
        self._refresh()
        self.clicked.connect(self._pick)

    def set_packed(self, packed):
        self._packed = int(packed) & 0xFFFFFFFF
        self._refresh()

    def _refresh(self):
        r, g, b, _a = _unpack(self._packed)
        # readable text colour over the swatch
        ink = "#000000" if (r * 299 + g * 587 + b * 114) / 1000 > 140 else "#ffffff"
        self.setText("%02X%02X%02X" % (r, g, b))
        self.setStyleSheet(
            "background-color: rgb(%d,%d,%d); color: %s; border: 1px solid #888;"
            % (r, g, b, ink))

    def _pick(self):
        r, g, b, a = _unpack(self._packed)
        col = QtWidgets.QColorDialog.getColor(QtGui.QColor(r, g, b), self, translate("AxisFlow", "Pick colour"))
        if col.isValid():
            self._packed = _pack(col.red(), col.green(), col.blue(), a)  # keep alpha
            self._refresh()
            self._on_change(self._packed)


class AxisFlowSettingsDialog(QtWidgets.QDialog):
    """Modeless, live settings editor."""

    def __init__(self, parent=None):
        super(AxisFlowSettingsDialog, self).__init__(parent)
        self.setWindowTitle(translate("AxisFlow", "AxisFlow settings"))
        self.setModal(False)
        self._updating = False
        self._editors = {}          # key -> (type, widget)

        values = AF.getSettings() if AF else {}

        # Section selector on the left (horizontal text), pages on the right.
        # A QListWidget + QStackedWidget gives left-hand tabs whose labels stay
        # horizontal -- a QTabWidget in the "West" position rotates them.
        self._nav = QtWidgets.QListWidget()
        self._nav.setFocusPolicy(QtCore.Qt.NoFocus)
        self._nav.setHorizontalScrollBarPolicy(QtCore.Qt.ScrollBarAlwaysOff)
        self._stack = QtWidgets.QStackedWidget()

        self._updating = True
        for title, rows in _SECTIONS:
            page = QtWidgets.QWidget()
            grid = QtWidgets.QGridLayout(page)
            grid.setColumnStretch(1, 1)
            grid.setHorizontalSpacing(16)
            grid.setVerticalSpacing(6)
            grid.setContentsMargins(12, 12, 12, 12)
            for r, (key, label, typ, extra) in enumerate(rows):
                grid.addWidget(QtWidgets.QLabel(_tr(label)), r, 0)
                widget = self._make_editor(key, typ, extra, values.get(key))
                grid.addWidget(widget, r, 1)
                self._editors[key] = (typ, widget)
            grid.setRowStretch(len(rows), 1)   # keep rows top-aligned

            # Per-section restore: writes back the factory defaults of THIS
            # section only, so tuning the compass never resets your hotkeys or
            # your indexing tolerances.
            restore = QtWidgets.QPushButton(translate("AxisFlow", "Restore defaults"))
            restore.setToolTip(
                translate("AxisFlow", "Restores the default values of this section only"))
            restore.clicked.connect(
                lambda _checked=False, rr=rows: self._reset_section(rr))
            grid.addWidget(restore, len(rows) + 1, 0, 1, 2, QtCore.Qt.AlignLeft)

            self._stack.addWidget(page)
            self._nav.addItem(_tr(title))

        # Hotkey sections. Not grids of typed options, so they get their own
        # pages instead of row lists in _SECTIONS. The buttons of BOTH pages
        # share self._keybuttons, so one _reload_hotkeys() refreshes them all.
        self._keybuttons = {}          # action -> _KeyButton
        self._stack.addWidget(self._build_hotkeys_page(_CONTROL_ACTIONS, with_reset=True))
        self._nav.addItem(translate("AxisFlow", "Control hotkeys"))
        self._stack.addWidget(self._build_hotkeys_page(_VIEW_ACTIONS, with_reset=True))
        self._nav.addItem(translate("AxisFlow", "View hotkeys"))
        self._updating = False

        # Width from the longest item rather than a fixed number of pixels: the
        # labels are translated, and the Russian ones did not fit in the old
        # fixed 104 px.
        self._nav.setFixedWidth(self._nav_width())

        self._nav.currentRowChanged.connect(self._stack.setCurrentIndex)
        self._nav.setCurrentRow(0)

        body = QtWidgets.QHBoxLayout()
        body.setSpacing(10)
        body.addWidget(self._nav)
        body.addWidget(self._stack, 1)

        close = QtWidgets.QPushButton(translate("AxisFlow", "Close"))   # closes only; changes are already applied
        close.clicked.connect(self.close)

        outer = QtWidgets.QVBoxLayout(self)
        outer.addLayout(body)
        outer.addWidget(close)

    # -- hotkeys page ------------------------------------------------------

    def _build_hotkeys_page(self, actions, with_reset):
        page = QtWidgets.QWidget()
        outer = QtWidgets.QVBoxLayout(page)
        outer.setContentsMargins(12, 12, 12, 12)
        outer.setSpacing(8)

        hint = QtWidgets.QLabel(translate("AxisFlow", 
            "Click a key field, then press the key you want. Assigning replaces "
            "that action's current key(s). Keys work while AxisFlow is running."))
        hint.setWordWrap(True)
        outer.addWidget(hint)

        grid = QtWidgets.QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(4)
        grid.setColumnStretch(1, 1)

        for r, (action, label) in enumerate(actions):
            grid.addWidget(QtWidgets.QLabel(_tr(label)), r, 0)

            btn = _KeyButton(action, self._on_key_captured)
            btn.setMinimumWidth(130)
            grid.addWidget(btn, r, 1)
            self._keybuttons[action] = btn

            clear = QtWidgets.QToolButton()
            clear.setText("\u2715")                       # ✕
            clear.setToolTip(translate("AxisFlow", "Unassign"))
            clear.clicked.connect(lambda _=False, a=action: self._clear_hotkey(a))
            grid.addWidget(clear, r, 2)

        outer.addLayout(grid)
        outer.addStretch(1)

        if with_reset:
            reset = QtWidgets.QPushButton(translate("AxisFlow", "Restore default keys"))
            reset.setToolTip(translate("AxisFlow", "Resets the keys of BOTH hotkey pages"))
            reset.clicked.connect(self._reset_hotkeys)
            outer.addWidget(reset, 0, QtCore.Qt.AlignLeft)

        self._reload_hotkeys()
        return page

    def _reload_hotkeys(self):
        """Re-read the live key map into the buttons."""
        if AF is None:
            return
        hk = AF.getSettings().get("hotkeys", {})
        for action, btn in getattr(self, "_keybuttons", {}).items():
            btn.set_codes(hk.get(action, []))

    def _on_key_captured(self, action, code):
        """code is an int (single key) or [first, second] (a two-key chord)."""
        if AF is None:
            return
        hk = AF.getSettings().get("hotkeys", {})
        chord = list(code) if isinstance(code, (list, tuple)) else None
        first = chord[0] if chord else int(code)

        def as_list(v):
            return [list(x) if isinstance(x, (list, tuple)) else x for x in (v or [])]

        # Already this action's only key? Nothing to do.
        if as_list(hk.get(action, [])) == [chord if chord else int(code)]:
            self._reload_hotkeys()
            return

        labels = dict(_ACTIONS)
        labels.update(_HIDDEN_ACTION_LABELS)

        # The single/chord rule: a key is either a binding of its own or the
        # first key of a chord, never both. Refuse early with an explanation --
        # the C++ side would reject it anyway, but silently.
        clash = None
        for other, codes in hk.items():
            if other == action:
                continue
            for c in as_list(codes):
                other_is_chord = isinstance(c, list)
                other_first = c[0] if other_is_chord else c
                if other_first != first:
                    continue
                if bool(chord) != other_is_chord:
                    clash = (other, c)
                    break
            if clash:
                break
        if clash is not None:
            other, c = clash
            QtWidgets.QMessageBox.warning(
                self, translate("AxisFlow", "Key cannot be used"),
                translate("AxisFlow",
                    "{key} is already the {kind} used by \"{action}\".\n\n"
                    "A key is either a binding of its own or the first key of a "
                    "two-key chord, never both.")
                .format(key=_key_name(first),
                        kind=(translate("AxisFlow", "start of a chord") if isinstance(c, list)
                              else translate("AxisFlow", "single key")),
                        action=_tr(labels.get(other, other))))
            self._reload_hotkeys()
            return

        # The map is key -> action, so assigning a binding another action owns
        # takes it away from that action. Say so instead of doing it silently.
        target = chord if chord else int(code)
        owner = None
        for other, codes in hk.items():
            if other != action and target in as_list(codes):
                owner = other
                break
        if owner is not None:
            answer = QtWidgets.QMessageBox.question(
                self, translate("AxisFlow", "Key already used"),
                translate("AxisFlow", "{key} is currently assigned to \"{action}\".\n\n"
                    "Reassign it? That action will be left without a key.")
                .format(key=_keys_text([target]), action=_tr(labels.get(owner, owner))),
                QtWidgets.QMessageBox.Yes | QtWidgets.QMessageBox.No,
                QtWidgets.QMessageBox.No)
            if answer != QtWidgets.QMessageBox.Yes:
                self._reload_hotkeys()
                return

        try:
            AF.setHotkeys(action, [target])
        except Exception as exc:
            # Includes the single/chord rule refused C++-side (a case the check
            # above cannot see, e.g. a binding made through hotkeys.ini).
            QtWidgets.QMessageBox.warning(
                self, translate("AxisFlow", "Key cannot be used"), str(exc))
        self._reload_hotkeys()

    def _clear_hotkey(self, action):
        if AF is None:
            return
        try:
            AF.setHotkeys(action, [])
        except Exception as exc:      # pragma: no cover
            App.Console.PrintError("AxisFlow: could not clear hotkey: {0}\n".format(exc))
        self._reload_hotkeys()

    def _nav_width(self):
        """Widest item plus the frame and a little air, floored at the old 104
        px so a short-labelled language does not give a cramped strip."""
        w = self._nav.sizeHintForColumn(0)
        w += 2 * self._nav.frameWidth() + 12
        return max(104, w)

    def _reset_section(self, rows):
        """Write the factory defaults of one section back, live."""
        if AF is None:
            return
        try:
            defaults = AF.getDefaultSettings()
        except Exception as exc:
            App.Console.PrintError(
                "axisflow_settings: cannot read defaults: {0}\n".format(exc))
            return
        for row in rows:
            key = row[0]
            if key in defaults:
                # _set() is the same path a manual edit takes, so the compass
                # and panel update as each value lands.
                self._set(key, defaults[key])
        self.reload()

    def _reset_hotkeys(self):
        if AF is None:
            return
        try:
            AF.resetHotkeys()
        except Exception as exc:      # pragma: no cover
            App.Console.PrintError("AxisFlow: could not reset hotkeys: {0}\n".format(exc))
        self._reload_hotkeys()

    # -- editor construction ----------------------------------------------
    def _make_editor(self, key, typ, extra, value):
        if typ == "bool":
            w = QtWidgets.QCheckBox()
            w.setChecked(bool(value))
            w.toggled.connect(lambda v, k=key: self._set(k, bool(v)))
            return w
        if typ == "int":
            lo, hi, step = extra
            w = QtWidgets.QSpinBox()
            w.setRange(lo, hi)
            w.setSingleStep(step)
            w.setValue(int(value if value is not None else lo))
            w.valueChanged.connect(lambda v, k=key: self._set(k, int(v)))
            return w
        if typ == "float":
            lo, hi, step, dec = extra
            w = QtWidgets.QDoubleSpinBox()
            w.setRange(lo, hi)
            w.setSingleStep(step)
            w.setDecimals(dec)
            w.setValue(float(value if value is not None else lo))
            w.valueChanged.connect(lambda v, k=key: self._set(k, float(v)))
            return w
        if typ == "enum":
            w = QtWidgets.QComboBox()
            w.addItems([_tr(it) for it in extra])
            idx = int(value) if value is not None else 0
            w.setCurrentIndex(max(0, min(idx, len(extra) - 1)))
            w.currentIndexChanged.connect(lambda i, k=key: self._set(k, int(i)))
            return w
        if typ == "color":
            return _ColorButton(int(value) if value is not None else 0,
                                lambda packed, k=key: self._set(k, int(packed)))
        # unknown type: read-only display
        return QtWidgets.QLabel(str(value))

    # -- live apply / refresh ---------------------------------------------
    def _set(self, key, value):
        if self._updating or AF is None:
            return
        try:
            AF.setSetting(key, value)
        except Exception as exc:
            App.Console.PrintError("axisflow_settings: {0} = {1}: {2}\n".format(key, value, exc))

    def reload(self):
        """Re-read current values (e.g. after they changed elsewhere)."""
        if AF is None:
            return
        values = AF.getSettings()
        self._updating = True
        try:
            for key, (typ, w) in self._editors.items():
                if key not in values:
                    continue
                v = values[key]
                if typ == "bool":
                    w.setChecked(bool(v))
                elif typ == "int":
                    w.setValue(int(v))
                elif typ == "float":
                    w.setValue(float(v))
                elif typ == "enum":
                    w.setCurrentIndex(int(v))
                elif typ == "color":
                    w.set_packed(int(v))
        finally:
            self._updating = False
        self._reload_hotkeys()


_dialog = None


def show():
    """Open (or raise) the settings dialog."""
    global _dialog
    if AF is None:
        App.Console.PrintError("axisflow_settings: AxisFlow not loaded\n")
        return None
    if _dialog is None:
        _dialog = AxisFlowSettingsDialog(Gui.getMainWindow())
    else:
        _dialog.reload()          # reflect any external changes since last open
    _dialog.show()
    _dialog.raise_()
    _dialog.activateWindow()
    return _dialog
