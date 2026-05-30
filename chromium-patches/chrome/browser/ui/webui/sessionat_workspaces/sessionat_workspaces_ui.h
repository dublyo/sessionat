// Copyright 2026 Sessionat. All rights reserved.
// WebUI controller for chrome://sessionat-workspaces/ — workspace manager.

#ifndef CHROME_BROWSER_UI_WEBUI_SESSIONAT_WORKSPACES_SESSIONAT_WORKSPACES_UI_H_
#define CHROME_BROWSER_UI_WEBUI_SESSIONAT_WORKSPACES_SESSIONAT_WORKSPACES_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class SessionatWorkspacesUI;

class SessionatWorkspacesUIConfig
    : public content::DefaultWebUIConfig<SessionatWorkspacesUI> {
 public:
  SessionatWorkspacesUIConfig();
};

class SessionatWorkspacesHandler : public content::WebUIMessageHandler {
 public:
  SessionatWorkspacesHandler();
  ~SessionatWorkspacesHandler() override;
  SessionatWorkspacesHandler(const SessionatWorkspacesHandler&) = delete;
  SessionatWorkspacesHandler& operator=(const SessionatWorkspacesHandler&) =
      delete;

  void RegisterMessages() override;

 private:
  void HandleGetWorkspaces(const base::ListValue& args);
  void HandleCreateWorkspace(const base::ListValue& args);
  void HandleSwitchWorkspace(const base::ListValue& args);
  void HandleDeleteWorkspace(const base::ListValue& args);
  void HandleRenameWorkspace(const base::ListValue& args);
  void SendList();
};

class SessionatWorkspacesUI : public content::WebUIController {
 public:
  explicit SessionatWorkspacesUI(content::WebUI* web_ui);
  ~SessionatWorkspacesUI() override;
  SessionatWorkspacesUI(const SessionatWorkspacesUI&) = delete;
  SessionatWorkspacesUI& operator=(const SessionatWorkspacesUI&) = delete;
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_SESSIONAT_WORKSPACES_SESSIONAT_WORKSPACES_UI_H_
