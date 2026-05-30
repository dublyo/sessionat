// Copyright 2024 Sessionat. All rights reserved.
// WorkspaceService implementation.

#include "chrome/browser/sessionat/workspace_service.h"

#include "base/uuid.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace sessionat {

namespace {

// Preference keys.
constexpr char kWorkspacesListPref[] = "sessionat.workspaces";
constexpr char kActiveWorkspaceIdPref[] = "sessionat.active_workspace_id";

}  // namespace

WorkspaceService::WorkspaceService(Profile* profile)
    : profile_(profile), pref_service_(profile->GetPrefs()) {
  LoadWorkspaces();
}

WorkspaceService::~WorkspaceService() = default;

// static
void WorkspaceService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kWorkspacesListPref);
  registry->RegisterStringPref(kActiveWorkspaceIdPref, "");
}

void WorkspaceService::AddObserver(WorkspaceServiceObserver* observer) {
  observers_.AddObserver(observer);
}

void WorkspaceService::RemoveObserver(WorkspaceServiceObserver* observer) {
  observers_.RemoveObserver(observer);
}

std::string WorkspaceService::CreateWorkspace(const std::string& name,
                                               const std::string& color,
                                               const std::string& icon) {
  Workspace workspace;
  workspace.id = GenerateWorkspaceId();
  workspace.name = name;
  workspace.color = color.empty() ? "#4285F4" : color;  // Default blue
  workspace.icon = icon;
  workspace.created_at = base::Time::Now();
  workspace.modified_at = workspace.created_at;
  workspace.sort_order = static_cast<int>(workspaces_.size());

  workspaces_.push_back(std::move(workspace));
  SaveWorkspaces();

  NotifyWorkspaceCreated(workspaces_.back());

  LOG(INFO) << "Sessionat: Created workspace '" << name << "' with ID "
            << workspaces_.back().id;

  return workspaces_.back().id;
}

const Workspace* WorkspaceService::GetWorkspace(
    const std::string& workspace_id) const {
  for (const auto& workspace : workspaces_) {
    if (workspace.id == workspace_id) {
      return &workspace;
    }
  }
  return nullptr;
}

const std::vector<Workspace>& WorkspaceService::GetAllWorkspaces() const {
  return workspaces_;
}

bool WorkspaceService::UpdateWorkspace(const std::string& workspace_id,
                                        const std::string& name,
                                        const std::string& color,
                                        const std::string& icon,
                                        const std::string& description) {
  Workspace* workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    return false;
  }

  workspace->name = name;
  if (!color.empty()) {
    workspace->color = color;
  }
  if (!icon.empty()) {
    workspace->icon = icon;
  }
  workspace->description = description;
  workspace->modified_at = base::Time::Now();

  SaveWorkspaces();
  NotifyWorkspaceUpdated(*workspace);

  return true;
}

bool WorkspaceService::DeleteWorkspace(const std::string& workspace_id) {
  auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
                         [&workspace_id](const Workspace& w) {
                           return w.id == workspace_id;
                         });

  if (it == workspaces_.end()) {
    return false;
  }

  workspaces_.erase(it);
  SaveWorkspaces();
  NotifyWorkspaceDeleted(workspace_id);

  // Clear active workspace if it was deleted.
  if (active_workspace_id_ == workspace_id) {
    active_workspace_id_.clear();
    pref_service_->SetString(kActiveWorkspaceIdPref, "");
  }

  LOG(INFO) << "Sessionat: Deleted workspace " << workspace_id;

  return true;
}

bool WorkspaceService::AddItemToWorkspace(const std::string& workspace_id,
                                           const GURL& url,
                                           const std::string& title,
                                           const std::string& folder_path) {
  Workspace* workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    return false;
  }

  // Check if URL already exists.
  if (workspace->ContainsUrl(url)) {
    LOG(INFO) << "Sessionat: URL already exists in workspace";
    return false;
  }

  WorkspaceItem item;
  item.id = GenerateItemId();
  item.url = url;
  item.title = title.empty() ? url.spec() : title;
  item.folder_path = folder_path;
  item.created_at = base::Time::Now();

  workspace->items.push_back(std::move(item));
  workspace->modified_at = base::Time::Now();

  SaveWorkspaces();
  NotifyWorkspaceUpdated(*workspace);

  LOG(INFO) << "Sessionat: Added '" << title << "' to workspace '"
            << workspace->name << "'";

  return true;
}

