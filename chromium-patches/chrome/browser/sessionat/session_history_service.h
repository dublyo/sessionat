// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SESSIONAT_SESSION_HISTORY_SERVICE_H_
#define CHROME_BROWSER_SESSIONAT_SESSION_HISTORY_SERVICE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
// NOTE(sessionat v2 rebase): chrome/browser/ui/browser_list_observer.h was
// removed in M150. Auto-save-on-window-close is temporarily disabled until we
// re-implement against M150's per-browser RegisterBrowserDidClose callbacks.
#include "components/keyed_service/core/keyed_service.h"

class Browser;
class Profile;

namespace sessionat {

// Represents a single saved session
struct SavedSession {
  std::string id;
  std::string name;
  std::string custom_name;
  int64_t timestamp = 0;
  bool is_pinned = false;
  std::vector<base::DictValue> tabs;

  SavedSession();
  SavedSession(const SavedSession& other);
  SavedSession& operator=(const SavedSession& other);
  SavedSession(SavedSession&&);
  SavedSession& operator=(SavedSession&&);
  ~SavedSession();

  base::DictValue ToDict() const;
  static std::optional<SavedSession> FromDict(const base::DictValue& dict);
};

// Service for managing persistent session history
class SessionHistoryService : public KeyedService {
 public:
  explicit SessionHistoryService(Profile* profile);
  ~SessionHistoryService() override;

  SessionHistoryService(const SessionHistoryService&) = delete;
  SessionHistoryService& operator=(const SessionHistoryService&) = delete;

  // Get all saved sessions
  std::vector<SavedSession> GetAllSessions() const;

  // Save current browser session
  void SaveCurrentSession(const std::string& custom_name = "");

  // Save a session with explicit tabs
  void SaveSession(const std::string& custom_name,
                   std::vector<base::DictValue> tabs);

  // Delete a session by ID
  void DeleteSession(const std::string& session_id);

  // Rename a session
  void RenameSession(const std::string& session_id,
                     const std::string& new_name);

  // Pin/unpin a session
  void TogglePinSession(const std::string& session_id);

  // Duplicate a session
  void DuplicateSession(const std::string& session_id);

  // Merge two sessions
  void MergeSessions(const std::string& session_id_1,
                     const std::string& session_id_2,
                     const std::string& merged_name);

  // Export session to JSON string
  std::string ExportSessionAsJson(const std::string& session_id) const;

  // Export session as plain text URLs
  std::string ExportSessionAsText(const std::string& session_id) const;

  // Get settings
  int GetMinTabsForAutoSave() const { return min_tabs_for_auto_save_; }
  bool IsAutoSaveEnabled() const { return auto_save_enabled_; }

  // Set settings
  void SetMinTabsForAutoSave(int min_tabs);
  void SetAutoSaveEnabled(bool enabled);

  // Check if should auto-save (based on current tabs count)
  bool ShouldAutoSave(int current_tab_count) const;

 private:
  // Load sessions from disk
  void LoadSessions();

  // Save sessions to disk
  void SaveToDisk();

  // Generate unique session ID
  std::string GenerateSessionId() const;

  // Generate session name from timestamp
  std::string GenerateSessionName(int64_t timestamp) const;

  // Get file path for session history
  base::FilePath GetSessionFilePath() const;

  // Get file path for backup
  base::FilePath GetBackupFilePath() const;

  // DanglingUntriaged: see other Sessionat services for rationale.
  raw_ptr<Profile, DanglingUntriaged> profile_;
  std::vector<SavedSession> sessions_;

  // Settings
  int min_tabs_for_auto_save_ = 5;
  bool auto_save_enabled_ = true;
  // Stubbed for v2.1+: not yet referenced by any code path. is_official_build
  // promotes -Wunused-private-field to error; mark explicitly until the
  // auto-save loop that reads these lands.
  [[maybe_unused]] bool skip_ntp_only_ = true;
  [[maybe_unused]] bool has_auto_saved_this_session_ = false;

  base::WeakPtrFactory<SessionHistoryService> weak_factory_{this};
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_SESSION_HISTORY_SERVICE_H_
