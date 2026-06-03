// Copyright 2026 Sessionat. All rights reserved.
//
// ClientTokenRegistry — per-MCP-client bearer tokens, persisted in profile
// prefs and mirrored to memory for fast Authorization-header lookup.

#ifndef CHROME_BROWSER_SESSIONAT_MCP_CLIENT_TOKEN_REGISTRY_H_
#define CHROME_BROWSER_SESSIONAT_MCP_CLIENT_TOKEN_REGISTRY_H_

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"

class PrefService;

namespace sessionat {

class ClientTokenRegistry {
 public:
  explicit ClientTokenRegistry(PrefService* prefs);
  ~ClientTokenRegistry();
  ClientTokenRegistry(const ClientTokenRegistry&) = delete;
  ClientTokenRegistry& operator=(const ClientTokenRegistry&) = delete;

  // Generates a fresh 64-hex token, persists it as the new token for `client`
  // (overwriting any prior entry — no proliferation), and returns it.
  std::string IssueToken(ClientConfigManager::Client client);

  void RevokeToken(ClientConfigManager::Client client);

  // O(1) on the in-memory mirror.
  std::optional<ClientConfigManager::Client> LookupClientForToken(
      std::string_view tok) const;

  // Returns the persisted master discovery token, generating + persisting one
  // on the first call if none exists. Persistence stops the token from
  // rotating across browser restarts (otherwise Cursor / Claude Desktop go
  // kStale every launch).
  std::string GetOrCreateMasterToken();

  // Force a new master token (returns it). Clears `out_invalidated` per-client
  // tokens so callers can re-issue + reconnect.
  std::string RotateMasterToken(
      std::vector<ClientConfigManager::Client>* out_invalidated);

  // For UI display.
  std::vector<ClientConfigManager::Client> ConnectedClients() const;
  std::optional<std::string> TokenForClient(
      ClientConfigManager::Client client) const;

 private:
  void LoadFromPrefs();
  void PersistClientTokens();

  raw_ptr<PrefService, DanglingUntriaged> prefs_;
  // token -> client mirror for the auth fast-path.
  std::unordered_map<std::string, ClientConfigManager::Client> token_to_client_;
  // client_id -> token mirror for IPC reverse lookup.
  std::unordered_map<std::string, std::string> client_to_token_;
  std::string master_token_;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_CLIENT_TOKEN_REGISTRY_H_
