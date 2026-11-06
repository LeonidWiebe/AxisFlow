#include "PreCompiled.h"

#include "AxisFlowPanel.h"
#include "AxisFlowService.h"
#include "AxisFlowSettings.h"
#include "AxisFlowHotkeys.h"
#include "AxisFlowEntry.h"
#include "AxisFlowKeyContext.h"

#include <Gui/MainWindow.h>

#include <Base/Quantity.h>
#include <Base/Exception.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QRegularExpression>
#include <QtCore/QSignalBlocker>
#include <QtGui/QRegularExpressionValidator>
#include <QtGui/QCloseEvent>
#include <QtGui/QHideEvent>
#include <QtGui/QMoveEvent>
#include <QtGui/QFont>
#include <QtGui/QKeySequence>
#include <QtGui/QScreen>
#include <QtGui/QPalette>
#include <QtGui/QMouseEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QStyle>

#include <algorithm>

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
    // Events during buildUi run before the child widgets exist.
    _uiReady = true;
    if (QWidget* host = parentWidget())
        host->installEventFilter(this);
    syncThemeAppearance();
}

AxisFlowPanel::~AxisFlowPanel()
{
    if (QWidget* host = parentWidget())
        host->removeEventFilter(this);
}

void AxisFlowPanel::buildUi()
{
    // A frameless tool window is not a QMainWindow or QDialog, so FreeCAD's
    // stylesheet does not fill it. The object name lets a later rule paint
    // this widget alone.
    setObjectName(QStringLiteral("AxisFlowPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

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

    // Chord hint: a SEPARATE little window parked beside the panel, shown only
    // while a two-key chord waits for its second key. Deliberately not a row in
    // the layout -- that changed the panel's minimum size and made it resize
    // (and the window manager complain) every time a prefix was pressed.
    // WA_ShowWithoutActivating + WindowDoesNotAcceptFocus are essential: the
    // caret must stay in the active coordinate field, or the next key would be
    // resolved as a global FreeCAD shortcut.
    _chordHint = new QLabel(this, Qt::Tool | Qt::FramelessWindowHint
                                  | Qt::WindowDoesNotAcceptFocus);
    _chordHint->setAttribute(Qt::WA_ShowWithoutActivating);
    _chordHint->setFocusPolicy(Qt::NoFocus);
    _chordHint->setFrameShape(QFrame::StyledPanel);
    _chordHint->setMargin(6);
    _chordHint->setAutoFillBackground(true);
    // Hidden until a chord. The theme pass paints it from the panel colours.
    QFont hf = _chordHint->font();
    hf.setBold(true);
    _chordHint->setFont(hf);
    _chordHint->hide();
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

    // The highlight colour is a setting, so follow it live. The handler also
    // reapplies the theme; it does not rewrite the stored color.
    connect(&AxisFlowSettings::instance(), &AxisFlowSettings::changed,
            this, [this]{ syncThemeAppearance(); });

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

QString AxisFlowPanel::actionLabel(AxisAction a)
{
    // Only the actions that can sensibly sit behind a chord need a friendly
    // name here; anything else falls back to its canonical name, which is what
    // hotkeys.ini and the settings dialog show too.
    switch (a) {
    case AxisAction::RotateAboutX: return tr("about X");
    case AxisAction::RotateAboutY: return tr("about Y");
    case AxisAction::RotateAboutZ: return tr("about Z");
    default: return QString::fromLatin1(Hotkeys::name(a));
    }
}

void AxisFlowPanel::updateChordHint()
{
    if (!_chordHint)
        return;

    AxisFlowService& s = AxisFlowService::instance();
    const int prefix = s.pendingPrefix();
    if (prefix == 0) {
        _chordHint->hide();
        return;
    }

    // The prefix on its own line, then one option per line:
    //
    //     R ->
    //     X about X
    //     Y about Y
    //     Z about Z
    //
    // A list scans faster vertically than a run-on line, and the window is free
    // to grow downwards -- it is not part of the panel's layout.
    const QString arrow = QString(QChar(0x2192));   // -> , encoding-safe
    QStringList parts;
    for (const auto& opt : s.pendingOptions()) {
        const QString keyName =
            QKeySequence(opt.first).toString(QKeySequence::PortableText);
        parts << QStringLiteral("%1 %2").arg(keyName, actionLabel(opt.second));
    }

    const QString prefixName =
        QKeySequence(prefix).toString(QKeySequence::PortableText);
    _chordHint->setText(QStringLiteral("%1 %2\n%3")
                            .arg(prefixName, arrow, parts.join(QLatin1Char('\n'))));

    positionChordHint();
    _chordHint->show();       // never raise()/activateWindow(): that would move
                              // the caret out of the coordinate field
}

void AxisFlowPanel::positionChordHint()
{
    if (!_chordHint)
        return;

    _chordHint->adjustSize();   // its own window, so this cannot disturb the panel

    const QRect panelRect = frameGeometry();
    const int   gap = 6;
    const QSize hint = _chordHint->size();

    QPoint pos(panelRect.right() + gap, panelRect.top());

    // Flip to the left edge when there is no room on the right, and keep the
    // whole thing on the screen the panel lives on.
    if (const QScreen* sc = screen()) {
        const QRect avail = sc->availableGeometry();
        if (pos.x() + hint.width() > avail.right())
            pos.setX(panelRect.left() - hint.width() - gap);
        if (pos.x() < avail.left())
            pos.setX(avail.left());
        if (pos.y() + hint.height() > avail.bottom())
            pos.setY(avail.bottom() - hint.height());
        if (pos.y() < avail.top())
            pos.setY(avail.top());
    }
    _chordHint->move(pos);
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

namespace {

constexpr double kReadableContrast = 4.5;

double linearChannel(double value)
{
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color)
{
    return 0.2126 * linearChannel(color.redF())
         + 0.7152 * linearChannel(color.greenF())
         + 0.0722 * linearChannel(color.blueF());
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double left = relativeLuminance(a);
    const double right = relativeLuminance(b);
    const double lighter = std::max(left, right);
    const double darker = std::min(left, right);
    return (lighter + 0.05) / (darker + 0.05);
}

QColor blendOver(const QColor& src, const QColor& dst, double amount)
{
    const double alpha = std::clamp(amount, 0.0, 1.0);
    auto channel = [&](int from, int to) {
        return std::clamp(static_cast<int>(std::lround(from * alpha + to * (1.0 - alpha))), 0, 255);
    };
    return QColor(channel(src.red(), dst.red()),
                  channel(src.green(), dst.green()),
                  channel(src.blue(), dst.blue()));
}

QColor emphasize(const QColor& base)
{
    const int delta = relativeLuminance(base) > 0.45 ? -48 : 48;
    auto channel = [&](int value) { return std::clamp(value + delta, 0, 255); };
    return QColor(channel(base.red()), channel(base.green()), channel(base.blue()));
}

QColor readableForeground(const QColor& background, const QColor& preferred)
{
    const QColor preferredRgb(preferred.red(), preferred.green(), preferred.blue());
    if (contrastRatio(preferredRgb, background) >= kReadableContrast)
        return preferredRgb;
    const QColor black(0, 0, 0);
    const QColor white(255, 255, 255);
    return contrastRatio(black, background) >= contrastRatio(white, background) ? black : white;
}

QString cssColor(const QColor& color)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

bool sameColor(const QColor& a, const QColor& b)
{
    return a.red() == b.red()
        && a.green() == b.green()
        && a.blue() == b.blue()
        && a.alpha() == b.alpha();
}

} // namespace

void AxisFlowPanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!_uiReady || !event)
        return;
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
        syncThemeAppearance();
        break;
    default:
        break;
    }
}

void AxisFlowPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (_uiReady)
        syncThemeAppearance();
}

