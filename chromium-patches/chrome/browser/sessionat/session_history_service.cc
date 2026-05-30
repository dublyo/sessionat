// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sessionat/session_history_service.h"

#include <algorithm>

#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
// NOTE(sessionat v2 rebase): chrome/browser/ui/browser_list.h was deleted in
// M150 (no replacement). Browser enumeration / SaveCurrentSession()
// temporarily stubbed; explicit SaveSession(tabs) from the NTP handler still
// works because tabs are supplied directly.
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"

namespace sessionat {

// SavedSession implementation
SavedSession::SavedSession() = default;
SavedSession::SavedSession(const SavedSession& other)
    : id(other.id),
      name(other.name),
      custom_name(other.custom_name),
      timestamp(other.timestamp),
      is_pinned(other.is_pinned) {
  for (const auto& tab : other.tabs) {
    tabs.push_back(tab.Clone());
  }
}
SavedSession& SavedSession::operator=(const SavedSession& other) {
  if (this != &other) {
    id = other.id;
    name = other.name;
    custom_name = other.custom_name;
    timestamp = other.timestamp;
    is_pinned = other.is_pinned;
    tabs.clear();
    for (const auto& tab : other.tabs) {
      tabs.push_back(tab.Clone());
    }
  }
  return *this;
}
SavedSession::SavedSession(SavedSession&&) = default;
SavedSession& SavedSession::operator=(SavedSession&&) = default;
SavedSession::~SavedSession() = default;

base::DictValue SavedSession::ToDict() const {
  base::DictValue dict;
  dict.Set("id", id);
  dict.Set("name", name);
  dict.Set("customName", custom_name);
  dict.Set("timestamp", static_cast<double>(timestamp));
  dict.Set("isPinned", is_pinned);

  base::ListValue tabs_list;
  for (const auto& tab : tabs) {
    tabs_list.Append(tab.Clone());
  }
  dict.Set("tabs", std::move(tabs_list));

  return dict;
}

std::optional<SavedSession> SavedSession::FromDict(
    const base::DictValue& dict) {
  SavedSession session;

  const std::string* id = dict.FindString("id");
  if (!id) return std::nullopt;
  session.id = *id;

  const std::string* name = dict.FindString("name");
  if (name) session.name = *name;

  const std::string* custom_name = dict.FindString("customName");
  if (custom_name) session.custom_name = *custom_name;

  std::optional<double> timestamp = dict.FindDouble("timestamp");
  if (timestamp) {
    session.timestamp = static_cast<int64_t>(*timestamp);
  }

  std::optional<bool> is_pinned = dict.FindBool("isPinned");
  if (is_pinned) session.is_pinned = *is_pinned;

  const base::ListValue* tabs_list = dict.FindList("tabs");
  if (tabs_list) {
    for (const auto& tab : *tabs_list) {
      if (tab.is_dict()) {
        session.tabs.push_back(tab.GetDict().Clone());
      }
    }
  }

  return session;
}

// SessionHistoryService implementation
SessionHistoryService::SessionHistoryService(Profile* profile)
    : profile_(profile) {
  LoadSessions();
  // TODO(sessionat v2): re-enable browser-close auto-save via M150's
  // BrowserWindowInterface::RegisterBrowserDidClose() callback.
}

SessionHistoryService::~SessionHistoryService() = default;

std::vector<SavedSession> SessionHistoryService::GetAllSessions() const {
  return sessions_;
}

void SessionHistoryService::SaveCurrentSession(const std::string& custom_name) {
  // TODO(sessionat v2): re-implement browser enumeration with M150 APIs.
  // For now, the NTP handler should call SaveSession(custom_name, tabs)
  // directly with tabs gathered on the JS side.
  std::vector<base::DictValue> tabs;
  // Stubbed: code below kept for reference / future restoration. The
  // explicit-parens-around-false form is the chromium-style approved way to
  // mark intentionally-dead code without triggering -Wunreachable-code in
  // is_official_build=true.
  if (/* DISABLES CODE */ (false)) {
    TabStripModel* tab_strip = nullptr;
    for (int i = 0; i < tab_strip->count(); ++i) {
      content::WebContents* contents = tab_strip->GetWebContentsAt(i);
      if (!contents) continue;

      GURL url = contents->GetVisibleURL();

      // Skip chrome:// URLs (including NTP)
      if (url.SchemeIs("chrome") || url.SchemeIs("chrome-extension")) {
        continue;
      }

      base::DictValue tab;
      tab.Set("url", url.spec());
      tab.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
      // Note: Favicon URL would need additional work to get
      tab.Set("favicon", "");

      tabs.push_back(std::move(tab));
    }
  }

  if (!tabs.empty()) {
    SaveSession(custom_name, std::move(tabs));
  }
}

void SessionHistoryService::SaveSession(const std::string& custom_name,
                                         std::vector<base::DictValue> tabs) {
  if (tabs.empty()) return;

  SavedSession session;
  session.id = GenerateSessionId();
  session.timestamp = base::Time::Now().InMillisecondsSinceUnixEpoch();
  session.name = GenerateSessionName(session.timestamp);
  session.custom_name = custom_name;
  session.is_pinned = false;
  session.tabs = std::move(tabs);

  // Add to beginning of list (most recent first)
  sessions_.insert(sessions_.begin(), std::move(session));

  SaveToDisk();

  LOG(INFO) << "[Sessionat] Saved session with " << sessions_[0].tabs.size()
            << " tabs";
}

void SessionHistoryService::DeleteSession(const std::string& session_id) {
  auto it = std::find_if(sessions_.begin(), sessions_.end(),
                         [&session_id](const SavedSession& s) {
                           return s.id == session_id;
                         });
  if (it != sessions_.end()) {
    sessions_.erase(it);
    SaveToDisk();
  }
}

void SessionHistoryService::RenameSession(const std::string& session_id,
                                           const std::string& new_name) {
  for (auto& session : sessions_) {
    if (session.id == session_id) {
      session.custom_name = new_name;
      SaveToDisk();
      break;
    }
  }
}

void SessionHistoryService::TogglePinSession(const std::string& session_id) {
  for (auto& session : sessions_) {
    if (session.id == session_id) {
      session.is_pinned = !session.is_pinned;
      SaveToDisk();
      break;
    }
  }
}

void SessionHistoryService::DuplicateSession(const std::string& session_id) {
  for (const auto& session : sessions_) {
    if (session.id == session_id) {
      SavedSession duplicate;
      duplicate.id = GenerateSessionId();
      duplicate.timestamp = base::Time::Now().InMillisecondsSinceUnixEpoch();
      duplicate.name = session.name + " (Copy)";
      duplicate.custom_name = "";
      duplicate.is_pinned = false;

      for (const auto& tab : session.tabs) {
        duplicate.tabs.push_back(tab.Clone());
      }

      sessions_.insert(sessions_.begin(), std::move(duplicate));
      SaveToDisk();
      break;
    }
  }
}

void SessionHistoryService::MergeSessions(const std::string& session_id_1,
                                           const std::string& session_id_2,
                                           const std::string& merged_name) {
  const SavedSession* session1 = nullptr;
  const SavedSession* session2 = nullptr;

  for (const auto& session : sessions_) {
    if (session.id == session_id_1) session1 = &session;
    if (session.id == session_id_2) session2 = &session;
  }

  if (!session1 || !session2) return;

  SavedSession merged;
  merged.id = GenerateSessionId();
  merged.timestamp = base::Time::Now().InMillisecondsSinceUnixEpoch();
  merged.name = merged_name.empty() ? "Merged Session" : merged_name;
  merged.is_pinned = false;

  for (const auto& tab : session1->tabs) {
    merged.tabs.push_back(tab.Clone());
  }
  for (const auto& tab : session2->tabs) {
    merged.tabs.push_back(tab.Clone());
  }

  sessions_.insert(sessions_.begin(), std::move(merged));
  SaveToDisk();
}

std::string SessionHistoryService::ExportSessionAsJson(
    const std::string& session_id) const {
  for (const auto& session : sessions_) {
    if (session.id == session_id) {
      base::DictValue export_data;
      export_data.Set("exportedAt",
                      static_cast<double>(base::Time::Now().InMillisecondsSinceUnixEpoch()));
      export_data.Set("session", session.ToDict());

      std::string json;
      base::JSONWriter::WriteWithOptions(
          export_data, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
      return json;
    }
  }
  return "";
}

std::string SessionHistoryService::ExportSessionAsText(
    const std::string& session_id) const {
  for (const auto& session : sessions_) {
    if (session.id == session_id) {
      std::string result;
      result += "# Session: " +
                (session.custom_name.empty() ? session.name
                                             : session.custom_name) +
                "\n";
      result +=
          "# Tabs: " + base::NumberToString(session.tabs.size()) + "\n\n";

      for (const auto& tab : session.tabs) {
        const std::string* url = tab.FindString("url");
        if (url) {
          result += *url + "\n";
        }
      }
      return result;
    }
  }
  return "";
}

void SessionHistoryService::SetMinTabsForAutoSave(int min_tabs) {
  min_tabs_for_auto_save_ = std::max(1, min_tabs);
}

void SessionHistoryService::SetAutoSaveEnabled(bool enabled) {
  auto_save_enabled_ = enabled;
}

bool SessionHistoryService::ShouldAutoSave(int current_tab_count) const {
  if (!auto_save_enabled_) return false;
  return current_tab_count >= min_tabs_for_auto_save_;
}

void SessionHistoryService::LoadSessions() {
  base::FilePath file_path = GetSessionFilePath();

  if (!base::PathExists(file_path)) {
    LOG(INFO) << "[Sessionat] No session history file found, starting fresh";
    return;
  }

  std::string contents;
  if (!base::ReadFileToString(file_path, &contents)) {
    LOG(ERROR) << "[Sessionat] Failed to read session history file";
    return;
  }

  auto parsed = base::JSONReader::Read(contents, base::JSON_ALLOW_TRAILING_COMMAS);
  if (!parsed || !parsed->is_dict()) {
    LOG(ERROR) << "[Sessionat] Failed to parse session history JSON";
    return;
  }

  const base::ListValue* sessions_list =
      parsed->GetDict().FindList("sessions");
  if (!sessions_list) return;

  sessions_.clear();
  for (const auto& session_value : *sessions_list) {
    if (session_value.is_dict()) {
      auto session = SavedSession::FromDict(session_value.GetDict());
      if (session) {
        sessions_.push_back(std::move(*session));
      }
    }
  }

  LOG(INFO) << "[Sessionat] Loaded " << sessions_.size()
            << " sessions from history";
}

void SessionHistoryService::SaveToDisk() {
  // Create backup first
  base::FilePath file_path = GetSessionFilePath();
  base::FilePath backup_path = GetBackupFilePath();

  if (base::PathExists(file_path)) {
    base::CopyFile(file_path, backup_path);
  }

  // Build JSON
  base::DictValue root;
  root.Set("version", 1);
  root.Set("lastModified",
           static_cast<double>(
               base::Time::Now().InMillisecondsSinceUnixEpoch()));

  base::ListValue sessions_list;
  for (const auto& session : sessions_) {
    sessions_list.Append(session.ToDict());
  }
  root.Set("sessions", std::move(sessions_list));

  std::string json;
  base::JSONWriter::WriteWithOptions(
      root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);

  // Ensure directory exists
  base::FilePath dir = file_path.DirName();
  if (!base::DirectoryExists(dir)) {
    base::CreateDirectory(dir);
  }

  // Write to disk
  if (!base::WriteFile(file_path, json)) {
    LOG(ERROR) << "[Sessionat] Failed to write session history to disk";
  }
}

std::string SessionHistoryService::GenerateSessionId() const {
  const std::string chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  std::string id;
  for (int i = 0; i < 16; ++i) {
    // RandIntInclusive bound is chars.length()-1, so indexing is safe.
    id += UNSAFE_TODO(chars[base::RandIntInclusive(0, chars.length() - 1)]);
  }
  return id;
}

std::string SessionHistoryService::GenerateSessionName(int64_t timestamp) const {
  base::Time time = base::Time::FromMillisecondsSinceUnixEpoch(timestamp);
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);

  // Format: "Dec 24, 2025 - 3:45 PM"
  const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  int hour12 = exploded.hour % 12;
  if (hour12 == 0) hour12 = 12;
  const char* ampm = exploded.hour < 12 ? "AM" : "PM";

  // months[] is bounded by base::Time::Exploded contract (month ∈ [1,12]),
  // so the -1 indexing is safe-by-construction. Chromium's
  // -Wunsafe-buffer-usage requires explicit acknowledgement.
  return base::StringPrintf("%s %d, %d - %d:%02d %s",
                            UNSAFE_TODO(months[exploded.month - 1]),
                            exploded.day_of_month,
                            exploded.year,
                            hour12,
                            exploded.minute,
                            ampm);
}

base::FilePath SessionHistoryService::GetSessionFilePath() const {
  return profile_->GetPath()
      .AppendASCII("Sessionat")
      .AppendASCII("session_history.json");
}

base::FilePath SessionHistoryService::GetBackupFilePath() const {
  return profile_->GetPath()
      .AppendASCII("Sessionat")
      .AppendASCII("session_history.backup.json");
}

}  // namespace sessionat
