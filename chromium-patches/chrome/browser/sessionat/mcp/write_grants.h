// Copyright 2026 Sessionat. All rights reserved.
//
// WriteGrants — per-token, per-client write-tool approvals (the "first-use
// confirmation" required by CLAUDE.md for any MCP write).

#ifndef CHROME_BROWSER_SESSIONAT_MCP_WRITE_GRANTS_H_
#define CHROME_BROWSER_SESSIONAT_MCP_WRITE_GRANTS_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"

class PrefService;

namespace sessionat {

// Synthetic client id used for the master discovery token.
inline constexpr char kMasterClientId[] = "_master";

class WriteGrants {
 public:
  explicit WriteGrants(PrefService* prefs);
  ~WriteGrants();
  WriteGrants(const WriteGrants&) = delete;
  WriteGrants& operator=(const WriteGrants&) = delete;

  bool HasGrantForToken(std::string_view token) const;
  void Grant(std::string_view token, std::string_view client_id);
  void Revoke(std::string_view token);
  void RevokeForClient(std::string_view client_id);
  void MarkUsed(std::string_view token);
  std::optional<std::string> ClientForToken(std::string_view token) const;

 private:
  raw_ptr<PrefService, DanglingUntriaged> prefs_;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_WRITE_GRANTS_H_
