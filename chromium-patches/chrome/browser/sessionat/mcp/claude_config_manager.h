// Copyright 2026 Sessionat. All rights reserved.
//
// ClaudeConfigManager — atomic read/write of Claude Desktop's
// claude_desktop_config.json so users can connect Sessionat's MCP server
// to Claude Desktop with one click. Honors the spec's "atomic config merge
// with .bak safety" requirement.

#ifndef CHROME_BROWSER_SESSIONAT_MCP_CLAUDE_CONFIG_MANAGER_H_
#define CHROME_BROWSER_SESSIONAT_MCP_CLAUDE_CONFIG_MANAGER_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"

namespace sessionat {

class ClaudeConfigManager {
 public:
  enum class Status {
    // ~/Library/Application Support/Claude/ doesn't exist on disk.
    kClaudeNotInstalled,
    // Config exists or directory exists but no sessionat entry.
    kInstalledNoEntry,
    // sessionat entry present and matches Sessionat's current port + token.
    kConnected,
    // sessionat entry present but port/token differ — needs re-connect.
    kStale,
    // Couldn't read or parse the config (returns error_message via callback).
    kError,
  };

  struct StatusResult {
    Status status = Status::kClaudeNotInstalled;
    base::FilePath config_path;
    std::string error_message;
  };

  using StatusCallback = base::OnceCallback<void(StatusResult)>;
  using WriteCallback =
      base::OnceCallback<void(bool ok, std::string error_message)>;

  // OS-specific default location of claude_desktop_config.json.
  //   macOS:   ~/Library/Application Support/Claude/claude_desktop_config.json
  //   Windows: %APPDATA%/Claude/claude_desktop_config.json
  //   Linux:   ~/.config/Claude/claude_desktop_config.json
  static base::FilePath ResolveConfigPath();

  // Reads the config (worker thread), determines status, calls back on the
  // caller's sequence.
  static void GetStatus(int sessionat_port,
                         const std::string& sessionat_token,
                         StatusCallback cb);

  // Atomically writes the config with a sessionat entry under
  // mcpServers.sessionat. Creates the parent directory if needed. Saves
  // the prior content to <path>.bak before overwriting.
  static void Connect(int sessionat_port,
                       const std::string& sessionat_token,
                       WriteCallback cb);

  // Removes the sessionat entry. Same .bak safety as Connect().
  static void Disconnect(WriteCallback cb);
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_CLAUDE_CONFIG_MANAGER_H_
