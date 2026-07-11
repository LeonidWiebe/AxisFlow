#include "PreCompiled.h"

#include "Workbench.h"

#include <Gui/MenuManager.h>
#include <Gui/ToolBarManager.h>
#include <Gui/DockWindowManager.h>

using namespace AxisFlowGui;

TYPESYSTEM_SOURCE(AxisFlowGui::AxisFlowWorkbench, Gui::StdWorkbench)

AxisFlowWorkbench::AxisFlowWorkbench() = default;
AxisFlowWorkbench::~AxisFlowWorkbench() = default;

Gui::MenuItem* AxisFlowWorkbench::setupMenuBar() const
{
    Gui::MenuItem* root = StdWorkbench::setupMenuBar();
    Gui::MenuItem* item = root->findItem("&Windows");

    Gui::MenuItem* axisflow = new Gui::MenuItem;
    axisflow->setCommand("AxisFlow");
    *axisflow << "AxisFlow_Activate";
    *axisflow << "AxisFlow_DrawLine";
    *axisflow << "AxisFlow_ToggleLiveLine";

    root->insertItem(item, axisflow);
    return root;
}

Gui::ToolBarItem* AxisFlowWorkbench::setupToolBars() const
{
    Gui::ToolBarItem* root = StdWorkbench::setupToolBars();

    Gui::ToolBarItem* bar = new Gui::ToolBarItem(root);
    bar->setCommand("AxisFlow");
    *bar << "AxisFlow_Activate";
    *bar << "AxisFlow_DrawLine";
    *bar << "AxisFlow_ToggleLiveLine";

    return root;
}

Gui::ToolBarItem* AxisFlowWorkbench::setupCommandBars() const
{
    return nullptr;
}

Gui::DockWindowItems* AxisFlowWorkbench::setupDockWindows() const
{
    Gui::DockWindowItems* root = StdWorkbench::setupDockWindows();
    root->setVisibility(false);
    return root;
}
