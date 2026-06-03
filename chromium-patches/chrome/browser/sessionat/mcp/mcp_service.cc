// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/mcp_service.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/mcp/client_token_registry.h"
#include "chrome/browser/sessionat/mcp/mcp_tools.h"
#include "chrome/browser/sessionat/mcp/write_grants.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace sessionat {

namespace {

constexpr char kLoopbackAddress[] = "127.0.0.1";
constexpr int kEphemeralPort = 0;
constexpr int kBacklog = 10;

constexpr char kMcpUrlPath[] = "/mcp";

// Standard JSON-RPC 2.0 error codes. The currently-unreferenced ones are
// [[maybe_unused]] so -Wunused-const-variable doesn't fail under
// is_official_build.
[[maybe_unused]] constexpr int kErrParseError = -32700;
constexpr int kErrInvalidRequest = -32600;
constexpr int kErrMethodNotFound = -32601;
[[maybe_unused]] constexpr int kErrInternalError = -32603;
// Sessionat-specific: write-tool first-use approval needed.
constexpr int kErrWriteRequiresApproval = -32010;

constexpr char kMcpEnabledPref[] = "sessionat.mcp.enabled";
// Internal hop key used to thread {token, client_id} from the IO-layer auth
// check into the UI-thread tool dispatch. Stripped before the request is
// surfaced to handlers.
constexpr char kRequestCtxKey[] = "_sessionat_ctx";

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("sessionat_mcp_server", R"(
      semantics {
        sender: "Sessionat MCP server"
        description:
          "Sessionat exposes its local workspace and visit-analytics data "
          "to MCP (Model Context Protocol) clients running on the same "
          "machine, over a loopback HTTP listener on 127.0.0.1 with a "
          "random ephemeral port. Bearer-token authenticated."
        trigger:
          "Started automatically when a regular Sessionat profile is "
          "loaded. No outbound network traffic — listen-only on loopback."
        data:
          "JSON-RPC requests from local MCP clients. The server only reads "
          "data; write operations require per-client first-use approval."
        destination: LOCAL
        internal {
          contacts {
            email: "info@meditechus.com"
          }
        }
        user_data {
          type: NONE
        }
        last_reviewed: "2026-05-25"
      }
      policy {
        cookies_allowed: NO
        setting: "Toggle via chrome://sessionat-mcp/."
        policy_exception_justification:
          "Local loopback only; no enterprise policy needed."
      })");

}  // namespace

// =============================================================================
// ServerCore — lives on the McpService's dedicated IO thread.
// =============================================================================
class McpService::ServerCore : public net::HttpServer::Delegate {
 public:
  ServerCore(scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
             base::WeakPtr<McpService> service)
      : ui_task_runner_(std::move(ui_task_runner)),
        service_(std::move(service)) {}

  ~ServerCore() override = default;

  base::WeakPtr<ServerCore> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

