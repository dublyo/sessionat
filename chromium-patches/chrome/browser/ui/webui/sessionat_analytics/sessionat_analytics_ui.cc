// Copyright 2025 Sessionat. All rights reserved.
// WebUI controller for Sessionat Visit Analytics.

#include "chrome/browser/ui/webui/sessionat_analytics/sessionat_analytics_ui.h"

#include <map>
#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/visit_analytics_service.h"
#include "chrome/browser/sessionat/visit_analytics_service_factory.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/common/webui_url_constants.h"
#include "components/favicon_base/favicon_url_parser.h"
#include "content/public/browser/url_data_source.h"
#include "chrome/grit/sessionat_analytics_resources.h"
#include "chrome/grit/sessionat_analytics_resources_map.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/webui/webui_util.h"

SessionatAnalyticsHandler::SessionatAnalyticsHandler() = default;
SessionatAnalyticsHandler::~SessionatAnalyticsHandler() = default;

void SessionatAnalyticsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getWorkspaces",
      base::BindRepeating(&SessionatAnalyticsHandler::HandleGetWorkspaces,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getRangeData",
      base::BindRepeating(&SessionatAnalyticsHandler::HandleGetRangeData,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getHostDetail",
      base::BindRepeating(&SessionatAnalyticsHandler::HandleGetHostDetail,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearAllVisits",
      base::BindRepeating(&SessionatAnalyticsHandler::HandleClearAllVisits,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPrivacySettings",
      base::BindRepeating(
          &SessionatAnalyticsHandler::HandleGetPrivacySettings,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setTrackingEnabled",
      base::BindRepeating(
          &SessionatAnalyticsHandler::HandleSetTrackingEnabled,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setExcludedHosts",
      base::BindRepeating(
          &SessionatAnalyticsHandler::HandleSetExcludedHosts,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "exportVisits",
      base::BindRepeating(&SessionatAnalyticsHandler::HandleExportVisits,
                          base::Unretained(this)));
}

void SessionatAnalyticsHandler::HandleGetWorkspaces(
    const base::ListValue& args) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::WorkspaceService* service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);

  base::ListValue list;
  if (service) {
    for (const auto& ws : service->GetAllWorkspaces()) {
      base::DictValue dict;
      dict.Set("id", ws.id);
      dict.Set("name", ws.name);
      dict.Set("color", ws.color);
      dict.Set("icon", ws.icon);
      list.Append(std::move(dict));
    }
  }
  // Direct call to a global render fn (cr.js isn't loaded on this page).
  CallJavascriptFunction("sessionatAnalyticsRenderWorkspaces", list);
}

// Maps "today"/"7d"/"30d" to a [start, end) range + a bucket size for the
// chart. Today: 24 hourly buckets; 7d: 7 daily; 30d: 30 daily.
namespace {
struct RangeSpec {
  base::Time start;
  base::Time end;
  base::TimeDelta bucket;
  std::string label;
};

RangeSpec ResolveRange(const std::string& key) {
  base::Time now = base::Time::Now();
  RangeSpec r;
  r.end = now;
  if (key == "30d") {
    r.start = now.LocalMidnight() - base::Days(29);
    r.bucket = base::Days(1);
    r.label = "Last 30 days";
  } else if (key == "7d") {
    r.start = now.LocalMidnight() - base::Days(6);
    r.bucket = base::Days(1);
    r.label = "Last 7 days";
  } else {
    r.start = now.LocalMidnight();
    r.bucket = base::Hours(1);
    r.label = "Today";
  }
  return r;
}
}  // namespace

void SessionatAnalyticsHandler::HandleGetRangeData(
    const base::ListValue& args) {
  AllowJavascript();
  const std::string range_key =
      (args.size() >= 1 && args[0].is_string()) ? args[0].GetString() : "today";
  const std::string ws_filter =
      (args.size() >= 2 && args[1].is_string()) ? args[1].GetString() : "";
  const RangeSpec range = ResolveRange(range_key);

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::VisitAnalyticsService* service =
      sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile);
  sessionat::WorkspaceService* ws_service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);

  std::map<std::string, std::pair<std::string, std::string>> ws_meta;
  if (ws_service) {
    for (const auto& ws : ws_service->GetAllWorkspaces()) {
      ws_meta[ws.id] = {ws.name, ws.color};
    }
  }

  base::DictValue payload;
  payload.Set("range_key", range_key);
  payload.Set("range_label", range.label);
  payload.Set("workspace_filter_id", ws_filter);
  payload.Set("bucket_count",
              static_cast<int>((range.end - range.start) / range.bucket));
  payload.Set("bucket_ms",
              static_cast<double>(range.bucket.InMillisecondsF()));
  payload.Set("range_start_ms",
              static_cast<double>(range.start.InMillisecondsFSinceUnixEpoch()));

  if (!service) {
    CallJavascriptFunction("sessionatAnalyticsRenderRangeData", payload);
    return;
  }

  base::ListValue top_hosts;
  for (const auto& [host, count] :
       service->GetHostCountsInRange(range.start, range.end, ws_filter)) {
    base::DictValue d;
    d.Set("host", host);
    d.Set("count", count);
    d.Set("category", service->GetCategoryForHost(host));
    top_hosts.Append(std::move(d));
  }
  payload.Set("top_hosts", std::move(top_hosts));

  // Per-category breakdown.
  base::ListValue categories;
  int total_active_ms = 0;
  for (const auto& s :
       service->GetCategoryStatsInRange(range.start, range.end, ws_filter)) {
    base::DictValue d;
    d.Set("category", s.category);
    d.Set("count", s.total_visits);
    d.Set("active_ms", s.total_active_ms);
    categories.Append(std::move(d));
    total_active_ms += s.total_active_ms;
  }
  payload.Set("categories", std::move(categories));
  payload.Set("total_active_ms", total_active_ms);

  base::ListValue visits;
  size_t cap = 0;
  for (const auto& v : service->GetVisitsInRange(range.start, range.end)) {
    if (!ws_filter.empty() && v.workspace_id != ws_filter) continue;
    if (cap++ >= 200) break;
    base::DictValue d;
    d.Set("url", v.url.spec());
    d.Set("host", v.host);
    d.Set("title", v.title);
    d.Set("workspace_id", v.workspace_id);
    auto it = ws_meta.find(v.workspace_id);
    d.Set("workspace_name", it != ws_meta.end() ? it->second.first : "");
    d.Set("workspace_color", it != ws_meta.end() ? it->second.second : "");
    d.Set("timestamp",
          static_cast<double>(v.timestamp.InMillisecondsSinceUnixEpoch()));
    d.Set("active_ms", v.active_ms);
    d.Set("category", service->GetCategoryForHost(v.host));
    visits.Append(std::move(d));
  }
  payload.Set("visits", std::move(visits));

  base::ListValue buckets;
  for (int n : service->GetBucketCounts(range.start, range.end, range.bucket,
                                         ws_filter)) {
    buckets.Append(n);
  }
  payload.Set("buckets", std::move(buckets));

  base::ListValue ws_counts;
  if (ws_service) {
    for (const auto& ws : ws_service->GetAllWorkspaces()) {
      int count = 0;
      for (const auto& v :
           service->GetVisitsInRange(range.start, range.end)) {
        if (v.workspace_id == ws.id) count++;
      }
      base::DictValue d;
      d.Set("id", ws.id);
      d.Set("name", ws.name);
      d.Set("color", ws.color);
      d.Set("icon", ws.icon);
      d.Set("count", count);
      ws_counts.Append(std::move(d));
    }
  }
  payload.Set("workspaces", std::move(ws_counts));

  size_t total_visits = 0;
  for (const auto& v : service->GetVisitsInRange(range.start, range.end)) {
    if (!ws_filter.empty() && v.workspace_id != ws_filter) continue;
    total_visits++;
  }
  payload.Set("total_visits", static_cast<int>(total_visits));
  const auto* hosts_list = payload.FindList("top_hosts");
  payload.Set("unique_hosts",
              static_cast<int>(hosts_list ? hosts_list->size() : 0));
  if (hosts_list && !hosts_list->empty()) {
    const auto* first = (*hosts_list)[0].GetIfDict();
    if (first) {
      const std::string* host = first->FindString("host");
      payload.Set("top_host", host ? *host : "");
    }
  }

  CallJavascriptFunction("sessionatAnalyticsRenderRangeData", payload);
}

void SessionatAnalyticsHandler::HandleGetHostDetail(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[2].is_string()) return;
  const std::string range_key =
      args[0].is_string() ? args[0].GetString() : "today";
  const std::string ws_filter =
      args[1].is_string() ? args[1].GetString() : "";
  const std::string host = args[2].GetString();
  const RangeSpec range = ResolveRange(range_key);

  Profile* profile = Profile::FromWebUI(web_ui());
  sessionat::VisitAnalyticsService* service =
      sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile);
  sessionat::WorkspaceService* ws_service =
      sessionat::WorkspaceServiceFactory::GetForProfile(profile);

  std::map<std::string, std::string> ws_names;
  if (ws_service) {
    for (const auto& ws : ws_service->GetAllWorkspaces()) {
      ws_names[ws.id] = ws.name;
    }
  }

  base::DictValue payload;
  payload.Set("host", host);
  payload.Set("range_key", range_key);

  base::ListValue visits;
  if (service) {
    for (const auto& v : service->GetVisitsForHostInRange(host, range.start,
                                                           range.end,
                                                           ws_filter)) {
      base::DictValue d;
      d.Set("url", v.url.spec());
      d.Set("host", v.host);
      d.Set("title", v.title);
      d.Set("workspace_id", v.workspace_id);
      auto it = ws_names.find(v.workspace_id);
      d.Set("workspace_name", it != ws_names.end() ? it->second : "");
      d.Set("timestamp",
            static_cast<double>(v.timestamp.InMillisecondsSinceUnixEpoch()));
      visits.Append(std::move(d));
    }
  }
  payload.Set("visits", std::move(visits));
  CallJavascriptFunction("sessionatAnalyticsRenderHostDetail", payload);
}