AxisFlowPanel::ThemeColors AxisFlowPanel::sampleThemeColors() const
{
    // Sample widgets this panel does not recolor. The active field's stylesheet
    // would otherwise be read back as if it were the theme.
    ThemeColors colors;
    QLineEdit* edits[3] = { _editA, _editB, _editC };
    QLineEdit* source = nullptr;
    for (QLineEdit* edit : edits) {
        if (edit && edit->styleSheet().isEmpty()) {
            source = edit;
            break;
        }
    }
    if (!source)
        source = _editA;
    if (!source)
        return colors;

    source->ensurePolished();
    const QPalette field = source->palette();
    colors.base = field.color(QPalette::Base);
    colors.text = field.color(QPalette::Text);
    colors.highlight = field.color(QPalette::Highlight);
    colors.highlightedText = field.color(QPalette::HighlightedText);
    colors.windowText = colors.text;

    const QColor appWindow = QApplication::palette().color(QPalette::Window);
    QColor window = appWindow;
    if (QWidget* host = parentWidget())
        window = host->palette().color(QPalette::Window);
    // FreeCAD's stylesheet sets `* { color }` and fills QMainWindow/QDialog/
    // QLineEdit, but it deliberately has no QWidget background rule. A frameless
    // tool window therefore keeps the ordinary Window color while its labels
    // use the theme foreground. When that pair does not contrast and the field
    // background does, the field role is the theme color Qt actually resolved.
    if (contrastRatio(colors.windowText, window) < kReadableContrast
        && contrastRatio(colors.windowText, colors.base) >= kReadableContrast)
        window = colors.base;
    colors.window = QColor(window.red(), window.green(), window.blue());
    colors.valid = colors.window.isValid() && colors.text.isValid() && colors.base.isValid();
    return colors;
}

