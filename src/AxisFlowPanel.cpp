#include "PreCompiled.h"

#include "AxisFlowPanel.h"
#include "AxisFlowService.h"
#include "AxisFlowHotkeys.h"

#include <Gui/MainWindow.h>

#include <QtCore/QEvent>
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

    // No typed-value wiring here: in the Draft adapter (variant A) numeric entry
    // is Draft's job and the fields are read-only. AxisFlow contributes the
    // geometric lock (checkboxes above, or the global lock keys) and the point
    // is constrained through the snap hook.

    // Initial sync.
    onServiceState();
}

// ---------------------------------------------------------------------------

void AxisFlowPanel::setValues(double x, double y, double z)
{
    if (!_editA->hasFocus()) _editA->setText(QString::number(x, 'f', 4));
    if (!_editB->hasFocus()) _editB->setText(QString::number(y, 'f', 4));
    if (!_editC->hasFocus()) _editC->setText(QString::number(z, 'f', 4));
}

void AxisFlowPanel::setValuesDist(double dist, double angle)
{
    if (!_editA->hasFocus()) _editA->setText(QString::number(dist, 'f', 4));
    if (!_editB->hasFocus()) _editB->setText(QString::number(angle, 'f', 2));
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
    _lockA->setChecked(lockedField == 0);
    _lockB->setChecked(lockedField == 1);
    _lockC->setChecked(lockedField == 2);
}

void AxisFlowPanel::focusField(int index)
{
    switch (index) {
    case 0: _editA->setFocus(); _editA->selectAll(); break;
    case 1: _editB->setFocus(); _editB->selectAll(); break;
    case 2: _editC->setFocus(); _editC->selectAll(); break;
    }
}

bool AxisFlowPanel::eventFilter(QObject* obj, QEvent* ev)
{
    // Drag the frameless window by its title label. (The value fields are
    // read-only and never focused in variant A, so no key filtering is needed
    // here — the global filter handles lock/mode keys and Draft keeps the rest.)
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
