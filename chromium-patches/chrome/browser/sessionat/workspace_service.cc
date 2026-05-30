// Copyright 2024 Sessionat. All rights reserved.
// WorkspaceService implementation.

#include "chrome/browser/sessionat/workspace_service.h"

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/webui_url_constants.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace sessionat {

namespace {

// Preference keys.
constexpr char kWorkspacesListPref[] = "sessionat.workspaces";
constexpr char kActiveWorkspaceIdPref[] = "sessionat.active_workspace_id";

}  // namespace

WorkspaceService::WorkspaceService(Profile* profile)
    : profile_(profile), pref_service_(profile->GetPrefs()) {
  LoadWorkspaces();

  // On first run (no workspaces yet), create the starter workspaces.
  // The fourth — "Social" — comes pre-loaded with five popular sites to
  // demonstrate what a workspace IS (a group of tabs you keep together).
  if (workspaces_.empty()) {
    CreateWorkspace("Work", "#F97316", "💼");      // orange-600 (primary)
    CreateWorkspace("Personal", "#FDBA74", "🏠");  // orange-200
    CreateWorkspace("Research", "#FB923C", "🔬");  // orange-400
    const std::string social_id =
        CreateWorkspace("Social (demo)", "#EA580C", "💬");  // orange-700

    // Seed the demo workspace so users can see "click → open these 5 tabs".
    AddItemToWorkspace(social_id, GURL("https://x.com/"), "X (Twitter)", "");
    AddItemToWorkspace(social_id, GURL("https://www.linkedin.com/"),
                       "LinkedIn", "");
    AddItemToWorkspace(social_id, GURL("https://www.reddit.com/"), "Reddit",
                       "");
    AddItemToWorkspace(social_id, GURL("https://news.ycombinator.com/"),
                       "Hacker News", "");
    AddItemToWorkspace(social_id, GURL("https://youtube.com/"), "YouTube", "");
    LOG(INFO) << "[Sessionat] Seeded 4 starter workspaces (incl. Social demo)";
  }

  // Observe profile-scoped browser creation/close so we can auto-restore on
  // launch and auto-snapshot before exit.
  if (auto* collection = ProfileBrowserCollection::GetForProfile(profile_)) {
    browser_collection_observation_.Observe(collection);
  }

  // Periodic snapshot — every 30s, capture the active workspace's tabs into
  // prefs so we don't lose state on crash or unclean exit. Trade-off chosen
  // over a TabStripModelObserver-on-every-window to keep the wiring small.
  auto_snapshot_timer_.Start(
      FROM_HERE, base::Seconds(30),
      base::BindRepeating(&WorkspaceService::AutoSnapshotTick,
                          weak_factory_.GetWeakPtr()));
}

WorkspaceService::~WorkspaceService() = default;

void WorkspaceService::OnBrowserCreated(BrowserWindowInterface* browser) {
  if (launch_restore_done_ || !browser ||
      browser->GetProfile() != profile_) {
    return;
  }
  // Only restore into "normal" windows. Skip popups, devtools, etc.
  if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) {
    return;
  }
  if (active_workspace_id_.empty()) {
    launch_restore_done_ = true;
    return;
  }
  const Workspace* ws = GetWorkspace(active_workspace_id_);
  if (!ws || ws->items.empty()) {
    launch_restore_done_ = true;
    return;
  }
  // Post a task so we don't reenter Browser construction.
  launch_restore_done_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceService::RestoreActiveWorkspaceTabs,
                                weak_factory_.GetWeakPtr()));
}

void WorkspaceService::OnBrowserClosed(BrowserWindowInterface* browser) {
  // Last-chance snapshot. By the time OnBrowserClosed fires, tabs are typically
  // already torn down, so SnapshotTabs will produce an empty list — which we
  // explicitly skip on the SnapshotTabs side (no destructive overwrite).
  // The real safety net is the 30s timer.
  if (!browser || browser->GetProfile() != profile_) {
    return;
  }
  if (!active_workspace_id_.empty()) {
    SnapshotTabs(active_workspace_id_, browser);
  }
}

void WorkspaceService::AutoSnapshotTick() {
  if (active_workspace_id_.empty()) return;
  auto* coll = ProfileBrowserCollection::GetForProfile(profile_);
  if (!coll) return;
  BrowserWindowInterface* browser = coll->FindTabbedBrowser();
  if (!browser) return;
  SnapshotTabs(active_workspace_id_, browser);
}

void WorkspaceService::RestoreActiveWorkspaceTabs() {
  if (active_workspace_id_.empty()) return;
  auto* coll = ProfileBrowserCollection::GetForProfile(profile_);
  if (!coll) return;
  BrowserWindowInterface* browser = coll->FindTabbedBrowser();
  if (!browser) return;
  // The destination workspace is the SAME as the active one — we want to
  // reload its tabs into this freshly-opened window. SwapToWorkspace's
  // "same workspace = no-op" guard would block us, so call the lower-level
  // pieces directly: open items, close the new-tab boundary tab.
  Workspace* ws = FindWorkspace(active_workspace_id_);
  if (!ws || ws->items.empty()) return;
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) return;

  const int boundary = tab_strip->count();
  bool opened_any = false;
  for (size_t i = 0; i < ws->items.size(); ++i) {
    const auto& item = ws->items[i];
    if (!item.url.is_valid() || item.url.is_empty()) continue;
    NavigateParams params(browser, item.url,
                          ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = !opened_any
                             ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                             : WindowOpenDisposition::NEW_BACKGROUND_TAB;
    Navigate(&params);
    opened_any = true;
  }
  if (!opened_any) return;
  // Close the launch boundary tab (usually the default new-tab page).
  for (int i = boundary - 1; i >= 0; --i) {
    tab_strip->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
  }
  LOG(INFO) << "[Sessionat] Restored " << ws->items.size()
            << " tabs from workspace '" << ws->name << "'";
}

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