void AxisFlowPanel::syncThemeAppearance()
{
    if (_applyingTheme || !_uiReady || !_editA || !_title || !_chordHint)
        return;
    _applyingTheme = true;
    const ThemeColors sampled = sampleThemeColors();
    if (sampled.valid)
        _theme = sampled;
    applyPanelChrome();
    applyChordHintTheme();
    refreshActiveHighlight();
    _applyingTheme = false;
}

void AxisFlowPanel::applyPanelChrome()
{
    if (!_theme.valid)
        return;

    const bool textFits = contrastRatio(_theme.windowText, _theme.window) >= kReadableContrast;
    const QColor labelFg = textFits
        ? QColor(_theme.windowText.red(), _theme.windowText.green(), _theme.windowText.blue())
        : readableForeground(_theme.window, _theme.windowText);
    // An empty sheet leaves the theme foreground in place. A color is written
    // only when that foreground does not contrast with the fill we resolved.
    const QString labelCss = textFits
        ? QString()
        : QStringLiteral("color: %1;").arg(cssColor(labelFg));

    QPalette pal = palette();
    if (!sameColor(pal.color(QPalette::Window), _theme.window)
        || !sameColor(pal.color(QPalette::WindowText), labelFg)) {
        pal.setColor(QPalette::Window, _theme.window);
        pal.setColor(QPalette::WindowText, labelFg);
        setPalette(pal);
    }

    const QString panelCss = QStringLiteral(
        "QWidget#AxisFlowPanel { background-color: %1; }").arg(cssColor(_theme.window));
    if (styleSheet() != panelCss)
        setStyleSheet(panelCss);

    for (QLabel* label : {_title, _labelA, _labelB, _labelC}) {
        if (!label)
            continue;
        if (label->styleSheet() != labelCss)
            label->setStyleSheet(labelCss);
        QPalette labelPal = label->palette();
        if (!sameColor(labelPal.color(QPalette::Window), _theme.window)
            || !sameColor(labelPal.color(QPalette::WindowText), labelFg)) {
            labelPal.setColor(QPalette::Window, _theme.window);
            labelPal.setColor(QPalette::WindowText, labelFg);
            label->setPalette(labelPal);
        }
    }

    // Local/World and the lock boxes stay on the Qt style and the application
    // stylesheet, including the checkbox indicator. Only the stock close icon
    // is refreshed, so a style change can replace it.
    if (_powerBtn)
        _powerBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
}

void AxisFlowPanel::applyChordHintTheme()
{
    if (!_chordHint || !_theme.valid)
        return;

    // This QLabel is not a QToolTip, so FreeCAD's tooltip stylesheet does not
    // paint it. Use the panel fill already resolved for this theme.
    const QColor background(_theme.window.red(), _theme.window.green(), _theme.window.blue());
    const QColor foreground = readableForeground(background, _theme.windowText);
    const QString hintCss = QStringLiteral("background-color: %1; color: %2;")
        .arg(cssColor(background), cssColor(foreground));
    if (_chordHint->styleSheet() != hintCss)
        _chordHint->setStyleSheet(hintCss);

    QPalette hintPal = _chordHint->palette();
    if (!sameColor(hintPal.color(QPalette::Window), background)
        || !sameColor(hintPal.color(QPalette::WindowText), foreground)) {
        hintPal.setColor(QPalette::Window, background);
        hintPal.setColor(QPalette::WindowText, foreground);
        _chordHint->setPalette(hintPal);
    }
}

