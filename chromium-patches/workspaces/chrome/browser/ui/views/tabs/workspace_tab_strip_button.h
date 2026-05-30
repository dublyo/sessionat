// Copyright 2024 Sessionat. All rights reserved.
// Workspace button for the Sessionat browser tab strip.

#ifndef CHROME_BROWSER_UI_VIEWS_TABS_WORKSPACE_TAB_STRIP_BUTTON_H_
#define CHROME_BROWSER_UI_VIEWS_TABS_WORKSPACE_TAB_STRIP_BUTTON_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/ui/views/tabs/tab_strip_control_button.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/menu/menu_runner.h"

class TabStripController;

namespace ui {
class DialogModel;
}  // namespace ui

namespace views {
class Widget;
}  // namespace views

// A button that appears in the tab strip region to show and switch workspaces.
// Displays the current workspace name and tab count, e.g., "Work (12)".
// Clicking opens a dropdown to switch between workspaces.
class WorkspaceTabStripButton : public TabStripControlButton,
                                public ui::SimpleMenuModel::Delegate,
                                public sessionat::WorkspaceServiceObserver {
  METADATA_HEADER(WorkspaceTabStripButton, TabStripControlButton)

 public:
  WorkspaceTabStripButton(TabStripController* tab_strip_controller,
                          PressedCallback callback);

  WorkspaceTabStripButton(const WorkspaceTabStripButton&) = delete;
  WorkspaceTabStripButton& operator=(const WorkspaceTabStripButton&) = delete;
  ~WorkspaceTabStripButton() override;

  // Sets the workspace name displayed on the button.
  void SetWorkspaceName(const std::u16string& name);

  // Sets the tab count displayed on the button.
  void SetTabCount(int count);

  // Updates the button text with current workspace name and count.
  void UpdateButtonText();

  // Returns the current workspace name.
  const std::u16string& workspace_name() const { return workspace_name_; }

  // Returns the current tab count.
  int tab_count() const { return tab_count_; }

  // views::LabelButton:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

 // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

  // sessionat::WorkspaceServiceObserver:
  void OnWorkspaceCreated(const sessionat::Workspace& workspace) override;
  void OnWorkspaceUpdated(const sessionat::Workspace& workspace) override;
  void OnWorkspaceDeleted(const std::string& workspace_id) override;
  void OnActiveWorkspaceChanged(const std::string& workspace_id) override;
  void OnWorkspacesLoaded() override;

 protected:
  // TabStripControlButton:
  void UpdateColors() override;
  void NotifyClick(const ui::Event& event) override;

 private:
  // Shows the workspace menu anchored to this button.
  void ShowWorkspaceMenu();

  // Builds the menu model with workspace options.
  void BuildMenuModel();

  // Current workspace name.
  std::u16string workspace_name_;

  // Current tab count in the workspace.
  int tab_count_ = 0;

  // Opens the extension's main UI in a new tab.
  void OpenExtensionUI();

  // Opens a new tab page.
  void OpenNewTab();

  // Opens the extension settings/management page.
  void OpenExtensionSettings();

  // Shows a dialog to create a new workspace with a user-provided name.
  void ShowNewWorkspaceDialog();

  // Callback when the new workspace dialog is accepted.
  void OnNewWorkspaceDialogAccepted(ui::DialogModel* dialog_model);

  // Menu model and runner for the workspace dropdown.
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;

  // Tab strip controller for accessing the browser.
  raw_ptr<TabStripController> controller_;

  // Workspace service for managing workspaces.
  raw_ptr<sessionat::WorkspaceService> workspace_service_ = nullptr;

  // Observation of the workspace service.
  base::ScopedObservation<sessionat::WorkspaceService,
                          sessionat::WorkspaceServiceObserver>
      workspace_service_observation_{this};

  // Must be last member.
  base::WeakPtrFactory<WorkspaceTabStripButton> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_VIEWS_TABS_WORKSPACE_TAB_STRIP_BUTTON_H_
