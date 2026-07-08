#include "PreCompiled.h"

#include "AxisFlowPanel.h"
#include "AxisFlowService.h"
#include "AxisFlowHotkeys.h"

#include <Gui/MainWindow.h>

#include <Base/Quantity.h>
#include <Base/Exception.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QSignalBlocker>
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

    // Variant A: the fields are a live read-out of the constrained point, fed by
    // cursorMoved. Numeric entry is Draft's job, so the fields are read-only and
    // never take keyboard focus (nothing here competes with Draft's inputs).
    for (QLineEdit* e : {_editA, _editB, _editC}) {
        e->setReadOnly(true);
        e->setFocusPolicy(Qt::NoFocus);
        e->setPlaceholderText(QString(QChar(0x2014)));   // em dash, encoding-safe
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
    connect(_editA, &QLineEdit::textEdited, this,
            [this](const QString& t){ pushFieldValue(0, t); });
    connect(_editB, &QLineEdit::textEdited, this,
            [this](const QString& t){ pushFieldValue(1, t); });
    connect(_editC, &QLineEdit::textEdited, this,
            [this](const QString& t){ pushFieldValue(2, t); });

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

    // Initial sync.
    onServiceState();
}

// ---------------------------------------------------------------------------

void AxisFlowPanel::setValues(double x, double y, double z)
{
    // A field carrying a typed override must never be overwritten by the live
    // read-out: the 3-D view takes focus back as soon as the mouse moves, so
    // hasFocus() alone is not enough to protect what the user is typing.
    const AxisFlowService& s = AxisFlowService::instance();
    if (!s.hasKeyValue(0) && !_editA->hasFocus()) _editA->setText(QString::number(x, 'f', 4));
    if (!s.hasKeyValue(1) && !_editB->hasFocus()) _editB->setText(QString::number(y, 'f', 4));
    if (!s.hasKeyValue(2) && !_editC->hasFocus()) _editC->setText(QString::number(z, 'f', 4));
}

void AxisFlowPanel::setValuesDist(double dist, double angle)
{
    const AxisFlowService& s = AxisFlowService::instance();
    if (!s.hasKeyValue(0) && !_editA->hasFocus()) _editA->setText(QString::number(dist, 'f', 4));
    if (!s.hasKeyValue(1) && !_editB->hasFocus()) _editB->setText(QString::number(angle, 'f', 2));
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
    const QString t = text.trimmed();
    if (t.isEmpty())
        return std::numeric_limits<double>::quiet_NaN();

    // Base::Quantity understands units and both decimal separators, so "1500",
    // "1,5 m" and "2 ft" all work. A bare number keeps the document's unit.
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

bool AxisFlowPanel::startTyping(QChar ch)
{
    QLineEdit* target = _editA;   // X (ortho) or Distance (radial)
    if (!target || !target->isVisible())
        return false;

    // Continue an entry already in progress instead of restarting it. Moving the
    // mouse hands focus back to the 3-D view, so the next digit arrives here
    // rather than in the field -- without this, "1500" would end up as "0".
    const bool continuing = AxisFlowService::instance().hasKeyValue(0);
    const QString text = continuing ? (target->text() + ch) : QString(ch);

    target->setFocus(Qt::OtherFocusReason);
    target->setText(text);
    target->setCursorPosition(int(text.length()));
    pushFieldValue(0, text);
    return true;
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
        auto* edit = static_cast<QLineEdit*>(obj);

        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter ||
            ke->key() == Qt::Key_Tab    || isValueKey(ke))
            return QWidget::eventFilter(obj, ev);

        // Characters that can legitimately appear inside a quantity ("1500 mm",
        // "1,5 m", "45 deg") must be typed, not interpreted -- but only once the
        // field has content. Escape, Insert and friends are never typable, so
        // they always run their action, even mid-entry.
        const QString kt = ke->text();
        const bool typable = !kt.isEmpty() &&
                             (kt.at(0).isLetterOrNumber() ||
                              QStringLiteral(" .,-+/^").contains(kt.at(0)));

        AxisFlowService& s = AxisFlowService::instance();
        AxisAction a = s.actionForKey(ke->key());
        if (a != AxisAction::None &&
            (!typable || edit->text().trimmed().isEmpty())) {
            s.performAction(a);
            return true;
        }
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
        if (_editA->hasFocus())      { focusField(1); }
        else if (_editB->hasFocus()) { focusField(_polar ? 0 : 2); }
        else                         { focusField(0); }
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
        for (QLineEdit* e : {_editA, _editB, _editC}) {
            if (e->hasFocus()) e->clearFocus();
        }
    }
}

void AxisFlowPanel::onCursorMoved(Base::Vector3d /*solved*/)
{
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
