// Copyright 2025 Sessionat. All rights reserved.
// WebUI controller for Sessionat New Tab Page.

#include "chrome/browser/ui/webui/sessionat_ntp/sessionat_ntp_ui.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "base/compiler_specific.h"
#include "base/functional/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/session_history_service.h"
#include "chrome/browser/sessionat/session_history_service_factory.h"
// update_notification_service removed in v2.0 — Sparkle 2 replaces it
// (chrome://settings/help → version_updater_mac.mm).
#include "chrome/browser/sessionat/visit_analytics_service.h"
#include "chrome/browser/sessionat/visit_analytics_service_factory.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/url_data_source.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_live_tab_context.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/sessionat_ntp_resources.h"
#include "chrome/grit/sessionat_ntp_resources_map.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_types.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/webui/webui_util.h"

namespace {

// Maximum number of sessions to return.
constexpr size_t kMaxSessions = 20;

base::DictValue TabToDict(const sessions::tab_restore::Tab& tab) {
  base::DictValue dict;

  dict.Set("sessionId", tab.id.id());
  dict.Set("pinned", tab.pinned);
  dict.Set("timestamp",
           static_cast<double>(tab.timestamp.InMillisecondsSinceUnixEpoch()));

  if (!tab.navigations.empty()) {
    int index = tab.normalized_navigation_index();
    // normalized_navigation_index() is guaranteed in-range by SessionTab.
    const auto& nav = UNSAFE_TODO(tab.navigations[index]);
    dict.Set("url", nav.virtual_url().spec());
    dict.Set("title", nav.title());
    dict.Set("faviconUrl", nav.favicon_url().spec());
  }

  return dict;
}

}  // namespace

// SessionatNtpHandler implementation
SessionatNtpHandler::SessionatNtpHandler() = default;
SessionatNtpHandler::~SessionatNtpHandler() = default;