bool WorkspaceService::RemoveItemFromWorkspace(const std::string& workspace_id,
                                                const std::string& item_id) {
  Workspace* workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    return false;
  }

  auto it = std::find_if(workspace->items.begin(), workspace->items.end(),
                         [&item_id](const WorkspaceItem& item) {
                           return item.id == item_id;
                         });

  if (it == workspace->items.end()) {
    return false;
  }

  workspace->items.erase(it);
  workspace->modified_at = base::Time::Now();

  SaveWorkspaces();
  NotifyWorkspaceUpdated(*workspace);

  return true;
}

bool WorkspaceService::UpdateItem(const std::string& workspace_id,
                                   const std::string& item_id,
                                   const std::string& title,
                                   const std::string& folder_path,
                                   const std::vector<std::string>& tags) {
  Workspace* workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    return false;
  }

  for (auto& item : workspace->items) {
    if (item.id == item_id) {
      item.title = title;
      item.folder_path = folder_path;
      item.tags = tags;
      workspace->modified_at = base::Time::Now();

      SaveWorkspaces();
      NotifyWorkspaceUpdated(*workspace);
      return true;
    }
  }

  return false;
}

std::string WorkspaceService::GetActiveWorkspaceId() const {
  return active_workspace_id_;
}

bool WorkspaceService::SetActiveWorkspace(const std::string& workspace_id) {
  // Allow empty to clear active workspace.
  if (!workspace_id.empty() && !GetWorkspace(workspace_id)) {
    return false;
  }

  // Update previous active workspace.
  if (!active_workspace_id_.empty()) {
    Workspace* prev = FindWorkspace(active_workspace_id_);
    if (prev) {
      prev->is_active = false;
    }
  }

  active_workspace_id_ = workspace_id;
  pref_service_->SetString(kActiveWorkspaceIdPref, workspace_id);

  // Update new active workspace.
  if (!workspace_id.empty()) {
    Workspace* workspace = FindWorkspace(workspace_id);
    if (workspace) {
      workspace->is_active = true;
      workspace->last_opened_at = base::Time::Now();
    }
  }

  SaveWorkspaces();
  NotifyActiveWorkspaceChanged(workspace_id);

  return true;
}

const Workspace* WorkspaceService::GetActiveWorkspace() const {
  if (active_workspace_id_.empty()) {
    return nullptr;
  }
  return GetWorkspace(active_workspace_id_);
}

bool WorkspaceService::AddTagsToWorkspace(
    const std::string& workspace_id,
    const std::vector<std::string>& tags) {
  Workspace* workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    return false;
  }

  for (const auto& tag : tags) {
    if (std::find(workspace->tags.begin(), workspace->tags.end(), tag) ==
        workspace->tags.end()) {
      workspace->tags.push_back(tag);
    }
  }

  workspace->modified_at = base::Time::Now();
  SaveWorkspaces();
  NotifyWorkspaceUpdated(*workspace);

  return true;
}

bool WorkspaceService::RemoveTagFromWorkspace(const std::string& workspace_id,
                                               const std::string& tag) {
  Workspace* workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    return false;
  }

  auto it = std::find(workspace->tags.begin(), workspace->tags.end(), tag);
  if (it == workspace->tags.end()) {
    return false;
  }

  workspace->tags.erase(it);
  workspace->modified_at = base::Time::Now();
  SaveWorkspaces();
  NotifyWorkspaceUpdated(*workspace);

  return true;
}

std::vector<std::string> WorkspaceService::GetAllTags() const {
  std::vector<std::string> all_tags;
  for (const auto& workspace : workspaces_) {
    for (const auto& tag : workspace.tags) {
      if (std::find(all_tags.begin(), all_tags.end(), tag) == all_tags.end()) {
        all_tags.push_back(tag);
      }
    }
  }
  return all_tags;
}

