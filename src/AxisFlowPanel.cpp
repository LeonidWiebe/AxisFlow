#include "PreCompiled.h"

#include "AxisFlowPanel.h"
#include "AxisFlowService.h"
#include "AxisFlowSettings.h"
#include "AxisFlowHotkeys.h"
#include "AxisFlowEntry.h"

#include <Gui/MainWindow.h>

#include <Base/Quantity.h>
#include <Base/Exception.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QRegularExpression>
#include <QtCore/QSignalBlocker>
#include <QtGui/QRegularExpressionValidator>
#include <QtGui/QCloseEvent>
#include <QtGui/QFont>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QStyle>

#include <cmath>
#include <limits>

namespace AxisFlowGui {

static constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// Format a coordinate value. A value that rounds to zero at this precision is
// pinned to +0 so the read-out shows a clean "0.000" instead of flickering
// between "0.000" and "-0.000" as a tiny offset crosses zero.
static QString fmtCoord(double v, int decimals)
{
    if (std::abs(v) < 0.5 * std::pow(10.0, -decimals))
        v = 0.0;
    return QString::number(v, 'f', decimals);
}

AxisFlowPanel* AxisFlowPanel::instance()
{
    static AxisFlowPanel* inst = nullptr;
    if (!inst)
        inst = new AxisFlowPanel(Gui::getMainWindow());
    return inst;
}

AxisFlowPanel::AxisFlowPanel(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    buildUi();
    wireToService();
    setWindowTitle(QLatin1String("AxisFlow"));
    setAttribute(Qt::WA_ShowWithoutActivating);
}

void AxisFlowPanel::buildUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(4);

    // ---- Title bar: name + power (off) button ----
    auto* titleRow = new QHBoxLayout;
    _title = new QLabel(QStringLiteral("AxisFlow"));
    QFont tf = _title->font();
    tf.setBold(true);
    _title->setFont(tf);
    _title->installEventFilter(this);   // drag the frameless window by its title

    _powerBtn = new QToolButton;
    _powerBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    _powerBtn->setToolTip(QLatin1String("Turn AxisFlow off"));
    _powerBtn->setAutoRaise(true);
    _powerBtn->setCursor(Qt::PointingHandCursor);

    titleRow->addWidget(_title);
    titleRow->addStretch(1);
    titleRow->addWidget(_powerBtn);
    mainLayout->addLayout(titleRow);

    auto* rowA = new QHBoxLayout;
    _lockA  = new QCheckBox;  _lockA->setToolTip(QLatin1String("Lock this axis"));
    _labelA = new QLabel(QStringLiteral("X"));
    _editA  = new QLineEdit;  _editA->setPlaceholderText(QLatin1String("0.000"));
    rowA->addWidget(_lockA); rowA->addWidget(_labelA); rowA->addWidget(_editA);

    auto* rowB = new QHBoxLayout;
    _lockB  = new QCheckBox;  _lockB->setToolTip(QLatin1String("Lock this axis"));
    _labelB = new QLabel(QStringLiteral("Y"));
    _editB  = new QLineEdit;  _editB->setPlaceholderText(QLatin1String("0.000"));
    rowB->addWidget(_lockB); rowB->addWidget(_labelB); rowB->addWidget(_editB);

    auto* rowC = new QHBoxLayout;
    _lockC  = new QCheckBox;  _lockC->setToolTip(QLatin1String("Lock this axis"));
    _labelC = new QLabel(QStringLiteral("Z"));
    _editC  = new QLineEdit;  _editC->setPlaceholderText(QLatin1String("0.000"));
    rowC->addWidget(_lockC); rowC->addWidget(_labelC); rowC->addWidget(_editC);

    _frameBtn = new QPushButton(QStringLiteral("Local"));
    _frameBtn->setToolTip(QLatin1String("Coordinates: Local (compass) or World"));

