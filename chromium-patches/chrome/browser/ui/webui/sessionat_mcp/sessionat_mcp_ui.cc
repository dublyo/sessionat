// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/ui/webui/sessionat_mcp/sessionat_mcp_ui.h"

#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/mcp/claude_config_manager.h"
#include "chrome/browser/sessionat/mcp/mcp_service.h"
#include "chrome/browser/sessionat/mcp/mcp_service_factory.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/sessionat_mcp_resources.h"
#include "chrome/grit/sessionat_mcp_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/webui/webui_util.h"

SessionatMcpHandler::SessionatMcpHandler() = default;
SessionatMcpHandler::~SessionatMcpHandler() = default;

void SessionatMcpHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getStatus",
      base::BindRepeating(&SessionatMcpHandler::HandleGetStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setWriteEnabled",
      base::BindRepeating(&SessionatMcpHandler::HandleSetWriteEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getClaudeStatus",
      base::BindRepeating(&SessionatMcpHandler::HandleGetClaudeStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "connectClaude",
      base::BindRepeating(&SessionatMcpHandler::HandleConnectClaude,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "disconnectClaude",
      base::BindRepeating(&SessionatMcpHandler::HandleDisconnectClaude,
                          base::Unretained(this)));
}

namespace {
const char* StatusName(sessionat::ClaudeConfigManager::Status s) {
  using S = sessionat::ClaudeConfigManager::Status;
  switch (s) {
    case S::kClaudeNotInstalled: return "not_installed";
    case S::kInstalledNoEntry:   return "installed_no_entry";
    case S::kConnected:          return "connected";
    case S::kStale:              return "stale";
    case S::kError:              return "error";
  }
  return "error";
}
}  // namespace

void SessionatMcpHandler::HandleGetClaudeStatus(
    const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc || !svc->IsRunning()) {
    base::DictValue payload;
    payload.Set("status", "server_not_running");
    payload.Set("config_path",
                sessionat::ClaudeConfigManager::ResolveConfigPath()
                    .AsUTF8Unsafe());
    CallJavascriptFunction("sessionatMcpRenderClaudeStatus", payload);
    return;
  }
  sessionat::ClaudeConfigManager::GetStatus(
      svc->port(), svc->auth_token(),
      base::BindOnce(
          [](base::WeakPtr<SessionatMcpHandler> self,
             sessionat::ClaudeConfigManager::StatusResult r) {
            if (!self) return;
            base::DictValue payload;
            payload.Set("status", StatusName(r.status));
            payload.Set("config_path", r.config_path.AsUTF8Unsafe());
            if (!r.error_message.empty()) {
              payload.Set("error", r.error_message);
            }
            self->CallJavascriptFunction(
                "sessionatMcpRenderClaudeStatus", payload);
          },
          weak_factory_.GetWeakPtr()));
}

void SessionatMcpHandler::HandleConnectClaude(
    const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc || !svc->IsRunning()) {
    base::DictValue err;
    err.Set("ok", false);
    err.Set("error", "MCP server is not running. Enable it first.");
    CallJavascriptFunction("sessionatMcpRenderClaudeWriteResult", err);
    return;
  }
  sessionat::ClaudeConfigManager::Connect(
      svc->port(), svc->auth_token(),
      base::BindOnce(
          [](base::WeakPtr<SessionatMcpHandler> self, bool ok,
             std::string error) {
            if (!self) return;
            base::DictValue payload;
            payload.Set("ok", ok);
            payload.Set("error", error);
            self->CallJavascriptFunction(
                "sessionatMcpRenderClaudeWriteResult", payload);
            // Re-fetch status so the UI reflects the new state.
            base::ListValue empty;
            self->HandleGetClaudeStatus(empty);
          },
          weak_factory_.GetWeakPtr()));
}

void SessionatMcpHandler::HandleDisconnectClaude(
    const base::ListValue& /*args*/) {
  AllowJavascript();
  sessionat::ClaudeConfigManager::Disconnect(base::BindOnce(
      [](base::WeakPtr<SessionatMcpHandler> self, bool ok,
         std::string error) {
        if (!self) return;
        base::DictValue payload;
        payload.Set("ok", ok);
        payload.Set("error", error);
        self->CallJavascriptFunction(
            "sessionatMcpRenderClaudeWriteResult", payload);
        base::ListValue empty;
        self->HandleGetClaudeStatus(empty);
      },
      weak_factory_.GetWeakPtr()));
}

void SessionatMcpHandler::HandleGetStatus(const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  base::DictValue payload;
  if (svc && svc->IsRunning()) {
    payload.Set("running", true);
    payload.Set("port", svc->port());
    payload.Set("token", svc->auth_token());
    payload.Set("discovery_path",
                svc->GetDiscoveryFilePath().AsUTF8Unsafe());
    payload.Set("tools", svc->GetToolMetadata());
    payload.Set("write_enabled", svc->IsWriteEnabled());
  } else {
    payload.Set("running", false);
    if (svc) {
      payload.Set("discovery_path",
                  svc->GetDiscoveryFilePath().AsUTF8Unsafe());
      payload.Set("tools", svc->GetToolMetadata());
      payload.Set("write_enabled", svc->IsWriteEnabled());
    }
  }
  CallJavascriptFunction("sessionatMcpRenderStatus", payload);
}

void SessionatMcpHandler::HandleSetWriteEnabled(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_bool()) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc) return;
  svc->SetWriteEnabled(args[0].GetBool());
  base::ListValue empty;
  HandleGetStatus(empty);
}

SessionatMcpUIConfig::SessionatMcpUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUISessionatMcpHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(SessionatMcpUI)

SessionatMcpUI::SessionatMcpUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(),
      chrome::kChromeUISessionatMcpHost);
  webui::SetupWebUIDataSource(
      source, kSessionatMcpResources,
      IDR_SESSIONAT_MCP_SESSIONAT_MCP_HTML);
  web_ui->AddMessageHandler(std::make_unique<SessionatMcpHandler>());
}

SessionatMcpUI::~SessionatMcpUI() = default;