void SessionatNtpHandler::RegisterMessages() {
  // Recently closed tabs (TabRestoreService)
  web_ui()->RegisterMessageCallback(
      "getRecentSessions",
      base::BindRepeating(&SessionatNtpHandler::HandleGetRecentSessions,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "restoreSession",
      base::BindRepeating(&SessionatNtpHandler::HandleRestoreSession,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openUrl",
      base::BindRepeating(&SessionatNtpHandler::HandleOpenUrl,
                          base::Unretained(this)));

  // Session History (persistent saved sessions)
  web_ui()->RegisterMessageCallback(
      "getSessionHistory",
      base::BindRepeating(&SessionatNtpHandler::HandleGetSessionHistory,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "saveCurrentSession",
      base::BindRepeating(&SessionatNtpHandler::HandleSaveCurrentSession,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "deleteSavedSession",
      base::BindRepeating(&SessionatNtpHandler::HandleDeleteSavedSession,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "restoreSavedSession",
      base::BindRepeating(&SessionatNtpHandler::HandleRestoreSavedSession,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "updateSessionHistorySettings",
      base::BindRepeating(
          &SessionatNtpHandler::HandleUpdateSessionHistorySettings,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "checkAutoSave",
      base::BindRepeating(&SessionatNtpHandler::HandleCheckAutoSave,
                          base::Unretained(this)));

  // Update checker removed in v2.0 — Sparkle 2 at chrome://settings/help.
  // If older NTP JS still calls chrome.send("checkForUpdates") it will get
  // an "Unrecognized message" log line that's harmless.

  // Workspaces (v2 Phase 1)
  web_ui()->RegisterMessageCallback(
      "getWorkspaces",
      base::BindRepeating(&SessionatNtpHandler::HandleGetWorkspaces,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "createWorkspace",
      base::BindRepeating(&SessionatNtpHandler::HandleCreateWorkspace,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "switchWorkspace",
      base::BindRepeating(&SessionatNtpHandler::HandleSwitchWorkspace,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "deleteWorkspace",
      base::BindRepeating(&SessionatNtpHandler::HandleDeleteWorkspace,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "togglePinWorkspace",
      base::BindRepeating(&SessionatNtpHandler::HandleTogglePinWorkspace,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getTopSitesToday",
      base::BindRepeating(&SessionatNtpHandler::HandleGetTopSitesToday,
                          base::Unretained(this)));
}

void SessionatNtpHandler::HandleGetRecentSessions(
    const base::ListValue& args) {
  AllowJavascript();

  base::ListValue sessions;

  Profile* profile = Profile::FromWebUI(web_ui());
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(profile);

  if (service) {
    service->LoadTabsFromLastSession();

    const auto& entries = service->entries();
    size_t count = 0;

    for (const auto& entry : entries) {
      if (count >= kMaxSessions) {
        break;
      }

      base::DictValue session;
      session.Set("id", entry->id.id());
      session.Set("timestamp", static_cast<double>(
          entry->timestamp.InMillisecondsSinceUnixEpoch()));

      base::ListValue tabs;

      switch (entry->type) {
        case sessions::tab_restore::TAB: {
          const auto* tab =
              static_cast<const sessions::tab_restore::Tab*>(entry.get());
          session.Set("type", "tab");

          if (!tab->navigations.empty()) {
            int index = tab->normalized_navigation_index();
            session.Set("name", UNSAFE_TODO(tab->navigations[index]).title());
          }

          tabs.Append(TabToDict(*tab));
          break;
        }

        case sessions::tab_restore::WINDOW: {
          const auto* window =
              static_cast<const sessions::tab_restore::Window*>(entry.get());
          session.Set("type", "window");
          session.Set("name", "Window (" + std::to_string(window->tabs.size()) +
                              " tabs)");

          for (const auto& tab : window->tabs) {
            tabs.Append(TabToDict(*tab));
          }
          break;
        }

        case sessions::tab_restore::GROUP: {
          const auto* group =
              static_cast<const sessions::tab_restore::Group*>(entry.get());
          session.Set("type", "group");
          std::string name = base::UTF16ToUTF8(group->visual_data.title());
          if (name.empty()) {
            name = "Tab Group (" + std::to_string(group->tabs.size()) +
                   " tabs)";
          }
          session.Set("name", name);

          for (const auto& tab : group->tabs) {
            tabs.Append(TabToDict(*tab));
          }
          break;
        }

        case sessions::tab_restore::SPLIT:
          // M150 added split-view tabs. Sessionat doesn't surface them on the
          // NTP card yet — skip silently (the session is still in the history,
          // just absent from the NTP recent-sessions strip).
          break;
      }

      if (!tabs.empty()) {
        session.Set("tabs", std::move(tabs));
        sessions.Append(std::move(session));
        count++;
      }
    }
  }

  FireWebUIListener("sessions-updated", sessions);
}

void SessionatNtpHandler::HandleRestoreSession(const base::ListValue& args) {
  if (args.size() < 1 || !args[0].is_int()) {
    return;
  }

  int session_id = args[0].GetInt();

  Profile* profile = Profile::FromWebUI(web_ui());
  sessions::TabRestoreService* service =
      TabRestoreServiceFactory::GetForProfile(profile);

  if (!service) {
    return;
  }

  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  if (!browser) {
    return;
  }

  service->RestoreEntryById(browser->GetFeatures().live_tab_context(),
                            SessionID::FromSerializedValue(session_id),
                            WindowOpenDisposition::NEW_FOREGROUND_TAB);
}

void SessionatNtpHandler::HandleOpenUrl(const base::ListValue& args) {
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }

  GURL url(args[0].GetString());
  if (!url.is_valid()) {
    return;
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  NavigateParams params(profile, url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  Navigate(&params);
}

// Session History handlers
void SessionatNtpHandler::HandleGetSessionHistory(
    const base::ListValue& args) {
  AllowJavascript();
  SendSessionHistoryToJS();
}

void SessionatNtpHandler::HandleSaveCurrentSession(
    const base::ListValue& args) {
  AllowJavascript();

  std::string custom_name;
  if (args.size() >= 1 && args[0].is_string()) {
    custom_name = args[0].GetString();
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::SessionHistoryService* service =
      sessionat::SessionHistoryServiceFactory::GetForProfile(profile);

  if (service) {
    service->SaveCurrentSession(custom_name);
    SendSessionHistoryToJS();
  }
}

void SessionatNtpHandler::HandleDeleteSavedSession(
    const base::ListValue& args) {
  AllowJavascript();

  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }

  std::string session_id = args[0].GetString();

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::SessionHistoryService* service =
      sessionat::SessionHistoryServiceFactory::GetForProfile(profile);

  if (service) {
    service->DeleteSession(session_id);
    SendSessionHistoryToJS();
  }
}

void SessionatNtpHandler::HandleRestoreSavedSession(
    const base::ListValue& args) {
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }

  std::string session_id = args[0].GetString();

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::SessionHistoryService* service =
      sessionat::SessionHistoryServiceFactory::GetForProfile(profile);

  if (!service) {
    return;
  }

  // Find the session and open all its tabs
  for (const auto& session : service->GetAllSessions()) {
    if (session.id == session_id) {
      for (const auto& tab : session.tabs) {
        const std::string* url = tab.FindString("url");
        if (url) {
          NavigateParams params(profile, GURL(*url), ui::PAGE_TRANSITION_LINK);
          params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
          Navigate(&params);
        }
      }
      break;
    }
  }
}

void SessionatNtpHandler::HandleUpdateSessionHistorySettings(
    const base::ListValue& args) {
  if (args.size() < 3) {
    return;
  }

  bool auto_save_enabled = args[0].is_bool() ? args[0].GetBool() : true;
  int min_tabs = args[1].is_int() ? args[1].GetInt() : 5;
  // save_on_close is captured for the v2.1 BrowserList-observer auto-save
  // path. Currently unused — mark explicitly so is_official_build doesn't
  // flag -Wunused-variable.
  [[maybe_unused]] bool save_on_close =
      args[2].is_bool() ? args[2].GetBool() : true;

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::SessionHistoryService* service =
      sessionat::SessionHistoryServiceFactory::GetForProfile(profile);

  if (service) {
    service->SetAutoSaveEnabled(auto_save_enabled);
    service->SetMinTabsForAutoSave(min_tabs);
    // save_on_close is handled by BrowserList observer in the service
  }
}

void SessionatNtpHandler::HandleCheckAutoSave(const base::ListValue& args) {
  if (args.size() < 1 || !args[0].is_int()) {
    return;
  }

  int min_tabs = args[0].GetInt();

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::SessionHistoryService* service =
      sessionat::SessionHistoryServiceFactory::GetForProfile(profile);

  if (service && service->ShouldAutoSave(min_tabs)) {
    service->SaveCurrentSession("");  // Auto-save with no custom name
    SendSessionHistoryToJS();
  }
}

void SessionatNtpHandler::SendSessionHistoryToJS() {
  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::SessionHistoryService* service =
      sessionat::SessionHistoryServiceFactory::GetForProfile(profile);

  base::ListValue sessions_list;

  if (service) {
    for (const auto& session : service->GetAllSessions()) {
      base::DictValue session_dict;
      session_dict.Set("id", session.id);
      session_dict.Set("name", session.name);
      session_dict.Set("customName", session.custom_name);
      session_dict.Set("timestamp", static_cast<double>(session.timestamp));
      session_dict.Set("isPinned", session.is_pinned);
      session_dict.Set("isAutoSave", session.custom_name.empty());

      base::ListValue tabs_list;
      for (const auto& tab : session.tabs) {
        tabs_list.Append(tab.Clone());
      }
      session_dict.Set("tabs", std::move(tabs_list));

      sessions_list.Append(std::move(session_dict));
    }
  }

  FireWebUIListener("session-history-updated", sessions_list);
}

// HandleCheckForUpdates removed in v2.0 — Sparkle 2 owns the update flow now
// via chrome://settings/help. See sessionat-update-plan.md.

void SessionatNtpHandler::HandleGetWorkspaces(const base::ListValue& args) {
  AllowJavascript();

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::WorkspaceService* service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);

  base::ListValue workspaces_list;
  if (service) {
    const std::string active_id = service->GetActiveWorkspaceId();
    // Pinned-first ordering — same shape as Cmd+1..9 binds against, so
    // the visual order on the NTP matches the keyboard order.
    for (const auto& ws : service->GetOrderedWorkspaces()) {
      base::DictValue dict;
      dict.Set("id", ws.id);
      dict.Set("name", ws.name);
      dict.Set("color", ws.color);
      dict.Set("icon", ws.icon);
      dict.Set("tab_count", static_cast<int>(ws.items.size()));
      dict.Set("is_active", ws.id == active_id);
      dict.Set("is_pinned", ws.is_pinned);
      workspaces_list.Append(std::move(dict));
    }
  }

  FireWebUIListener("workspaces-updated", workspaces_list);
}

void SessionatNtpHandler::HandleCreateWorkspace(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string name = args[0].GetString();
  if (name.empty()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::WorkspaceService* service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  // Cycle through the Sessionat orange palette so successive new workspaces
  // visually differ. Index is just the current workspace count modulo palette.
  static constexpr const char* kPalette[] = {
      "#F97316", "#FDBA74", "#FB923C", "#EA580C", "#FCA658", "#9A3412",
  };
  const size_t idx = service->GetAllWorkspaces().size() %
                     (sizeof(kPalette) / sizeof(kPalette[0]));
  const std::string new_id =
      service->CreateWorkspace(name, UNSAFE_TODO(kPalette[idx]), "✨");
  // Auto-switch to the new workspace so the orange "active" highlight lands
  // on it immediately — matches the mental model "I made it, take me there".
  if (!new_id.empty()) {
    service->SetActiveWorkspace(new_id);
  }
  // Re-emit so the NTP refreshes immediately.
  base::ListValue empty;
  HandleGetWorkspaces(empty);
}

void SessionatNtpHandler::HandleDeleteWorkspace(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string workspace_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::WorkspaceService* service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  service->DeleteWorkspace(workspace_id);
  base::ListValue empty;
  HandleGetWorkspaces(empty);
}

void SessionatNtpHandler::HandleGetTopSitesToday(
    const base::ListValue& args) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::VisitAnalyticsService* service =
      sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile);
  base::ListValue list;
  if (service) {
    for (const auto& v :
         service->GetTopVisitsForDay(base::Time::Now(), /*n=*/5)) {
      base::DictValue d;
      d.Set("url", v.url.spec());
      d.Set("host", v.host);
      d.Set("title", v.title);
      list.Append(std::move(d));
    }
  }
  // Use CallJavascriptFunction (not FireWebUIListener) — the NTP is a plain
  // <script src> page; cr.js isn't loaded so addWebUIListener would no-op.
  CallJavascriptFunction("sessionatNtpRenderTopSitesToday", list);
}

void SessionatNtpHandler::HandleTogglePinWorkspace(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string workspace_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::WorkspaceService* service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  const sessionat::Workspace* ws = service->GetWorkspace(workspace_id);
  if (!ws) return;
  service->SetWorkspacePinned(workspace_id, !ws->is_pinned);
  base::ListValue empty;
  HandleGetWorkspaces(empty);
}

void SessionatNtpHandler::HandleSwitchWorkspace(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string workspace_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::WorkspaceService* service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }
  // Phase 1.4: workspaces now OWN their tab set. SwapToWorkspace snapshots
  // the current tabs into the outgoing workspace, then opens the destination
  // workspace's tabs and closes the old ones — atomic-ish from the user's
  // point of view (window stays alive throughout because we open the first
  // destination tab as foreground before closing).
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  service->SwapToWorkspace(workspace_id, browser);

  base::ListValue empty;
  HandleGetWorkspaces(empty);
}

// SessionatNtpUIConfig implementation
SessionatNtpUIConfig::SessionatNtpUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUISessionatNtpHost) {}

// SessionatNtpUI implementation
WEB_UI_CONTROLLER_TYPE_IMPL(SessionatNtpUI)

SessionatNtpUI::SessionatNtpUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(),
      chrome::kChromeUISessionatNtpHost);

  webui::SetupWebUIDataSource(
      source, kSessionatNtpResources, IDR_SESSIONAT_NTP_SESSIONAT_NTP_HTML);

  // Allow the trusted types policy for innerHTML usage
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types sessionat-ntp;");

  // Allow loading images from any source (for favicons)
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc,
      "img-src * data: blob: 'self';");

  // chrome://favicon2 only works from a chrome:// page if that profile has
  // registered a FaviconSource — without this, every <img src="chrome://favicon2/..">
  // on this page fails with ERR_INVALID_URL.
  Profile* profile = Profile::FromWebUI(web_ui);
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));

  // Add the message handler
  web_ui->AddMessageHandler(std::make_unique<SessionatNtpHandler>());
}

SessionatNtpUI::~SessionatNtpUI() = default;