    // AxisFlow owns the input in its own tools, so the fields are editable.
    // Digits, separators and a sign only: letters are reserved for the action
    // keys (X/Y/Z lock), and the validator also blocks them arriving by paste.
    auto* numeric = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9.,+\\-]*")), this);

    for (QLineEdit* e : {_editA, _editB, _editC}) {
        e->setPlaceholderText(QString(QChar(0x2014)));   // em dash, encoding-safe
        e->setValidator(numeric);
    }

    mainLayout->addLayout(rowA);
    mainLayout->addLayout(rowB);
    mainLayout->addLayout(rowC);
    mainLayout->addWidget(_frameBtn);
}

void AxisFlowPanel::wireToService()
{
    AxisFlowService& svc = AxisFlowService::instance();

    // React to service state.
    connect(&svc, &AxisFlowService::stateChanged, this, &AxisFlowPanel::onServiceState);
    connect(&svc, &AxisFlowService::cursorMoved,  this, &AxisFlowPanel::onCursorMoved);

    // Power button -> turn AxisFlow off (deactivate). The session stays alive;
    // the global ToggleActive key can bring it back. Full teardown is done by
    // the Python adapter's uninstall().
    connect(_powerBtn, &QToolButton::clicked, this,
            [](bool){ AxisFlowService::instance().deactivate(); });

    // Coordinate-frame button -> toggle Local <-> World readout. Mode
    // (rect/polar) stays on Space; the field labels already show it.
    connect(_frameBtn, &QPushButton::clicked, this, [this](bool){
        _worldFrame = !_worldFrame;
        updateFrameButton();
        // Tell the service which frame we are in: indexing runs only in Local.
        // setLocalFrame() re-solves and redraws the fields in the new frame.
        AxisFlowService::instance().setLocalFrame(!_worldFrame);
    });

    // Lock checkboxes -> service. 'clicked' fires only on user interaction, so
    // setLockState()'s programmatic setChecked() won't feed back here.
    // Checked = the field is FROZEN at its current value; unchecked = it
    // follows the cursor again. In the Draft adapter the boxes only display
    // Draft's own restriction, so a click there just re-syncs the display.
    auto toggleLock = [this](int field){
        AxisFlowService& s = AxisFlowService::instance();
        if (!s.constrainEnabled()) { setLockState(); return; }   // display-only
        s.toggleFieldLock(field);
    };
    connect(_lockA, &QCheckBox::clicked, this, [toggleLock](bool){ toggleLock(0); });
    connect(_lockB, &QCheckBox::clicked, this, [toggleLock](bool){ toggleLock(1); });
    connect(_lockC, &QCheckBox::clicked, this, [toggleLock](bool){ toggleLock(2); });

    // Typed values become live keyboard overrides on the solver.
    auto onEdited = [this](int idx) {
        return [this, idx](const QString& t) {
            _entryField = t.isEmpty() ? -1 : idx;
            pushFieldValue(idx, t);
        };
    };
    connect(_editA, &QLineEdit::textEdited, this, onEdited(0));
    connect(_editB, &QLineEdit::textEdited, this, onEdited(1));
    connect(_editC, &QLineEdit::textEdited, this, onEdited(2));

    // Bound letters (X/Y/Z/Space/...) must act even while a field has focus,
    // otherwise they would just be typed into the number.
    _editA->installEventFilter(this);
    _editB->installEventFilter(this);
    _editC->installEventFilter(this);

    // The highlight colour is a setting, so follow it live.
    connect(&AxisFlowSettings::instance(), &AxisFlowSettings::changed,
            this, [this]{ refreshActiveHighlight(); });

    // A confirmed point clears overrides in the solver; drop our entry state too
    // so the fields follow the cursor again and the next digit starts fresh.
    connect(&AxisFlowService::instance(), &AxisFlowService::pointConfirmed,
            this, [this](Base::Vector3d){
                _entryField = -1;
                _advanceArmed = false;
                _advanceHold = false;
                focusActiveField();
            });

    // Initial sync.
    updateFrameButton();
    AxisFlowService::instance().setLocalFrame(!_worldFrame);   // default: Local
    refreshActiveHighlight();
    onServiceState();
}