QString AxisFlowPanel::activeFieldStyleSheet() const
{
    if (!_theme.valid)
        return QString();

    const unsigned long rgba = AxisFlowSettings::instance().activeFieldColor();
    QColor fieldBg;
    if (rgba == AxisFlowDefaults::activeFieldColor) {
        // The saved factory value is indistinguishable from "never changed".
        fieldBg = blendOver(_theme.highlight, _theme.base, 0.55);
        if (contrastRatio(fieldBg, _theme.base) < 1.2)
            fieldBg = emphasize(_theme.base);
    }
    else {
        float r, g, b, a;
        AxisFlowSettings::unpackColor(rgba, r, g, b, a);
        QColor src;
        src.setRgbF(std::clamp(r, 0.0f, 1.0f),
                    std::clamp(g, 0.0f, 1.0f),
                    std::clamp(b, 0.0f, 1.0f),
                    std::clamp(a, 0.0f, 1.0f));
        fieldBg = blendOver(src, _theme.base, src.alphaF());
    }

    const QColor fieldFg = readableForeground(fieldBg, _theme.text);
    QColor selectionBg(_theme.highlight.red(), _theme.highlight.green(), _theme.highlight.blue());
    if (contrastRatio(selectionBg, fieldBg) < 1.3) {
        selectionBg = relativeLuminance(fieldBg) > 0.5
            ? QColor(0, 0, 0)
            : QColor(255, 255, 255);
    }
    const QColor selectionFg = readableForeground(selectionBg, _theme.highlightedText);
    return QStringLiteral(
        "QLineEdit { background-color: %1; color: %2; "
        "selection-background-color: %3; selection-color: %4; }")
        .arg(cssColor(fieldBg), cssColor(fieldFg), cssColor(selectionBg), cssColor(selectionFg));
}

