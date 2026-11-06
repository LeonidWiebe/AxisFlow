#pragma once

#include <QtWidgets/QWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtGui/QColor>
#include <QtGui/QKeyEvent>
#include <QtGui/QShowEvent>
#include <QtCore/QPoint>

#include <Base/Vector3D.h>

#include "AxisFlowHotkeys.h"   // AxisAction (actionLabel, chord hint)

namespace AxisFlowGui {

/**
 * AxisFlowPanel
 *
 * Autonomous floating view of AxisFlowService. It talks to the service
 * DIRECTLY (checkboxes / fields / keys -> service) and reflects service state
 * (stateChanged -> mode+locks, cursorMoved -> live X/Y/Z readout). Because it
 * has no dependency on AxisFlowTool, it can be shown in any mode (standalone
 * or Snapper-adapter) just by calling instance()->show().
 *
 * A single shared instance is parented to the main window.
 */
class AxisFlowPanel : public QWidget
{
    Q_OBJECT

public:
    /** Shared instance, created lazily and parented to the main window. */
    static AxisFlowPanel* instance();

    explicit AxisFlowPanel(QWidget* parent = nullptr);
    ~AxisFlowPanel() override;

    void setValues(double x, double y, double z);
    void setValuesDist(double dist, double angle, double z);
    void setMode(bool polar);
    void setLockState();   // checkboxes <- fixed-field state (or the adapter mirror)
    void focusField(int index);

    /**
     * "Just start typing": a printable character arrived while the 3-D view had
     * focus. Focus the primary field (X / Distance), seed it with the character
     * and push the value as a keyboard override. From then on the field holds
     * focus, so the rest of the number is typed normally.
     */
    bool startTyping(QChar ch);

    /** Backspace from the 3-D view: drop the last character of the entry. */
    bool backspaceEntry();
    /** Delete/clear the whole entry, releasing its override. */
    bool clearEntry();
    /** A value is being typed right now (as opposed to the field merely having
     *  the caret, which it almost always does). Backspace and Delete belong to
     *  AxisFlow only while this is true. */
    bool entryInProgress() const { return _entryField >= 0; }

    /**
     * Layered cancel (Escape). First press aborts an in-progress value entry and
     * releases that field's override; if there is nothing being typed, it
     * releases the axis locks. Never ends the command (a separate key does).
     */
    void cancelInput();

    /**
     * The field where a value is expected / being typed. Exactly one field is
     * always active. It follows the axis the cursor leans towards, EXCEPT while
     * an entry is in progress -- then it is frozen, whatever the cursor does.
     */
    int  activeField() const { return _activeField; }
    void setActiveField(int index);

    /**
     * Put the blinking caret in the active field and keep it there.
     *
     * This is the whole point of the AccuDraw input model: while the caret sits
     * in a QLineEdit, that widget consumes digits, Backspace and the arrows, so
     * FreeCAD's global shortcuts (2 -> Std_ViewTop) never fire. Without it we
     * are forced to intercept keys application-wide and fight Qt for them.
     */
    void focusActiveField();

