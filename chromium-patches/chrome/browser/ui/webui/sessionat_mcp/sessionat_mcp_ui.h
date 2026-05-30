// Copyright 2026 Sessionat. All rights reserved.

#ifndef CHROME_BROWSER_UI_WEBUI_SESSIONAT_MCP_SESSIONAT_MCP_UI_H_
#define CHROME_BROWSER_UI_WEBUI_SESSIONAT_MCP_SESSIONAT_MCP_UI_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class SessionatMcpUI;

class SessionatMcpUIConfig
    : public content::DefaultWebUIConfig<SessionatMcpUI> {
 public:
  SessionatMcpUIConfig();
};

class SessionatMcpHandler : public content::WebUIMessageHandler {
 public:
  SessionatMcpHandler();
  ~SessionatMcpHandler() override;
  SessionatMcpHandler(const SessionatMcpHandler&) = delete;
  SessionatMcpHandler& operator=(const SessionatMcpHandler&) = delete;
  void RegisterMessages() override;

 private:
  void HandleGetStatus(const base::ListValue& args);
  void HandleSetWriteEnabled(const base::ListValue& args);

  // Claude Desktop integration.
  void HandleGetClaudeStatus(const base::ListValue& args);
  void HandleConnectClaude(const base::ListValue& args);
  void HandleDisconnectClaude(const base::ListValue& args);

  base::WeakPtrFactory<SessionatMcpHandler> weak_factory_{this};
};

class SessionatMcpUI : public content::WebUIController {
 public:
  explicit SessionatMcpUI(content::WebUI* web_ui);
  ~SessionatMcpUI() override;
  SessionatMcpUI(const SessionatMcpUI&) = delete;
  SessionatMcpUI& operator=(const SessionatMcpUI&) = delete;
  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_SESSIONAT_MCP_SESSIONAT_MCP_UI_H_
