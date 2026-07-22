#include "PreCompiled.h"

#ifdef FC_OS_WIN32
# include <windows.h>
#endif

#include <QtWidgets/QApplication>

#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/MainWindow.h>
#include <Gui/View3DInventor.h>

#include "AxisFlowTool.h"
#include "AxisFlowSettings.h"

// ---------------------------------------------------------------------------
// DEF_STD_CMD_A creates a command with isActive() override
// DEF_STD_CMD   creates a simple command (isActive always true)
// ---------------------------------------------------------------------------

class CmdAxisFlowActivate : public Gui::Command
{
public:
    CmdAxisFlowActivate();
    const char* className() const override { return "CmdAxisFlowActivate"; }
protected:
    void activated(int iMsg) override;
    bool isActive() override;
private:
    AxisFlowGui::AxisFlowTool* _tool {nullptr};
};

CmdAxisFlowActivate::CmdAxisFlowActivate()
    : Gui::Command("AxisFlow_Activate")
{
    sAppModule   = "AxisFlow";
    sGroup       = "AxisFlow";
    sMenuText    = QT_TR_NOOP("Activate AxisFlow");
    sToolTipText = QT_TR_NOOP("Toggle the AxisFlow coordinate input compass");
    sWhatsThis   = "AxisFlow_Activate";
    sStatusTip   = sToolTipText;
    sPixmap      = "AxisFlow_Activate";
    sAccel       = "A, D";
}

void CmdAxisFlowActivate::activated(int)
{
    auto* mw = Gui::getMainWindow();
    if (!mw) return;

    auto* view = qobject_cast<Gui::View3DInventor*>(mw->activeWindow());
    if (!view) {
        Base::Console().warning("AxisFlow: no active 3D view.\n");
        return;
    }

    if (_tool && _tool->isActive()) {
        _tool->deactivate();
    } else {
        if (!_tool || _tool->parent() != view) {
            delete _tool;
            _tool = new AxisFlowGui::AxisFlowTool(view, view);
        }
        _tool->activate();
    }
}

bool CmdAxisFlowActivate::isActive()
{
    return Gui::Application::Instance->activeDocument() != nullptr;
}

// ---------------------------------------------------------------------------

class CmdAxisFlowToggleLiveLine : public Gui::Command
{
public:
    CmdAxisFlowToggleLiveLine();
    const char* className() const override { return "CmdAxisFlowToggleLiveLine"; }
protected:
    void activated(int iMsg) override;
    bool isActive() override;
};

CmdAxisFlowToggleLiveLine::CmdAxisFlowToggleLiveLine()
    : Gui::Command("AxisFlow_ToggleLiveLine")
{
    sAppModule   = "AxisFlow";
    sGroup       = "AxisFlow";
    sMenuText    = QT_TR_NOOP("Toggle live line");
    sToolTipText = QT_TR_NOOP("Show or hide the live guide line from the compass centre to the cursor");
    sWhatsThis   = "AxisFlow_ToggleLiveLine";
    sStatusTip   = sToolTipText;
    sPixmap      = "AxisFlow_ToggleLiveLine";
}

void CmdAxisFlowToggleLiveLine::activated(int)
{
    auto& s = AxisFlowGui::AxisFlowSettings::instance();
    s.setLiveLineEnabled(!s.liveLineEnabled());   // persisted; compass redraws via changed()
}

bool CmdAxisFlowToggleLiveLine::isActive()
{
    return true;
}

// ---------------------------------------------------------------------------

// Draws a polyline through AxisFlow's public getPoint() API. The geometry and
// preview live in axisflow_demo.py (the reference consumer, outside the core);
// this command is just a toolbar entry point to it.
class CmdAxisFlowDrawLine : public Gui::Command
{
public:
    CmdAxisFlowDrawLine();
    const char* className() const override { return "CmdAxisFlowDrawLine"; }
protected:
    void activated(int iMsg) override;
    bool isActive() override;
};

CmdAxisFlowDrawLine::CmdAxisFlowDrawLine()
    : Gui::Command("AxisFlow_DrawLine")
{
    sAppModule   = "AxisFlow";
    sGroup       = "AxisFlow";
    sMenuText    = QT_TR_NOOP("Draw line");
    sToolTipText = QT_TR_NOOP("Draw a polyline using the AxisFlow compass (Escape starts a "
                             "new line, backtick ends the command)");
    sWhatsThis   = "AxisFlow_DrawLine";
    sStatusTip   = sToolTipText;
    sPixmap      = "AxisFlow_DrawLine";
}

void CmdAxisFlowDrawLine::activated(int)
{
    doCommand(Gui::Command::Gui,
              "import axisflow_demo\naxisflow_demo.line()");
}

bool CmdAxisFlowDrawLine::isActive()
{
    return Gui::Application::Instance->activeDocument() != nullptr;
}

// ---------------------------------------------------------------------------

// Opens the live settings dialog (axisflow_settings.py -- PySide UI over the
// public getSettings()/setSetting() API).
class CmdAxisFlowSettings : public Gui::Command
{
public:
    CmdAxisFlowSettings();
    const char* className() const override { return "CmdAxisFlowSettings"; }
protected:
    void activated(int iMsg) override;
    bool isActive() override;
};

CmdAxisFlowSettings::CmdAxisFlowSettings()
    : Gui::Command("AxisFlow_Settings")
{
    sAppModule   = "AxisFlow";
    sGroup       = "AxisFlow";
    sMenuText    = QT_TR_NOOP("Settings");
    sToolTipText = QT_TR_NOOP("Open the AxisFlow settings (changes apply live)");
    sWhatsThis   = "AxisFlow_Settings";
    sStatusTip   = sToolTipText;
    sPixmap      = "AxisFlow_Settings";
}

void CmdAxisFlowSettings::activated(int)
{
    doCommand(Gui::Command::Gui,
              "import axisflow_settings\naxisflow_settings.show()");
}

bool CmdAxisFlowSettings::isActive()
{
    return true;
}

// ---------------------------------------------------------------------------

void CreateAxisFlowCommands()
{
    Gui::CommandManager& mgr = Gui::Application::Instance->commandManager();
    mgr.addCommand(new CmdAxisFlowActivate());
    mgr.addCommand(new CmdAxisFlowDrawLine());
    mgr.addCommand(new CmdAxisFlowToggleLiveLine());
    mgr.addCommand(new CmdAxisFlowSettings());
}