  void StartOnIo() {
    auto server_socket = std::make_unique<net::TCPServerSocket>(
        /*net_log=*/nullptr, net::NetLogSource());
    int rv = server_socket->ListenWithAddressAndPort(
        kLoopbackAddress, kEphemeralPort, kBacklog);
    if (rv != net::OK) {
      LOG(ERROR) << "[Sessionat MCP] could not listen on loopback: "
                 << net::ErrorToString(rv);
      ui_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&McpService::OnServerListening,
                                     service_, /*port=*/0));
      return;
    }
    net::IPEndPoint endpoint;
    if (server_socket->GetLocalAddress(&endpoint) != net::OK) {
      LOG(ERROR) << "[Sessionat MCP] could not read local address";
      ui_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&McpService::OnServerListening,
                                     service_, /*port=*/0));
      return;
    }
    const int port = endpoint.port();
    http_server_ =
        std::make_unique<net::HttpServer>(std::move(server_socket), this);
    LOG(INFO) << "[Sessionat MCP] listening on http://127.0.0.1:" << port;
    ui_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&McpService::OnServerListening, service_, port));
  }

  // net::HttpServer::Delegate:
  void OnConnect(int /*connection_id*/) override {}
  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override;
  void OnWebSocketRequest(int, const net::HttpServerRequestInfo&) override {}
  void OnWebSocketMessage(int, std::string) override {}
  void OnClose(int /*connection_id*/) override {}

  void SendResponseOnIo(int connection_id,
                         net::HttpStatusCode status,
                         const std::string& body,
                         const std::string& mime) {
    if (!http_server_) return;
    http_server_->Send(connection_id, status, body, mime,
                       kTrafficAnnotation);
  }

  void SendNoContent(int connection_id) {
    if (!http_server_) return;
    http_server_->Send(connection_id, net::HTTP_NO_CONTENT, "",
                       "application/json", kTrafficAnnotation);
  }

  void Send404(int connection_id) {
    if (!http_server_) return;
    http_server_->Send404(connection_id, kTrafficAnnotation);
  }

 private:
  scoped_refptr<base::SequencedTaskRunner> ui_task_runner_;
  base::WeakPtr<McpService> service_;
  std::unique_ptr<net::HttpServer> http_server_;
  base::WeakPtrFactory<ServerCore> weak_factory_{this};
};

void McpService::ServerCore::OnHttpRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info) {
  // Health/info GET — always public.
  if (info.method == "GET" &&
      (info.path == "/" || info.path == "/healthz")) {
    base::DictValue body;
    body.Set("name", "sessionat-mcp");
    body.Set("version", "0.9.0");
    body.Set("transport", "streamable-http");
    std::string text;
    base::JSONWriter::Write(body, &text);
    SendResponseOnIo(connection_id, net::HTTP_OK, text, "application/json");
    return;
  }

  if (info.method != "POST" || info.path != kMcpUrlPath) {
    Send404(connection_id);
    return;
  }

  // Auth check: we extract the bearer token here on the IO thread and pass
  // it through to the UI thread along with the parsed request body. The
  // UI thread does the actual token-validity lookup (the master token + the
  // per-client token registry live in McpService).
  auto auth_it = info.headers.find("authorization");
  if (auth_it == info.headers.end()) {
    SendResponseOnIo(connection_id, net::HTTP_UNAUTHORIZED,
                      R"({"error":"unauthorized"})", "application/json");
    return;
  }
  const std::string& header = auth_it->second;
  static constexpr char kBearer[] = "Bearer ";
  if (!base::StartsWith(header, kBearer)) {
    SendResponseOnIo(connection_id, net::HTTP_UNAUTHORIZED,
                      R"({"error":"unauthorized"})", "application/json");
    return;
  }
  std::string token = header.substr(sizeof(kBearer) - 1);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(info.data, base::JSON_PARSE_RFC);
  if (!parsed) {
    SendResponseOnIo(connection_id, net::HTTP_OK,
                      R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})",
                      "application/json");
    return;
  }

  // Stash the token under our internal context key on the parsed request.
  // McpService strips it before tool dispatch.
  if (parsed->is_dict()) {
    base::DictValue ctx;
    ctx.Set("token", token);
    parsed->GetDict().Set(kRequestCtxKey, std::move(ctx));
  }

  auto reply_on_io = base::BindOnce(
      [](base::WeakPtr<ServerCore> self, int conn,
         base::DictValue response) {
        if (!self) return;
        if (response.empty()) {
          self->SendNoContent(conn);
          return;
        }
        // Authoritative reject path for unknown tokens — McpService surfaces
        // this via a sentinel "error.code = -32099". Map it to a 401.
        if (const base::DictValue* err = response.FindDict("error")) {
          if (err->FindInt("code") == -32099) {
            self->SendResponseOnIo(conn, net::HTTP_UNAUTHORIZED,
                                     R"({"error":"unauthorized"})",
                                     "application/json");
            return;
          }
        }
        std::string body;
        base::JSONWriter::Write(response, &body);
        self->SendResponseOnIo(conn, net::HTTP_OK, body, "application/json");
      },
      AsWeakPtr(), connection_id);

  ui_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&McpService::HandleRequestFromIo, service_,
                     std::move(*parsed), std::move(reply_on_io)));
}