void SessionatAnalyticsHandler::HandleGetPrivacySettings(
    const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  auto* svc = sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile);
  base::DictValue payload;
  if (svc) {
    payload.Set("tracking_enabled", svc->IsTrackingEnabled());
    base::ListValue hosts;
    for (auto& h : svc->GetExcludedHosts()) hosts.Append(std::move(h));
    payload.Set("excluded_hosts", std::move(hosts));
  } else {
    payload.Set("tracking_enabled", true);
    payload.Set("excluded_hosts", base::ListValue());
  }
  CallJavascriptFunction("sessionatAnalyticsRenderPrivacy", payload);
}

void SessionatAnalyticsHandler::HandleSetTrackingEnabled(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_bool()) return;
  Profile* profile = Profile::FromWebUI(web_ui());
  if (auto* svc =
          sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile)) {
    svc->SetTrackingEnabled(args[0].GetBool());
  }
  base::ListValue empty;
  HandleGetPrivacySettings(empty);
}

void SessionatAnalyticsHandler::HandleSetExcludedHosts(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_list()) return;
  std::vector<std::string> hosts;
  for (const auto& v : args[0].GetList()) {
    if (v.is_string() && !v.GetString().empty()) {
      hosts.push_back(v.GetString());
    }
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  if (auto* svc =
          sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile)) {
    svc->SetExcludedHosts(std::move(hosts));
  }
  base::ListValue empty;
  HandleGetPrivacySettings(empty);
}