    /**
     * True only when AxisFlow drives the input itself (its own tool). Then, and
     * only then, the panel keeps the caret in the active field. In the Draft
     * adapter this is FALSE: the active field still moves and highlights, but
     * the panel must never steal keyboard focus away from Draft's own fields.
     */
    void setOwnsInput(bool on) { _ownsInput = on; }
    bool ownsInput() const { return _ownsInput; }
    /** Tab: move the active status to the next visible field. */
    void cycleActiveField();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    /** The hint is its own window, so it has to follow the panel and go away
     *  with it. */
    void moveEvent(QMoveEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    /**
     * Filters the child line edits so bound letter keys (X/Y/Z/Space/...) act as
     * AxisFlow actions instead of being typed, while digits still edit the field.
     * Also drives window dragging from the title bar.
     * The main-window filter only notices appearance events and never consumes
     * them: keyboard and mouse handling on that window stay with FreeCAD.
     */
    bool eventFilter(QObject* obj, QEvent* ev) override;

private Q_SLOTS:
    void onServiceState();
    void onCursorMoved(Base::Vector3d solved);

private:
    void buildUi();
    void wireToService();
    double parseField(QLineEdit* edit) const;
    /** Parse text with units ("1500", "1,5 m", "2 ft"); NaN if unparsable. */
    static double parseQuantity(const QString& text);
    /** Push a field's text to the service as a keyboard override. */
    void pushFieldValue(int index, const QString& text);
    /** The line edit currently receiving typed characters. */
    QLineEdit* entryEdit();
    /** Repaint the active field from the cached theme colors. */
    void refreshActiveHighlight();
    /** Read the current theme into _theme and apply it. Safe to call again. */
    void syncThemeAppearance();
    /** Panel fill, label fallback and the close icon. Does not touch field text. */
    void applyPanelChrome();
    /** Chord hint uses the panel fill and a readable foreground. NoFocus stays. */
    void applyChordHintTheme();
    /** Stylesheet for the active coordinate field, or empty when theme is unknown. */
    QString activeFieldStyleSheet() const;

    /**
     * Colors resolved from Qt palette roles of widgets the theme actually
     * paints. Not a theme name and not a copy of a stylesheet file.
     */
    struct ThemeColors {
        QColor window;
        QColor windowText;
        QColor base;
        QColor text;
        QColor highlight;
        QColor highlightedText;
        bool valid {false};
    };
    ThemeColors sampleThemeColors() const;
    /** Pick the active field from the axis the cursor leans towards. */
    void updateActiveFieldFromCursor();
    /** Focus advance on movement: after a value is typed into an in-plane
     *  field, a deliberate cursor move finishes the entry and puts the caret
     *  into the adjacent field (X <-> Y; D <-> A in radial mode). */
    void maybeAdvanceFocus();
    /** Reflect the current readout frame (Local/World) on the frame button. */
    void updateFrameButton();
    /** Show which second keys are on offer while a two-key chord is pending,
     *  and hide the hint again once it resolves. */
    void updateChordHint();
    /** Park the hint window beside the panel (left side if there is no room
     *  on the right), sized to its text. */
    void positionChordHint();
    /** Short, translated description of an action, for the chord hint. */
    static QString actionLabel(AxisAction a);
    /** Round a committed manual entry to the standard precision on focus-out. */
    void reformatField(int index);
    /** Decimals shown per field: 2 for the polar angle, 4 otherwise. */
    int  fieldDecimals(int index) const;

    QLabel*      _title  {nullptr};
    QToolButton* _powerBtn {nullptr};

    QCheckBox*   _lockA {nullptr};
    QCheckBox*   _lockB {nullptr};
    QCheckBox*   _lockC {nullptr};
    QLineEdit*   _editA {nullptr};
    QLineEdit*   _editB {nullptr};
    QLineEdit*   _editC {nullptr};
    QLabel*      _labelA {nullptr};
    QLabel*      _labelB {nullptr};
    QLabel*      _labelC {nullptr};
    QPushButton* _frameBtn {nullptr};   // toggles Local <-> World readout
    /** "R -> X / Y / Z" while a chord waits. A SEPARATE top-level window, not
     *  a row in the layout: as a layout item it changed the panel's minimum
     *  size, and the resulting adjustSize() fought the window manager (Qt
     *  logged "Unable to set geometry ... minimum size" on every press). */
    QLabel*      _chordHint {nullptr};

    bool         _polar { false };
    bool         _worldFrame { false };   // false = compass-local, true = world

    // Frameless-window drag state (title bar).
    bool         _dragging { false };
    QPoint       _dragOffset;

    /** Field currently being typed into (-1 = none). Protected from the live
     *  read-out even before the text parses into a valid number. */
    int          _entryField { -1 };
    /** Focus-advance-on-movement state: armed by a keystroke that produced a
     *  valid value in field 0/1; the anchor is the raw cursor at that moment,
     *  so only movement AFTER typing counts. */
    bool         _advanceArmed { false };
    double       _advanceAnchorX { 0.0 };
    double       _advanceAnchorY { 0.0 };
    /** After an advance the caret must STAY in the handed-over field even
     *  though the cursor may still hug the previous axis -- this holds off the
     *  nearest-axis auto-targeting until Tab / Escape / a confirmed point. */
    bool         _advanceHold { false };
    /** Always 0..2 (0/1 in polar). Frozen while _entryField >= 0. */
    int          _activeField { 0 };
    bool         _ownsInput   { false };   // AxisFlow drives input (own tool)

    /** False until buildUi has created the widgets changeEvent may touch. */
    bool         _uiReady { false };
    /** setPalette/setStyleSheet emit the same events that start a refresh. */
    bool         _applyingTheme { false };
    ThemeColors  _theme;
};

} // namespace AxisFlowGui