// =============================================================================
// McpService — public, lives on the UI thread.
// =============================================================================

McpService::McpService(Profile* profile) : profile_(profile) {
  RegisterBuiltinTools();
  if (profile_->IsOffTheRecord()) {
    return;
  }
  token_registry_ =
      std::make_unique<ClientTokenRegistry>(profile_->GetPrefs());
  write_grants_ = std::make_unique<WriteGrants>(profile_->GetPrefs());
  if (!profile_->GetPrefs()->GetBoolean(kMcpEnabledPref)) {
    LOG(INFO) << "[Sessionat MCP] disabled by user pref; not starting server";
    return;
  }
  StartServer();
}

McpService::~McpService() {
  if (server_core_ && io_thread_) {
    ServerCore* raw = server_core_;
    server_core_ = nullptr;
    io_thread_->task_runner()->DeleteSoon(FROM_HERE, raw);
  }
  DeleteDiscoveryFile();
}

// static
void McpService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kMcpEnabledPref, true);
  // New pref schema for v0.9. See per_client_grants_plan in the design doc.
  registry->RegisterStringPref("sessionat.mcp.master_token", std::string());
  registry->RegisterDictionaryPref("sessionat.mcp.client_tokens");
  registry->RegisterDictionaryPref("sessionat.mcp.write_grants");
  // Migration shim: the v0.8 boolean. Kept registered for two releases so
  // existing profiles don't error on load.
  registry->RegisterBooleanPref("sessionat.mcp.write_enabled", false);
}

std::string McpService::IssueTokenForClient(ClientConfigManager::Client c) {
  if (!token_registry_) return std::string();
  return token_registry_->IssueToken(c);
}

void McpService::RevokeTokenForClient(ClientConfigManager::Client c) {
  if (!token_registry_) return;
  auto tok = token_registry_->TokenForClient(c);
  token_registry_->RevokeToken(c);
  if (tok && write_grants_) write_grants_->Revoke(*tok);
}

std::vector<ClientConfigManager::Client> McpService::ConnectedClients() const {
  if (!token_registry_) return {};
  return token_registry_->ConnectedClients();
}

std::optional<std::string> McpService::TokenForClient(
    ClientConfigManager::Client c) const {
  if (!token_registry_) return std::nullopt;
  return token_registry_->TokenForClient(c);
}

bool McpService::IsWriteAllowedForToken(const std::string& token) const {
  if (!write_grants_) return false;
  return write_grants_->HasGrantForToken(token);
}

bool McpService::IsTokenKnown(const std::string& token) const {
  if (token.empty()) return false;
  if (token == master_token_) return true;
  if (token_registry_ && token_registry_->LookupClientForToken(token)) {
    return true;
  }
  return false;
}

void McpService::SetWriteGrant(const std::string& client_id, bool granted) {
  if (!write_grants_) return;
  std::string tok;
  if (client_id == kMasterClientId) {
    tok = master_token_;
  } else {
    auto c = ClientConfigManager::ClientFromId(client_id);
    if (!c) return;
    auto maybe = token_registry_ ? token_registry_->TokenForClient(*c)
                                  : std::nullopt;
    if (!maybe) return;
    tok = *maybe;
  }
  if (tok.empty()) return;
  if (granted) {
    write_grants_->Grant(tok, client_id);
  } else {
    write_grants_->Revoke(tok);
  }
}

