// Copyright 2026 Sessionat. All rights reserved.
// chrome://sessionat-workspaces/ — workspace manager WebUI.

#include "chrome/browser/ui/webui/sessionat_workspaces/sessionat_workspaces_ui.h"

#include "base/compiler_specific.h"
#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/common/webui_url_constants.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/url_data_source.h"
#include "chrome/grit/sessionat_workspaces_resources.h"
#include "chrome/grit/sessionat_workspaces_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/window_open_disposition.h"
#include "ui/webui/webui_util.h"

SessionatWorkspacesHandler::SessionatWorkspacesHandler() = default;
SessionatWorkspacesHandler::~SessionatWorkspacesHandler() = default;

void SessionatWorkspacesHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getWorkspaces",
      base::BindRepeating(&SessionatWorkspacesHandler::HandleGetWorkspaces,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "createWorkspace",
      base::BindRepeating(&SessionatWorkspacesHandler::HandleCreateWorkspace,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "switchWorkspace",
      base::BindRepeating(&SessionatWorkspacesHandler::HandleSwitchWorkspace,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "deleteWorkspace",
      base::BindRepeating(&SessionatWorkspacesHandler::HandleDeleteWorkspace,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "renameWorkspace",
      base::BindRepeating(&SessionatWorkspacesHandler::HandleRenameWorkspace,
                          base::Unretained(this)));
}

void SessionatWorkspacesHandler::SendList() {
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* service = sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  base::ListValue list;
  if (service) {
    const std::string active = service->GetActiveWorkspaceId();
    for (const auto& ws : service->GetOrderedWorkspaces()) {
      base::DictValue d;
      d.Set("id", ws.id);
      d.Set("name", ws.name);
      d.Set("color", ws.color);
      d.Set("icon", ws.icon);
      d.Set("is_active", ws.id == active);
      d.Set("is_pinned", ws.is_pinned);
      base::ListValue items;
      for (const auto& item : ws.items) {
        base::DictValue id;
        id.Set("url", item.url.spec());
        id.Set("title", item.title);
        items.Append(std::move(id));
      }
      d.Set("items", std::move(items));
      list.Append(std::move(d));
    }
  }
  // Direct global function call. FireWebUIListener routes through
  // cr.webUIListenerCallback which requires the ES-module cr.js to be imported;
  // we don't import it on this page, so we invoke the render function directly.
  CallJavascriptFunction("sessionatWorkspacesRender", list);
}

void SessionatWorkspacesHandler::HandleGetWorkspaces(
    const base::ListValue& args) {
  AllowJavascript();
  SendList();
}

void SessionatWorkspacesHandler::HandleCreateWorkspace(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  const std::string name = args[0].GetString();
  if (name.empty()) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* service = sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) return;
  static constexpr const char* kPalette[] = {
      "#F97316", "#FDBA74", "#FB923C", "#EA580C", "#FCA658",
  };
  const size_t idx = service->GetAllWorkspaces().size() %
                     (sizeof(kPalette) / sizeof(kPalette[0]));
  const std::string id =
      service->CreateWorkspace(name, UNSAFE_TODO(kPalette[idx]), "✨");
  if (!id.empty()) service->SetActiveWorkspace(id);
  SendList();
}

void SessionatWorkspacesHandler::HandleSwitchWorkspace(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  const std::string id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* service = sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) return;
  // Phase 1.4: swap the window's tab set instead of just opening saved URLs.
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  service->SwapToWorkspace(id, browser);
  SendList();
}

void SessionatWorkspacesHandler::HandleDeleteWorkspace(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* service = sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) return;
  service->DeleteWorkspace(args[0].GetString());
  SendList();
}

void SessionatWorkspacesHandler::HandleRenameWorkspace(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* service = sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (!service) return;
  service->UpdateWorkspace(args[0].GetString(), args[1].GetString());
  SendList();
}

SessionatWorkspacesUIConfig::SessionatWorkspacesUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUISessionatWorkspacesHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(SessionatWorkspacesUI)

SessionatWorkspacesUI::SessionatWorkspacesUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(),
      chrome::kChromeUISessionatWorkspacesHost);
  webui::SetupWebUIDataSource(
      source, kSessionatWorkspacesResources,
      IDR_SESSIONAT_WORKSPACES_SESSIONAT_WORKSPACES_HTML);
  // No img-src override needed: chrome://favicon2 is in the default whitelist
  // and handles the Google CDN fallback server-side when we pass
  // allowGoogleServerFallback=1.
  // Each chrome:// page that wants to use chrome://favicon2/ URLs must
  // register its own FaviconSource — otherwise the URLs fail at the
  // URLDataSource layer with ERR_INVALID_URL.
  Profile* profile =
      Profile::FromBrowserContext(web_ui->GetWebContents()->GetBrowserContext());
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
  web_ui->AddMessageHandler(std::make_unique<SessionatWorkspacesHandler>());
}

SessionatWorkspacesUI::~SessionatWorkspacesUI() = default;
