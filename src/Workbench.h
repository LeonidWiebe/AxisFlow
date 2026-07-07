#pragma once

#include <Gui/Workbench.h>

namespace AxisFlowGui {

class AxisFlowWorkbench : public Gui::StdWorkbench
{
    TYPESYSTEM_HEADER();

public:
    AxisFlowWorkbench();
    ~AxisFlowWorkbench();

protected:
    Gui::MenuItem*        setupMenuBar()     const;
    Gui::ToolBarItem*     setupToolBars()    const;
    Gui::ToolBarItem*     setupCommandBars() const;
    Gui::DockWindowItems* setupDockWindows() const;
};

} // namespace AxisFlowGui