void AxisFlowPanel::refreshActiveHighlight()
{
    if (!_editA || !_editB || !_editC)
        return;

    const QString activeCss = activeFieldStyleSheet();
    QLineEdit* edits[3] = { _editA, _editB, _editC };
    for (int i = 0; i < 3; ++i) {
        QLineEdit* edit = edits[i];
        const QString want = (i == _activeField) ? activeCss : QString();
        if (edit->styleSheet() == want)
            continue;
        const QString text = edit->text();
        const int cursor = edit->cursorPosition();
        const int selStart = edit->selectionStart();
        const int selLength = selStart >= 0 ? edit->selectedText().size() : 0;
        edit->setStyleSheet(want);
        if (edit->text() != text)
            edit->setText(text);

        const int afterStart = edit->selectionStart();
        const int afterLength = afterStart >= 0 ? edit->selectedText().size() : 0;
        const bool unchanged = edit->text() == text
            && edit->cursorPosition() == cursor
            && afterStart == selStart
            && afterLength == selLength;
        if (unchanged)
            continue;

        if (selLength > 0) {
            // QLineEdit::setSelection(start, length) accepts a negative length.
            // The caret stays at start+length: positive length puts it on the
            // right of the range, negative length puts it on the left.
            // setCursorPosition after setSelection would clear the selection.
            const int right = selStart + selLength;
            if (cursor == selStart)
                edit->setSelection(right, -selLength);
            else
                edit->setSelection(selStart, selLength);
        } else if (edit->cursorPosition() != cursor) {
            edit->setCursorPosition(cursor);
        }
    }
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

bool AxisFlowPanel::eventFilter(QObject* obj, QEvent* ev)
{
    // Appearance only. Returning false leaves key and mouse delivery unchanged.
    if (obj == parentWidget()) {
        const QEvent::Type type = ev->type();
        if (type == QEvent::StyleChange
            || type == QEvent::PaletteChange
            || type == QEvent::ApplicationPaletteChange)
            syncThemeAppearance();
        return false;
    }

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

    // Delete / Backspace with nothing being typed have to reach FreeCAD, so
    // that Del still deletes the selected object while the compass is on.
    //
    // Qt offers the key to the focused widget first, as a ShortcutOverride, and
    // QLineEdit ACCEPTS that for editing keys -- which is what swallowed Del:
    // the shortcut was suppressed and the key went into the read-out instead.
    // Blocking the override here without accepting it leaves the event
    // unclaimed, so Qt goes on to resolve the shortcut.
    if (ev->type() == QEvent::ShortcutOverride &&
        (obj == _editA || obj == _editB || obj == _editC)) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        const bool eraser = (ke->key() == Qt::Key_Delete ||
                             ke->key() == Qt::Key_Backspace);
        if (eraser && !entryInProgress())
            return true;                 // deliberately NOT accepted
        return QWidget::eventFilter(obj, ev);
    }

    // Action keys must fire even while a value field has focus. The decision of
    // what a key means lives in AxisFlowKeyRouting.h (Tier-1 tested); this only
    // executes the verdict.
    if (ev->type() == QEvent::KeyPress &&
        (obj == _editA || obj == _editB || obj == _editC)) {
        auto* ke = static_cast<QKeyEvent*>(ev);
        QLineEdit* edit = qobject_cast<QLineEdit*>(obj);
        const int idx = (edit == _editB) ? 1 : (edit == _editC ? 2 : 0);

        AxisFlowService& s = AxisFlowService::instance();
        KeyContext ctx = keyContextFromEvent(ke, /*inField*/ true, _ownsInput);
        // Only the panel knows which field is mid-entry. "Fresh" means the
        // character lands in a DIFFERENT field than the one being typed --
        // including after Tab, where _entryField still points at the previous
        // field (that was the bug: the new field kept its dynamic value and the
        // digit appended, e.g. Z became "0.00005").
        ctx.sameEntryField = (_entryField == idx);

        const KeyRoute route = routeKey(ctx);
        applyChordState(ke, route);   // start / end the two-key chord wait

        switch (route) {
        case KeyRoute::BeginChord:
        case KeyRoute::ChordHold:
            return true;                            // waiting for the second key
        case KeyRoute::TabCycle:
            cycleActiveField();
            return true;                            // never walk Qt's focus chain
        case KeyRoute::BeginEntry:
            _entryField = idx;
            edit->clear();                          // replace the live read-out
            return QWidget::eventFilter(obj, ev);   // QLineEdit inserts the char
        case KeyRoute::PassToWidget:
            return QWidget::eventFilter(obj, ev);
        case KeyRoute::CancelEntry:
            cancelInput();
            return true;
        case KeyRoute::Action:
            // Bound keys act even mid-entry: after typing "1500" the X key must
            // still lock X, and Enter still commits the point instead of being
            // eaten by the line edit.
            s.performAction(ctx.bound);
            return true;
        case KeyRoute::Swallow:
            return true;
        case KeyRoute::Ignore:
            // Backspace / Delete handed back above. If FreeCAD had a shortcut
            // for it, that already ran and this KeyPress never happens; getting
            // here means nothing is bound, so swallow it rather than let the
            // line edit chew a character out of the read-out.
            if (ctx.backspace || ctx.del)
                return true;
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(obj, ev);
}

void AxisFlowPanel::moveEvent(QMoveEvent* e)
{
    // Dragging the panel by its title bar must take the hint along.
    if (_chordHint && _chordHint->isVisible())
        positionChordHint();
    QWidget::moveEvent(e);
}

void AxisFlowPanel::hideEvent(QHideEvent* e)
{
    // The hint is a top-level window of its own: hiding the panel (AxisFlow
    // switched off, session torn down) has to take it off screen too.
    if (_chordHint)
        _chordHint->hide();
    QWidget::hideEvent(e);
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
    // Same policy as the field filter, minus the in-field cases: this is the
    // panel window itself, so no coordinate field has the key.
    AxisFlowService& s = AxisFlowService::instance();
    const KeyContext ctx = keyContextFromEvent(e, /*inField*/ false, _ownsInput);

    const KeyRoute route = routeKey(ctx);
    applyChordState(e, route);

    switch (route) {
    case KeyRoute::BeginChord:
    case KeyRoute::ChordHold:   return;             // waiting for the second key
    case KeyRoute::TabCycle:    cycleActiveField();      return;
    case KeyRoute::CancelEntry: cancelInput();           return;
    case KeyRoute::Action:      s.performAction(ctx.bound); return;
    case KeyRoute::Backspace:   backspaceEntry();        return;
    case KeyRoute::ClearEntry:  clearEntry();            return;
    case KeyRoute::StartTyping:
        if (!e->text().isEmpty() && startTyping(e->text().at(0)))
            return;
        break;
    case KeyRoute::Swallow:     return;
    default:                    break;
    }

    QWidget::keyPressEvent(e);
}

// ---------------------------------------------------------------------------

void AxisFlowPanel::onServiceState()
{
    AxisFlowService& s = AxisFlowService::instance();
    setMode(s.mode() == CompassMode::Polar);
    setLockState();
    updateChordHint();

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
