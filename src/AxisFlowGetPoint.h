#pragma once

#include <QtCore/QObject>

#include <Base/Vector3D.h>

typedef struct _object PyObject;

namespace AxisFlowGui {

class AxisFlowTool;

/**
 * GetPointBridge
 *
 * Turns AxisFlow's input state machine into the Python getPoint() contract:
 * hold the consumer's callbacks and fire them as points arrive.
 *
 *   callback(point, ok)  - a point was confirmed (ok=True), or the input was
 *                          cancelled (point=None, ok=False).
 *   movecallback(point)  - the live point on every cursor move (optional).
 *
 * continuous=False: callback fires once, then the bridge finishes.
 * continuous=True : callback fires per point until cancel; the final cancel
 *                   fires callback(None, False).
 *
 * The bridge lives on the C++/Qt side and calls into Python from Qt/Coin event
 * handlers, so every call is wrapped in Base::PyGILStateLocker. It owns a
 * reference to each callback (Py_INCREF) and deletes itself (deleteLater) when
 * finished, which is safe because it is a plain QObject, not part of the scene.
 *
 * Only one getPoint may be active at a time (matches the single-session rule).
 */
class GetPointBridge : public QObject
{
    Q_OBJECT

public:
    GetPointBridge(AxisFlowTool* tool,
                   PyObject* callback,
                   PyObject* moveCallback,
                   PyObject* finishCallback,
                   bool continuous,
                   QObject* parent = nullptr);
    ~GetPointBridge() override;

    void start();

    static GetPointBridge* active() { return s_active; }
    /** End any running getPoint WITHOUT invoking its callback (a new getPoint
     *  or endSession supersedes it). */
    static void discardActive();

private Q_SLOTS:
    void onPoint(Base::Vector3d p);   // tool->pointConfirmed
    void onCancel();                  // service->cancelRequested
    void onFinish();                  // service->finishRequested (keep session)
    void onMove(Base::Vector3d p);    // service->cursorMoved

private:
    void callPoint(const Base::Vector3d* p, bool ok);
    void callMove(const Base::Vector3d& p);
    void callFinish();
    void finish();

    AxisFlowTool* _tool;
    PyObject*     _cb;
    PyObject*     _moveCb;
    PyObject*     _finishCb;
    bool          _continuous;

    static GetPointBridge* s_active;
};

} // namespace AxisFlowGui
