// Copyright 2025 Sessionat. All rights reserved.
// WebUI controller for Sessionat New Tab Page.

#ifndef CHROME_BROWSER_UI_WEBUI_SESSIONAT_NTP_SESSIONAT_NTP_UI_H_
#define CHROME_BROWSER_UI_WEBUI_SESSIONAT_NTP_SESSIONAT_NTP_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class SessionatNtpUI;

class SessionatNtpUIConfig
    : public content::DefaultWebUIConfig<SessionatNtpUI> {
 public:
  SessionatNtpUIConfig();
};

// Message handler for the Sessionat NTP page.
class SessionatNtpHandler : public content::WebUIMessageHandler {
 public:
  SessionatNtpHandler();
  ~SessionatNtpHandler() override;

  SessionatNtpHandler(const SessionatNtpHandler&) = delete;
  SessionatNtpHandler& operator=(const SessionatNtpHandler&) = delete;

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;

 private:
  // Recently closed tabs (TabRestoreService)
  void HandleGetRecentSessions(const base::ListValue& args);
  void HandleRestoreSession(const base::ListValue& args);
  void HandleOpenUrl(const base::ListValue& args);

  // Session History (persistent saved sessions)
  void HandleGetSessionHistory(const base::ListValue& args);
  void HandleSaveCurrentSession(const base::ListValue& args);
  void HandleDeleteSavedSession(const base::ListValue& args);
  void HandleRestoreSavedSession(const base::ListValue& args);
  void HandleUpdateSessionHistorySettings(const base::ListValue& args);
  void HandleCheckAutoSave(const base::ListValue& args);

  // Update checker removed in v2.0 — Sparkle 2 (see version_updater_mac.mm).

  // Workspaces (Sessionat v2 Phase 1)
  void HandleGetWorkspaces(const base::ListValue& args);
  void HandleCreateWorkspace(const base::ListValue& args);
  void HandleSwitchWorkspace(const base::ListValue& args);
  void HandleDeleteWorkspace(const base::ListValue& args);
  void HandleTogglePinWorkspace(const base::ListValue& args);

  // Visit Analytics (Sessionat v2 Phase 2) — Top sites today widget.
  void HandleGetTopSitesToday(const base::ListValue& args);

  // Helper to send session history to JS
  void SendSessionHistoryToJS();
};

class SessionatNtpUI : public content::WebUIController {
 public:
  explicit SessionatNtpUI(content::WebUI* web_ui);
  ~SessionatNtpUI() override;

  SessionatNtpUI(const SessionatNtpUI&) = delete;
  SessionatNtpUI& operator=(const SessionatNtpUI&) = delete;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_SESSIONAT_NTP_SESSIONAT_NTP_UI_H_