std::string McpService::RotateMasterToken(
    std::vector<ClientConfigManager::Client>* out_invalidated) {
  if (!token_registry_) return std::string();
  master_token_ = token_registry_->RotateMasterToken(out_invalidated);
  if (write_grants_) {
    // Drop master grant; per-client tokens are gone so their grants are dead
    // anyway, but we wipe their entries too for tidiness.
    if (auto* prefs = profile_->GetPrefs()) {
      prefs->ClearPref("sessionat.mcp.write_grants");
      (void)prefs;
    }
  }
  if (port_ > 0) WriteDiscoveryFile();
  return master_token_;
}

void McpService::StartServer() {
  master_token_ = token_registry_->GetOrCreateMasterToken();
  io_thread_ = std::make_unique<base::Thread>("Sessionat MCP IO");
  base::Thread::Options options(base::MessagePumpType::IO, /*size=*/0);
  if (!io_thread_->StartWithOptions(std::move(options))) {
    LOG(ERROR) << "[Sessionat MCP] could not start IO thread";
    io_thread_.reset();
    return;
  }
  auto* core = new ServerCore(base::SequencedTaskRunner::GetCurrentDefault(),
                                weak_factory_.GetWeakPtr());
  server_core_ = core;
  io_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&ServerCore::StartOnIo,
                                 base::Unretained(server_core_)));
}

void McpService::OnServerListening(int port) {
  port_ = port;
  if (port_ > 0) {
    WriteDiscoveryFile();
  }
}

void McpService::RegisterBuiltinTools() {
  RegisterSessionatTools(this, profile_, &tools_);
}

base::FilePath McpService::GetDiscoveryFilePath() const {
  return profile_->GetPath().AppendASCII("mcp.json");
}

void McpService::WriteDiscoveryFile() {
  base::DictValue d;
  d.Set("port", port_);
  d.Set("token", master_token_);
  d.Set("transport", "http");
  d.Set("url", base::StringPrintf("http://127.0.0.1:%d%s", port_, kMcpUrlPath));
  std::string serialized;
  if (!base::JSONWriter::WriteWithOptions(
          d, base::JSONWriter::OPTIONS_PRETTY_PRINT, &serialized)) {
    return;
  }
  base::FilePath path = GetDiscoveryFilePath();
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](const base::FilePath& p, const std::string& body) {
            base::WriteFile(p, body);
          },
          path, std::move(serialized)));
}

void McpService::DeleteDiscoveryFile() {
  base::FilePath path = GetDiscoveryFilePath();
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(
          [](const base::FilePath& p) { base::DeleteFile(p); }, path));
}

base::ListValue McpService::GetToolMetadata() const {
  base::ListValue list;
  for (const auto& [name, entry] : tools_) {
    base::DictValue d;
    d.Set("name", entry.name);
    d.Set("description", entry.description);
    d.Set("inputSchema", entry.input_schema.Clone());
    list.Append(std::move(d));
  }
  return list;
}

// ---- Cross-thread request handling ----