// ---------------------------------------------------------------------------

void AxisFlowPanel::setValues(double x, double y, double z)
{
    const AxisFlowService& s = AxisFlowService::instance();
    auto held = [&](int i) {
        // Protect a field only while it is genuinely being typed into or carries
        // an override. NOT on hasFocus(): the caret now lives in the active
        // field at all times, so focus no longer means "the user is typing" --
        // gating on it would freeze the live read-out under the blinking caret.
        return s.hasKeyValue(i) || i == _entryField;
    };
    if (!held(0)) _editA->setText(fmtCoord(x, 4));
    if (!held(1)) _editB->setText(fmtCoord(y, 4));
    if (!held(2)) _editC->setText(fmtCoord(z, 4));
}

void AxisFlowPanel::setValuesDist(double dist, double angle, double z)
{
    const AxisFlowService& s = AxisFlowService::instance();
    auto held = [&](int i) {
        return s.hasKeyValue(i) || i == _entryField;
    };
    if (!held(0)) _editA->setText(fmtCoord(dist, 4));
    if (!held(1)) _editB->setText(fmtCoord(angle, 2));
    if (!held(2)) _editC->setText(fmtCoord(z, 4));
}

void AxisFlowPanel::setMode(bool polar)
{
    if (polar == _polar) return;   // avoid needless relayout on every update
    _polar = polar;
    // Both modes have three fields; only the in-plane labels change.
    // Polar keeps the Z row: distance/angle live in the plane, Z is the height
    // above it (and with an object snap, the snapped point's real height).
    if (polar) {
        _labelA->setText(QLatin1String("D"));
        _labelB->setText(QLatin1String("A"));
    } else {
        _labelA->setText(QLatin1String("X"));
        _labelB->setText(QLatin1String("Y"));
    }
    setActiveField(_activeField);
    refreshActiveHighlight();
    adjustSize();
}

void AxisFlowPanel::updateFrameButton()
{
    if (_frameBtn)
        _frameBtn->setText(_worldFrame ? tr("World")
                                       : tr("Local"));
}

void AxisFlowPanel::setLockState()
{
    // Checked = the field is FIXED: a lock (checkbox/hotkey/SmartLock) and a
    // typed value both pin the coordinate, exactly as in AccuDraw.
    const AxisFlowService& s = AxisFlowService::instance();
    QCheckBox* boxes[3] = { _lockA, _lockB, _lockC };
    bool on[3];
    if (!s.constrainEnabled()) {
        // Draft adapter: mirror Draft's restriction (Snapper.mask). Draft's
        // lock means "movement ALONG that axis", i.e. the other two coordinates
        // are the frozen ones -- so those get the checkmarks.
        const AxisLock dl = s.displayLock();
        on[0] = (dl == AxisLock::Y || dl == AxisLock::Z);
        on[1] = (dl == AxisLock::X || dl == AxisLock::Z);
        on[2] = (dl == AxisLock::X || dl == AxisLock::Y);
    } else {
        for (int i = 0; i < 3; ++i)
            on[i] = s.hasKeyValue(i);
    }
    for (int i = 0; i < 3; ++i) {
        QSignalBlocker block(boxes[i]);   // programmatic change must not re-enter
        boxes[i]->setChecked(on[i]);
    }
}

void AxisFlowPanel::focusField(int index)
{
    switch (index) {
    case 0: _editA->setFocus(); _editA->selectAll(); break;
    case 1: _editB->setFocus(); _editB->selectAll(); break;
    case 2: _editC->setFocus(); _editC->selectAll(); break;
    }
}

