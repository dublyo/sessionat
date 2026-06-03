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

base::FilePath PathImpl() {
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
  return home.Append(FILE_PATH_LITERAL(".cursor"))
      .Append(FILE_PATH_LITERAL("mcp.json"));
}

base::FilePath CursorDir() {
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
  return home.Append(FILE_PATH_LITERAL(".cursor"));
}

bool DetectImpl() {
#if BUILDFLAG(IS_MAC)
  base::FilePath app(FILE_PATH_LITERAL("/Applications/Cursor.app"));
  if (base::PathExists(app)) return true;
#elif BUILDFLAG(IS_WIN)
  base::FilePath local;
  if (base::PathService::Get(base::DIR_LOCAL_APP_DATA, &local)) {
    base::FilePath exe =
        local.Append(FILE_PATH_LITERAL("Programs"))
            .Append(FILE_PATH_LITERAL("cursor"))
            .Append(FILE_PATH_LITERAL("Cursor.exe"));
    if (base::PathExists(exe)) return true;
  }
#else
  if (!ResolveCliOnPath("cursor").empty()) return true;
#endif
  base::FilePath cdir = CursorDir();
  return !cdir.empty() && base::DirectoryExists(cdir);
}

base::Value BuildEntryImpl(int port, const std::string& token) {
  return base::Value(BuildHttpHeaderEntry(port, token));
}

std::pair<bool, std::string> ApplyImpl(bool remove,
                                         int port,
                                         const std::string& token) {
  base::FilePath path = PathImpl();
  if (path.empty()) return {false, "Could not resolve $HOME/.cursor/mcp.json"};

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

const WriterOps& GetCursorOps() {
  static const WriterOps ops = {
      &PathImpl, &DetectImpl, &BuildEntryImpl, &ApplyImpl, &ReadStatusImpl};
  return ops;
}

}  // namespace sessionat