void McpService::HandleRequestFromIo(
    base::Value request,
    base::OnceCallback<void(base::DictValue)> reply_on_io) {
  auto* io_runner = io_thread_ ? io_thread_->task_runner().get() : nullptr;
  if (!io_runner) {
    return;
  }
  scoped_refptr<base::SingleThreadTaskRunner> io_runner_ref =
      io_thread_->task_runner();
  auto reply_to_io = base::BindOnce(
      [](scoped_refptr<base::SingleThreadTaskRunner> io_runner,
         base::OnceCallback<void(base::DictValue)> reply,
         base::DictValue response) {
        if (!io_runner) return;
        io_runner->PostTask(
            FROM_HERE,
            base::BindOnce(std::move(reply), std::move(response)));
      },
      io_runner_ref, std::move(reply_on_io));

  // Pull the token out of the internal ctx that ServerCore stuffed in.
  std::string token;
  std::string client_id = kMasterClientId;
  if (request.is_dict()) {
    if (base::DictValue* ctx = request.GetDict().FindDict(kRequestCtxKey)) {
      if (const std::string* t = ctx->FindString("token")) token = *t;
    }
  }
  // Resolve.
  if (!token.empty() && token != master_token_) {
    auto maybe = token_registry_
                      ? token_registry_->LookupClientForToken(token)
                      : std::nullopt;
    if (!maybe) {
      // Unknown token. ServerCore maps -32099 to HTTP 401.
      base::DictValue env;
      env.Set("jsonrpc", "2.0");
      base::DictValue err;
      err.Set("code", -32099);
      err.Set("message", "Unknown bearer token");
      env.Set("error", std::move(err));
      std::move(reply_to_io).Run(std::move(env));
      return;
    }
    client_id = ClientConfigManager::ClientId(*maybe);
  } else if (token.empty()) {
    base::DictValue env;
    env.Set("jsonrpc", "2.0");
    base::DictValue err;
    err.Set("code", -32099);
    err.Set("message", "Missing bearer token");
    env.Set("error", std::move(err));
    std::move(reply_to_io).Run(std::move(env));
    return;
  }

  // Re-write _sessionat_ctx with the resolved client id so downstream tools
  // can read it as args._ctx.client_id without doing the lookup again.
  if (request.is_dict()) {
    base::DictValue ctx;
    ctx.Set("token", token);
    ctx.Set("client_id", client_id);
    request.GetDict().Set(kRequestCtxKey, std::move(ctx));
  }

  if (request.is_dict()) {
    HandleSingleRequestAsync(request.GetDict(), std::move(reply_to_io));
    return;
  }
  if (request.is_list()) {
    base::DictValue response;
    base::ListValue batch_out;
    for (const base::Value& v : request.GetList()) {
      if (!v.is_dict()) continue;
      base::DictValue r;
      HandleSingleRequestAsync(
          v.GetDict(),
          base::BindOnce(
              [](base::DictValue* out, base::DictValue resp) {
                *out = std::move(resp);
              },
              &r));
      if (!r.empty()) batch_out.Append(std::move(r));
    }
    if (!batch_out.empty()) {
      response.Set("jsonrpc", "2.0");
      response.Set("batch", std::move(batch_out));
    }
    std::move(reply_to_io).Run(std::move(response));
    return;
  }
  std::move(reply_to_io)
      .Run(MakeError(nullptr, kErrInvalidRequest, "Invalid request"));
}

void McpService::HandleSingleRequestAsync(
    const base::DictValue& req,
    base::OnceCallback<void(base::DictValue)> reply) {
  const std::string* method = req.FindString("method");
  if (!method) {
    std::move(reply).Run(
        MakeError(req.Find("id"), kErrInvalidRequest, "Missing 'method'"));
    return;
  }
  const base::Value* id = req.Find("id");
  const bool is_notification = (id == nullptr);

  if (*method == "initialize") {
    const base::DictValue* params = req.FindDict("params");
    base::DictValue empty;
    std::move(reply).Run(
        MakeResult(id, HandleInitialize(params ? *params : empty)));
    return;
  }
  if (*method == "notifications/initialized" || *method == "initialized") {
    std::move(reply).Run(base::DictValue());
    return;
  }
  if (*method == "ping") {
    std::move(reply).Run(MakeResult(id, base::DictValue()));
    return;
  }
  if (*method == "tools/list") {
    std::move(reply).Run(MakeResult(id, HandleToolsList()));
    return;
  }
  if (*method == "tools/call") {
    const base::DictValue* params = req.FindDict("params");
    base::DictValue empty;
    // Pull the resolved ctx out of the request envelope and pass it through
    // to HandleToolsCallAsync via the params clone (we copy because the
    // const-ref doesn't let us mutate).
    base::DictValue params_with_ctx;
    if (params) {
      params_with_ctx = params->Clone();
    } else {
      params_with_ctx = empty.Clone();
    }
    if (const base::DictValue* ctx = req.FindDict(kRequestCtxKey)) {
      params_with_ctx.Set(kRequestCtxKey, ctx->Clone());
    }
    HandleToolsCallAsync(params_with_ctx, id, std::move(reply));
    return;
  }
  if (is_notification) {
    std::move(reply).Run(base::DictValue());
    return;
  }
  std::move(reply).Run(
      MakeError(id, kErrMethodNotFound, "Method not found: " + *method));
}

