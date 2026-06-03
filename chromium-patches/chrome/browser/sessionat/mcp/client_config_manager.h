// Copyright 2026 Sessionat. All rights reserved.
//
// ClientConfigManager — multi-client facade for installing the Sessionat MCP
// entry into external AI clients (Claude Desktop, Cursor, Codex, Claude Code,
// Windsurf, VS Code). All writes go through writers under client_writers/ so
// the manager itself stays a thin dispatcher.

#ifndef CHROME_BROWSER_SESSIONAT_MCP_CLIENT_CONFIG_MANAGER_H_
#define CHROME_BROWSER_SESSIONAT_MCP_CLIENT_CONFIG_MANAGER_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/values.h"

namespace sessionat {

class ClientConfigManager {
 public:
  enum class Client {
    kClaudeDesktop,
    kCursor,
    kCodex,
    kClaudeCode,
    kWindsurf,
    kVSCode,
  };

  enum class Status {
    kNotInstalled,
    kInstalledNoEntry,
    kConnected,
    kStale,
    kError,
  };

  struct StatusResult {
    Client client = Client::kClaudeDesktop;
    Status status = Status::kNotInstalled;
    base::FilePath config_path;
    std::string error_message;
    // True when the client cannot be configured automatically (no CLI on PATH,
    // for example) and the UI must surface `manual_snippet` for the user to
    // copy.
    bool requires_manual_snippet = false;
    std::string manual_snippet;
  };

  using StatusCallback = base::OnceCallback<void(StatusResult)>;
  using WriteCallback =
      base::OnceCallback<void(bool ok, std::string error_message)>;
  using AllStatusCallback =
      base::OnceCallback<void(std::vector<StatusResult>)>;

  // Stable identifiers used in prefs and IPC payloads. Do not rename.
  static const char* ClientId(Client c);
  static std::optional<Client> ClientFromId(std::string_view id);
  static std::vector<Client> AllClients();

  // Path the writer would touch. May be empty for clients we manage via a CLI
  // rather than a config file (Claude Code).
  static base::FilePath GetConfigPath(Client c);

  // The connector entry as a JSON-shaped Value. TOML-flavored clients still
  // return a Dict whose keys describe the section to emit; the writer renders
  // the actual TOML when applying.
  static base::Value BuildEntry(Client c,
                                 int port,
                                 const std::string& token);

  // Worker-thread driven, callback on the caller's sequence.
  static void GetStatus(Client c,
                         int port,
                         const std::string& token,
                         StatusCallback cb);

  static void Connect(Client c,
                       int port,
                       const std::string& token,
                       WriteCallback cb);

  static void Disconnect(Client c, WriteCallback cb);

  static void GetAllStatuses(int port,
                              const std::string& master_token,
                              AllStatusCallback cb);

  // Fire-and-forget; no callback.
  static void RevealConfig(Client c);
};

// Each writer registers its operations in a small WriterOps struct so that
// ClientConfigManager can dispatch by Client without learning the per-writer
// details. Writers live under client_writers/.
struct WriterOps {
  base::FilePath (*GetPath)();
  bool (*Detect)();
  base::Value (*BuildEntry)(int port, const std::string& token);
  std::pair<bool, std::string> (*Apply)(bool remove,
                                          int port,
                                          const std::string& token);
  ClientConfigManager::StatusResult (*ReadStatus)(int port,
                                                    const std::string& token);
};

const WriterOps& WriterOpsFor(ClientConfigManager::Client c);

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_CLIENT_CONFIG_MANAGER_H_
