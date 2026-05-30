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
//   - v0.2 read-only. Write tools land in a follow-up with per-client
//     first-use approval (CLAUDE.md).

#ifndef CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_H_
#define CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_H_

#include <map>
#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"

class PrefRegistrySimple;
class Profile;

namespace sessionat {

class McpService : public KeyedService {
 public:
  // Tool callback runs on the UI thread. Async signature: the tool delivers
  // its tools/call "content" payload via the `reply` callback (one-shot).
  // Sync tools wrap the synchronous result with an immediate reply.Run(...);
  // async tools (page_text, click, type) chain through a renderer round-trip
  // before invoking reply.
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
  const std::string& auth_token() const { return auth_token_; }
  base::ListValue GetToolMetadata() const;
  base::FilePath GetDiscoveryFilePath() const;
  Profile* profile() { return profile_; }

  // Write-tools gate (CLAUDE.md hard rule: MCP write tools never auto-grant).
  // Default false. User flips it via the chrome://sessionat-mcp/ toggle.
  // Persisted in the user's Sessionat profile prefs.
  bool IsWriteEnabled() const;
  void SetWriteEnabled(bool enabled);

  // Called from ServerCore on the IO thread to dispatch a parsed JSON-RPC
  // request. Bounces back to the IO thread with the response Dict.
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

  // Async — tools/call dispatch can take a renderer round-trip, so the whole
  // request handling is funneled through OnceCallbacks. `reply` always gets
  // called exactly once with the JSON-RPC envelope (or an empty DictValue
  // for notifications, which the IO thread translates to 204 No Content).
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

  std::string auth_token_;
  int port_ = 0;
  std::map<std::string, ToolEntry> tools_;

  base::WeakPtrFactory<McpService> weak_factory_{this};
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_H_
