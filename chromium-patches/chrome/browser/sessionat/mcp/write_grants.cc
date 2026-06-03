// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/write_grants.h"

#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"

namespace sessionat {

namespace {

constexpr char kWriteGrantsPref[] = "sessionat.mcp.write_grants";

}  // namespace

WriteGrants::WriteGrants(PrefService* prefs) : prefs_(prefs) {}
WriteGrants::~WriteGrants() = default;

bool WriteGrants::HasGrantForToken(std::string_view token) const {
  if (!prefs_) return false;
  const base::DictValue& dict = prefs_->GetDict(kWriteGrantsPref);
  return dict.FindDict(std::string(token)) != nullptr;
}

void WriteGrants::Grant(std::string_view token, std::string_view client_id) {
  if (!prefs_) return;
  ScopedDictPrefUpdate update(prefs_, kWriteGrantsPref);
  base::DictValue entry;
  entry.Set("client_id", std::string(client_id));
  entry.Set("granted_at", static_cast<double>(
                              base::Time::Now().InMillisecondsSinceUnixEpoch()));
  update.Get().Set(std::string(token), std::move(entry));
}

void WriteGrants::Revoke(std::string_view token) {
  if (!prefs_) return;
  ScopedDictPrefUpdate update(prefs_, kWriteGrantsPref);
  update.Get().Remove(std::string(token));
}

void WriteGrants::RevokeForClient(std::string_view client_id) {
  if (!prefs_) return;
  ScopedDictPrefUpdate update(prefs_, kWriteGrantsPref);
  base::DictValue& dict = update.Get();
  std::vector<std::string> to_remove;
  for (const auto pair : dict) {
    if (!pair.second.is_dict()) continue;
    const std::string* cid = pair.second.GetDict().FindString("client_id");
    if (cid && *cid == client_id) to_remove.push_back(pair.first);
  }
  for (const auto& k : to_remove) dict.Remove(k);
}

void WriteGrants::MarkUsed(std::string_view token) {
  if (!prefs_) return;
  ScopedDictPrefUpdate update(prefs_, kWriteGrantsPref);
  base::DictValue* entry = update.Get().FindDict(std::string(token));
  if (!entry) return;
  entry->Set("last_used_at",
              static_cast<double>(
                  base::Time::Now().InMillisecondsSinceUnixEpoch()));
}

std::optional<std::string> WriteGrants::ClientForToken(
    std::string_view token) const {
  if (!prefs_) return std::nullopt;
  const base::DictValue& dict = prefs_->GetDict(kWriteGrantsPref);
  const base::DictValue* entry = dict.FindDict(std::string(token));
  if (!entry) return std::nullopt;
  const std::string* cid = entry->FindString("client_id");
  if (!cid) return std::nullopt;
  return *cid;
}

}  // namespace sessionat