bool WorkspaceService::SetWorkspacePinned(const std::string& workspace_id,
                                           bool pinned) {
  Workspace* workspace = FindWorkspace(workspace_id);
  if (!workspace) {
    return false;
  }
  if (workspace->is_pinned == pinned) {
    return true;
  }
  workspace->is_pinned = pinned;
  workspace->modified_at = base::Time::Now();
  SaveWorkspaces();
  NotifyWorkspaceUpdated(*workspace);
  return true;
}

std::vector<Workspace> WorkspaceService::GetOrderedWorkspaces() const {
  // Stable partition: pinned first, others after. Order within each group
  // matches the insertion order from workspaces_.
  std::vector<Workspace> ordered;
  ordered.reserve(workspaces_.size());
  for (const auto& ws : workspaces_) {
    if (ws.is_pinned) ordered.push_back(ws);
  }
  for (const auto& ws : workspaces_) {
    if (!ws.is_pinned) ordered.push_back(ws);
  }
  return ordered;
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
  base::ListValue list;
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

  const base::ListValue& list = pref_service_->GetList(kWorkspacesListPref);
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
  base::ListValue list;
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

// ============ Phase 1.4: workspace-owned tabs ============

void WorkspaceService::SnapshotTabs(const std::string& workspace_id,
                                     BrowserWindowInterface* browser) {
  Workspace* ws = FindWorkspace(workspace_id);
  if (!ws || !browser) {
    return;
  }
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) {
    return;
  }

  std::vector<WorkspaceItem> snapshot;
  snapshot.reserve(tab_strip->count());
  for (int i = 0; i < tab_strip->count(); ++i) {
    content::WebContents* wc = tab_strip->GetWebContentsAt(i);
    if (!wc) {
      continue;
    }
    GURL url = wc->GetLastCommittedURL();
    // Skip transient / placeholder URLs we don't want to "save" as workspace
    // members. Empty URL = about:blank or still-loading; the sessionat-newtab
    // landing isn't a saved tab either.
    if (!url.is_valid() || url.is_empty()) {
      continue;
    }
    if (url.SchemeIs(content::kChromeUIScheme) &&
        (url.host() == chrome::kChromeUISessionatNtpHost ||
         url.host() == chrome::kChromeUISessionatQuickSwitchHost)) {
      continue;
    }
    WorkspaceItem item;
    item.id = GenerateItemId();
    item.url = url;
    item.title = base::UTF16ToUTF8(wc->GetTitle());
    item.created_at = base::Time::Now();
    snapshot.push_back(std::move(item));
  }

  // Defensive: never overwrite a non-empty items list with an empty snapshot.
  // This happens during browser close (tabs already torn down) and at the
  // very start of a session (no tabs opened yet) — both cases would silently
  // wipe the user's workspace.
  if (snapshot.empty() && !ws->items.empty()) {
    return;
  }
  ws->items = std::move(snapshot);
  ws->modified_at = base::Time::Now();
  SaveWorkspaces();
  NotifyWorkspaceUpdated(*ws);
}

void WorkspaceService::SwapToWorkspace(const std::string& dest_id,
                                        BrowserWindowInterface* browser) {
  // Without a window we can't swap tabs — fall back to the prefs-only switch.
  if (!browser || dest_id.empty() || !GetWorkspace(dest_id)) {
    if (!dest_id.empty()) {
      SetActiveWorkspace(dest_id);
    }
    return;
  }
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) {
    SetActiveWorkspace(dest_id);
    return;
  }

  // 1. Capture the current workspace's tabs before we throw them away.
  // Skip when switching to the same workspace (no-op).
  if (!active_workspace_id_.empty() && active_workspace_id_ != dest_id) {
    SnapshotTabs(active_workspace_id_, browser);
  } else if (active_workspace_id_ == dest_id) {
    return;
  }

  // 2. Open the destination workspace's items. Capture the boundary so we
  // know which tabs to close afterwards. Open the first item as foreground
  // (so the window has an active tab while we close old ones); subsequent
  // items as background.
  const int boundary = tab_strip->count();
  Workspace* dest = FindWorkspace(dest_id);
  bool opened_any = false;
  if (dest && !dest->items.empty()) {
    for (size_t i = 0; i < dest->items.size(); ++i) {
      const auto& item = dest->items[i];
      if (!item.url.is_valid() || item.url.is_empty()) {
        continue;
      }
      NavigateParams params(browser, item.url,
                            ui::PAGE_TRANSITION_AUTO_BOOKMARK);
      params.disposition = !opened_any
                               ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                               : WindowOpenDisposition::NEW_BACKGROUND_TAB;
      Navigate(&params);
      opened_any = true;
    }
  }
  // Empty destination workspace: ensure the window stays alive with a single
  // Sessionat NTP tab.
  if (!opened_any) {
    NavigateParams params(browser, GURL(chrome::kChromeUISessionatNtpURL),
                          ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  }

  // 3. Close the tabs that belonged to the previous workspace. Iterate in
  // reverse so index shifts don't matter.
  for (int i = boundary - 1; i >= 0; --i) {
    tab_strip->CloseWebContentsAt(i, TabCloseTypes::CLOSE_NONE);
  }

  // 4. Mark the destination active.
  SetActiveWorkspace(dest_id);
}

}  // namespace sessionat
