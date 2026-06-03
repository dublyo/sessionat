// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/client_config_manager.h"

#include <array>
#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/sessionat/mcp/client_writers/writer_common.h"

namespace sessionat {

namespace {

constexpr char kIdClaudeDesktop[] = "claude_desktop";
constexpr char kIdCursor[] = "cursor";
constexpr char kIdCodex[] = "codex";
constexpr char kIdClaudeCode[] = "claude_code";
constexpr char kIdWindsurf[] = "windsurf";
constexpr char kIdVSCode[] = "vscode";

ClientConfigManager::StatusResult GetStatusOnWorker(
    ClientConfigManager::Client c,
    int port,
    const std::string& token) {
  auto r = WriterOpsFor(c).ReadStatus(port, token);
  r.client = c;
  return r;
}

std::pair<bool, std::string> ApplyOnWorker(ClientConfigManager::Client c,
                                            bool remove,
                                            int port,
                                            const std::string& token) {
  return WriterOpsFor(c).Apply(remove, port, token);
}

std::vector<ClientConfigManager::StatusResult> GetAllStatusesOnWorker(
    int port,
    const std::string& token) {
  std::vector<ClientConfigManager::StatusResult> out;
  for (ClientConfigManager::Client c : ClientConfigManager::AllClients()) {
    auto r = WriterOpsFor(c).ReadStatus(port, token);
    r.client = c;
    out.push_back(std::move(r));
  }
  return out;
}

}  // namespace

// static
const char* ClientConfigManager::ClientId(Client c) {
  switch (c) {
    case Client::kClaudeDesktop: return kIdClaudeDesktop;
    case Client::kCursor:        return kIdCursor;
    case Client::kCodex:         return kIdCodex;
    case Client::kClaudeCode:    return kIdClaudeCode;
    case Client::kWindsurf:      return kIdWindsurf;
    case Client::kVSCode:        return kIdVSCode;
  }
  return kIdClaudeDesktop;
}

// static
std::optional<ClientConfigManager::Client>
ClientConfigManager::ClientFromId(std::string_view id) {
  if (id == kIdClaudeDesktop) return Client::kClaudeDesktop;
  if (id == kIdCursor)        return Client::kCursor;
  if (id == kIdCodex)         return Client::kCodex;
  if (id == kIdClaudeCode)    return Client::kClaudeCode;
  if (id == kIdWindsurf)      return Client::kWindsurf;
  if (id == kIdVSCode)        return Client::kVSCode;
  return std::nullopt;
}

// static
std::vector<ClientConfigManager::Client> ClientConfigManager::AllClients() {
  return {Client::kClaudeDesktop, Client::kCursor,    Client::kCodex,
          Client::kClaudeCode,    Client::kWindsurf, Client::kVSCode};
}

// static
base::FilePath ClientConfigManager::GetConfigPath(Client c) {
  return WriterOpsFor(c).GetPath();
}

// static
base::Value ClientConfigManager::BuildEntry(Client c,
                                             int port,
                                             const std::string& token) {
  return WriterOpsFor(c).BuildEntry(port, token);
}

// static
void ClientConfigManager::GetStatus(Client c,
                                     int port,
                                     const std::string& token,
                                     StatusCallback cb) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&GetStatusOnWorker, c, port, token),
      base::BindOnce(
          [](StatusCallback cb, StatusResult r) {
            std::move(cb).Run(std::move(r));
          },
          std::move(cb)));
}

// static
void ClientConfigManager::Connect(Client c,
                                   int port,
                                   const std::string& token,
                                   WriteCallback cb) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&ApplyOnWorker, c, /*remove=*/false, port, token),
      base::BindOnce(
          [](WriteCallback cb, std::pair<bool, std::string> r) {
            std::move(cb).Run(r.first, std::move(r.second));
          },
          std::move(cb)));
}

// static
void ClientConfigManager::Disconnect(Client c, WriteCallback cb) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&ApplyOnWorker, c, /*remove=*/true, /*port=*/0,
                      std::string()),
      base::BindOnce(
          [](WriteCallback cb, std::pair<bool, std::string> r) {
            std::move(cb).Run(r.first, std::move(r.second));
          },
          std::move(cb)));
}

// static
void ClientConfigManager::GetAllStatuses(int port,
                                          const std::string& master_token,
                                          AllStatusCallback cb) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&GetAllStatusesOnWorker, port, master_token),
      std::move(cb));
}

// static
void ClientConfigManager::RevealConfig(Client c) {
  base::FilePath path = WriterOpsFor(c).GetPath();
  if (path.empty()) return;
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&RevealInFileManager, path));
}

// Per-writer accessors implemented in client_writers/*.cc.
extern const WriterOps& GetClaudeDesktopOps();
extern const WriterOps& GetCursorOps();
extern const WriterOps& GetCodexOps();
extern const WriterOps& GetClaudeCodeOps();
extern const WriterOps& GetWindsurfOps();
extern const WriterOps& GetVSCodeOps();

const WriterOps& WriterOpsFor(ClientConfigManager::Client c) {
  switch (c) {
    case ClientConfigManager::Client::kClaudeDesktop:
      return GetClaudeDesktopOps();
    case ClientConfigManager::Client::kCursor:
      return GetCursorOps();
    case ClientConfigManager::Client::kCodex:
      return GetCodexOps();
    case ClientConfigManager::Client::kClaudeCode:
      return GetClaudeCodeOps();
    case ClientConfigManager::Client::kWindsurf:
      return GetWindsurfOps();
    case ClientConfigManager::Client::kVSCode:
      return GetVSCodeOps();
  }
  return GetClaudeDesktopOps();
}

}  // namespace sessionat
