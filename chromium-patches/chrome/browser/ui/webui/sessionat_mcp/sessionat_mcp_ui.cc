// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/ui/webui/sessionat_mcp/sessionat_mcp_ui.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"
#include "chrome/browser/sessionat/mcp/mcp_service.h"
#include "chrome/browser/sessionat/mcp/mcp_service_factory.h"
#include "chrome/browser/sessionat/mcp/write_grants.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/sessionat_mcp_resources.h"
#include "chrome/grit/sessionat_mcp_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/webui/webui_util.h"

namespace {

const char* StatusName(sessionat::ClientConfigManager::Status s) {
  using S = sessionat::ClientConfigManager::Status;
  switch (s) {
    case S::kNotInstalled:     return "not_installed";
    case S::kInstalledNoEntry: return "installed_no_entry";
    case S::kConnected:        return "connected";
    case S::kStale:            return "stale";
    case S::kError:            return "error";
  }
  return "error";
}

base::DictValue StatusToDict(
    const sessionat::ClientConfigManager::StatusResult& r,
    bool has_grant) {
  base::DictValue d;
  d.Set("client_id", sessionat::ClientConfigManager::ClientId(r.client));
  d.Set("status", StatusName(r.status));
  d.Set("config_path", r.config_path.AsUTF8Unsafe());
  d.Set("error", r.error_message);
  d.Set("requires_manual_snippet", r.requires_manual_snippet);
  d.Set("manual_snippet", r.manual_snippet);
  d.Set("has_write_grant", has_grant);
  return d;
}

}  // namespace

SessionatMcpHandler::SessionatMcpHandler() = default;
SessionatMcpHandler::~SessionatMcpHandler() = default;

void SessionatMcpHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getStatus",
      base::BindRepeating(&SessionatMcpHandler::HandleGetStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "connectClient",
      base::BindRepeating(&SessionatMcpHandler::HandleConnectClient,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "disconnectClient",
      base::BindRepeating(&SessionatMcpHandler::HandleDisconnectClient,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getClientStatus",
      base::BindRepeating(&SessionatMcpHandler::HandleGetClientStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getAllClientStatuses",
      base::BindRepeating(&SessionatMcpHandler::HandleGetAllClientStatuses,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "revealClientConfig",
      base::BindRepeating(&SessionatMcpHandler::HandleRevealClientConfig,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setClientWriteGrant",
      base::BindRepeating(&SessionatMcpHandler::HandleSetClientWriteGrant,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "testConnection",
      base::BindRepeating(&SessionatMcpHandler::HandleTestConnection,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "rotateToken",
      base::BindRepeating(&SessionatMcpHandler::HandleRotateToken,
                          base::Unretained(this)));
}

void SessionatMcpHandler::HandleGetStatus(const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  base::DictValue payload;
  if (svc && svc->IsRunning()) {
    payload.Set("running", true);
    payload.Set("port", svc->port());
    payload.Set("master_token", svc->master_token());
    payload.Set("discovery_path",
                svc->GetDiscoveryFilePath().AsUTF8Unsafe());
    payload.Set("tools", svc->GetToolMetadata());
    payload.Set("master_has_write_grant",
                svc->IsWriteAllowedForToken(svc->master_token()));
  } else {
    payload.Set("running", false);
    if (svc) {
      payload.Set("discovery_path",
                  svc->GetDiscoveryFilePath().AsUTF8Unsafe());
      payload.Set("tools", svc->GetToolMetadata());
    }
  }
  CallJavascriptFunction("sessionatMcpRenderStatus", payload);
}

void SessionatMcpHandler::HandleConnectClient(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  const std::string client_id = args[0].GetString();
  auto client = sessionat::ClientConfigManager::ClientFromId(client_id);
  if (!client) {
    base::DictValue payload;
    payload.Set("ok", false);
    payload.Set("error", "unknown client_id");
    payload.Set("client_id", client_id);
    CallJavascriptFunction("sessionatMcpRenderClientWriteResult", payload);
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc || !svc->IsRunning()) {
    base::DictValue payload;
    payload.Set("ok", false);
    payload.Set("error", "MCP server is not running.");
    payload.Set("client_id", client_id);
    CallJavascriptFunction("sessionatMcpRenderClientWriteResult", payload);
    return;
  }
  const std::string token = svc->IssueTokenForClient(*client);
  sessionat::ClientConfigManager::Connect(
      *client, svc->port(), token,
      base::BindOnce(
          [](base::WeakPtr<SessionatMcpHandler> self, std::string cid,
             bool ok, std::string error) {
            if (!self) return;
            base::DictValue payload;
            payload.Set("ok", ok);
            payload.Set("error", error);
            payload.Set("client_id", cid);
            self->CallJavascriptFunction(
                "sessionatMcpRenderClientWriteResult", payload);
          },
          weak_factory_.GetWeakPtr(), client_id));
}

void SessionatMcpHandler::HandleDisconnectClient(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  const std::string client_id = args[0].GetString();
  auto client = sessionat::ClientConfigManager::ClientFromId(client_id);
  if (!client) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (svc) svc->RevokeTokenForClient(*client);
  sessionat::ClientConfigManager::Disconnect(
      *client,
      base::BindOnce(
          [](base::WeakPtr<SessionatMcpHandler> self, std::string cid,
             bool ok, std::string error) {
            if (!self) return;
            base::DictValue payload;
            payload.Set("ok", ok);
            payload.Set("error", error);
            payload.Set("client_id", cid);
            self->CallJavascriptFunction(
                "sessionatMcpRenderClientWriteResult", payload);
          },
          weak_factory_.GetWeakPtr(), client_id));
}

void SessionatMcpHandler::HandleGetClientStatus(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  const std::string client_id = args[0].GetString();
  auto client = sessionat::ClientConfigManager::ClientFromId(client_id);
  if (!client) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc) return;
  // For status purposes, use the per-client token if one exists, else the
  // master — so existing curl-flavored connections still register as kConnected.
  auto tok = svc->TokenForClient(*client);
  const std::string token = tok ? *tok : svc->master_token();
  sessionat::ClientConfigManager::GetStatus(
      *client, svc->port(), token,
      base::BindOnce(
          [](base::WeakPtr<SessionatMcpHandler> self, std::string token,
             sessionat::ClientConfigManager::StatusResult r) {
            if (!self) return;
            Profile* profile = Profile::FromWebUI(self->web_ui());
            auto* svc =
                sessionat::McpServiceFactory::GetForProfile(profile);
            bool grant = svc && svc->IsWriteAllowedForToken(token);
            self->CallJavascriptFunction(
                "sessionatMcpRenderClientStatus", StatusToDict(r, grant));
          },
          weak_factory_.GetWeakPtr(), token));
}

void SessionatMcpHandler::HandleGetAllClientStatuses(
    const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc) return;
  const std::string master = svc->master_token();
  sessionat::ClientConfigManager::GetAllStatuses(
      svc->port(), master,
      base::BindOnce(
          [](base::WeakPtr<SessionatMcpHandler> self,
             std::vector<sessionat::ClientConfigManager::StatusResult>
                 results) {
            if (!self) return;
            Profile* profile = Profile::FromWebUI(self->web_ui());
            auto* svc =
                sessionat::McpServiceFactory::GetForProfile(profile);
            base::DictValue payload;
            base::ListValue list;
            for (const auto& r : results) {
              std::optional<std::string> tok =
                  svc ? svc->TokenForClient(r.client) : std::nullopt;
              const std::string token = tok ? *tok : (svc ? svc->master_token()
                                                             : std::string());
              const bool grant =
                  svc && svc->IsWriteAllowedForToken(token);
              list.Append(StatusToDict(r, grant));
            }
            payload.Set("clients", std::move(list));
            self->CallJavascriptFunction(
                "sessionatMcpRenderAllClientStatuses", payload);
          },
          weak_factory_.GetWeakPtr()));
}

void SessionatMcpHandler::HandleRevealClientConfig(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) return;
  const std::string client_id = args[0].GetString();
  auto client = sessionat::ClientConfigManager::ClientFromId(client_id);
  base::DictValue payload;
  payload.Set("client_id", client_id);
  if (!client) {
    payload.Set("ok", false);
    payload.Set("error", "unknown client_id");
    CallJavascriptFunction("sessionatMcpRenderRevealResult", payload);
    return;
  }
  sessionat::ClientConfigManager::RevealConfig(*client);
  payload.Set("ok", true);
  payload.Set("error", std::string());
  CallJavascriptFunction("sessionatMcpRenderRevealResult", payload);
}

void SessionatMcpHandler::HandleSetClientWriteGrant(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) return;
  const std::string client_id = args[0].GetString();
  const bool granted = args[1].GetBool();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc) return;
  svc->SetWriteGrant(client_id, granted);
  base::DictValue payload;
  payload.Set("ok", true);
  payload.Set("client_id", client_id);
  payload.Set("granted", granted);
  CallJavascriptFunction("sessionatMcpRenderGrantResult", payload);
}

void SessionatMcpHandler::HandleTestConnection(
    const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  base::DictValue payload;
  if (!svc || !svc->IsRunning()) {
    payload.Set("ok", false);
    payload.Set("error", "MCP server is not running.");
    CallJavascriptFunction("sessionatMcpRenderTestResult", payload);
    return;
  }
  // We surface a synthetic OK here — the actual loopback probe is the
  // WebUI's job (fetch() against http://127.0.0.1:<port>/healthz). Doing
  // it from the C++ side would require a SimpleURLLoader plumbed through
  // the system network context, deferred to a follow-up to avoid the
  // cross-scheme renderer trap (see CLAUDE.md webui_no_cross_scheme).
  payload.Set("ok", true);
  payload.Set("port", svc->port());
  payload.Set("master_token", svc->master_token());
  CallJavascriptFunction("sessionatMcpRenderTestResult", payload);
}

void SessionatMcpHandler::HandleRotateToken(const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!svc) return;
  std::vector<sessionat::ClientConfigManager::Client> invalidated;
  const std::string new_master = svc->RotateMasterToken(&invalidated);
  base::DictValue payload;
  payload.Set("ok", true);
  payload.Set("new_master_token", new_master);
  base::ListValue inv_list;
  for (auto c : invalidated) {
    inv_list.Append(sessionat::ClientConfigManager::ClientId(c));
  }
  payload.Set("invalidated_clients", std::move(inv_list));
  CallJavascriptFunction("sessionatMcpRenderRotateResult", payload);
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
