// Copyright 2024 Sessionat. All rights reserved.
// Submenu model for adding tabs to workspaces.

#include "chrome/browser/ui/tabs/workspace_sub_menu_model.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

WorkspaceSubMenuModel::WorkspaceSubMenuModel(
    ui::SimpleMenuModel::Delegate* parent_delegate,
    TabMenuModelDelegate* tab_menu_model_delegate,
    TabStripModel* model,
    int context_index)
    : ui::SimpleMenuModel(this),
      model_(model),
      context_index_(context_index) {
  Build();
}

WorkspaceSubMenuModel::~WorkspaceSubMenuModel() = default;

// static
bool WorkspaceSubMenuModel::ShouldShowSubmenu(Profile* profile) {
  if (!profile) {
    return false;
  }

  auto* workspace_service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!workspace_service) {
    return false;
  }

  // Show submenu if there are any workspaces.
  return !workspace_service->GetAllWorkspaces().empty();
}

bool WorkspaceSubMenuModel::IsCommandIdChecked(int command_id) const {
  return false;
}

bool WorkspaceSubMenuModel::IsCommandIdEnabled(int command_id) const {
  return true;
}

void WorkspaceSubMenuModel::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kNewWorkspaceCommandId) {
    // Create a new workspace and add the tab to it.
    if (!model_ || !model_->profile()) {
      return;
    }

    auto* workspace_service =
        sessionat::WorkspaceServiceFactory::GetForProfile(model_->profile());
    if (!workspace_service) {
      return;
    }

    std::string new_id = workspace_service->CreateWorkspace("New Workspace");
    AddTabToWorkspace(new_id);
    return;
  }

  int workspace_index = command_id - kWorkspaceCommandIdStart;
  if (workspace_index >= 0 &&
      static_cast<size_t>(workspace_index) < workspace_ids_.size()) {
    AddTabToWorkspace(workspace_ids_[workspace_index]);
  }
}

void WorkspaceSubMenuModel::Build() {
  if (!model_ || !model_->profile()) {
    return;
  }

  auto* workspace_service =
      sessionat::WorkspaceServiceFactory::GetForProfile(model_->profile());
  if (!workspace_service) {
    return;
  }

  const auto& workspaces = workspace_service->GetAllWorkspaces();
  workspace_ids_.clear();

  int command_id = kWorkspaceCommandIdStart;
  for (const auto& workspace : workspaces) {
    workspace_ids_.push_back(workspace.id);

    std::u16string label = base::UTF8ToUTF16(workspace.name);
    if (!workspace.items.empty()) {
      label += u" (" + base::UTF8ToUTF16(base::NumberToString(workspace.items.size())) + u")";
    }

    AddItem(command_id, label);
    command_id++;
  }

  // Add separator and "New Workspace" option.
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(kNewWorkspaceCommandId, u"+ New Workspace");
}

void WorkspaceSubMenuModel::AddTabToWorkspace(const std::string& workspace_id) {
  if (!model_ || !model_->profile()) {
    return;
  }

  auto* workspace_service =
      sessionat::WorkspaceServiceFactory::GetForProfile(model_->profile());
  if (!workspace_service) {
    return;
  }

  content::WebContents* contents = model_->GetWebContentsAt(context_index_);
  if (!contents) {
    return;
  }

  GURL url = contents->GetLastCommittedURL();
  std::string title = base::UTF16ToUTF8(contents->GetTitle());

  bool added =
      workspace_service->AddItemToWorkspace(workspace_id, url, title, "");

  if (added) {
    LOG(INFO) << "Sessionat: Added tab '" << title << "' to workspace";
  } else {
    LOG(INFO) << "Sessionat: Tab already exists in workspace or add failed";
  }
}
