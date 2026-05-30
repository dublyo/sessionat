// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/mcp_service.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/mcp/mcp_tools.h"
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

// Standard JSON-RPC 2.0 error codes. Kept as a complete set even when
// individual call sites don't reference every code yet — marking the
// currently-unreferenced ones [[maybe_unused]] avoids -Wunused-const-variable
// under is_official_build without dropping documentation value.
[[maybe_unused]] constexpr int kErrParseError = -32700;
constexpr int kErrInvalidRequest = -32600;
constexpr int kErrMethodNotFound = -32601;
[[maybe_unused]] constexpr int kErrInternalError = -32603;

constexpr char kMcpEnabledPref[] = "sessionat.mcp.enabled";
// Write tools (open_url, create_workspace, etc.) require this to be true.
// Default false — user enables explicitly via chrome://sessionat-mcp/.
constexpr char kMcpWriteEnabledPref[] = "sessionat.mcp.write_enabled";

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

std::string GenerateAuthToken() {
  uint8_t bytes[32];
  base::RandBytes(bytes);
  std::string out;
  out.reserve(64);
  for (uint8_t b : bytes) out += base::StringPrintf("%02x", b);
  return out;
}

}  // namespace

// =============================================================================
// ServerCore — lives on the McpService's dedicated IO thread.
// =============================================================================
class McpService::ServerCore : public net::HttpServer::Delegate {
 public:
  ServerCore(scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
             base::WeakPtr<McpService> service,
             std::string auth_token)
      : ui_task_runner_(std::move(ui_task_runner)),
        service_(std::move(service)),
        auth_token_(std::move(auth_token)) {}

  ~ServerCore() override = default;

  base::WeakPtr<ServerCore> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Initial bind + listen, then report back to the UI thread.
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

  // Send response on the IO thread.
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
  std::string auth_token_;
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
    body.Set("version", "0.8.0");
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

  // Bearer-token auth.
  auto auth_it = info.headers.find("authorization");
  const std::string expected = "Bearer " + auth_token_;
  if (auth_it == info.headers.end() || auth_it->second != expected) {
    SendResponseOnIo(connection_id, net::HTTP_UNAUTHORIZED,
                      R"({"error":"unauthorized"})", "application/json");
    return;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(info.data, base::JSON_PARSE_RFC);
  if (!parsed) {
    SendResponseOnIo(connection_id, net::HTTP_OK,
                      R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})",
                      "application/json");
    return;
  }

  // Bounce to UI thread for tool dispatch. The reply lambda PostTasks back
  // here with the response Dict.
  auto reply_on_io = base::BindOnce(
      [](base::WeakPtr<ServerCore> self, int conn,
         base::DictValue response) {
        if (!self) return;
        if (response.empty()) {
          self->SendNoContent(conn);
          return;
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
  if (!profile_->GetPrefs()->GetBoolean(kMcpEnabledPref)) {
    LOG(INFO) << "[Sessionat MCP] disabled by user pref; not starting server";
    return;
  }
  StartServer();
}

McpService::~McpService() {
  if (server_core_ && io_thread_) {
    // Hand the core over to the IO thread for destruction.
    ServerCore* raw = server_core_;
    server_core_ = nullptr;
    io_thread_->task_runner()->DeleteSoon(FROM_HERE, raw);
  }
  DeleteDiscoveryFile();
}

// static
void McpService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(kMcpEnabledPref, true);
  registry->RegisterBooleanPref(kMcpWriteEnabledPref, false);
}

bool McpService::IsWriteEnabled() const {
  return profile_ && profile_->GetPrefs()->GetBoolean(kMcpWriteEnabledPref);
}

void McpService::SetWriteEnabled(bool enabled) {
  if (profile_) {
    profile_->GetPrefs()->SetBoolean(kMcpWriteEnabledPref, enabled);
  }
}

void McpService::StartServer() {
  auth_token_ = GenerateAuthToken();
  io_thread_ = std::make_unique<base::Thread>("Sessionat MCP IO");
  base::Thread::Options options(base::MessagePumpType::IO, /*size=*/0);
  if (!io_thread_->StartWithOptions(std::move(options))) {
    LOG(ERROR) << "[Sessionat MCP] could not start IO thread";
    io_thread_.reset();
    auth_token_.clear();
    return;
  }
  // Construct ServerCore on the IO thread.
  auto* core = new ServerCore(base::SequencedTaskRunner::GetCurrentDefault(),
                                weak_factory_.GetWeakPtr(), auth_token_);
  server_core_ = core;
  io_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&ServerCore::StartOnIo,
                                 base::Unretained(server_core_)));
}

void McpService::OnServerListening(int port) {
  port_ = port;
  if (port_ > 0) {
    WriteDiscoveryFile();
  } else {
    auth_token_.clear();
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
  d.Set("token", auth_token_);
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
  // We're on the UI thread now. We funnel through async dispatch and post
  // the final response back to the IO thread.
  auto* io_runner = io_thread_ ? io_thread_->task_runner().get() : nullptr;
  if (!io_runner) {
    // IO thread is gone (shutdown). Drop the reply.
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

  if (request.is_dict()) {
    HandleSingleRequestAsync(request.GetDict(), std::move(reply_to_io));
    return;
  }
  if (request.is_list()) {
    // Sync-only batch path. Real MCP clients almost never batch
    // initialize/tools/list, and we don't support batched async tool calls.
    base::DictValue response;
    base::ListValue batch_out;
    for (const base::Value& v : request.GetList()) {
      if (!v.is_dict()) continue;
      // Synthesize a sync envelope by capturing the reply inline.
      base::DictValue r;
      HandleSingleRequestAsync(
          v.GetDict(),
          base::BindOnce(
              [](base::DictValue* out, base::DictValue resp) {
                *out = std::move(resp);
              },
              &r));
      // If the tool was sync (the common case for batched methods like
      // initialize/tools/list/ping), the callback ran inline above. If it was
      // async, `r` stays empty — batch path doesn't support async tools.
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
    HandleToolsCallAsync(params ? *params : empty, id, std::move(reply));
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
  server_info.Set("version", "0.8.0");
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
  // Capture id by clone so we can use it after the renderer round-trip.
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
  base::DictValue empty;

  // The tool delivers its tools/call content via inner_reply. We wrap it in
  // the JSON-RPC envelope before sending back to the IO thread.
  auto envelope_cb = base::BindOnce(
      [](base::Value id_clone,
         base::OnceCallback<void(base::DictValue)> outer_reply,
         base::DictValue tool_result) {
        base::Value* id_ptr = id_clone.is_none() ? nullptr : &id_clone;
        std::move(outer_reply)
            .Run(McpService::MakeResult(id_ptr, std::move(tool_result)));
      },
      std::move(id_clone), std::move(reply));

  it->second.handler.Run(args ? *args : empty, std::move(envelope_cb));
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

}  // namespace sessionat
