// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/claude_config_manager.h"

#include <optional>
#include <utility>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include "base/apple/foundation_util.h"
#endif

namespace sessionat {

namespace {

// JSON keys we read/write in claude_desktop_config.json. Keep in sync with
// the MCP docs at https://modelcontextprotocol.io/quickstart/user
constexpr char kMcpServersKey[] = "mcpServers";
constexpr char kSessionatKey[] = "sessionat";

// We register Sessionat using the `mcp-remote` bridge so Claude Desktop's
// stdio transport can wrap our HTTP MCP endpoint. The user installs the
// bridge once: `npm install -g mcp-remote`.
constexpr char kBridgeCommand[] = "mcp-remote";

base::FilePath GetClaudeConfigPath() {
#if BUILDFLAG(IS_MAC)
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home)) {
    return base::FilePath();
  }
  return home.Append("Library")
      .Append("Application Support")
      .Append("Claude")
      .Append("claude_desktop_config.json");
#elif BUILDFLAG(IS_WIN)
  base::FilePath appdata;
  if (!base::PathService::Get(base::DIR_ROAMING_APP_DATA, &appdata)) {
    return base::FilePath();
  }
  return appdata.Append(FILE_PATH_LITERAL("Claude"))
      .Append(FILE_PATH_LITERAL("claude_desktop_config.json"));
#else  // Linux / others
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home)) {
    return base::FilePath();
  }
  return home.Append(".config")
      .Append("Claude")
      .Append("claude_desktop_config.json");
#endif
}

// Build the canonical Sessionat MCP entry the user wants installed.
base::DictValue MakeSessionatEntry(int port, const std::string& token) {
  base::DictValue server;
  server.Set("command", kBridgeCommand);
  base::ListValue args;
  args.Append(base::StringPrintf("http://127.0.0.1:%d/mcp", port));
  args.Append("--header");
  args.Append(base::StringPrintf("Authorization: Bearer %s", token.c_str()));
  server.Set("args", std::move(args));
  return server;
}

// Returns true iff the sessionat entry under mcpServers matches the supplied
// port + token (we look for the URL substring and the token substring in
// its args list — robust against extra args the user may have added).
bool EntryMatches(const base::DictValue& mcp_servers,
                  int port,
                  const std::string& token) {
  const base::DictValue* entry = mcp_servers.FindDict(kSessionatKey);
  if (!entry) return false;
  const base::ListValue* args = entry->FindList("args");
  if (!args) return false;
  const std::string want_url =
      base::StringPrintf("http://127.0.0.1:%d/mcp", port);
  bool url_ok = false;
  bool token_ok = false;
  for (const base::Value& v : *args) {
    if (!v.is_string()) continue;
    const std::string& s = v.GetString();
    if (s == want_url) url_ok = true;
    if (s.find(token) != std::string::npos) token_ok = true;
  }
  return url_ok && token_ok;
}

bool HasSessionatEntry(const base::DictValue& mcp_servers) {
  return mcp_servers.FindDict(kSessionatKey) != nullptr;
}

// ---------- Worker-thread helpers ----------

ClaudeConfigManager::StatusResult ReadStatusOnWorker(
    int port,
    const std::string& token) {
  using R = ClaudeConfigManager::StatusResult;
  R r;
  r.config_path = GetClaudeConfigPath();
  if (r.config_path.empty()) {
    r.status = ClaudeConfigManager::Status::kError;
    r.error_message = "Could not resolve config path on this platform.";
    return r;
  }
  // If the Claude/ dir doesn't exist, Claude Desktop probably isn't installed.
  base::FilePath dir = r.config_path.DirName();
  if (!base::DirectoryExists(dir)) {
    r.status = ClaudeConfigManager::Status::kClaudeNotInstalled;
    return r;
  }
  if (!base::PathExists(r.config_path)) {
    r.status = ClaudeConfigManager::Status::kInstalledNoEntry;
    return r;
  }
  std::string body;
  if (!base::ReadFileToString(r.config_path, &body)) {
    r.status = ClaudeConfigManager::Status::kError;
    r.error_message = "Could not read claude_desktop_config.json";
    return r;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    r.status = ClaudeConfigManager::Status::kError;
    r.error_message =
        "claude_desktop_config.json is invalid JSON; refusing to touch it.";
    return r;
  }
  const base::DictValue* servers = parsed->GetDict().FindDict(kMcpServersKey);
  if (!servers || !HasSessionatEntry(*servers)) {
    r.status = ClaudeConfigManager::Status::kInstalledNoEntry;
    return r;
  }
  r.status = EntryMatches(*servers, port, token)
                 ? ClaudeConfigManager::Status::kConnected
                 : ClaudeConfigManager::Status::kStale;
  return r;
}

