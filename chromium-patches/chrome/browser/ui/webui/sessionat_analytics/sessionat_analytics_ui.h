// Copyright 2025 Sessionat. All rights reserved.
// WebUI controller for chrome://sessionat-analytics/ (Visit Analytics dashboard).

#ifndef CHROME_BROWSER_UI_WEBUI_SESSIONAT_ANALYTICS_SESSIONAT_ANALYTICS_UI_H_
#define CHROME_BROWSER_UI_WEBUI_SESSIONAT_ANALYTICS_SESSIONAT_ANALYTICS_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/common/url_constants.h"

class SessionatAnalyticsUI;

class SessionatAnalyticsUIConfig
    : public content::DefaultWebUIConfig<SessionatAnalyticsUI> {
 public:
  SessionatAnalyticsUIConfig();
};

class SessionatAnalyticsHandler : public content::WebUIMessageHandler {
 public:
  SessionatAnalyticsHandler();
  ~SessionatAnalyticsHandler() override;

  SessionatAnalyticsHandler(const SessionatAnalyticsHandler&) = delete;
  SessionatAnalyticsHandler& operator=(const SessionatAnalyticsHandler&) = delete;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleGetWorkspaces(const base::ListValue& args);
  // Unified entry point: args = [rangeKey, workspaceFilterId].
  //   rangeKey ∈ {"today", "7d", "30d"}
  //   workspaceFilterId = "" for all workspaces, otherwise a workspace id.
  // Returns a single payload via sessionatAnalyticsRenderRangeData with stats,
  // top hosts, visit timeline, bucket counts (for the chart), and per-workspace
  // breakdown — all filtered to the requested range/workspace.
  void HandleGetRangeData(const base::ListValue& args);
  // Drill-down on a single host.
  // args = [rangeKey, workspaceFilterId, host].
  void HandleGetHostDetail(const base::ListValue& args);
  void HandleClearAllVisits(const base::ListValue& args);

  // Privacy controls.
  void HandleGetPrivacySettings(const base::ListValue& args);
  void HandleSetTrackingEnabled(const base::ListValue& args);
  void HandleSetExcludedHosts(const base::ListValue& args);
  void HandleExportVisits(const base::ListValue& args);
};

class SessionatAnalyticsUI : public content::WebUIController {
 public:
  explicit SessionatAnalyticsUI(content::WebUI* web_ui);
  ~SessionatAnalyticsUI() override;

  SessionatAnalyticsUI(const SessionatAnalyticsUI&) = delete;
  SessionatAnalyticsUI& operator=(const SessionatAnalyticsUI&) = delete;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

#endif  // CHROME_BROWSER_UI_WEBUI_SESSIONAT_ANALYTICS_SESSIONAT_ANALYTICS_UI_H_