double AxisFlowPanel::parseQuantity(const QString& text)
{
    QString t = text.trimmed();
    if (t.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();

    // The field allows a comma as decimal separator; the parser wants a dot.
    t.replace(QLatin1Char(','), QLatin1Char('.'));

    // Base::Quantity is kept (rather than toDouble) so a unit suffix keeps
    // working if letters are ever allowed back into the field.
    try {
        Base::Quantity q = Base::Quantity::parse(t.toStdString());
        return q.getValue();
    }
    catch (const Base::Exception&) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    catch (...) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

void AxisFlowPanel::pushFieldValue(int index, const QString& text)
{
    AxisFlowService& s = AxisFlowService::instance();

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        // Clearing a field releases ONLY its own override; the others stay put.
        _advanceArmed = false;             // entry abandoned
        s.clearKeyValue(index);
        return;
    }

    double v = parseQuantity(text);
    if (std::isnan(v))
        return;   // partial/invalid entry ("-", "1,") -- keep the last override

    // Focus advance on movement: every accepted keystroke into an in-plane
    // field re-arms the trigger and re-anchors it at the CURRENT cursor, so
    // only movement after the last keystroke counts as "done typing".
    if (index == 0 || index == 1) {
        const Base::Vector3d l = s.localOffset();
        _advanceArmed  = AxisFlowSettings::instance().focusAdvanceOnMove();
        _advanceAnchorX = l.x;
        _advanceAnchorY = l.y;
    }

    const bool rect = (s.mode() == CompassMode::Rectangular);

    if (_worldFrame && rect) {
        // World frame is absolute entry: the sign is whatever the user typed.
        // Convert the world coordinate along the axis to a local origin offset.
        const Base::Vector3d o = s.origin();
        const double proj = (index == 0) ? o.Dot(s.planeU())
                          : (index == 1) ? o.Dot(s.planeV())
                                         : o.Dot(s.planeNormal());
        s.setKeyValue(index, v - proj);
        return;
    }

    // Local rectangular entry, AccuDraw-style: a bare magnitude takes the sign
    // of the cursor's current direction along this axis, so pointing down -X and
    // typing "5" locks X = -5 (and the live line follows). An explicit sign the
    // user typed always wins; polar distance/angle are entered as-is. The rule
    // itself lives in AxisFlowEntry.h so it can be unit-tested without the GUI.
    // Optional since the "Smart negative direction" setting: when it is off,
    // the typed sign is used literally, whichever way the cursor points.
    if (rect && AxisFlowSettings::instance().smartNegativeDirection()) {
        const bool explicitSign = trimmed.startsWith(QLatin1Char('-'))
                               || trimmed.startsWith(QLatin1Char('+'));
        const Base::Vector3d l = s.localOffset();   // raw cursor direction
        const double dir = (index == 0) ? l.x : (index == 1) ? l.y : l.z;
        v = signedByDirection(v, dir, explicitSign);
    }
    s.setKeyValue(index, v);
    // An unparsable partial entry is simply ignored until it becomes valid;
    // nothing is reset under the user's fingers.
}

int AxisFlowPanel::fieldDecimals(int index) const
{
    // Match the live read-out: angle (polar field 1) is 2 decimals, else 4.
    if (_polar && index == 1)
        return 2;
    return 4;
}

void AxisFlowPanel::reformatField(int index)
{
    // Normalise a committed manual entry when the field loses focus. We display
    // the SIGNED override value (frame-transformed), so a magnitude typed while
    // pointing down an axis picks up its direction sign here: "5" -> "-5.0000".
    // Units are already normalised (the override is in base units). A plain
    // read-out field (no override) is left to setValues().
    AxisFlowService& s = AxisFlowService::instance();
    if (!s.hasKeyValue(index))
        return;
    QLineEdit* edits[3] = { _editA, _editB, _editC };
    QLineEdit* edit = (index >= 0 && index < 3) ? edits[index] : nullptr;
    if (!edit)
        return;
    double disp = s.keyValue(index);          // signed local override
    if (std::isnan(disp))
        return;
    if (_worldFrame && s.mode() == CompassMode::Rectangular) {
        const Base::Vector3d o = s.origin();
        const double proj = (index == 0) ? o.Dot(s.planeU())
                          : (index == 1) ? o.Dot(s.planeV())
                                         : o.Dot(s.planeNormal());
        disp += proj;                         // local -> world for display
    }
    const QString formatted = fmtCoord(disp, fieldDecimals(index));
    if (formatted != edit->text())
        edit->setText(formatted);   // programmatic: no textEdited, no re-push
}

void AxisFlowPanel::setActiveField(int index)
{
    const int maxField = 2;                // D/A/Z or X/Y/Z -- always three
    if (index < 0) index = 0;
    if (index > maxField) index = maxField;
    if (index == _activeField) return;

    _activeField = index;
    refreshActiveHighlight();
    focusActiveField();     // the caret follows the active field
}

void AxisFlowPanel::focusActiveField()
{
    // Only when AxisFlow owns the input. In the Draft adapter, grabbing focus
    // here -- which happens on every cursor move via setActiveField() -- would
    // yank the caret out of Draft's coordinate field mid-typing.
    if (!_ownsInput)
        return;

    QLineEdit* edits[3] = { _editA, _editB, _editC };
    QLineEdit* target = edits[_activeField];
    if (!target || !target->isVisible())
        return;

    // A widget in an inactive window shows no caret and receives no keys, so the
    // panel window has to be the active one while AxisFlow is taking input.
    if (!isActiveWindow()) {
        raise();
        activateWindow();
    }
    if (!target->hasFocus())
        target->setFocus(Qt::OtherFocusReason);
}

void AxisFlowPanel::cycleActiveField()
{
    _advanceHold = false;              // an explicit Tab overrides the hold
    setActiveField((_activeField + 1) % 3);
}

void AxisFlowPanel::maybeAdvanceFocus()
{
    if (!_advanceArmed)
        return;
    // The entry may have ended some other way (Enter, Tab, blur, Escape) --
    // then there is nothing to hand over; drop the stale trigger.
    if (_entryField != 0 && _entryField != 1) {
        _advanceArmed = false;
        return;
    }

    AxisFlowService& s = AxisFlowService::instance();
    const Base::Vector3d l = s.localOffset();
    if (!focusAdvanceExceeded(l.x - _advanceAnchorX, l.y - _advanceAnchorY,
                              s.viewScale()))
        return;   // jitter, not a deliberate move

    // The typed value stays as the override; the entry itself is finished
    // (round-on-blur formatting included) and the caret moves to the adjacent
    // in-plane field: X <-> Y (D <-> A in radial mode).
    const int from = _entryField;
    const int to   = 1 - from;
    _entryField    = -1;
    _advanceArmed  = false;
    _advanceHold   = true;   // keep the caret in 'to' despite the nearest axis
    reformatField(from);
    setActiveField(to);
    focusActiveField();
}

void AxisFlowPanel::updateActiveFieldFromCursor()
{
    // Frozen while the user is typing: the entry must not jump to another field
    // because the cursor drifted past the diagonal.
    if (_entryField >= 0)
        return;

    // Frozen after a focus advance too: the hand-over said "now type Y" -- the
    // cursor still hugging the X axis must not snatch the caret back.
    if (_advanceHold)
        return;

    // Polar has no meaningful "nearest axis": distance stays the default target.
    if (_polar)
        return;

    switch (AxisFlowService::instance().nearestAxis()) {
    case AxisLock::X: setActiveField(0); break;
    case AxisLock::Y: setActiveField(1); break;
    case AxisLock::Z: setActiveField(2); break;
    default: break;
    }
}

void AxisFlowPanel::refreshActiveHighlight()
{
    const unsigned long rgba = AxisFlowSettings::instance().activeFieldColor();
    float r, g, b, a;
    AxisFlowSettings::unpackColor(rgba, r, g, b, a);

    const QString css = QStringLiteral("QLineEdit { background-color: rgba(%1,%2,%3,%4); }")
        .arg(int(r * 255.0f)).arg(int(g * 255.0f))
        .arg(int(b * 255.0f)).arg(QString::number(a, 'f', 3));

    QLineEdit* edits[3] = { _editA, _editB, _editC };
    for (int i = 0; i < 3; ++i)
        edits[i]->setStyleSheet(i == _activeField ? css : QString());
}

QLineEdit* AxisFlowPanel::entryEdit()
{
    switch (_entryField) {
    case 1:  return _editB;
    case 2:  return _editC;
    default: return _editA;   // X (ortho) or Distance (radial)
    }
}

bool AxisFlowPanel::startTyping(QChar ch)
{
    // The first character lands in whichever field is active; from then on the
    // entry (and therefore the active field) is frozen.
    // The digit always targets the ACTIVE field. After Tab, _entryField still
    // points at the previously typed field, so keying off it would type into the
    // wrong field / append; _activeField is the field the user actually selected.
    const int idx = _activeField;
    QLineEdit* edits[3] = { _editA, _editB, _editC };
    QLineEdit* target = edits[idx];
    if (!target || !target->isVisible())
        return false;

    // Continue an entry already in progress instead of restarting it: the 3-D
    // view can take focus back between keystrokes, so the next digit arrives
    // here rather than in the field -- without this, "1500" would become "0".
    const bool continuing = (_entryField == idx) && !target->text().isEmpty();
    const QString text = continuing ? (target->text() + ch) : QString(ch);

    _entryField = idx;

    // A widget inside an INACTIVE window gets no keyboard focus: no caret, and
    // Backspace never reaches it. Activate the panel so the entry behaves like a
    // normal text field from the second character on.
    if (!target->hasFocus()) {
        window()->raise();
        window()->activateWindow();
        target->setFocus(Qt::OtherFocusReason);
    }

    target->setText(text);
    target->setCursorPosition(int(text.length()));
    pushFieldValue(idx, text);
    return true;
}

bool AxisFlowPanel::backspaceEntry()
{
    if (_entryField < 0) return false;

    const int idx = _entryField;          // capture before the entry may end
    QLineEdit* target = entryEdit();
    QString text = target->text();
    if (text.isEmpty()) { _entryField = -1; return false; }

    text.chop(1);
    target->setText(text);
    target->setCursorPosition(int(text.length()));

    if (text.isEmpty())
        _entryField = -1;          // entry abandoned: the read-out takes over
    pushFieldValue(idx, text);     // empty text releases that field's override
    return true;
}

bool AxisFlowPanel::clearEntry()
{
    if (_entryField < 0) return false;
    const int idx = _entryField;
    entryEdit()->clear();
    _entryField = -1;
    pushFieldValue(idx, QString());
    return true;
}

void AxisFlowPanel::cancelInput()
{
    _advanceArmed = false;
    _advanceHold  = false;
    AxisFlowService& s = AxisFlowService::instance();

    // Layer 1: an entry is being typed -> drop it and release its override.
    if (_entryField >= 0) {
        const int idx = _entryField;
        entryEdit()->clear();
        _entryField = -1;
        s.clearKeyValue(idx);          // read-out takes the field back
        focusActiveField();
        return;
    }

    // Layer 1b: a value was committed but nothing is mid-typing -> drop values.
    if (s.hasKeyValue(0) || s.hasKeyValue(1) || s.hasKeyValue(2)) {
        s.clearKeyValues();
        focusActiveField();
        return;
    }

    // Layer 2: nothing typed -> release the axis lock.
    s.clearLocks();
    focusActiveField();
}

bool AxisFlowPanel::isEntryStartKey(const QKeyEvent* ke)
{
    const QString t = ke->text();
    if (t.isEmpty()) return false;
    const QChar c = t.at(0);
    return c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char(',')
                       || c == QLatin1Char('-') || c == QLatin1Char('+');
}

bool AxisFlowPanel::isValueKey(const QKeyEvent* ke)
{
    switch (ke->key()) {
    case Qt::Key_0: case Qt::Key_1: case Qt::Key_2: case Qt::Key_3:
    case Qt::Key_4: case Qt::Key_5: case Qt::Key_6: case Qt::Key_7:
    case Qt::Key_8: case Qt::Key_9:
    case Qt::Key_Period: case Qt::Key_Comma:
    case Qt::Key_Minus:  case Qt::Key_Plus:
    case Qt::Key_Backspace: case Qt::Key_Delete:
    case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Home: case Qt::Key_End:
        return true;
    default:
        // Letters are unit suffixes ("m", "ft", "deg") when they follow digits;
        // on an empty field they are free to act as action keys.
        return false;
    }
}

bool AxisFlowPanel::eventFilter(QObject* obj, QEvent* ev)
{
    // Drag the frameless window by its title label.
    if (obj == _title) {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() == Qt::LeftButton) {
                _dragging   = true;
                _dragOffset = me->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        }
        else if (ev->type() == QEvent::MouseMove && _dragging) {
            auto* me = static_cast<QMouseEvent*>(ev);
            move(me->globalPosition().toPoint() - _dragOffset);
            return true;
        }
        else if (ev->type() == QEvent::MouseButtonRelease) {
            _dragging = false;
        }
        return QWidget::eventFilter(obj, ev);
    }

    // Round a committed manual entry to the standard precision when its field
    // loses focus (Tab, click away, window deactivation): "5" -> "5.0000".
    if (ev->type() == QEvent::FocusOut &&
        (obj == _editA || obj == _editB || obj == _editC)) {
        const int idx = (obj == _editB) ? 1 : (obj == _editC ? 2 : 0);
        reformatField(idx);
        return QWidget::eventFilter(obj, ev);
    }

    // Action keys must fire even while a value field has focus.
    if (ev->type() == QEvent::KeyPress &&
        (obj == _editA || obj == _editB || obj == _editC)) {
        auto* ke = static_cast<QKeyEvent*>(ev);

        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            cycleActiveField();
            return true;   // never let Qt walk the focus chain
        }

        // The caret lives in the active field, which shows the live read-out.
        // The FIRST typed character must replace that read-out, not be appended.
        // "Fresh" means the digit lands in a DIFFERENT field than the one being
        // typed -- including after Tab, where _entryField still points at the
        // previous field (that was the bug: the new field kept its dynamic value
        // and the digit appended, e.g. Z became "0.00005").
        if (isEntryStartKey(ke)) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(obj);
            const int idx = (edit == _editB) ? 1 : (edit == _editC ? 2 : 0);
            if (_entryField != idx) {
                _entryField = idx;
                edit->clear();               // replace the read-out
            }
            return QWidget::eventFilter(obj, ev);   // let QLineEdit insert the char
        }

        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            // Enter commits the point (whatever the keymap binds to it), rather
            // than being eaten by the line edit. Typed values are already live
            // overrides on the solver, so the solved point includes them.
            AxisFlowService& sv = AxisFlowService::instance();
            sv.performAction(sv.actionForKey(ke->key()));
            return true;
        }
        if (isValueKey(ke))
            return QWidget::eventFilter(obj, ev);

        // Bound keys always act, even mid-entry: after typing "1500" the X key
        // must still lock the X axis. Letters cannot be typed into the field
        // anyway (validator), so there is no ambiguity with unit suffixes.
        AxisFlowService& s = AxisFlowService::instance();
        AxisAction a = s.actionForKey(ke->key());
        if (a == AxisAction::CancelEntry) {
            cancelInput();
            return true;
        }
        if (a != AxisAction::None) {
            s.performAction(a);
            return true;
        }

        // Any other letter is meaningless in a coordinate: swallow it rather
        // than let it flash in the field and be rejected.
        const QString kt = ke->text();
        if (!kt.isEmpty() && kt.at(0).isLetter())
            return true;
    }

    return QWidget::eventFilter(obj, ev);
}

