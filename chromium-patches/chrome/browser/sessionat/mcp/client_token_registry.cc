// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/client_token_registry.h"

#include <utility>

#include "base/rand_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace sessionat {

namespace {

constexpr char kClientTokensPref[] = "sessionat.mcp.client_tokens";
constexpr char kMasterTokenPref[] = "sessionat.mcp.master_token";
constexpr char kMasterClientId[] = "_master";

std::string Generate64Hex() {
  uint8_t bytes[32];
  base::RandBytes(bytes);
  std::string out;
  out.reserve(64);
  for (uint8_t b : bytes) out += base::StringPrintf("%02x", b);
  return out;
}

}  // namespace

ClientTokenRegistry::ClientTokenRegistry(PrefService* prefs) : prefs_(prefs) {
  LoadFromPrefs();
}

ClientTokenRegistry::~ClientTokenRegistry() = default;

void ClientTokenRegistry::LoadFromPrefs() {
  token_to_client_.clear();
  client_to_token_.clear();
  if (!prefs_) return;
  master_token_ = prefs_->GetString(kMasterTokenPref);
  const base::DictValue& dict = prefs_->GetDict(kClientTokensPref);
  for (const auto pair : dict) {
    const std::string& client_id = pair.first;
    if (!pair.second.is_dict()) continue;
    const std::string* tok = pair.second.GetDict().FindString("token");
    if (!tok || tok->empty()) continue;
    auto c = ClientConfigManager::ClientFromId(client_id);
    if (!c) continue;
    token_to_client_[*tok] = *c;
    client_to_token_[client_id] = *tok;
  }
}

void ClientTokenRegistry::PersistClientTokens() {
  if (!prefs_) return;
  ScopedDictPrefUpdate update(prefs_, kClientTokensPref);
  base::DictValue& dict = update.Get();
  dict.clear();
  const int64_t now_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  for (const auto& [client_id, token] : client_to_token_) {
    base::DictValue entry;
    entry.Set("token", token);
    entry.Set("created_at", static_cast<double>(now_ms));
    dict.Set(client_id, std::move(entry));
  }
}

std::string ClientTokenRegistry::IssueToken(
    ClientConfigManager::Client client) {
  const std::string id = ClientConfigManager::ClientId(client);
  // Drop any old token for this client first to keep the map invariant clean.
  auto it = client_to_token_.find(id);
  if (it != client_to_token_.end()) {
    token_to_client_.erase(it->second);
    client_to_token_.erase(it);
  }
  std::string tok = Generate64Hex();
  token_to_client_[tok] = client;
  client_to_token_[id] = tok;
  PersistClientTokens();
  return tok;
}

void ClientTokenRegistry::RevokeToken(ClientConfigManager::Client client) {
  const std::string id = ClientConfigManager::ClientId(client);
  auto it = client_to_token_.find(id);
  if (it == client_to_token_.end()) return;
  token_to_client_.erase(it->second);
  client_to_token_.erase(it);
  PersistClientTokens();
}

std::optional<ClientConfigManager::Client>
ClientTokenRegistry::LookupClientForToken(std::string_view tok) const {
  auto it = token_to_client_.find(std::string(tok));
  if (it == token_to_client_.end()) return std::nullopt;
  return it->second;
}

std::string ClientTokenRegistry::GetOrCreateMasterToken() {
  if (!master_token_.empty()) return master_token_;
  master_token_ = Generate64Hex();
  if (prefs_) prefs_->SetString(kMasterTokenPref, master_token_);
  return master_token_;
}

std::string ClientTokenRegistry::RotateMasterToken(
    std::vector<ClientConfigManager::Client>* out_invalidated) {
  if (out_invalidated) {
    out_invalidated->clear();
    for (const auto& [id, tok] : client_to_token_) {
      auto c = ClientConfigManager::ClientFromId(id);
      if (c) out_invalidated->push_back(*c);
    }
  }
  token_to_client_.clear();
  client_to_token_.clear();
  PersistClientTokens();
  master_token_ = Generate64Hex();
  if (prefs_) prefs_->SetString(kMasterTokenPref, master_token_);
  // Master client_id sentinel for any callers that key on it (write grants).
  (void)kMasterClientId;
  return master_token_;
}

std::vector<ClientConfigManager::Client>
ClientTokenRegistry::ConnectedClients() const {
  std::vector<ClientConfigManager::Client> out;
  for (const auto& [id, tok] : client_to_token_) {
    auto c = ClientConfigManager::ClientFromId(id);
    if (c) out.push_back(*c);
  }
  return out;
}

std::optional<std::string> ClientTokenRegistry::TokenForClient(
    ClientConfigManager::Client client) const {
  auto it = client_to_token_.find(ClientConfigManager::ClientId(client));
  if (it == client_to_token_.end()) return std::nullopt;
  return it->second;
}

}  // namespace sessionat
