#pragma once

#include <QtWidgets/QWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtGui/QKeyEvent>

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
     * Begin value entry from a key that arrived while the 3-D view had focus
     * (adapter mode). Focuses the primary field (X / Distance), selects its
     * live readout so the first char overwrites it, and re-posts the key there
     * so it is processed as normal typing. Returns true if handled.
     */
    bool startTyping(QKeyEvent* ke);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private Q_SLOTS:
    void onServiceState();
    void onCursorMoved(Base::Vector3d solved);

private:
    void buildUi();
    void wireToService();
    double parseField(QLineEdit* edit) const;

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
};

} // namespace AxisFlowGui