std::vector<const Workspace*> WorkspaceService::FindWorkspacesByTag(
    const std::string& tag) const {
  std::vector<const Workspace*> result;
  for (const auto& workspace : workspaces_) {
    if (std::find(workspace.tags.begin(), workspace.tags.end(), tag) !=
        workspace.tags.end()) {
      result.push_back(&workspace);
    }
  }
  return result;
}

std::vector<const Workspace*> WorkspaceService::SearchWorkspaces(
    const std::string& query) const {
  std::vector<const Workspace*> result;
  std::string lower_query = base::ToLowerASCII(query);

  for (const auto& workspace : workspaces_) {
    std::string lower_name = base::ToLowerASCII(workspace.name);
    std::string lower_desc = base::ToLowerASCII(workspace.description);

    if (lower_name.find(lower_query) != std::string::npos ||
        lower_desc.find(lower_query) != std::string::npos) {
      result.push_back(&workspace);
    }
  }
  return result;
}

std::string WorkspaceService::ExportToJson() const {
  base::Value::List list;
  for (const auto& workspace : workspaces_) {
    list.Append(workspace.ToDict());
  }

  std::string json;
  base::JSONWriter::WriteWithOptions(
      list, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

int WorkspaceService::ImportFromJson(const std::string& json) {
  auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return 0;
  }

  int imported = 0;
  for (const auto& value : parsed->GetList()) {
    if (!value.is_dict()) {
      continue;
    }

    auto workspace = Workspace::FromDict(value.GetDict());
    if (workspace) {
      // Generate new ID to avoid conflicts.
      workspace->id = GenerateWorkspaceId();
      workspaces_.push_back(std::move(*workspace));
      imported++;
    }
  }

  if (imported > 0) {
    SaveWorkspaces();
    NotifyWorkspacesLoaded();
  }

  return imported;
}

void WorkspaceService::LoadWorkspaces() {
  workspaces_.clear();

  const base::Value::List& list = pref_service_->GetList(kWorkspacesListPref);
  for (const auto& value : list) {
    if (!value.is_dict()) {
      continue;
    }

    auto workspace = Workspace::FromDict(value.GetDict());
    if (workspace) {
      workspaces_.push_back(std::move(*workspace));
    }
  }

  active_workspace_id_ = pref_service_->GetString(kActiveWorkspaceIdPref);

  // Create default workspace if none exist.
  if (workspaces_.empty()) {
    CreateWorkspace("Default", "#4285F4", "");
  }

  NotifyWorkspacesLoaded();

  LOG(INFO) << "Sessionat: Loaded " << workspaces_.size() << " workspaces";
}

void WorkspaceService::SaveWorkspaces() {
  base::Value::List list;
  for (const auto& workspace : workspaces_) {
    list.Append(workspace.ToDict());
  }

  pref_service_->SetList(kWorkspacesListPref, std::move(list));
}

Workspace* WorkspaceService::FindWorkspace(const std::string& workspace_id) {
  for (auto& workspace : workspaces_) {
    if (workspace.id == workspace_id) {
      return &workspace;
    }
  }
  return nullptr;
}

std::string WorkspaceService::GenerateWorkspaceId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

std::string WorkspaceService::GenerateItemId() {
  return base::Uuid::GenerateRandomV4().AsLowercaseString();
}

void WorkspaceService::NotifyWorkspaceCreated(const Workspace& workspace) {
  for (auto& observer : observers_) {
    observer.OnWorkspaceCreated(workspace);
  }
}

void WorkspaceService::NotifyWorkspaceUpdated(const Workspace& workspace) {
  for (auto& observer : observers_) {
    observer.OnWorkspaceUpdated(workspace);
  }
}

void WorkspaceService::NotifyWorkspaceDeleted(const std::string& workspace_id) {
  for (auto& observer : observers_) {
    observer.OnWorkspaceDeleted(workspace_id);
  }
}

void WorkspaceService::NotifyActiveWorkspaceChanged(
    const std::string& workspace_id) {
  for (auto& observer : observers_) {
    observer.OnActiveWorkspaceChanged(workspace_id);
  }
}

void WorkspaceService::NotifyWorkspacesLoaded() {
  for (auto& observer : observers_) {
    observer.OnWorkspacesLoaded();
  }
}

}  // namespace sessionat
