// Copyright 2026 Sessionat. All rights reserved.
// Submenu implementation — see header.

#include "chrome/browser/renderer_context_menu/sessionat_page_workspace_submenu.h"

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/common/webui_url_constants.h"
#include "ui/base/window_open_disposition.h"

SessionatPageWorkspaceSubMenu::SessionatPageWorkspaceSubMenu(
    Profile* profile,
    const GURL& page_url,
    const std::string& title)
    : ui::SimpleMenuModel(this),
      profile_(profile),
      page_url_(page_url),
      title_(title) {
  Build();
}

SessionatPageWorkspaceSubMenu::~SessionatPageWorkspaceSubMenu() = default;

// static
bool SessionatPageWorkspaceSubMenu::ShouldShow(Profile* profile,
                                                const GURL& page_url) {
  if (!profile || !page_url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  auto* ws = sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  return ws != nullptr;
}

bool SessionatPageWorkspaceSubMenu::IsCommandIdChecked(int command_id) const {
  return false;
}

bool SessionatPageWorkspaceSubMenu::IsCommandIdEnabled(int command_id) const {
  return true;
}

void SessionatPageWorkspaceSubMenu::ExecuteCommand(int command_id,
                                                    int event_flags) {
  if (command_id == kNewWorkspaceId) {
    auto* ws =
        sessionat::WorkspaceServiceFactory::GetForProfile(profile_);
    if (!ws) return;
    std::string new_id = ws->CreateWorkspace("New Workspace");
    if (!new_id.empty()) {
      AddPageTo(new_id);
    }
    return;
  }
  if (command_id == kPickWorkspaceId) {
    // Too many workspaces to list inline — open the manager so user can pick.
    // (Future: pass the URL+title to chrome://sessionat-quickswitch/ via the
    // fragment so the switcher knows to add-on-pick rather than switch.)
    auto* coll = ProfileBrowserCollection::GetForProfile(profile_);
    BrowserWindowInterface* browser =
        coll ? coll->FindTabbedBrowser() : nullptr;
    if (browser) {
      NavigateParams params(browser,
                            GURL(chrome::kChromeUISessionatWorkspacesURL),
                            ui::PAGE_TRANSITION_AUTO_BOOKMARK);
      params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
      Navigate(&params);
    }
    return;
  }
  const int idx = command_id - kFirstId;
  if (idx >= 0 && static_cast<size_t>(idx) < workspace_ids_.size()) {
    AddPageTo(workspace_ids_[idx]);
  }
}

void SessionatPageWorkspaceSubMenu::Build() {
  auto* ws = sessionat::WorkspaceServiceFactory::GetForProfile(profile_);
  if (!ws) return;

  workspace_ids_.clear();
  const auto& all = ws->GetAllWorkspaces();
  // Past kSubmenuCap workspaces, native menus become unscrollable. Show the
  // first kSubmenuCap and pivot to "Pick workspace…" which opens the manager.
  const size_t shown = std::min(all.size(), kSubmenuCap);
  int cmd = kFirstId;
  for (size_t i = 0; i < shown; ++i) {
    const auto& workspace = all[i];
    workspace_ids_.push_back(workspace.id);
    std::u16string label = base::UTF8ToUTF16(workspace.name);
    if (!workspace.items.empty()) {
      label += u" (";
      label += base::ASCIIToUTF16(std::to_string(workspace.items.size()));
      label += u")";
    }
    AddItem(cmd, label);
    cmd++;
  }
  AddSeparator(ui::NORMAL_SEPARATOR);
  if (all.size() > kSubmenuCap) {
    std::u16string more = u"More (" +
        base::ASCIIToUTF16(std::to_string(all.size() - kSubmenuCap)) +
        u" hidden)…";
    AddItem(kPickWorkspaceId, more);
  }
  AddItem(kNewWorkspaceId, u"+ New Workspace");
}

void SessionatPageWorkspaceSubMenu::AddPageTo(const std::string& workspace_id) {
  auto* ws = sessionat::WorkspaceServiceFactory::GetForProfile(profile_);
  if (!ws) return;
  if (ws->AddItemToWorkspace(workspace_id, page_url_, title_, "")) {
    LOG(INFO) << "[Sessionat] Added page '" << title_ << "' to workspace";
  }
}
