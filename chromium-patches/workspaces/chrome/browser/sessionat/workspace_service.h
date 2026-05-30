// Copyright 2024 Sessionat. All rights reserved.
// Service for managing workspaces in the Sessionat browser.

#ifndef CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_H_
#define CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class PrefService;
class Profile;

namespace sessionat {

// Observer interface for workspace changes.
class WorkspaceServiceObserver : public base::CheckedObserver {
 public:
  // Called when a workspace is created.
  virtual void OnWorkspaceCreated(const Workspace& workspace) {}

  // Called when a workspace is updated.
  virtual void OnWorkspaceUpdated(const Workspace& workspace) {}

  // Called when a workspace is deleted.
  virtual void OnWorkspaceDeleted(const std::string& workspace_id) {}

  // Called when the active workspace changes.
  virtual void OnActiveWorkspaceChanged(const std::string& workspace_id) {}

  // Called when workspaces are loaded from storage.
  virtual void OnWorkspacesLoaded() {}
};

// Service for managing workspaces.
// Workspaces are curated collections of URLs that users can organize and
// open with one click.
class WorkspaceService : public KeyedService {
 public:
  explicit WorkspaceService(Profile* profile);
  ~WorkspaceService() override;

  WorkspaceService(const WorkspaceService&) = delete;
  WorkspaceService& operator=(const WorkspaceService&) = delete;

  // Registers preferences used by this service.
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Observer management.
  void AddObserver(WorkspaceServiceObserver* observer);
  void RemoveObserver(WorkspaceServiceObserver* observer);

  // ===== Workspace CRUD Operations =====

  // Creates a new workspace and returns its ID.
  std::string CreateWorkspace(const std::string& name,
                              const std::string& color = "",
                              const std::string& icon = "");

  // Gets a workspace by ID. Returns nullptr if not found.
  const Workspace* GetWorkspace(const std::string& workspace_id) const;

  // Gets all workspaces.
  const std::vector<Workspace>& GetAllWorkspaces() const;

  // Updates a workspace's metadata (name, color, icon, description).
  bool UpdateWorkspace(const std::string& workspace_id,
                       const std::string& name,
                       const std::string& color = "",
                       const std::string& icon = "",
                       const std::string& description = "");

  // Deletes a workspace.
  bool DeleteWorkspace(const std::string& workspace_id);

  // ===== Workspace Item Operations =====

  // Adds a URL to a workspace.
  bool AddItemToWorkspace(const std::string& workspace_id,
                          const GURL& url,
                          const std::string& title,
                          const std::string& folder_path = "");

  // Removes an item from a workspace.
  bool RemoveItemFromWorkspace(const std::string& workspace_id,
                               const std::string& item_id);

  // Updates an item in a workspace.
  bool UpdateItem(const std::string& workspace_id,
                  const std::string& item_id,
                  const std::string& title,
                  const std::string& folder_path,
                  const std::vector<std::string>& tags);

  // ===== Active Workspace =====

  // Gets the currently active workspace ID.
  std::string GetActiveWorkspaceId() const;

  // Sets the active workspace.
  bool SetActiveWorkspace(const std::string& workspace_id);

  // Gets the active workspace. Returns nullptr if none.
  const Workspace* GetActiveWorkspace() const;

  // ===== Tags and Organization =====

  // Adds tags to a workspace.
  bool AddTagsToWorkspace(const std::string& workspace_id,
                          const std::vector<std::string>& tags);

  // Removes a tag from a workspace.
  bool RemoveTagFromWorkspace(const std::string& workspace_id,
                              const std::string& tag);

  // Gets all unique tags across all workspaces.
  std::vector<std::string> GetAllTags() const;

  // Finds workspaces by tag.
  std::vector<const Workspace*> FindWorkspacesByTag(
      const std::string& tag) const;

  // ===== Search =====

  // Searches workspaces by name or description.
  std::vector<const Workspace*> SearchWorkspaces(
      const std::string& query) const;

  // ===== Import/Export =====

  // Exports all workspaces to a JSON string.
  std::string ExportToJson() const;

  // Imports workspaces from a JSON string. Returns number of imported.
  int ImportFromJson(const std::string& json);

 private:
  // Loads workspaces from preferences.
  void LoadWorkspaces();

  // Saves workspaces to preferences.
  void SaveWorkspaces();

  // Finds a workspace by ID (mutable version).
  Workspace* FindWorkspace(const std::string& workspace_id);

  // Generates a unique ID for a new workspace.
  std::string GenerateWorkspaceId();

  // Generates a unique ID for a new item.
  std::string GenerateItemId();

  // Notifies observers of changes.
  void NotifyWorkspaceCreated(const Workspace& workspace);
  void NotifyWorkspaceUpdated(const Workspace& workspace);
  void NotifyWorkspaceDeleted(const std::string& workspace_id);
  void NotifyActiveWorkspaceChanged(const std::string& workspace_id);
  void NotifyWorkspacesLoaded();

  raw_ptr<Profile> profile_;
  raw_ptr<PrefService> pref_service_;

  // All workspaces.
  std::vector<Workspace> workspaces_;

  // ID of the currently active workspace.
  std::string active_workspace_id_;

  // Observers for workspace changes.
  base::ObserverList<WorkspaceServiceObserver> observers_;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_H_
