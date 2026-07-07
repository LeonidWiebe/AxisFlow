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
    sMenuText    = "Activate AxisFlow";
    sToolTipText = "Toggle the AxisFlow coordinate input compass";
    sWhatsThis   = "AxisFlow_Activate";
    sStatusTip   = sToolTipText;
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

void CreateAxisFlowCommands()
{
    Gui::CommandManager& mgr = Gui::Application::Instance->commandManager();
    mgr.addCommand(new CmdAxisFlowActivate());
}