void AxisFlowPanel::closeEvent(QCloseEvent* e)
{
    // Closing the window turns AxisFlow off.
    AxisFlowService::instance().deactivate();
    e->accept();
}

// ---------------------------------------------------------------------------

void AxisFlowPanel::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Tab) {
        cycleActiveField();
        return;
    }

    AxisFlowService& s = AxisFlowService::instance();
    AxisAction a = s.actionForKey(e->key());
    if (a == AxisAction::CancelEntry) { cancelInput(); return; }
    if (a != AxisAction::None) {
        s.performAction(a);
        return;
    }

    QWidget::keyPressEvent(e);
}

// ---------------------------------------------------------------------------

void AxisFlowPanel::onServiceState()
{
    AxisFlowService& s = AxisFlowService::instance();
    setMode(s.mode() == CompassMode::Polar);
    setLockState();

    // A field frozen by a lock must immediately SHOW the captured value
    // (SmartLock freezes Y/Z at 0 while the read-out still shows the cursor).
    // The field being typed into is left alone -- reformat would rewrite the
    // entry under the user's fingers.
    for (int i = 0; i < 3; ++i)
        if (i != _entryField && s.hasKeyValue(i))
            reformatField(i);

    // When every override is gone (Escape), let the read-out take the fields
    // back: while an edit keeps focus, setValues() deliberately will not touch
    // it, so the stale typed text would otherwise linger.
    if (!s.hasKeyValue(0) && !s.hasKeyValue(1) && !s.hasKeyValue(2)) {
        _entryField = -1;
        // Never drop the caret: a panel with no focused widget sends the next
        // digit straight into FreeCAD's global shortcuts.
        focusActiveField();
    }
}