// Returns {ok, error_message}.
std::pair<bool, std::string> ApplyConfigChange(
    bool remove,
    int port,
    const std::string& token) {
  base::FilePath path = GetClaudeConfigPath();
  if (path.empty()) {
    return {false, "Could not resolve Claude Desktop config path."};
  }
  base::FilePath dir = path.DirName();
  if (!base::DirectoryExists(dir)) {
    if (!base::CreateDirectory(dir)) {
      return {false, "Could not create " + dir.AsUTF8Unsafe()};
    }
  }

  // Read existing config (or start fresh).
  base::DictValue root;
  if (base::PathExists(path)) {
    std::string body;
    if (!base::ReadFileToString(path, &body)) {
      return {false, "Could not read existing config."};
    }
    std::optional<base::Value> parsed =
        base::JSONReader::Read(body, base::JSON_PARSE_RFC);
    if (!parsed) {
      return {false,
              "claude_desktop_config.json is invalid JSON; refusing to "
              "overwrite. Fix or remove the file and try again."};
    }
    if (!parsed->is_dict()) {
      return {false, "Existing config is not a JSON object; aborting."};
    }
    root = std::move(*parsed).TakeDict();
    // Backup ONLY when overwriting an existing file. Suffix .bak so a second
    // round-trip doesn't multiply backups.
    base::FilePath bak = path.AddExtensionASCII(".bak");
    if (!base::CopyFile(path, bak)) {
      LOG(WARNING) << "[Sessionat MCP] could not write " << bak.value()
                   << " — proceeding without backup";
    }
  }

  // Merge / remove the entry.
  base::DictValue* servers = root.EnsureDict(kMcpServersKey);
  if (remove) {
    servers->Remove(kSessionatKey);
    // Optional polish: drop the whole mcpServers key if empty so the user's
    // file stays tidy.
    if (servers->empty()) {
      root.Remove(kMcpServersKey);
    }
  } else {
    servers->Set(kSessionatKey, MakeSessionatEntry(port, token));
  }

  std::string serialized;
  if (!base::JSONWriter::WriteWithOptions(
          root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &serialized)) {
    return {false, "Failed to serialize config."};
  }

  // Atomic write via temp file + rename. base::WriteFile already does
  // best-effort; we use ImportantFileWriter-equivalent by writing to .tmp
  // then renaming. For simplicity here we just WriteFile — claude_desktop
  // isn't running while users would typically click this, so torn writes
  // are very unlikely.
  if (!base::WriteFile(path, serialized)) {
    return {false, "Failed to write " + path.AsUTF8Unsafe()};
  }
  return {true, ""};
}

}  // namespace

// static
base::FilePath ClaudeConfigManager::ResolveConfigPath() {
  return GetClaudeConfigPath();
}

// static
void ClaudeConfigManager::GetStatus(int port,
                                     const std::string& token,
                                     StatusCallback cb) {
  auto reply_runner = base::SequencedTaskRunner::GetCurrentDefault();
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadStatusOnWorker, port, token),
      base::BindOnce(
          [](StatusCallback cb, StatusResult r) {
            std::move(cb).Run(std::move(r));
          },
          std::move(cb)));
}

// static
void ClaudeConfigManager::Connect(int port,
                                   const std::string& token,
                                   WriteCallback cb) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&ApplyConfigChange, /*remove=*/false, port, token),
      base::BindOnce(
          [](WriteCallback cb, std::pair<bool, std::string> result) {
            std::move(cb).Run(result.first, std::move(result.second));
          },
          std::move(cb)));
}

// static
void ClaudeConfigManager::Disconnect(WriteCallback cb) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(&ApplyConfigChange, /*remove=*/true,
                     /*port=*/0, /*token=*/std::string()),
      base::BindOnce(
          [](WriteCallback cb, std::pair<bool, std::string> result) {
            std::move(cb).Run(result.first, std::move(result.second));
          },
          std::move(cb)));
}

}  // namespace sessionat
