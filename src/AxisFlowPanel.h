#pragma once

#include <QtWidgets/QWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>
#include <QtGui/QKeyEvent>
#include <QtCore/QPoint>

#include <Base/Vector3D.h>

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

    void setValues(double x, double y, double z);
    void setValuesDist(double dist, double angle);
    void setMode(bool polar);
    void setLockState(int lockedField);   // -1 none, 0 X, 1 Y, 2 Z
    void focusField(int index);

    /**
     * "Just start typing": a printable character arrived while the 3-D view had
     * focus. Focus the primary field (X / Distance), seed it with the character
     * and push the value as a keyboard override. From then on the field holds
     * focus, so the rest of the number is typed normally.
     */
    bool startTyping(QChar ch);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    /**
     * Filters the child line edits so bound letter keys (X/Y/Z/Space/...) act as
     * AxisFlow actions instead of being typed, while digits still edit the field.
     * Also drives window dragging from the title bar.
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
    /** True for keys that edit the number rather than trigger an action. */
    static bool isValueKey(const QKeyEvent* ke);

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
    QPushButton* _modeBtn {nullptr};

    bool         _polar { false };

    // Frameless-window drag state (title bar).
    bool         _dragging { false };
    QPoint       _dragOffset;
};

} // namespace AxisFlowGui
