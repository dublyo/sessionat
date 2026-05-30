// Copyright 2026 Sessionat. All rights reserved.
// WebUI controller for chrome://sessionat-quickswitch/ — Cmd+K spotlight.

#ifndef CHROME_BROWSER_UI_WEBUI_SESSIONAT_QUICKSWITCH_SESSIONAT_QUICKSWITCH_UI_H_
#define CHROME_BROWSER_UI_WEBUI_SESSIONAT_QUICKSWITCH_SESSIONAT_QUICKSWITCH_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class SessionatQuickSwitchUI;

class SessionatQuickSwitchUIConfig
    : public content::DefaultWebUIConfig<SessionatQuickSwitchUI> {
 public:
  SessionatQuickSwitchUIConfig();
};

class SessionatQuickSwitchHandler : public content::WebUIMessageHandler {
 public:
  SessionatQuickSwitchHandler();
  ~SessionatQuickSwitchHandler() override;
  SessionatQuickSwitchHandler(const SessionatQuickSwitchHandler&) = delete;
  SessionatQuickSwitchHandler& operator=(
      const SessionatQuickSwitchHandler&) = delete;

  void RegisterMessages() override;

 private:
  void HandleGetWorkspaces(const base::ListValue& args);
  void HandleSwitchWorkspaceAndClose(const base::ListValue& args);
  void HandleOpenPageAndClose(const base::ListValue& args);
  void SendList();
  // Closes the popup window hosting this WebUI.
  void ClosePopup();
};

class SessionatQuickSwitchUI : public content::WebUIController {
 public:
  explicit SessionatQuickSwitchUI(content::WebUI* web_ui);
  ~SessionatQuickSwitchUI() override;
  SessionatQuickSwitchUI(const SessionatQuickSwitchUI&) = delete;
  SessionatQuickSwitchUI& operator=(const SessionatQuickSwitchUI&) = delete;
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_SESSIONAT_QUICKSWITCH_SESSIONAT_QUICKSWITCH_UI_H_
