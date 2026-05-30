// Copyright 2024 Sessionat. All rights reserved.
// Workspace data model for native workspace management.

#ifndef CHROME_BROWSER_SESSIONAT_WORKSPACE_MODEL_H_
#define CHROME_BROWSER_SESSIONAT_WORKSPACE_MODEL_H_

#include <string>
#include <vector>

#include "base/time/time.h"
#include "base/values.h"
#include "url/gurl.h"

namespace sessionat {

// Represents a single item (URL) within a workspace.
struct WorkspaceItem {
  WorkspaceItem();
  WorkspaceItem(const WorkspaceItem&);
  WorkspaceItem& operator=(const WorkspaceItem&);
  WorkspaceItem(WorkspaceItem&&);
  WorkspaceItem& operator=(WorkspaceItem&&);
  ~WorkspaceItem();

  // Unique identifier for the item.
  std::string id;

  // The URL of the website.
  GURL url;

  // User-friendly title (defaults to page title).
  std::string title;

  // Favicon URL for display.
  GURL favicon_url;

  // Optional folder path within the workspace (e.g., "Work/Projects").
  std::string folder_path;

  // Tags for organization and search.
  std::vector<std::string> tags;

  // When this item was added.
  base::Time created_at;

  // When this item was last opened.
  base::Time last_opened_at;

  // Convert to/from base::Value for storage.
  base::Value::Dict ToDict() const;
  static std::optional<WorkspaceItem> FromDict(const base::Value::Dict& dict);
};

// Represents a workspace - a curated collection of websites.
struct Workspace {
  Workspace();
  Workspace(const Workspace&);
  Workspace& operator=(const Workspace&);
  Workspace(Workspace&&);
  Workspace& operator=(Workspace&&);
  ~Workspace();

  // Unique identifier for the workspace.
  std::string id;

  // Display name of the workspace.
  std::string name;

  // Color for visual identification (hex format, e.g., "#FF5733").
  std::string color;

  // Icon name or emoji for the workspace.
  std::string icon;

  // Description of what this workspace is for.
  std::string description;

  // Tags for organization.
  std::vector<std::string> tags;

  // Items (URLs) in this workspace.
  std::vector<WorkspaceItem> items;

  // When the workspace was created.
  base::Time created_at;

  // When the workspace was last modified.
  base::Time modified_at;

  // When the workspace was last opened (all items loaded).
  base::Time last_opened_at;

  // Whether this is the currently active workspace.
  bool is_active = false;

  // Sort order for display.
  int sort_order = 0;

  // Convert to/from base::Value for storage.
  base::Value::Dict ToDict() const;
  static std::optional<Workspace> FromDict(const base::Value::Dict& dict);

  // Helper to get item count.
  size_t item_count() const { return items.size(); }

  // Helper to check if workspace contains a URL.
  bool ContainsUrl(const GURL& url) const;

  // Helper to find item by URL.
  const WorkspaceItem* FindItemByUrl(const GURL& url) const;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_WORKSPACE_MODEL_H_
