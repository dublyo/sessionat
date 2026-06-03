// Copyright 2026 Sessionat. All rights reserved.

#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"
#include "chrome/browser/sessionat/mcp/client_writers/writer_common.h"

namespace sessionat {

namespace {

constexpr char kMcpServersKey[] = "mcpServers";
constexpr char kSessionatKey[] = "sessionat";

base::FilePath WindsurfBaseDir() {
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
  return home.Append(FILE_PATH_LITERAL(".codeium"))
      .Append(FILE_PATH_LITERAL("windsurf"));
}

base::FilePath PathImpl() {
  base::FilePath dir = WindsurfBaseDir();
  if (dir.empty()) return base::FilePath();
  return dir.Append(FILE_PATH_LITERAL("mcp_config.json"));
}

bool DetectImpl() {
#if BUILDFLAG(IS_MAC)
  if (base::PathExists(
          base::FilePath(FILE_PATH_LITERAL("/Applications/Windsurf.app")))) {
    return true;
  }
#elif BUILDFLAG(IS_WIN)
  base::FilePath local;
  if (base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local)) {
    base::FilePath dir = local.Append(FILE_PATH_LITERAL("Programs"))
                              .Append(FILE_PATH_LITERAL("Windsurf"));
    if (base::DirectoryExists(dir)) return true;
  }
#endif
  base::FilePath base = WindsurfBaseDir();
  return !base.empty() && base::DirectoryExists(base);
}

base::Value BuildEntryImpl(int port, const std::string& token) {
  return base::Value(BuildHttpHeaderEntry(port, token));
}

std::pair<bool, std::string> ApplyImpl(bool remove,
                                         int port,
                                         const std::string& token) {
  base::FilePath path = PathImpl();
  if (path.empty()) {
    return {false, "Could not resolve $HOME/.codeium/windsurf/mcp_config.json"};
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

const WriterOps& GetWindsurfOps() {
  static const WriterOps ops = {
      &PathImpl, &DetectImpl, &BuildEntryImpl, &ApplyImpl, &ReadStatusImpl};
  return ops;
}

}  // namespace sessionat
