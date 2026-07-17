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

try:
    import AxisFlow as AF
except Exception as exc:      # pragma: no cover
    AF = None
    App.Console.PrintError("axisflow_settings: AxisFlow not available: {0}\n".format(exc))


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
    ("Compass", [
        ("alwaysShowCompass", "Always show compass",     "bool",  None),
        ("compassSize",       "Compass size (px)",       "int",   (20, 1000, 5)),
        ("axisThickness",     "Axis thickness (px)",     "int",   (1, 20, 1)),
        ("axisStartPct",      "Axis start (fraction)",   "float", (0.0, 1.0, 0.05, 2)),
        ("axisLengthPct",     "Axis length (fraction)",  "float", (0.0, 1.0, 0.05, 2)),
        ("fillTransparency",  "Fill transparency",       "float", (0.0, 1.0, 0.05, 2)),
        ("axisColorX",        "X axis colour",           "color", None),
        ("axisColorY",        "Y axis colour",           "color", None),
        ("activeFieldColor",  "Active field colour",     "color", None),
    ]),
    ("Live Line", [
        ("liveLineEnabled",   "Live line",               "bool",  None),
        ("liveLineStyle",     "Live line style",         "enum",  ["Solid", "Dashed", "Dotted", "Dash-dot"]),
        ("liveLineColor",     "Live line colour",        "color", None),
        ("liveLineTransparency", "Live line transparency", "float", (0.0, 1.0, 0.05, 2)),
        ("useDistanceIndexing", "Use distance indexing", "bool",  None),
        ("useAxesIndexing",     "Use axes indexing",     "bool",  None),
        ("indexingTolerance",   "Indexing tolerance (px)", "int", (0, 50, 1)),
        ("indexingAxisWeight",  "Indexing axis weight (px)", "int", (1, 20, 1)),
        ("indexingAxisTransparency", "Indexing axis transparency", "float", (0.0, 1.0, 0.05, 2)),
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
        col = QtWidgets.QColorDialog.getColor(QtGui.QColor(r, g, b), self, "Pick colour")
        if col.isValid():
            self._packed = _pack(col.red(), col.green(), col.blue(), a)  # keep alpha
            self._refresh()
            self._on_change(self._packed)


class AxisFlowSettingsDialog(QtWidgets.QDialog):
    """Modeless, live settings editor."""

    def __init__(self, parent=None):
        super(AxisFlowSettingsDialog, self).__init__(parent)
        self.setWindowTitle("AxisFlow settings")
        self.setModal(False)
        self._updating = False
        self._editors = {}          # key -> (type, widget)

        values = AF.getSettings() if AF else {}

        # Section selector on the left (horizontal text), pages on the right.
        # A QListWidget + QStackedWidget gives left-hand tabs whose labels stay
        # horizontal -- a QTabWidget in the "West" position rotates them.
        self._nav = QtWidgets.QListWidget()
        self._nav.setFixedWidth(104)
        self._nav.setFocusPolicy(QtCore.Qt.NoFocus)
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
                grid.addWidget(QtWidgets.QLabel(label), r, 0)
                widget = self._make_editor(key, typ, extra, values.get(key))
                grid.addWidget(widget, r, 1)
                self._editors[key] = (typ, widget)
            grid.setRowStretch(len(rows), 1)   # keep rows top-aligned
            self._stack.addWidget(page)
            self._nav.addItem(title)
        self._updating = False

        self._nav.currentRowChanged.connect(self._stack.setCurrentIndex)
        self._nav.setCurrentRow(0)

        body = QtWidgets.QHBoxLayout()
        body.setSpacing(10)
        body.addWidget(self._nav)
        body.addWidget(self._stack, 1)

        close = QtWidgets.QPushButton("Close")   # closes only; changes are already applied
        close.clicked.connect(self.close)

        outer = QtWidgets.QVBoxLayout(self)
        outer.addLayout(body)
        outer.addWidget(close)

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
            w.addItems(extra)
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