void SessionatAnalyticsHandler::HandleExportVisits(
    const base::ListValue& /*args*/) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  std::string json;
  if (auto* svc =
          sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile)) {
    json = svc->ExportToJson();
  }
  base::DictValue payload;
  payload.Set("filename",
              "sessionat-visits-" +
                  base::NumberToString(
                      base::Time::Now().InMillisecondsSinceUnixEpoch()) +
                  ".json");
  payload.Set("json", std::move(json));
  CallJavascriptFunction("sessionatAnalyticsDownloadExport", payload);
}

void SessionatAnalyticsHandler::HandleClearAllVisits(
    const base::ListValue& args) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  if (auto* service =
          sessionat::VisitAnalyticsServiceFactory::GetForProfile(profile)) {
    service->Clear();
  }
  // Refresh whatever range the page is currently showing — defaults to today.
  base::ListValue refresh;
  refresh.Append("today");
  refresh.Append("");
  HandleGetRangeData(refresh);
}

SessionatAnalyticsUIConfig::SessionatAnalyticsUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme,
                         chrome::kChromeUISessionatAnalyticsHost) {}

WEB_UI_CONTROLLER_TYPE_IMPL(SessionatAnalyticsUI)

SessionatAnalyticsUI::SessionatAnalyticsUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(),
      chrome::kChromeUISessionatAnalyticsHost);

  webui::SetupWebUIDataSource(source, kSessionatAnalyticsResources,
                              IDR_SESSIONAT_ANALYTICS_SESSIONAT_ANALYTICS_HTML);

  // Register FaviconSource so chrome://favicon2/?pageUrl=... URLs from
  // this page resolve (favicons on the visit list + top-hosts cards).
  Profile* profile =
      Profile::FromBrowserContext(web_ui->GetWebContents()->GetBrowserContext());
  content::URLDataSource::Add(
      profile, std::make_unique<FaviconSource>(
                   profile, chrome::FaviconUrlFormat::kFavicon2));

  web_ui->AddMessageHandler(std::make_unique<SessionatAnalyticsHandler>());
}

SessionatAnalyticsUI::~SessionatAnalyticsUI() = default;
