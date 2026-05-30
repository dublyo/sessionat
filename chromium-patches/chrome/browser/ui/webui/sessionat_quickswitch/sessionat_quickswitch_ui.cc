// Copyright 2026 Sessionat. All rights reserved.
// chrome://sessionat-quickswitch/ — Cmd+K spotlight WebUI.

#include "chrome/browser/ui/webui/sessionat_quickswitch/sessionat_quickswitch_ui.h"

#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/common/webui_url_constants.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/url_data_source.h"
#include "chrome/grit/sessionat_quickswitch_resources.h"
#include "chrome/grit/sessionat_quickswitch_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/window_open_disposition.h"
#include "ui/webui/webui_util.h"

SessionatQuickSwitchHandler::SessionatQuickSwitchHandler() = default;
SessionatQuickSwitchHandler::~SessionatQuickSwitchHandler() = default;

void SessionatQuickSwitchHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getWorkspaces",
      base::BindRepeating(&SessionatQuickSwitchHandler::HandleGetWorkspaces,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "switchWorkspaceAndClose",
      base::BindRepeating(
          &SessionatQuickSwitchHandler::HandleSwitchWorkspaceAndClose,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openPageAndClose",
      base::BindRepeating(
          &SessionatQuickSwitchHandler::HandleOpenPageAndClose,
          base::Unretained(this)));
}

void SessionatQuickSwitchHandler::SendList() {
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
  CallJavascriptFunction("sessionatQuickSwitchRender", list);
}

void SessionatQuickSwitchHandler::HandleGetWorkspaces(
    const base::ListValue& args) {
  AllowJavascript();
  SendList();
}

// Finds the most-recently-active tabbed (TYPE_NORMAL) browser window for
// this profile — that's where workspace switches and opened pages should
// land. The popup hosting this WebUI is itself a Browser (TYPE_POPUP) but
// FindTabbedBrowser() filters those out automatically.
namespace {
BrowserWindowInterface* FindMainBrowserForProfile(Profile* profile) {
  auto* coll = ProfileBrowserCollection::GetForProfile(profile);
  return coll ? coll->FindTabbedBrowser() : nullptr;
}
}  // namespace

void SessionatQuickSwitchHandler::HandleSwitchWorkspaceAndClose(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  const std::string id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* service = sessionat::WorkspaceServiceFactory::GetForProfile(profile);
  if (service) {
    // Phase 1.4: swap tabs in the main window, not the popup.
    BrowserWindowInterface* target = FindMainBrowserForProfile(profile);
    service->SwapToWorkspace(id, target);
  }
  ClosePopup();
}

void SessionatQuickSwitchHandler::HandleOpenPageAndClose(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string url_str = args[0].GetString();
  const bool background = args.size() >= 2 && args[1].is_int() &&
                          args[1].GetInt() == 1;
  GURL url(url_str);
  if (!url.is_valid()) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  BrowserWindowInterface* target = FindMainBrowserForProfile(profile);
  if (target) {
    NavigateParams params(target, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = background
                             ? WindowOpenDisposition::NEW_BACKGROUND_TAB
                             : WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  }
  ClosePopup();
}

void SessionatQuickSwitchHandler::ClosePopup() {
  // Closing the popup window directly via the Browser is safer than relying
  // on window.close() in JS (which only works for renderer-initiated popups).
  Browser* popup = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  if (popup) {
    popup->window()->Close();
  }
}

SessionatQuickSwitchUIConfig::SessionatQuickSwitchUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUISessionatQuickSwitchHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(SessionatQuickSwitchUI)

SessionatQuickSwitchUI::SessionatQuickSwitchUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(),
      chrome::kChromeUISessionatQuickSwitchHost);
  webui::SetupWebUIDataSource(
      source, kSessionatQuickswitchResources,
      IDR_SESSIONAT_QUICKSWITCH_SESSIONAT_QUICKSWITCH_HTML);
  // chrome://favicon2 + allowGoogleServerFallback=1 handles the Google
  // CDN fetch server-side. Register FaviconSource for this profile so the
  // favicon URLs resolve from this page.
  Profile* profile =
      Profile::FromBrowserContext(web_ui->GetWebContents()->GetBrowserContext());
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));
  web_ui->AddMessageHandler(std::make_unique<SessionatQuickSwitchHandler>());
}

SessionatQuickSwitchUI::~SessionatQuickSwitchUI() = default;
