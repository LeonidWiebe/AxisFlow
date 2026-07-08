#include "PreCompiled.h"

#include "AxisFlowPanel.h"
#include "AxisFlowService.h"
#include "AxisFlowSettings.h"
#include "AxisFlowHotkeys.h"

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

    _modeBtn = new QPushButton(QStringLiteral("Rect"));

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
    mainLayout->addWidget(_modeBtn);
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

    // Mode button -> service.
    connect(_modeBtn, &QPushButton::clicked, this,
            [](bool){ AxisFlowService::instance().toggleMode(); });

    // Lock checkboxes -> service. 'clicked' fires only on user interaction, so
    // setLockState()'s programmatic setChecked() won't feed back here.
    connect(_lockA, &QCheckBox::clicked, this, [](bool){
        AxisFlowService& s = AxisFlowService::instance();
        (s.lock() == AxisLock::X) ? s.clearLocks() : s.lockAxis(AxisLock::X);
    });
    connect(_lockB, &QCheckBox::clicked, this, [](bool){
        AxisFlowService& s = AxisFlowService::instance();
        (s.lock() == AxisLock::Y) ? s.clearLocks() : s.lockAxis(AxisLock::Y);
    });
    connect(_lockC, &QCheckBox::clicked, this, [](bool){
        AxisFlowService& s = AxisFlowService::instance();
        (s.lock() == AxisLock::Z) ? s.clearLocks() : s.lockAxis(AxisLock::Z);
    });

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

    // Enter runs whatever the keymap binds to Return (smart lock by default).
    auto onReturn = []() {
        AxisFlowService& s = AxisFlowService::instance();
        s.performAction(s.actionForKey(Qt::Key_Return));
    };
    connect(_editA, &QLineEdit::returnPressed, this, onReturn);
    connect(_editB, &QLineEdit::returnPressed, this, onReturn);
    connect(_editC, &QLineEdit::returnPressed, this, onReturn);

    // Bound letters (X/Y/Z/Space/...) must act even while a field has focus,
    // otherwise they would just be typed into the number.
    _editA->installEventFilter(this);
    _editB->installEventFilter(this);
    _editC->installEventFilter(this);

    // The highlight colour is a setting, so follow it live.
    connect(&AxisFlowSettings::instance(), &AxisFlowSettings::changed,
            this, [this]{ refreshActiveHighlight(); });

    // Initial sync.
    refreshActiveHighlight();
    onServiceState();
}

// ---------------------------------------------------------------------------

void AxisFlowPanel::setValues(double x, double y, double z)
{
    // A field carrying a typed override must never be overwritten by the live
    // read-out: the 3-D view takes focus back as soon as the mouse moves, so
    // hasFocus() alone is not enough to protect what the user is typing.
    const AxisFlowService& s = AxisFlowService::instance();
    auto held = [&](int i, QLineEdit* e) {
        // Held while it carries an override, while it is being typed into (even
        // if the text does not parse yet, e.g. a lone "-"), or while focused.
        return s.hasKeyValue(i) || i == _entryField || e->hasFocus();
    };
    if (!held(0, _editA)) _editA->setText(QString::number(x, 'f', 4));
    if (!held(1, _editB)) _editB->setText(QString::number(y, 'f', 4));
    if (!held(2, _editC)) _editC->setText(QString::number(z, 'f', 4));
}

void AxisFlowPanel::setValuesDist(double dist, double angle)
{
    const AxisFlowService& s = AxisFlowService::instance();
    auto held = [&](int i, QLineEdit* e) {
        return s.hasKeyValue(i) || i == _entryField || e->hasFocus();
    };
    if (!held(0, _editA)) _editA->setText(QString::number(dist, 'f', 4));
    if (!held(1, _editB)) _editB->setText(QString::number(angle, 'f', 2));
}

void AxisFlowPanel::setMode(bool polar)
{
    if (polar == _polar) return;   // avoid needless relayout on every update
    _polar = polar;
    if (polar) {
        _labelA->setText(QLatin1String("D"));
        _labelB->setText(QLatin1String("A"));
        _editC->setVisible(false); _labelC->setVisible(false); _lockC->setVisible(false);
        _modeBtn->setText(QLatin1String("Polar"));
    } else {
        _labelA->setText(QLatin1String("X"));
        _labelB->setText(QLatin1String("Y"));
        _editC->setVisible(true); _labelC->setVisible(true); _lockC->setVisible(true);
        _modeBtn->setText(QLatin1String("Rect"));
    }
    // Z disappears in polar: the active field must stay within range.
    setActiveField(_activeField);
    refreshActiveHighlight();
    adjustSize();
}

