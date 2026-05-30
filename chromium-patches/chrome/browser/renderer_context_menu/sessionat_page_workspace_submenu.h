// Copyright 2026 Sessionat. All rights reserved.
// Submenu for the page context menu's "Add page to workspace…" entry.

#ifndef CHROME_BROWSER_RENDERER_CONTEXT_MENU_SESSIONAT_PAGE_WORKSPACE_SUBMENU_H_
#define CHROME_BROWSER_RENDERER_CONTEXT_MENU_SESSIONAT_PAGE_WORKSPACE_SUBMENU_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"

class Profile;

// Submenu listing every Sessionat workspace (+ a "+ New Workspace" entry).
// Clicking a workspace adds the page URL + title to that workspace via
// WorkspaceService::AddItemToWorkspace. Clicking "+ New Workspace" creates
// one on the fly and adds the page to it.
class SessionatPageWorkspaceSubMenu : public ui::SimpleMenuModel,
                                       public ui::SimpleMenuModel::Delegate {
 public:
  SessionatPageWorkspaceSubMenu(Profile* profile,
                                const GURL& page_url,
                                const std::string& title);
  SessionatPageWorkspaceSubMenu(const SessionatPageWorkspaceSubMenu&) = delete;
  SessionatPageWorkspaceSubMenu& operator=(
      const SessionatPageWorkspaceSubMenu&) = delete;
  ~SessionatPageWorkspaceSubMenu() override;

  static bool ShouldShow(Profile* profile, const GURL& page_url);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void Build();
  void AddPageTo(const std::string& workspace_id);

  raw_ptr<Profile> profile_;
  GURL page_url_;
  std::string title_;

  // command_id - kFirstId  →  index into workspace_ids_
  std::vector<std::string> workspace_ids_;

  // Same numeric range used by WorkspaceSubMenuModel for tab moves so the
  // two submenus never collide.
  static constexpr int kFirstId = 7000;
  static constexpr int kNewWorkspaceId = 6999;
  // Shown after kSubmenuCap workspaces — opens the full manager.
  static constexpr int kPickWorkspaceId = 6998;
  // Above this count, the submenu trims and shows "Pick workspace…" instead
  // of every workspace. Native submenus become unscrollable past ~30 items.
  static constexpr size_t kSubmenuCap = 12;
};

#endif  // CHROME_BROWSER_RENDERER_CONTEXT_MENU_SESSIONAT_PAGE_WORKSPACE_SUBMENU_H_
