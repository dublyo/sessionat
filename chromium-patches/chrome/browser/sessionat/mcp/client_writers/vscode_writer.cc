// Copyright 2026 Sessionat. All rights reserved.

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

// VS Code diverges from the Anthropic convention: top-level key is "servers"
// (not "mcpServers") and each entry carries an explicit "type" field.
constexpr char kServersKey[] = "servers";
constexpr char kSessionatKey[] = "sessionat";

base::FilePath PathImpl() {
#if BUILDFLAG(IS_MAC)
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
  return home.Append("Library")
      .Append("Application Support")
      .Append("Code")
      .Append("User")
      .Append("mcp.json");
#elif BUILDFLAG(IS_WIN)
  base::FilePath appdata;
  if (!base::PathService::Get(base::DIR_ROAMING_APP_DATA, &appdata)) {
    return base::FilePath();
  }
  return appdata.Append(FILE_PATH_LITERAL("Code"))
      .Append(FILE_PATH_LITERAL("User"))
      .Append(FILE_PATH_LITERAL("mcp.json"));
#else
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
  return home.Append(".config")
      .Append("Code")
      .Append("User")
      .Append("mcp.json");
#endif
}

bool DetectImpl() {
#if BUILDFLAG(IS_MAC)
  if (base::PathExists(base::FilePath(
          FILE_PATH_LITERAL("/Applications/Visual Studio Code.app")))) {
    return true;
  }
#elif BUILDFLAG(IS_WIN)
  base::FilePath local;
  if (base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local)) {
    base::FilePath dir = local.Append(FILE_PATH_LITERAL("Programs"))
                              .Append(FILE_PATH_LITERAL("Microsoft VS Code"));
    if (base::DirectoryExists(dir)) return true;
  }
#endif
  if (!ResolveCliOnPath("code").empty()) return true;
  // User-level config dir means VS Code has run on this account.
  base::FilePath p = PathImpl();
  return !p.empty() && base::DirectoryExists(p.DirName());
}

base::Value BuildEntryImpl(int port, const std::string& token) {
  base::DictValue entry;
  entry.Set("type", "http");
  entry.Set("url", base::StringPrintf("http://127.0.0.1:%d/mcp", port));
  base::DictValue headers;
  headers.Set("Authorization",
                base::StringPrintf("Bearer %s", token.c_str()));
  entry.Set("headers", std::move(headers));
  return base::Value(std::move(entry));
}

std::pair<bool, std::string> ApplyImpl(bool remove,
                                         int port,
                                         const std::string& token) {
  base::FilePath path = PathImpl();
  if (path.empty()) {
    return {false, "Could not resolve the VS Code user mcp.json path."};
  }
  base::DictValue root;
  if (base::PathExists(path)) {
    std::string err;
    auto parsed = ReadJsonDict(path, &err);
    if (!parsed) return {false, err};
    root = std::move(*parsed);
  }
  base::DictValue* servers = root.EnsureDict(kServersKey);
  if (remove) {
    servers->Remove(kSessionatKey);
    if (servers->empty()) root.Remove(kServersKey);
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
  if (!DetectImpl()) {
    r.status = ClientConfigManager::Status::kNotInstalled;
    return r;
  }
  if (r.config_path.empty() || !base::PathExists(r.config_path)) {
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
  const base::DictValue* servers = root->FindDict(kServersKey);
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

const WriterOps& GetVSCodeOps() {
  static const WriterOps ops = {
      &PathImpl, &DetectImpl, &BuildEntryImpl, &ApplyImpl, &ReadStatusImpl};
  return ops;
}

}  // namespace sessionat