void AxisFlowPanel::setLockState(int lockedField)
{
    // A field is "locked" either because its axis is locked, or because the user
    // typed a value into it -- in AccuDraw both pin the coordinate.
    const AxisFlowService& s = AxisFlowService::instance();
    QCheckBox* boxes[3] = { _lockA, _lockB, _lockC };
    for (int i = 0; i < 3; ++i) {
        const bool on = (lockedField == i) || s.hasKeyValue(i);
        QSignalBlocker block(boxes[i]);   // programmatic change must not re-enter
        boxes[i]->setChecked(on);
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

    if (text.trimmed().isEmpty()) {
        // Clearing a field releases ONLY its own override; the others stay put.
        s.clearKeyValue(index);
        return;
    }

    const double v = parseQuantity(text);
    if (!std::isnan(v))
        s.setKeyValue(index, v);
    // An unparsable partial entry ("1,5 m" mid-typing, "-") is simply ignored
    // until it becomes valid; nothing is reset under the user's fingers.
}

void AxisFlowPanel::setActiveField(int index)
{
    const int maxField = _polar ? 1 : 2;   // polar has no Z row
    if (index < 0) index = 0;
    if (index > maxField) index = maxField;
    if (index == _activeField) return;

    _activeField = index;
    refreshActiveHighlight();
    focusActiveField();     // the caret follows the active field
}

void AxisFlowPanel::focusActiveField()
{
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
    const int count = _polar ? 2 : 3;
    setActiveField((_activeField + 1) % count);
}

void AxisFlowPanel::updateActiveFieldFromCursor()
{
    // Frozen while the user is typing: the entry must not jump to another field
    // because the cursor drifted past the diagonal.
    if (_entryField >= 0)
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
    const int idx = (_entryField >= 0) ? _entryField : _activeField;
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

    // Action keys must fire even while a value field has focus.
    if (ev->type() == QEvent::KeyPress &&
        (obj == _editA || obj == _editB || obj == _editC)) {
        auto* ke = static_cast<QKeyEvent*>(ev);

        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            cycleActiveField();
            return true;   // never let Qt walk the focus chain
        }

        // The caret lives in the active field, which shows the live read-out.
        // The FIRST typed character must therefore replace that read-out, not be
        // appended to it: "1500" typed over "1234.5678" must not become
        // "1234.56781500".
        if (_entryField < 0 && isEntryStartKey(ke)) {
            QLineEdit* edit = qobject_cast<QLineEdit*>(obj);
            const int idx = (edit == _editB) ? 1 : (edit == _editC ? 2 : 0);
            _entryField = idx;
            edit->clear();
            return QWidget::eventFilter(obj, ev);   // let QLineEdit insert it
        }

        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter ||
            isValueKey(ke))
            return QWidget::eventFilter(obj, ev);

        // Bound keys always act, even mid-entry: after typing "1500" the X key
        // must still lock the X axis. Letters cannot be typed into the field
        // anyway (validator), so there is no ambiguity with unit suffixes.
        AxisFlowService& s = AxisFlowService::instance();
        AxisAction a = s.actionForKey(ke->key());
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

    int lf = -1;
    switch (s.lock()) {
    case AxisLock::X: lf = 0; break;
    case AxisLock::Y: lf = 1; break;
    case AxisLock::Z: lf = 2; break;
    default: break;
    }
    setLockState(lf);

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
    // The active field follows the axis the cursor leans towards (frozen while
    // typing -- see updateActiveFieldFromCursor).
    updateActiveFieldFromCursor();

    AxisFlowService& s = AxisFlowService::instance();
    if (s.mode() == CompassMode::Polar) {
        setValuesDist(s.distance(), s.angleDeg());
    } else {
        Base::Vector3d l = s.localOffset();
        setValues(l.x, l.y, l.z);
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
