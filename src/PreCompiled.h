#pragma once

// Python MUST come first on Windows to avoid slot/emit/signals conflicts
// between Python.h and Qt headers
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4100 4127 4512)
#endif

#include <FCConfig.h>

#ifdef FC_OS_WIN32
# include <windows.h>
#endif

// Python (must be before Qt)
#include <Python.h>

// FreeCAD interpreter (defines PyMOD_INIT_FUNC etc.)
#include <Base/Interpreter.h>

// Qt — comes AFTER Python to avoid keyword conflicts
// Qt6 disables keywords (signals/slots/emit) when QT_NO_KEYWORDS is set
// We use Q_SIGNALS / Q_SLOTS / Q_EMIT instead
#define QT_NO_KEYWORDS
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtWidgets/QWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLabel>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>

#ifdef _MSC_VER
# pragma warning(pop)
#endif