void AxisFlowPanel::onCursorMoved(Base::Vector3d /*solved*/)
{
    // Focus advance on movement runs first: it may just have FINISHED the
    // entry, in which case the nearest-axis logic below is free again.
    maybeAdvanceFocus();

    // The active field follows the axis the cursor leans towards (frozen while
    // typing -- see updateActiveFieldFromCursor).
    updateActiveFieldFromCursor();

    AxisFlowService& s = AxisFlowService::instance();

    // While a candidate is active, the point that will actually be placed is the
    // SNAPPED one, so mirror it in the fields instead of the raw cursor:
    //   candidate point -> length snapped to the indexed distance;
    //   candidate axis  -> perpendicular component zeroed (the off-axis field
    //                      reads 0.000, matching the point on the axis).
    // currentIndexing() carries the snapped local offset (lx,ly,lz). When no
    // candidate is active the read-out behaves as usual (raw cursor).
    const IndexingResult ix = s.currentIndexing();
    const bool snapped = ix.distanceActive || ix.axisActive;

    if (s.mode() == CompassMode::Polar) {
        if (snapped) {
            const double dist  = std::sqrt(ix.lx * ix.lx + ix.ly * ix.ly);
            const double angle = std::atan2(ix.ly, ix.lx) * 180.0 / M_PI;
            setValuesDist(dist, angle, ix.lz);
        } else {
            // Polar is inherently a relative distance/angle; the frame toggle
            // only affects the rectangular read-out.
            setValuesDist(s.distance(), s.angleDeg(), s.localOffset().z);
        }
    } else {
        const Base::Vector3d l = snapped
            ? Base::Vector3d(ix.lx, ix.ly, ix.lz)   // snapped candidate point/axis
            : s.localOffset();                       // raw cursor
        if (_worldFrame) {
            // Same compass axes, but measured from the world origin instead of
            // the compass origin: value_i = localOffset_i + origin . axis_i.
            const Base::Vector3d o = s.origin();
            setValues(l.x + o.Dot(s.planeU()),
                      l.y + o.Dot(s.planeV()),
                      l.z + o.Dot(s.planeNormal()));
        } else {
            setValues(l.x, l.y, l.z);
        }
    }
}

// ---------------------------------------------------------------------------

double AxisFlowPanel::parseField(QLineEdit* edit) const
{
    bool ok;
    double v = edit->text().toDouble(&ok);
    return ok ? v : NaN;
}

} // namespace AxisFlowGui
