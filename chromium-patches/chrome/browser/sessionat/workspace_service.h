// Copyright 2024 Sessionat. All rights reserved.
// Service for managing workspaces in the Sessionat browser.

#ifndef CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_H_
#define CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "components/keyed_service/core/keyed_service.h"

class BrowserWindowInterface;
class PrefRegistrySimple;
class PrefService;
class Profile;
class ProfileBrowserCollection;

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
class WorkspaceService : public KeyedService,
                          public BrowserCollectionObserver {
 public:
  explicit WorkspaceService(Profile* profile);
  ~WorkspaceService() override;

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

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

  // ===== Pinning =====

  // Sets the pinned state of a workspace. Pinned workspaces appear first
  // and own the leading Cmd+1..9 shortcuts. Returns false if no such
  // workspace exists.
  bool SetWorkspacePinned(const std::string& workspace_id, bool pinned);

  // Returns workspaces in the order they should appear in UI: pinned first
  // (in their existing GetAllWorkspaces order among themselves), then the
  // rest. Cmd+1..9 binds to the first 9 entries of this list.
  std::vector<Workspace> GetOrderedWorkspaces() const;

  // ===== Phase 1.4: workspace-owned tabs =====

  // High-level "switch to this workspace" that swaps the window's tab set:
  //   1. Snapshots the active window's open tabs into the *currently* active
  //      workspace's items (so the user's tabs are preserved before leaving).
  //   2. Marks `dest_id` active.
  //   3. Opens `dest_id`'s items as tabs in the same window, then closes the
  //      tabs that belonged to the previous workspace.
  // If no workspace is currently active, step 1 is skipped. If `dest_id` is
  // empty or invalid, returns without changes. `browser` is the window in
  // which to perform the swap; if nullptr, falls back to the bookmark-style
  // "open as new background tabs" behaviour of SetActiveWorkspace.
  void SwapToWorkspace(const std::string& dest_id,
                       BrowserWindowInterface* browser);

  // Snapshots the open tabs in `browser` into `workspace_id`'s items list.
  // Replaces the existing items list. Used by SwapToWorkspace before leaving
  // a workspace, and by the auto-save-on-close path.
  void SnapshotTabs(const std::string& workspace_id,
                    BrowserWindowInterface* browser);

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

  // Fires every kAutoSnapshotInterval (30s by default) and snapshots tabs
  // from the most-recently-active normal browser into the current workspace.
  void AutoSnapshotTick();
  // Posted from OnBrowserCreated to do the launch-time restore after the
  // browser is fully initialised.
  void RestoreActiveWorkspaceTabs();

  // DanglingUntriaged: Profile and PrefService are destroyed by the
  // KeyedServiceFactory teardown order BEFORE this WorkspaceService's
  // destructor runs. The raw_ptr destructor then sees the dangling pointers
  // and trips DanglingRawPtr DCHECK in debug builds (causing a hard crash
  // on Sessionat quit). DanglingUntriaged silences that assertion — the
  // pointers are only ever dereferenced while the service is alive, never
  // in the destructor itself.
  raw_ptr<Profile, DanglingUntriaged> profile_;
  raw_ptr<PrefService, DanglingUntriaged> pref_service_;

  // All workspaces.
  std::vector<Workspace> workspaces_;

  // ID of the currently active workspace.
  std::string active_workspace_id_;

  // Set true once we've performed the launch-time restore for this process
  // life (so we don't re-restore when subsequent windows open).
  bool launch_restore_done_ = false;

  // Periodic timer that snapshots tabs into the active workspace so they
  // survive an unexpected exit. Trade-off: up to 30s of state loss on crash.
  base::RepeatingTimer auto_snapshot_timer_;

  base::ScopedObservation<ProfileBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};

  // Observers for workspace changes.
  base::ObserverList<WorkspaceServiceObserver> observers_;

  base::WeakPtrFactory<WorkspaceService> weak_factory_{this};
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_H_
