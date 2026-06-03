// Copyright 2026 Sessionat. All rights reserved.
//
// Embedded MCP (Model Context Protocol) server for Sessionat. Runs an HTTP
// listener on 127.0.0.1 inside the browser process and exposes Sessionat's
// workspace + analytics data to MCP clients (Claude Desktop, etc).
//
// Architecture:
//   - McpService (KeyedService on the UI thread): public surface, owns the
//     dedicated IO thread, holds the tool registry, and dispatches tool
//     calls (because tools touch UI-thread KeyedServices).
//   - ServerCore (lives on the McpService's dedicated IO thread): owns
//     net::TCPServerSocket + net::HttpServer, implements
//     net::HttpServer::Delegate. net::HttpServer's async accept loop needs
//     a base::MessagePumpType::IO thread; the UI thread won't do.
//   - Tools call directly into in-process services (WorkspaceService,
//     VisitAnalyticsService) — no Mojo, no file reads.
//   - Multi-token auth: master discovery token + per-client tokens minted
//     at Connect time. Write tools gated per-token via WriteGrants.

#ifndef CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_H_
#define CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class Profile;

namespace sessionat {

class ClientTokenRegistry;
class WriteGrants;

class McpService : public KeyedService {
 public:
  // Tool callback runs on the UI thread. Async signature: the tool delivers
  // its tools/call "content" payload via the `reply` callback (one-shot).
  // The args dict carries an internal "_ctx" sub-dict with the
  // requesting bearer token and resolved client id (or "_master") for tools
  // that need to gate on identity.
  using ToolCallback = base::RepeatingCallback<void(
      const base::DictValue& args,
      base::OnceCallback<void(base::DictValue)> reply)>;

  struct ToolEntry {
    std::string name;
    std::string description;
    base::DictValue input_schema;
    ToolCallback handler;
  };

  explicit McpService(Profile* profile);
  ~McpService() override;
  McpService(const McpService&) = delete;
  McpService& operator=(const McpService&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Status surface for chrome://sessionat-mcp/.
  bool IsRunning() const { return port_ > 0; }
  int port() const { return port_; }
  // The master discovery token. Used by curl, the WebUI test ping, and the
  // mcp.json discovery file. Stable across restarts (persisted in prefs).
  const std::string& master_token() const { return master_token_; }
  // Backwards-compat shim — old call sites used auth_token().
  const std::string& auth_token() const { return master_token_; }
  base::ListValue GetToolMetadata() const;
  base::FilePath GetDiscoveryFilePath() const;
  Profile* profile() { return profile_; }

  // Per-client token management.
  std::string IssueTokenForClient(ClientConfigManager::Client c);
  void RevokeTokenForClient(ClientConfigManager::Client c);
  std::vector<ClientConfigManager::Client> ConnectedClients() const;
  std::optional<std::string> TokenForClient(
      ClientConfigManager::Client c) const;

  // Per-token write-tool gate (CLAUDE.md hard rule: no auto-grant).
  bool IsWriteAllowedForToken(const std::string& token) const;
  bool IsTokenKnown(const std::string& token) const;
  void SetWriteGrant(const std::string& client_id, bool granted);

  // Returns the new master token. All per-client tokens are wiped, plus all
  // write grants (caller's responsibility to reconnect / re-grant clients).
  std::string RotateMasterToken(
      std::vector<ClientConfigManager::Client>* out_invalidated);

  WriteGrants* write_grants() { return write_grants_.get(); }
  ClientTokenRegistry* token_registry() { return token_registry_.get(); }

  // Called from ServerCore on the IO thread to dispatch a parsed JSON-RPC
  // request. The request dict is augmented with a "_sessionat_ctx" entry
  // (token + client id) before dispatch and stripped before returning.
  void HandleRequestFromIo(
      base::Value request,
      base::OnceCallback<void(base::DictValue)> reply_on_io);

 private:
  class ServerCore;

  void StartServer();
  void RegisterBuiltinTools();
  void OnServerListening(int port);
  void WriteDiscoveryFile();
  void DeleteDiscoveryFile();

  void HandleSingleRequestAsync(
      const base::DictValue& req,
      base::OnceCallback<void(base::DictValue)> reply);
  base::DictValue HandleInitialize(const base::DictValue& params);
  base::DictValue HandleToolsList();
  void HandleToolsCallAsync(
      const base::DictValue& params,
      const base::Value* id,
      base::OnceCallback<void(base::DictValue)> reply);

  static base::DictValue MakeError(const base::Value* id,
                                    int code,
                                    const std::string& message);
  static base::DictValue MakeResult(const base::Value* id,
                                     base::DictValue result);
  static base::DictValue MakeToolsCallErrorContent(const std::string& message);

  // DanglingUntriaged: Profile is destroyed by the KeyedServiceFactory
  // shutdown order before this service's destructor runs.
  raw_ptr<Profile, DanglingUntriaged> profile_;
  std::unique_ptr<base::Thread> io_thread_;
  // Owned by io_thread_; deleted via DeleteSoon when this McpService dies.
  raw_ptr<ServerCore, AcrossTasksDanglingUntriaged> server_core_ = nullptr;

  std::unique_ptr<ClientTokenRegistry> token_registry_;
  std::unique_ptr<WriteGrants> write_grants_;
  std::string master_token_;
  int port_ = 0;
  std::map<std::string, ToolEntry> tools_;

  base::WeakPtrFactory<McpService> weak_factory_{this};
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_H_