base::DictValue McpService::HandleInitialize(const base::DictValue&) {
  base::DictValue result;
  result.Set("protocolVersion", "2025-03-26");
  base::DictValue capabilities;
  capabilities.Set("tools", base::DictValue());
  result.Set("capabilities", std::move(capabilities));
  base::DictValue server_info;
  server_info.Set("name", "sessionat-mcp");
  server_info.Set("version", "0.9.0");
  result.Set("serverInfo", std::move(server_info));
  return result;
}

base::DictValue McpService::HandleToolsList() {
  base::DictValue result;
  result.Set("tools", GetToolMetadata());
  return result;
}

void McpService::HandleToolsCallAsync(
    const base::DictValue& params,
    const base::Value* id,
    base::OnceCallback<void(base::DictValue)> reply) {
  base::Value id_clone = id ? id->Clone() : base::Value();

  const std::string* name = params.FindString("name");
  if (!name) {
    std::move(reply).Run(
        MakeResult(id, MakeToolsCallErrorContent("Missing 'name'")));
    return;
  }
  auto it = tools_.find(*name);
  if (it == tools_.end()) {
    std::move(reply).Run(
        MakeResult(id, MakeToolsCallErrorContent("Unknown tool: " + *name)));
    return;
  }
  const base::DictValue* args = params.FindDict("arguments");
  base::DictValue final_args = args ? args->Clone() : base::DictValue();
  // Forward the request context to the tool so it can gate on the caller's
  // identity without re-parsing the auth header.
  if (const base::DictValue* ctx = params.FindDict(kRequestCtxKey)) {
    final_args.Set("_ctx", ctx->Clone());
  }

  auto envelope_cb = base::BindOnce(
      [](base::Value id_clone,
         base::OnceCallback<void(base::DictValue)> outer_reply,
         base::DictValue tool_result) {
        base::Value* id_ptr = id_clone.is_none() ? nullptr : &id_clone;
        std::move(outer_reply)
            .Run(McpService::MakeResult(id_ptr, std::move(tool_result)));
      },
      std::move(id_clone), std::move(reply));

  it->second.handler.Run(final_args, std::move(envelope_cb));
}

// static
base::DictValue McpService::MakeToolsCallErrorContent(
    const std::string& message) {
  base::DictValue result;
  result.Set("isError", true);
  base::ListValue content;
  base::DictValue text;
  text.Set("type", "text");
  text.Set("text", message);
  content.Append(std::move(text));
  result.Set("content", std::move(content));
  return result;
}

// static
base::DictValue McpService::MakeError(const base::Value* id,
                                       int code,
                                       const std::string& message) {
  base::DictValue envelope;
  envelope.Set("jsonrpc", "2.0");
  if (id) envelope.Set("id", id->Clone());
  base::DictValue err;
  err.Set("code", code);
  err.Set("message", message);
  envelope.Set("error", std::move(err));
  return envelope;
}

// static
base::DictValue McpService::MakeResult(const base::Value* id,
                                        base::DictValue result) {
  base::DictValue envelope;
  envelope.Set("jsonrpc", "2.0");
  if (id) envelope.Set("id", id->Clone());
  envelope.Set("result", std::move(result));
  return envelope;
}

// Avoid -Wunused-const-variable on configs that elide the error path entirely.
[[maybe_unused]] constexpr int kErrWriteRequiresApprovalRef =
    kErrWriteRequiresApproval;

}  // namespace sessionat
