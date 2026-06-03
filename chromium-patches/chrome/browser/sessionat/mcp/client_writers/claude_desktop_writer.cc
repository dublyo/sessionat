// Copyright 2026 Sessionat. All rights reserved.

#include <optional>
#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"
#include "chrome/browser/sessionat/mcp/client_writers/writer_common.h"

namespace sessionat {

namespace {

// Top-level dict key in claude_desktop_config.json.
constexpr char kMcpServersKey[] = "mcpServers";
constexpr char kSessionatKey[] = "sessionat";

// mcp-remote bridges Claude Desktop's stdio transport to our HTTP endpoint.
// Users install it once via `npm install -g mcp-remote`.
constexpr char kBridgeCommand[] = "mcp-remote";

base::FilePath PathImpl() {
#if BUILDFLAG(IS_MAC)
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
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
#else
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
  return home.Append(".config")
      .Append("Claude")
      .Append("claude_desktop_config.json");
#endif
}

bool DetectImpl() {
  base::FilePath p = PathImpl();
  if (p.empty()) return false;
  // The Claude/ dir exists iff Claude Desktop has been launched at least once.
  return base::DirectoryExists(p.DirName());
}

base::Value BuildEntryImpl(int port, const std::string& token) {
  base::DictValue server;
  server.Set("command", kBridgeCommand);
  base::ListValue args;
  args.Append(base::StringPrintf("http://127.0.0.1:%d/mcp", port));
  args.Append("--header");
  args.Append(base::StringPrintf("Authorization: Bearer %s", token.c_str()));
  server.Set("args", std::move(args));
  return base::Value(std::move(server));
}

std::pair<bool, std::string> ApplyImpl(bool remove,
                                         int port,
                                         const std::string& token) {
  base::FilePath path = PathImpl();
  if (path.empty()) {
    return {false, "Could not resolve Claude Desktop config path."};
  }
  base::DictValue root;
  if (base::PathExists(path)) {
    std::string err;
    auto parsed = ReadJsonDict(path, &err);
    if (!parsed) return {false, err};
    root = std::move(*parsed);
  }
  base::DictValue* servers = root.EnsureDict(kMcpServersKey);
  if (remove) {
    servers->Remove(kSessionatKey);
    if (servers->empty()) root.Remove(kMcpServersKey);
  } else {
    servers->Set(kSessionatKey, BuildEntryImpl(port, token));
  }
  std::string err;
  if (!WriteJsonAtomically(path, root, &err)) return {false, err};
  return {true, ""};
}

ClientConfigManager::StatusResult ReadStatusImpl(int port,
                                                  const std::string& token) {
  ClientConfigManager::StatusResult r;
  r.config_path = PathImpl();
  if (r.config_path.empty()) {
    r.status = ClientConfigManager::Status::kError;
    r.error_message = "Could not resolve config path on this platform.";
    return r;
  }
  if (!base::DirectoryExists(r.config_path.DirName())) {
    r.status = ClientConfigManager::Status::kNotInstalled;
    return r;
  }
  if (!base::PathExists(r.config_path)) {
    r.status = ClientConfigManager::Status::kInstalledNoEntry;
    return r;
  }
  std::string err;
  auto root = ReadJsonDict(r.config_path, &err);
  if (!root) {
    r.status = ClientConfigManager::Status::kError;
    r.error_message = err;
    return r;
  }
  const base::DictValue* servers = root->FindDict(kMcpServersKey);
  const base::DictValue* entry =
      servers ? servers->FindDict(kSessionatKey) : nullptr;
  if (!entry) {
    r.status = ClientConfigManager::Status::kInstalledNoEntry;
    return r;
  }
  r.status = EntryMatchesPortAndToken(*entry, port, token)
                  ? ClientConfigManager::Status::kConnected
                  : ClientConfigManager::Status::kStale;
  return r;
}

}  // namespace

const WriterOps& GetClaudeDesktopOps() {
  static const WriterOps ops = {
      &PathImpl, &DetectImpl, &BuildEntryImpl, &ApplyImpl, &ReadStatusImpl};
  return ops;
}

}  // namespace sessionat
