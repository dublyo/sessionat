// Copyright 2024 Sessionat. All rights reserved.
// Submenu model for adding tabs to workspaces.

#ifndef CHROME_BROWSER_UI_TABS_WORKSPACE_SUB_MENU_MODEL_H_
#define CHROME_BROWSER_UI_TABS_WORKSPACE_SUB_MENU_MODEL_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"

class Profile;
class TabStripModel;
class TabMenuModelDelegate;

// Submenu model for adding the current tab to an existing workspace.
class WorkspaceSubMenuModel : public ui::SimpleMenuModel,
                               public ui::SimpleMenuModel::Delegate {
 public:
  WorkspaceSubMenuModel(ui::SimpleMenuModel::Delegate* parent_delegate,
                        TabMenuModelDelegate* tab_menu_model_delegate,
                        TabStripModel* model,
                        int context_index);
  WorkspaceSubMenuModel(const WorkspaceSubMenuModel&) = delete;
  WorkspaceSubMenuModel& operator=(const WorkspaceSubMenuModel&) = delete;
  ~WorkspaceSubMenuModel() override;

  // Whether the submenu should be shown. True if there are any workspaces.
  static bool ShouldShowSubmenu(Profile* profile);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // Build the submenu with existing workspaces.
  void Build();

  // Add the tab at context_index to the specified workspace.
  void AddTabToWorkspace(const std::string& workspace_id);

  raw_ptr<TabStripModel> model_;
  int context_index_;

  // Mapping from command ID to workspace ID.
  std::vector<std::string> workspace_ids_;

  // Starting command ID for workspace items.
  static constexpr int kWorkspaceCommandIdStart = 6000;
  static constexpr int kNewWorkspaceCommandId = 5999;
};

#endif  // CHROME_BROWSER_UI_TABS_WORKSPACE_SUB_MENU_MODEL_H_
