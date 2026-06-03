// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/mcp_tools.h"

#include <optional>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/mcp/mcp_service.h"
#include "chrome/browser/sessionat/mcp/mcp_service_factory.h"
#include "chrome/browser/sessionat/visit_analytics_service.h"
#include "chrome/browser/sessionat/visit_analytics_service_factory.h"
#include "chrome/browser/sessionat/workspace_model.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_user_gesture_details.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

namespace sessionat {

namespace {

// Pull the request token (placed under args._ctx by McpService) for write-gate
// lookups. Returns empty when the tool was invoked from a non-MCP path (e.g.
// internal test harness).
std::string RequestTokenFromArgs(const base::DictValue& args) {
  const base::DictValue* ctx = args.FindDict("_ctx");
  if (!ctx) return std::string();
  const std::string* t = ctx->FindString("token");
  return t ? *t : std::string();
}

std::string RequestClientIdFromArgs(const base::DictValue& args) {
  const base::DictValue* ctx = args.FindDict("_ctx");
  if (!ctx) return std::string();
  const std::string* c = ctx->FindString("client_id");
  return c ? *c : std::string();
}

// Wrap a JSON-style Dict as a tools/call result with content blocks.
base::DictValue TextResult(base::Value v) {
  std::string text;
  base::JSONWriter::WriteWithOptions(
      v, base::JSONWriter::OPTIONS_PRETTY_PRINT, &text);
  base::DictValue result;
  base::ListValue content;
  base::DictValue block;
  block.Set("type", "text");
  block.Set("text", std::move(text));
  content.Append(std::move(block));
  result.Set("content", std::move(content));
  result.Set("isError", false);
  return result;
}

base::DictValue ErrorResult(const std::string& message) {
  base::DictValue result;
  base::ListValue content;
  base::DictValue block;
  block.Set("type", "text");
  block.Set("text", message);
  content.Append(std::move(block));
  result.Set("content", std::move(content));
  result.Set("isError", true);
  return result;
}

// Structured "needs approval" error. Adds a `_meta.sessionat_error` so the
// WebUI can pick it up and prompt the user.
base::DictValue WriteApprovalNeeded(const std::string& client_id) {
  base::DictValue result = ErrorResult(
      "Sessionat write tools require approval. Open "
      "chrome://sessionat-mcp/ to grant access.");
  base::DictValue meta;
  meta.Set("sessionat_error", "write_requires_approval");
  meta.Set("client_id", client_id);
  result.Set("_meta", std::move(meta));
  return result;
}

// Gate helper: returns std::nullopt when the caller is allowed to write,
// otherwise the appropriate error payload.
std::optional<base::DictValue> CheckWriteGate(Profile* profile,
                                               const base::DictValue& args) {
  auto* mcp_svc = sessionat::McpServiceFactory::GetForProfile(profile);
  if (!mcp_svc) return ErrorResult("MCP service unavailable.");
  const std::string token = RequestTokenFromArgs(args);
  if (token.empty() || !mcp_svc->IsTokenKnown(token)) {
    return ErrorResult(
        "Write tools require a known bearer token. Connect this client "
        "from chrome://sessionat-mcp/.");
  }
  if (!mcp_svc->IsWriteAllowedForToken(token)) {
    return WriteApprovalNeeded(RequestClientIdFromArgs(args));
  }
  return std::nullopt;
}

// MCP image content block — Claude Desktop / clients will surface this as
// a real image rather than a base64 blob. See spec at
// modelcontextprotocol.io/docs/specification (tools/call result content).
base::DictValue ImageResult(const std::string& data_base64,
                            const std::string& mime_type) {
  base::DictValue result;
  base::ListValue content;
  base::DictValue block;
  block.Set("type", "image");
  block.Set("data", data_base64);
  block.Set("mimeType", mime_type);
  content.Append(std::move(block));
  result.Set("content", std::move(content));
  result.Set("isError", false);
  return result;
}

// Schema helpers.
base::DictValue EmptyInputSchema() {
  base::DictValue s;
  s.Set("type", "object");
  s.Set("properties", base::DictValue());
  s.Set("additionalProperties", false);
  return s;
}

base::DictValue StringProp(const std::string& desc) {
  base::DictValue p;
  p.Set("type", "string");
  p.Set("description", desc);
  return p;
}

base::DictValue EnumProp(const std::string& desc,
                            std::vector<std::string> values,
                            const std::string& default_value = "") {
  base::DictValue p;
  p.Set("type", "string");
  p.Set("description", desc);
  base::ListValue enum_values;
  for (auto& v : values) enum_values.Append(std::move(v));
  p.Set("enum", std::move(enum_values));
  if (!default_value.empty()) p.Set("default", default_value);
  return p;
}

base::DictValue NumberProp(const std::string& desc,
                              int min = 1,
                              int max = 500) {
  base::DictValue p;
  p.Set("type", "integer");
  p.Set("description", desc);
  p.Set("minimum", min);
  p.Set("maximum", max);
  return p;
}

// Time-range mapping shared with the analytics dashboard.
struct Range {
  base::Time start, end;
  std::string label;
};
Range ResolveRange(const std::string& key) {
  Range r;
  r.end = base::Time::Now();
  if (key == "all") {
    r.start = base::Time();
    r.label = "All time";
  } else if (key == "30d") {
    r.start = r.end.LocalMidnight() - base::Days(29);
    r.label = "Last 30 days";
  } else if (key == "7d") {
    r.start = r.end.LocalMidnight() - base::Days(6);
    r.label = "Last 7 days";
  } else {
    r.start = r.end.LocalMidnight();
    r.label = "Today";
  }
  return r;
}

// ============ Tool implementations ============

// sessionat_list_workspaces
base::DictValue ListWorkspaces(Profile* profile,
                                   const base::DictValue& /*args*/) {
  auto* ws_svc = WorkspaceServiceFactory::GetForProfile(profile);
  if (!ws_svc) return ErrorResult("WorkspaceService unavailable");

  base::DictValue out;
  out.Set("active_workspace_id", ws_svc->GetActiveWorkspaceId());
  base::ListValue list;
  for (const auto& ws : ws_svc->GetAllWorkspaces()) {
    base::DictValue d;
    d.Set("id", ws.id);
    d.Set("name", ws.name);
    d.Set("color", ws.color);
    d.Set("icon", ws.icon);
    d.Set("description", ws.description);
    d.Set("tab_count", static_cast<int>(ws.items.size()));
    d.Set("is_active", ws.id == ws_svc->GetActiveWorkspaceId());
    d.Set("is_pinned", ws.is_pinned);
    list.Append(std::move(d));
  }
  out.Set("workspaces", std::move(list));
  return TextResult(base::Value(std::move(out)));
}

// sessionat_get_active_workspace
base::DictValue GetActiveWorkspace(Profile* profile,
                                       const base::DictValue& /*args*/) {
  auto* ws_svc = WorkspaceServiceFactory::GetForProfile(profile);
  if (!ws_svc) return ErrorResult("WorkspaceService unavailable");
  const Workspace* ws = ws_svc->GetActiveWorkspace();
  if (!ws) {
    base::DictValue d;
    d.Set("active", false);
    d.Set("message", "No active workspace");
    return TextResult(base::Value(std::move(d)));
  }
  base::DictValue d;
  d.Set("id", ws->id);
  d.Set("name", ws->name);
  d.Set("color", ws->color);
  d.Set("icon", ws->icon);
  d.Set("tab_count", static_cast<int>(ws->items.size()));
  base::ListValue tabs;
  for (const auto& item : ws->items) {
    base::DictValue t;
    t.Set("url", item.url.spec());
    t.Set("title", item.title);
    tabs.Append(std::move(t));
  }
  d.Set("tabs", std::move(tabs));
  return TextResult(base::Value(std::move(d)));
}

// sessionat_get_workspace
base::DictValue GetWorkspace(Profile* profile,
                                const base::DictValue& args) {
  const std::string* id = args.FindString("workspace_id");
  if (!id || id->empty()) return ErrorResult("Missing workspace_id");
  auto* ws_svc = WorkspaceServiceFactory::GetForProfile(profile);
  if (!ws_svc) return ErrorResult("WorkspaceService unavailable");
  const Workspace* ws = ws_svc->GetWorkspace(*id);
  if (!ws) return ErrorResult("No workspace with id " + *id);
  base::DictValue d;
  d.Set("id", ws->id);
  d.Set("name", ws->name);
  d.Set("color", ws->color);
  d.Set("icon", ws->icon);
  d.Set("description", ws->description);
  d.Set("is_active", ws->id == ws_svc->GetActiveWorkspaceId());
  d.Set("is_pinned", ws->is_pinned);
  base::ListValue tabs;
  for (const auto& item : ws->items) {
    base::DictValue t;
    t.Set("id", item.id);
    t.Set("url", item.url.spec());
    t.Set("title", item.title);
    tabs.Append(std::move(t));
  }
  d.Set("tabs", std::move(tabs));
  return TextResult(base::Value(std::move(d)));
}

// sessionat_create_workspace — creates a new workspace with the given name,
// optional color (hex like "#F97316") and icon (emoji or single char).
// Returns the new workspace's id + initial state. Write-gated.
base::DictValue CreateWorkspace(Profile* profile,
                                const base::DictValue& args) {
  if (auto err = CheckWriteGate(profile, args); err) return std::move(*err);
  const std::string* name = args.FindString("name");
  if (!name || name->empty()) {
    return ErrorResult("Missing 'name' argument (1-50 chars).");
  }
  if (name->size() > 50) {
    return ErrorResult("'name' must be 50 characters or fewer.");
  }
  const std::string color =
      args.FindString("color") ? *args.FindString("color") : "";
  const std::string icon =
      args.FindString("icon") ? *args.FindString("icon") : "";

  auto* ws_svc = WorkspaceServiceFactory::GetForProfile(profile);
  if (!ws_svc) return ErrorResult("WorkspaceService unavailable");

  const std::string new_id = ws_svc->CreateWorkspace(*name, color, icon);
  if (new_id.empty()) {
    return ErrorResult("Failed to create workspace.");
  }

  const Workspace* ws = ws_svc->GetWorkspace(new_id);
  base::DictValue d;
  d.Set("ok", true);
  d.Set("id", new_id);
  d.Set("name", ws ? ws->name : *name);
  d.Set("color", ws ? ws->color : color);
  d.Set("icon", ws ? ws->icon : icon);
  d.Set("tab_count", 0);
  return TextResult(base::Value(std::move(d)));
}

// sessionat_get_visits
base::DictValue GetVisits(Profile* profile, const base::DictValue& args) {
  const std::string range_key =
      args.FindString("range") ? *args.FindString("range") : "today";
  const std::string ws_filter =
      args.FindString("workspace_id") ? *args.FindString("workspace_id") : "";
  int limit = args.FindInt("limit").value_or(50);
  limit = std::clamp(limit, 1, 500);

  auto* svc = VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (!svc) return ErrorResult("VisitAnalyticsService unavailable");

  const Range r = ResolveRange(range_key);
  base::Time effective_start =
      range_key == "all" ? base::Time::UnixEpoch() : r.start;
  std::vector<Visit> visits =
      svc->GetVisitsInRange(effective_start, r.end);

  base::ListValue rows;
  int kept = 0;
  for (const auto& v : visits) {
    if (!ws_filter.empty() && v.workspace_id != ws_filter) continue;
    if (kept >= limit) break;
    kept++;
    base::DictValue d;
    d.Set("url", v.url.spec());
    d.Set("host", v.host);
    d.Set("title", v.title);
    d.Set("workspace_id", v.workspace_id);
    d.Set("timestamp_ms",
          static_cast<double>(v.timestamp.InMillisecondsSinceUnixEpoch()));
    d.Set("active_seconds", v.active_ms / 1000);
    d.Set("category", svc->GetCategoryForHost(v.host));
    rows.Append(std::move(d));
  }
  base::DictValue out;
  out.Set("range", r.label);
  out.Set("workspace_filter", ws_filter);
  out.Set("count", kept);
  out.Set("visits", std::move(rows));
  return TextResult(base::Value(std::move(out)));
}

// sessionat_get_top_sites
base::DictValue GetTopSites(Profile* profile,
                                const base::DictValue& args) {
  const std::string range_key =
      args.FindString("range") ? *args.FindString("range") : "today";
  const std::string ws_filter =
      args.FindString("workspace_id") ? *args.FindString("workspace_id") : "";
  int limit = args.FindInt("limit").value_or(10);
  limit = std::clamp(limit, 1, 100);
  std::string order_by =
      args.FindString("order_by") ? *args.FindString("order_by") : "visits";
  if (order_by != "visits" && order_by != "active_seconds") {
    return ErrorResult(
        "order_by must be \"visits\" or \"active_seconds\"");
  }

  auto* svc = VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (!svc) return ErrorResult("VisitAnalyticsService unavailable");
  const Range r = ResolveRange(range_key);
  base::Time effective_start =
      range_key == "all" ? base::Time::UnixEpoch() : r.start;

  auto stats = svc->GetHostStatsInRange(effective_start, r.end, ws_filter);
  if (order_by == "active_seconds") {
    std::sort(stats.begin(), stats.end(),
              [](const auto& a, const auto& b) {
                if (a.total_active_ms != b.total_active_ms) {
                  return a.total_active_ms > b.total_active_ms;
                }
                return a.host < b.host;
              });
  }

  base::ListValue rows;
  int kept = 0;
  for (const auto& s : stats) {
    if (kept >= limit) break;
    kept++;
    base::DictValue d;
    d.Set("host", s.host);
    d.Set("visit_count", s.visit_count);
    d.Set("active_seconds", s.total_active_ms / 1000);
    d.Set("category", svc->GetCategoryForHost(s.host));
    rows.Append(std::move(d));
  }
  base::DictValue out;
  out.Set("range", r.label);
  out.Set("workspace_filter", ws_filter);
  out.Set("order_by", order_by);
  out.Set("top_sites", std::move(rows));
  return TextResult(base::Value(std::move(out)));
}

// sessionat_get_category_breakdown
base::DictValue GetCategoryBreakdown(Profile* profile,
                                         const base::DictValue& args) {
  const std::string range_key =
      args.FindString("range") ? *args.FindString("range") : "today";
  const std::string ws_filter =
      args.FindString("workspace_id") ? *args.FindString("workspace_id") : "";

  auto* svc = VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (!svc) return ErrorResult("VisitAnalyticsService unavailable");
  const Range r = ResolveRange(range_key);
  base::Time effective_start =
      range_key == "all" ? base::Time::UnixEpoch() : r.start;

  auto stats =
      svc->GetCategoryStatsInRange(effective_start, r.end, ws_filter);

  int total_visits = 0;
  int total_active_ms = 0;
  for (const auto& s : stats) {
    total_visits += s.total_visits;
    total_active_ms += s.total_active_ms;
  }

  base::ListValue rows;
  for (const auto& s : stats) {
    base::DictValue d;
    d.Set("category", s.category);
    d.Set("visit_count", s.total_visits);
    d.Set("active_seconds", s.total_active_ms / 1000);
    d.Set("share_pct",
          total_visits == 0
              ? 0
              : static_cast<int>(100.0 * s.total_visits / total_visits + 0.5));
    rows.Append(std::move(d));
  }
  base::DictValue out;
  out.Set("range", r.label);
  out.Set("workspace_filter", ws_filter);
  out.Set("total_visits", total_visits);
  out.Set("total_active_seconds", total_active_ms / 1000);
  out.Set("categories", std::move(rows));
  return TextResult(base::Value(std::move(out)));
}

// sessionat_search_visits
base::DictValue SearchVisits(Profile* profile, const base::DictValue& args) {
  const std::string query =
      args.FindString("query") ? *args.FindString("query") : "";
  if (query.empty()) {
    return ErrorResult("query is required and must be non-empty");
  }
  const std::string range_key =
      args.FindString("range") ? *args.FindString("range") : "7d";
  const std::string ws_filter =
      args.FindString("workspace_id") ? *args.FindString("workspace_id") : "";
  int limit = args.FindInt("limit").value_or(50);
  limit = std::clamp(limit, 1, 500);

  auto* svc = VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (!svc) return ErrorResult("VisitAnalyticsService unavailable");

  const Range r = ResolveRange(range_key);
  base::Time effective_start =
      range_key == "all" ? base::Time::UnixEpoch() : r.start;

  std::vector<Visit> visits = svc->SearchVisitsInRange(
      query, effective_start, r.end, static_cast<size_t>(limit), ws_filter);

  base::ListValue rows;
  for (const auto& v : visits) {
    base::DictValue d;
    d.Set("url", v.url.spec());
    d.Set("host", v.host);
    d.Set("title", v.title);
    d.Set("workspace_id", v.workspace_id);
    d.Set("timestamp_ms",
          static_cast<double>(v.timestamp.InMillisecondsSinceUnixEpoch()));
    d.Set("active_seconds", v.active_ms / 1000);
    d.Set("category", svc->GetCategoryForHost(v.host));
    rows.Append(std::move(d));
  }

  base::DictValue out;
  out.Set("query", query);
  out.Set("range", r.label);
  out.Set("workspace_filter", ws_filter);
  out.Set("count", static_cast<int>(visits.size()));
  out.Set("visits", std::move(rows));
  return TextResult(base::Value(std::move(out)));
}

// sessionat_get_visits_for_host
base::DictValue GetVisitsForHost(Profile* profile,
                                 const base::DictValue& args) {
  const std::string* host_in = args.FindString("host");
  if (!host_in || host_in->empty()) {
    return ErrorResult("Missing 'host' argument.");
  }
  std::string host = base::ToLowerASCII(*host_in);
  if (base::StartsWith(host, "www.", base::CompareCase::SENSITIVE)) {
    host = host.substr(4);
  }

  const std::string range_key =
      args.FindString("range") ? *args.FindString("range") : "30d";
  const std::string ws_filter =
      args.FindString("workspace_id") ? *args.FindString("workspace_id") : "";
  int limit = args.FindInt("limit").value_or(100);
  limit = std::clamp(limit, 1, 500);

  auto* svc = VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (!svc) return ErrorResult("VisitAnalyticsService unavailable");

  const Range r = ResolveRange(range_key);
  base::Time effective_start =
      range_key == "all" ? base::Time::UnixEpoch() : r.start;

  std::vector<Visit> visits =
      svc->GetVisitsForHostInRange(host, effective_start, r.end, ws_filter);

  base::ListValue rows;
  int kept = 0;
  for (const auto& v : visits) {
    if (kept >= limit) break;
    kept++;
    base::DictValue d;
    d.Set("url", v.url.spec());
    d.Set("host", v.host);
    d.Set("title", v.title);
    d.Set("workspace_id", v.workspace_id);
    d.Set("timestamp_ms",
          static_cast<double>(v.timestamp.InMillisecondsSinceUnixEpoch()));
    d.Set("active_seconds", v.active_ms / 1000);
    d.Set("category", svc->GetCategoryForHost(v.host));
    rows.Append(std::move(d));
  }

  base::DictValue out;
  out.Set("host", host);
  out.Set("range", r.label);
  out.Set("workspace_filter", ws_filter);
  out.Set("count", kept);
  out.Set("visits", std::move(rows));
  return TextResult(base::Value(std::move(out)));
}

// sessionat_get_visit_buckets
base::DictValue GetVisitBuckets(Profile* profile,
                                const base::DictValue& args) {
  const std::string bucket_key =
      args.FindString("bucket") ? *args.FindString("bucket") : "";
  if (bucket_key != "hour" && bucket_key != "day_of_week" &&
      bucket_key != "day") {
    return ErrorResult(
        "Missing or invalid 'bucket' (expected hour|day_of_week|day).");
  }
  const std::string range_key =
      args.FindString("range") ? *args.FindString("range") : "30d";
  const std::string ws_filter =
      args.FindString("workspace_id") ? *args.FindString("workspace_id") : "";

  auto* svc = VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (!svc) return ErrorResult("VisitAnalyticsService unavailable");

  const Range r = ResolveRange(range_key);
  base::Time effective_start =
      range_key == "all" ? base::Time::UnixEpoch() : r.start;

  VisitAnalyticsService::BucketMode mode =
      VisitAnalyticsService::BucketMode::kHour;
  if (bucket_key == "day_of_week") {
    mode = VisitAnalyticsService::BucketMode::kDayOfWeek;
  } else if (bucket_key == "day") {
    mode = VisitAnalyticsService::BucketMode::kDay;
  }

  // Day-bucket runs scale with the date range; cap at one year to keep the
  // response bounded for range="all".
  if (mode == VisitAnalyticsService::BucketMode::kDay) {
    constexpr int kMaxDayBuckets = 366;
    base::Time min_start =
        r.end.LocalMidnight() - base::Days(kMaxDayBuckets - 1);
    if (effective_start < min_start) effective_start = min_start;
  }

  std::vector<VisitAnalyticsService::BucketStat> stats =
      svc->GetVisitBuckets(effective_start, r.end, mode, ws_filter);

  base::ListValue rows;
  int total_visits = 0;
  int total_active_ms = 0;
  for (const auto& s : stats) {
    base::DictValue d;
    d.Set("key", s.key);
    d.Set("visit_count", s.visit_count);
    d.Set("active_seconds", s.total_active_ms / 1000);
    rows.Append(std::move(d));
    total_visits += s.visit_count;
    total_active_ms += s.total_active_ms;
  }

  base::DictValue out;
  out.Set("bucket", bucket_key);
  out.Set("range", r.label);
  out.Set("workspace_filter", ws_filter);
  out.Set("bucket_count", static_cast<int>(stats.size()));
  out.Set("total_visits", total_visits);
  out.Set("total_active_seconds", total_active_ms / 1000);
  out.Set("buckets", std::move(rows));
  return TextResult(base::Value(std::move(out)));
}

// ====================================================================
// Write tools — gated on McpService::IsWriteEnabled() (CLAUDE.md rule)
// ====================================================================

// sessionat_open_url — opens a URL in a new foreground tab in the user's
// active Sessionat window.
base::DictValue OpenUrl(Profile* profile, const base::DictValue& args) {
  if (auto err = CheckWriteGate(profile, args); err) return std::move(*err);
  const std::string* url_str = args.FindString("url");
  if (!url_str || url_str->empty()) {
    return ErrorResult("Missing 'url' argument.");
  }
  GURL url(*url_str);
  if (!url.is_valid() || !(url.SchemeIs("http") || url.SchemeIs("https"))) {
    return ErrorResult("URL must be a valid http(s):// URL.");
  }
  const bool background = args.FindBool("background").value_or(false);

  // Find the most-recently-active tabbed browser for this profile.
  auto* coll = ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface* browser =
      coll ? coll->FindTabbedBrowser(/*match_original_profiles=*/false)
            : nullptr;
  if (!browser) {
    return ErrorResult(
        "No Sessionat browser window open. Open Sessionat first.");
  }
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = background
                           ? WindowOpenDisposition::NEW_BACKGROUND_TAB
                           : WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);

  base::DictValue out;
  out.Set("opened_url", url.spec());
  out.Set("disposition", background ? "background_tab" : "foreground_tab");
  return TextResult(base::Value(std::move(out)));
}

// ====================================================================
// Browser-driving tools — these let an MCP client (the AI) inspect and
// control the user's tabs as a browsing surface, not just a bookmark
// store. All are gated on IsWriteEnabled() because they mutate state.
// ====================================================================

// Internal helper — returns active tabbed browser or null + sets `err`.
BrowserWindowInterface* GetActiveTabbedBrowser(Profile* profile,
                                                 std::string* err) {
  auto* coll = ProfileBrowserCollection::GetForProfile(profile);
  BrowserWindowInterface* b =
      coll ? coll->FindTabbedBrowser(/*match_original_profiles=*/false)
            : nullptr;
  if (!b && err) *err = "No Sessionat browser window open.";
  return b;
}

base::DictValue WriteGate(Profile* profile, base::DictValue (*body)(
                              Profile*, const base::DictValue&),
                          const base::DictValue& args) {
  if (auto err = CheckWriteGate(profile, args); err) return std::move(*err);
  return body(profile, args);
}

base::DictValue TabToDict(content::WebContents* wc, int index, bool is_active) {
  base::DictValue t;
  t.Set("index", index);
  t.Set("is_active", is_active);
  if (wc) {
    t.Set("url", wc->GetLastCommittedURL().spec());
    t.Set("title", base::UTF16ToUTF8(wc->GetTitle()));
  }
  return t;
}

// sessionat_list_open_tabs — every tab in the currently-active browser
// window with its index, url, title, and active flag.
base::DictValue ListOpenTabsImpl(Profile* profile, const base::DictValue&) {
  std::string err;
  auto* browser = GetActiveTabbedBrowser(profile, &err);
  if (!browser) return ErrorResult(err);
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) return ErrorResult("No tab strip on the active browser.");
  base::DictValue out;
  out.Set("count", tab_strip->count());
  out.Set("active_index", tab_strip->active_index());
  base::ListValue tabs;
  for (int i = 0; i < tab_strip->count(); ++i) {
    tabs.Append(TabToDict(tab_strip->GetWebContentsAt(i), i,
                          i == tab_strip->active_index()));
  }
  out.Set("tabs", std::move(tabs));
  return TextResult(base::Value(std::move(out)));
}
base::DictValue ListOpenTabs(Profile* p, const base::DictValue& a) {
  return WriteGate(p, &ListOpenTabsImpl, a);
}

// sessionat_get_active_tab — info on the currently-focused tab.
base::DictValue GetActiveTabImpl(Profile* profile, const base::DictValue&) {
  std::string err;
  auto* browser = GetActiveTabbedBrowser(profile, &err);
  if (!browser) return ErrorResult(err);
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip || tab_strip->count() == 0) {
    return ErrorResult("No tabs open.");
  }
  const int idx = tab_strip->active_index();
  return TextResult(base::Value(
      TabToDict(tab_strip->GetWebContentsAt(idx), idx, true)));
}
base::DictValue GetActiveTab(Profile* p, const base::DictValue& a) {
  return WriteGate(p, &GetActiveTabImpl, a);
}

// sessionat_focus_tab — bring a tab to the foreground.
base::DictValue FocusTabImpl(Profile* profile, const base::DictValue& args) {
  std::string err;
  auto* browser = GetActiveTabbedBrowser(profile, &err);
  if (!browser) return ErrorResult(err);
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) return ErrorResult("No tab strip.");
  const int idx = args.FindInt("index").value_or(-1);
  if (idx < 0 || idx >= tab_strip->count()) {
    return ErrorResult("Tab index out of range (0.." +
                       base::NumberToString(tab_strip->count() - 1) + ").");
  }
  tab_strip->ActivateTabAt(idx);
  base::DictValue out;
  out.Set("focused_index", idx);
  return TextResult(base::Value(std::move(out)));
}
base::DictValue FocusTab(Profile* p, const base::DictValue& a) {
  return WriteGate(p, &FocusTabImpl, a);
}

// sessionat_close_tab — close a tab by index.
base::DictValue CloseTabImpl(Profile* profile, const base::DictValue& args) {
  std::string err;
  auto* browser = GetActiveTabbedBrowser(profile, &err);
  if (!browser) return ErrorResult(err);
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip) return ErrorResult("No tab strip.");
  const int idx = args.FindInt("index").value_or(-1);
  if (idx < 0 || idx >= tab_strip->count()) {
    return ErrorResult("Tab index out of range.");
  }
  tab_strip->CloseWebContentsAt(idx, TabCloseTypes::CLOSE_USER_GESTURE);
  base::DictValue out;
  out.Set("closed_index", idx);
  return TextResult(base::Value(std::move(out)));
}
base::DictValue CloseTab(Profile* p, const base::DictValue& a) {
  return WriteGate(p, &CloseTabImpl, a);
}

// sessionat_navigate_active_tab — navigate the active tab to a URL in-place
// (no new tab). The page the user was viewing is replaced.
base::DictValue NavigateActiveTabImpl(Profile* profile,
                                       const base::DictValue& args) {
  std::string err;
  auto* browser = GetActiveTabbedBrowser(profile, &err);
  if (!browser) return ErrorResult(err);
  TabStripModel* tab_strip = browser->GetTabStripModel();
  if (!tab_strip || tab_strip->count() == 0) {
    return ErrorResult("No active tab.");
  }
  const std::string* url_str = args.FindString("url");
  if (!url_str || url_str->empty()) return ErrorResult("Missing 'url'.");
  GURL url(*url_str);
  if (!url.is_valid() || !(url.SchemeIs("http") || url.SchemeIs("https"))) {
    return ErrorResult("URL must be http(s)://.");
  }
  content::WebContents* wc =
      tab_strip->GetWebContentsAt(tab_strip->active_index());
  if (!wc) return ErrorResult("Active tab has no WebContents.");
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  wc->GetController().LoadURLWithParams(params);
  base::DictValue out;
  out.Set("navigated_url", url.spec());
  out.Set("tab_index", tab_strip->active_index());
  return TextResult(base::Value(std::move(out)));
}
base::DictValue NavigateActiveTab(Profile* p, const base::DictValue& a) {
  return WriteGate(p, &NavigateActiveTabImpl, a);
}

// ====================================================================
// Page-driving tools — async because they wait on a renderer JS round-trip.
// All run in ISOLATED_WORLD_ID_CHROME_INTERNAL so the page's own scripts
// can't observe or interfere with our DOM probes.
// ====================================================================

// Returns "<json-encoded string>" — safe to splice directly into JS source
// as a string literal. base::JSONWriter handles all escaping (quotes,
// backslashes, control characters, lone surrogates).
std::string EncodeJsStringLiteral(const std::string& s) {
  std::string out;
  base::JSONWriter::Write(base::Value(s), &out);
  return out;  // includes the surrounding quotes
}

// Resolve the active RenderFrameHost. Returns nullptr + sets `err` on failure.
content::RenderFrameHost* GetActiveMainFrame(Profile* profile,
                                              std::string* err) {
  auto* browser = GetActiveTabbedBrowser(profile, err);
  if (!browser) return nullptr;
  TabStripModel* ts = browser->GetTabStripModel();
  if (!ts || ts->count() == 0) {
    if (err) *err = "No tabs open.";
    return nullptr;
  }
  content::WebContents* wc = ts->GetWebContentsAt(ts->active_index());
  if (!wc) {
    if (err) *err = "Active tab has no WebContents.";
    return nullptr;
  }
  // Restrict to http(s) — refuse to probe chrome://, devtools://, file://, etc.
  const GURL& url = wc->GetLastCommittedURL();
  if (!(url.SchemeIs("http") || url.SchemeIs("https"))) {
    if (err) {
      *err = base::StringPrintf(
          "Active tab is not an http(s) page (URL scheme: %s). "
          "Page tools only work on regular web pages.",
          std::string(url.scheme()).c_str());
    }
    return nullptr;
  }
  content::RenderFrameHost* rfh = wc->GetPrimaryMainFrame();
  if (!rfh) {
    if (err) *err = "No primary main frame.";
    return nullptr;
  }
  return rfh;
}

// Resolve the target RenderFrameHost for a page-driving tool call.
// Default: the primary main frame of the active tab.
// If `frame_url_match` is set in args, walks every frame in the tab (main +
// every same- AND cross-origin subframe via ForEachRenderFrameHost) and
// returns the first whose committed URL contains that substring. This is
// how we run JS inside cross-origin iframes — each subframe has its own
// RenderFrameHost in a different RenderProcess, and we can run JS in its
// isolated world without violating same-origin policy from the JS side.
content::RenderFrameHost* ResolveTargetFrame(Profile* profile,
                                              const base::DictValue& args,
                                              std::string* err) {
  content::RenderFrameHost* main = GetActiveMainFrame(profile, err);
  if (!main) return nullptr;

  const std::string* match = args.FindString("frame_url_match");
  if (!match || match->empty()) return main;
  if (match->size() > 1024) {
    if (err) *err = "frame_url_match too long (max 1024 chars).";
    return nullptr;
  }
  content::WebContents* wc = content::WebContents::FromRenderFrameHost(main);
  if (!wc) return main;

  content::RenderFrameHost* found = nullptr;
  const std::string needle = *match;
  wc->ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
    if (found) return;
    if (rfh->GetLastCommittedURL().spec().find(needle) != std::string::npos) {
      found = rfh;
    }
  });
  if (!found) {
    if (err) {
      *err = "No frame matched frame_url_match='" + *match +
             "'. Use sessionat_list_frames to enumerate available frames.";
    }
    return nullptr;
  }
  return found;
}

// Async write-gate wrapper for tools that take a reply callback.
void WriteGateAsync(
    Profile* profile,
    void (*body)(Profile*, const base::DictValue&,
                 base::OnceCallback<void(base::DictValue)>),
    const base::DictValue& args,
    base::OnceCallback<void(base::DictValue)> reply) {
  if (auto err = CheckWriteGate(profile, args); err) {
    std::move(reply).Run(std::move(*err));
    return;
  }
  body(profile, args, std::move(reply));
}

// sessionat_get_page_text — pull the visible text of the active tab's page.
// Runs document.body.innerText in an isolated world; truncated to keep the
// response small (default 50 000 chars, max 200 000). Optional iframe_selector
// reads the text inside a same-origin iframe (cross-origin returns empty).
void GetPageTextImpl(Profile* profile,
                     const base::DictValue& args,
                     base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* rfh = ResolveTargetFrame(profile, args, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }

  int max_chars = args.FindInt("max_chars").value_or(50000);
  max_chars = std::clamp(max_chars, 1, 200000);

  const std::string* iframe = args.FindString("iframe_selector");
  const bool has_iframe = iframe && !iframe->empty();
  if (has_iframe && iframe->size() > 1024) {
    std::move(reply).Run(ErrorResult(
        "iframe_selector too long (max 1024 chars)."));
    return;
  }
  const std::string* root_sel = args.FindString("root_selector");
  const bool has_root = root_sel && !root_sel->empty();
  if (has_root && root_sel->size() > 1024) {
    std::move(reply).Run(ErrorResult(
        "root_selector too long (max 1024 chars)."));
    return;
  }
  const std::string iframe_js =
      has_iframe ? EncodeJsStringLiteral(*iframe) : std::string("null");
  const std::string root_js =
      has_root ? EncodeJsStringLiteral(*root_sel) : std::string("null");

  content::WebContents* wc = content::WebContents::FromRenderFrameHost(rfh);
  std::string url = wc ? wc->GetLastCommittedURL().spec() : std::string();
  std::string title = wc ? base::UTF16ToUTF8(wc->GetTitle()) : std::string();

  std::u16string js = base::UTF8ToUTF16(base::StringPrintf(
      "(function(){try{"
      "var iframeSel=%s, rootSel=%s;"
      "var root=document;"
      "if(iframeSel){"
      "var f=document.querySelector(iframeSel);"
      "if(!f)return {error:'iframe not found: '+iframeSel};"
      "try{root=f.contentDocument;}catch(e){return {error:"
      "'cross-origin iframe is not readable'};}"
      "if(!root)return {error:'cross-origin iframe is not readable'};"
      "}"
      "var node;"
      "if(rootSel){"
      "node=root.querySelector(rootSel);"
      "if(!node)return {error:'root_selector not found: '+rootSel};"
      "}else{"
      "node=root.body;"
      "if(!node)return {text:''};"
      "}"
      "var t=node.innerText||'';"
      "return {text:t.length>%d?t.slice(0,%d):t};"
      "}catch(e){return {error:String(e)};}})()",
      iframe_js.c_str(), root_js.c_str(), max_chars, max_chars));

  rfh->ExecuteJavaScriptInIsolatedWorld(
      js,
      base::BindOnce(
          [](std::string url, std::string title,
             base::OnceCallback<void(base::DictValue)> reply,
             base::Value result) {
            if (!result.is_dict()) {
              std::move(reply).Run(
                  ErrorResult("get_page_text returned non-dict."));
              return;
            }
            const std::string* err = result.GetDict().FindString("error");
            if (err) {
              std::move(reply).Run(ErrorResult(*err));
              return;
            }
            const std::string* t = result.GetDict().FindString("text");
            const std::string text = t ? *t : std::string();
            base::DictValue out;
            out.Set("url", url);
            out.Set("title", title);
            out.Set("text", text);
            out.Set("text_chars", static_cast<int>(text.size()));
            std::move(reply).Run(TextResult(base::Value(std::move(out))));
          },
          std::move(url), std::move(title), std::move(reply)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}
void GetPageText(Profile* p, const base::DictValue& a,
                 base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &GetPageTextImpl, a, std::move(reply));
}

// sessionat_click — click the first element matching either a CSS selector
// (`selector`) or visible text (`text`, case-insensitive contains). The
// text-based finder walks clickable-looking elements (a, button, role=button,
// summary, etc.) and only counts visible ones (non-zero bounding box).
// Returns {ok, tag, text, href?} on success or {ok:false, error} if no match.
void ClickImpl(Profile* profile, const base::DictValue& args,
               base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* rfh = ResolveTargetFrame(profile, args, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }
  const std::string* selector = args.FindString("selector");
  const std::string* text = args.FindString("text");
  const bool text_exact = args.FindBool("text_exact").value_or(false);
  const bool has_selector = selector && !selector->empty();
  const bool has_text = text && !text->empty();
  if (!has_selector && !has_text) {
    std::move(reply).Run(ErrorResult(
        "Missing locator: provide either 'selector' (CSS) or 'text' "
        "(visible-text contains; pass text_exact:true for equality)."));
    return;
  }
  if (has_selector && selector->size() > 1024) {
    std::move(reply).Run(ErrorResult("Selector too long (max 1024 chars)."));
    return;
  }
  if (has_text && text->size() > 500) {
    std::move(reply).Run(ErrorResult("Text locator too long (max 500 chars)."));
    return;
  }

  const std::string sel_js = has_selector ? EncodeJsStringLiteral(*selector)
                                           : std::string("null");
  const std::string txt_js =
      has_text ? EncodeJsStringLiteral(*text) : std::string("null");

  std::u16string js = base::UTF8ToUTF16(base::StringPrintf(
      "(function(){try{"
      "var sel=%s, txt=%s, exact=%s;"
      // Shadow-DOM-piercing walker. Yields the first match across the
      // light tree AND every open shadow root recursively. Required for
      // sites like Reddit (shreddit-*, faceplate-*) whose interactive
      // elements live inside custom-element shadow roots.
      "function deepQuery(root,s){"
      "var f=root.querySelector(s);if(f)return f;"
      "var all=root.querySelectorAll('*');"
      "for(var i=0;i<all.length;i++){"
      "if(all[i].shadowRoot){f=deepQuery(all[i].shadowRoot,s);if(f)return f;}"
      "}return null;}"
      "function deepAll(root,s){"
      "var out=Array.from(root.querySelectorAll(s));"
      "var all=root.querySelectorAll('*');"
      "for(var i=0;i<all.length;i++){"
      "if(all[i].shadowRoot)out=out.concat(deepAll(all[i].shadowRoot,s));"
      "}return out;}"
      "function visible(el){var r=el.getBoundingClientRect();"
      "return r.width>0&&r.height>0;}"
      "function findByText(t,ex){"
      "t=t.toLowerCase();"
      "var els=deepAll(document,"
      "'a,button,[role=\"button\"],[role=\"link\"],[role=\"menuitem\"],"
      "[role=\"tab\"],input[type=\"submit\"],input[type=\"button\"],"
      "summary,label,[onclick]');"
      "for(var i=0;i<els.length;i++){"
      "var el=els[i];"
      "var s=(el.innerText||el.value||el.getAttribute('aria-label')||"
      "el.getAttribute('title')||'').trim().toLowerCase();"
      "if(!s||!visible(el))continue;"
      "if(ex?s===t:(s.indexOf(t)!==-1&&s.length<500))return el;"
      "}return null;}"
      "var el=sel?deepQuery(document,sel):findByText(txt,exact);"
      "if(!el)return {ok:false,error:sel?'no element matched selector: '+sel:"
      "'no clickable element matched text: '+txt};"
      "el.scrollIntoView({block:'center'});"
      "el.click();"
      "return {ok:true,tag:el.tagName.toLowerCase(),"
      "text:(el.innerText||el.value||'').slice(0,200),"
      "href:el.href||null};"
      "}catch(e){return {ok:false,error:String(e)};}})()",
      sel_js.c_str(), txt_js.c_str(), text_exact ? "true" : "false"));

  rfh->ExecuteJavaScriptInIsolatedWorld(
      js,
      base::BindOnce(
          [](base::OnceCallback<void(base::DictValue)> reply,
             base::Value result) {
            if (!result.is_dict()) {
              std::move(reply).Run(
                  ErrorResult("Click returned a non-dict result."));
              return;
            }
            std::move(reply).Run(TextResult(std::move(result)));
          },
          std::move(reply)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}
void Click(Profile* p, const base::DictValue& a,
           base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &ClickImpl, a, std::move(reply));
}

// sessionat_type — focus an element (located by `selector` OR `field_label`),
// set its value, and dispatch input + change events so frameworks like React
// notice. Optional `submit:true` sends an Enter keydown + form.requestSubmit().
// `field_label` finds the input by associated <label>, aria-label, or
// placeholder text — case-insensitive contains.
void TypeImpl(Profile* profile, const base::DictValue& args,
              base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* rfh = ResolveTargetFrame(profile, args, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }
  const std::string* selector = args.FindString("selector");
  const std::string* field_label = args.FindString("field_label");
  const std::string* text = args.FindString("text");
  const bool has_selector = selector && !selector->empty();
  const bool has_label = field_label && !field_label->empty();
  if (!has_selector && !has_label) {
    std::move(reply).Run(ErrorResult(
        "Missing locator: provide either 'selector' (CSS) or 'field_label' "
        "(label / aria-label / placeholder contains)."));
    return;
  }
  if (!text) {
    std::move(reply).Run(ErrorResult("Missing 'text' argument."));
    return;
  }
  if (has_selector && selector->size() > 1024) {
    std::move(reply).Run(ErrorResult("Selector too long (max 1024 chars)."));
    return;
  }
  if (has_label && field_label->size() > 500) {
    std::move(reply).Run(ErrorResult(
        "field_label too long (max 500 chars)."));
    return;
  }
  if (text->size() > 100000) {
    std::move(reply).Run(ErrorResult("Text too long (max 100 000 chars)."));
    return;
  }
  const bool submit = args.FindBool("submit").value_or(false);

  const std::string sel_js = has_selector ? EncodeJsStringLiteral(*selector)
                                           : std::string("null");
  const std::string lbl_js =
      has_label ? EncodeJsStringLiteral(*field_label) : std::string("null");

  std::u16string js = base::UTF8ToUTF16(base::StringPrintf(
      "(function(){try{"
      "var sel=%s, lbl=%s, t=%s, submit=%s;"
      "function deepQuery(root,s){"
      "var f=root.querySelector(s);if(f)return f;"
      "var all=root.querySelectorAll('*');"
      "for(var i=0;i<all.length;i++){"
      "if(all[i].shadowRoot){f=deepQuery(all[i].shadowRoot,s);if(f)return f;}"
      "}return null;}"
      "function deepAll(root,s){"
      "var out=Array.from(root.querySelectorAll(s));"
      "var all=root.querySelectorAll('*');"
      "for(var i=0;i<all.length;i++){"
      "if(all[i].shadowRoot)out=out.concat(deepAll(all[i].shadowRoot,s));"
      "}return out;}"
      "function isTypable(el){var tag=el.tagName;return tag==='INPUT'||"
      "tag==='TEXTAREA'||tag==='SELECT'||el.isContentEditable;}"
      "function findByLabel(label){"
      "label=label.toLowerCase();"
      "var labels=deepAll(document,'label');"
      "for(var i=0;i<labels.length;i++){"
      "var l=labels[i];"
      "var lt=(l.innerText||'').trim().toLowerCase();"
      "if(lt&&lt.indexOf(label)!==-1){"
      "if(l.htmlFor){"
      "var el=(l.getRootNode().getElementById)?l.getRootNode()."
      "getElementById(l.htmlFor):document.getElementById(l.htmlFor);"
      "if(el&&isTypable(el))return el;}"
      "var inner=l.querySelector('input,textarea,select,"
      "[contenteditable=\"true\"]');"
      "if(inner)return inner;}}"
      "var inputs=deepAll(document,'input,textarea,select,"
      "[contenteditable=\"true\"]');"
      "for(var j=0;j<inputs.length;j++){"
      "var el=inputs[j];"
      "var a=((el.getAttribute('aria-label')||'')+' '+"
      "(el.getAttribute('placeholder')||'')+' '+"
      "(el.getAttribute('name')||'')).toLowerCase();"
      "if(a.indexOf(label)!==-1)return el;}"
      "return null;}"
      "var el=sel?deepQuery(document,sel):findByLabel(lbl);"
      "if(!el)return {ok:false,error:sel?'no element matched selector: '+sel:"
      "'no input found for label: '+lbl};"
      "el.scrollIntoView({block:'center'});"
      "el.focus();"
      "var tag=el.tagName;"
      "if(tag==='INPUT'||tag==='TEXTAREA'){el.value=t;}"
      "else if(tag==='SELECT'){el.value=t;}"
      "else if(el.isContentEditable){"
      // React-controlled contenteditable (FB, Twitter, Slack, etc.)
      // ignores `el.textContent = t` — React reconciles back to empty.
      // execCommand fires a real beforeinput event that React listens to.
      "document.execCommand('selectAll',false,null);"
      "document.execCommand('delete',false,null);"
      "document.execCommand('insertText',false,t);}"
      "else{return {ok:false,error:'element is not typable (tag='+tag+')'};}"
      "el.dispatchEvent(new Event('input',{bubbles:true}));"
      "el.dispatchEvent(new Event('change',{bubbles:true}));"
      "if(submit){"
      "var ev=new KeyboardEvent('keydown',{key:'Enter',code:'Enter',"
      "keyCode:13,which:13,bubbles:true,cancelable:true});"
      "el.dispatchEvent(ev);"
      "if(!ev.defaultPrevented&&el.form){"
      "el.form.requestSubmit?el.form.requestSubmit():el.form.submit();}}"
      "return {ok:true,tag:tag.toLowerCase(),submitted:submit};"
      "}catch(e){return {ok:false,error:String(e)};}})()",
      sel_js.c_str(), lbl_js.c_str(),
      EncodeJsStringLiteral(*text).c_str(),
      submit ? "true" : "false"));

  rfh->ExecuteJavaScriptInIsolatedWorld(
      js,
      base::BindOnce(
          [](base::OnceCallback<void(base::DictValue)> reply,
             base::Value result) {
            if (!result.is_dict()) {
              std::move(reply).Run(
                  ErrorResult("Type returned a non-dict result."));
              return;
            }
            std::move(reply).Run(TextResult(std::move(result)));
          },
          std::move(reply)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}
void Type(Profile* p, const base::DictValue& a,
          base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &TypeImpl, a, std::move(reply));
}

// ====================================================================
// sessionat_wait_for — poll until selector or text matches, or timeout.
//
// JavaScript Promises don't reliably round-trip through
// ExecuteJavaScriptInIsolatedWorld in M150, so we drive the loop from
// C++ via PostDelayedTask. Each tick is a fresh isolated-world JS call
// that returns the element synopsis (or null). When we get a hit, or
// when the deadline elapses, we Reply once.
// ====================================================================

// Forward declaration so the WaitForPoll lambda can recurse.
void WaitForPoll(Profile* profile,
                 std::string sel_js,
                 std::string txt_js,
                 std::string exact_js,
                 base::Time deadline,
                 base::OnceCallback<void(base::DictValue)> reply);

void WaitForPoll(Profile* profile,
                 std::string sel_js,
                 std::string txt_js,
                 std::string exact_js,
                 base::Time deadline,
                 base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* rfh = GetActiveMainFrame(profile, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }
  if (base::Time::Now() > deadline) {
    base::DictValue out;
    out.Set("ok", false);
    out.Set("error", "timeout");
    std::move(reply).Run(TextResult(base::Value(std::move(out))));
    return;
  }

  // Single-tick: same find logic as click(), with shadow-pierce + text_exact.
  std::u16string js = base::UTF8ToUTF16(base::StringPrintf(
      "(function(){try{"
      "var sel=%s, txt=%s, exact=%s;"
      "function deepQuery(root,s){"
      "var f=root.querySelector(s);if(f)return f;"
      "var all=root.querySelectorAll('*');"
      "for(var i=0;i<all.length;i++){"
      "if(all[i].shadowRoot){f=deepQuery(all[i].shadowRoot,s);if(f)return f;}"
      "}return null;}"
      "function deepAll(root,s){"
      "var out=Array.from(root.querySelectorAll(s));"
      "var all=root.querySelectorAll('*');"
      "for(var i=0;i<all.length;i++){"
      "if(all[i].shadowRoot)out=out.concat(deepAll(all[i].shadowRoot,s));"
      "}return out;}"
      "function visible(el){var r=el.getBoundingClientRect();"
      "return r.width>0&&r.height>0;}"
      "function findByText(t,ex){"
      "t=t.toLowerCase();"
      "var els=deepAll(document,'a,button,[role=\"button\"],"
      "[role=\"link\"],[role=\"menuitem\"],[role=\"tab\"],"
      "input[type=\"submit\"],input[type=\"button\"],summary,label,"
      "[onclick],h1,h2,h3,h4,div,span');"
      "for(var i=0;i<els.length;i++){"
      "var el=els[i];"
      "var s=(el.innerText||el.value||el.getAttribute('aria-label')||"
      "el.getAttribute('title')||'').trim().toLowerCase();"
      "if(!s||!visible(el))continue;"
      "if(ex?s===t:(s.indexOf(t)!==-1&&s.length<500))return el;"
      "}return null;}"
      "var el=sel?deepQuery(document,sel):findByText(txt,exact);"
      "if(!el)return null;"
      "return {tag:el.tagName.toLowerCase(),"
      "text:(el.innerText||el.value||'').trim().slice(0,200),"
      "visible:visible(el)};"
      "}catch(e){return null;}})()",
      sel_js.c_str(), txt_js.c_str(), exact_js.c_str()));

  rfh->ExecuteJavaScriptInIsolatedWorld(
      js,
      base::BindOnce(
          [](Profile* profile, std::string sel_js, std::string txt_js,
             std::string exact_js, base::Time deadline,
             base::OnceCallback<void(base::DictValue)> reply,
             base::Value result) {
            if (result.is_dict()) {
              base::DictValue out;
              out.Set("ok", true);
              out.Set("element", std::move(result));
              std::move(reply).Run(TextResult(base::Value(std::move(out))));
              return;
            }
            // Not found yet — wait 200 ms and retry.
            base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE,
                base::BindOnce(&WaitForPoll, profile, std::move(sel_js),
                                std::move(txt_js), std::move(exact_js),
                                deadline, std::move(reply)),
                base::Milliseconds(200));
          },
          profile, sel_js, txt_js, exact_js, deadline, std::move(reply)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void WaitForImpl(Profile* profile,
                 const base::DictValue& args,
                 base::OnceCallback<void(base::DictValue)> reply) {
  const std::string* selector = args.FindString("selector");
  const std::string* text = args.FindString("text");
  const bool text_exact = args.FindBool("text_exact").value_or(false);
  const bool has_selector = selector && !selector->empty();
  const bool has_text = text && !text->empty();
  if (!has_selector && !has_text) {
    std::move(reply).Run(ErrorResult(
        "Missing locator: provide 'selector' or 'text'."));
    return;
  }
  if (has_selector && selector->size() > 1024) {
    std::move(reply).Run(ErrorResult("Selector too long (max 1024 chars)."));
    return;
  }
  if (has_text && text->size() > 500) {
    std::move(reply).Run(ErrorResult("Text too long (max 500 chars)."));
    return;
  }

  int timeout_ms = args.FindInt("timeout_ms").value_or(5000);
  timeout_ms = std::clamp(timeout_ms, 100, 30000);
  base::Time deadline = base::Time::Now() + base::Milliseconds(timeout_ms);

  WaitForPoll(profile,
              has_selector ? EncodeJsStringLiteral(*selector) : "null",
              has_text ? EncodeJsStringLiteral(*text) : "null",
              text_exact ? "true" : "false",
              deadline, std::move(reply));
}
void WaitFor(Profile* p, const base::DictValue& a,
             base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &WaitForImpl, a, std::move(reply));
}

// ====================================================================
// sessionat_scroll — either window.scrollBy(dx,dy) or scrollIntoView for
// a selector. Use the latter to bring a button below the fold into view
// before clicking; use the former to trigger infinite-scroll feeds.
// ====================================================================
void ScrollImpl(Profile* profile,
                const base::DictValue& args,
                base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* rfh = GetActiveMainFrame(profile, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }
  const std::string* selector = args.FindString("selector");
  const bool has_selector = selector && !selector->empty();
  const int dx = args.FindInt("dx").value_or(0);
  const int dy = args.FindInt("dy").value_or(0);
  if (!has_selector && dx == 0 && dy == 0) {
    std::move(reply).Run(ErrorResult(
        "Need either 'selector' (scroll into view) or non-zero 'dx'/'dy'."));
    return;
  }
  if (has_selector && selector->size() > 1024) {
    std::move(reply).Run(ErrorResult("Selector too long (max 1024 chars)."));
    return;
  }

  std::string sel_js =
      has_selector ? EncodeJsStringLiteral(*selector) : std::string("null");
  std::string position =
      args.FindString("position") ? *args.FindString("position") : "center";
  if (position != "start" && position != "center" && position != "end") {
    position = "center";
  }

  std::u16string js = base::UTF8ToUTF16(base::StringPrintf(
      "(function(){try{"
      "var sel=%s, dx=%d, dy=%d, pos=%s;"
      "function findScrollable(el){"
      "while(el&&el!==document.body&&el!==document.documentElement){"
      "var s=getComputedStyle(el);"
      "if(((s.overflowY==='auto'||s.overflowY==='scroll')"
      "&&el.scrollHeight>el.clientHeight)||"
      "((s.overflowX==='auto'||s.overflowX==='scroll')"
      "&&el.scrollWidth>el.clientWidth))return el;"
      "el=el.parentElement;}return null;}"
      "if(sel){"
      "var el=document.querySelector(sel);"
      "if(!el)return {ok:false,error:'no element matched: '+sel};"
      "el.scrollIntoView({block:pos,inline:'nearest',behavior:'instant'});"
      "return {ok:true,scrolled_to:'element',tag:el.tagName.toLowerCase(),"
      "scrollY:Math.round(window.scrollY)};}"
      // Try window first.
      "var bx=window.scrollX, by=window.scrollY;"
      "window.scrollBy(dx,dy);"
      "if((dx!==0||dy!==0)&&window.scrollX===bx&&window.scrollY===by){"
      // Window didn't move — find scrollable container at viewport center.
      "var center=document.elementFromPoint("
      "Math.floor(window.innerWidth/2),Math.floor(window.innerHeight/2));"
      "var c=findScrollable(center);"
      "if(c){"
      "var cb=c.scrollTop;"
      "c.scrollBy(dx,dy);"
      "return {ok:true,target:'container',"
      "container_tag:c.tagName.toLowerCase(),"
      "container_scrollTop:Math.round(c.scrollTop),"
      "container_moved:c.scrollTop!==cb,"
      "scrolled_by:{dx:dx,dy:dy}};}"
      "}"
      "return {ok:true,target:'window',scrolled_by:{dx:dx,dy:dy},"
      "scrollX:Math.round(window.scrollX),"
      "scrollY:Math.round(window.scrollY),"
      "max_scroll_y:Math.round(document.documentElement.scrollHeight-"
      "window.innerHeight)};"
      "}catch(e){return {ok:false,error:String(e)};}})()",
      sel_js.c_str(), dx, dy,
      EncodeJsStringLiteral(position).c_str()));

  rfh->ExecuteJavaScriptInIsolatedWorld(
      js,
      base::BindOnce(
          [](base::OnceCallback<void(base::DictValue)> reply,
             base::Value result) {
            if (!result.is_dict()) {
              std::move(reply).Run(
                  ErrorResult("Scroll returned non-dict."));
              return;
            }
            std::move(reply).Run(TextResult(std::move(result)));
          },
          std::move(reply)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}
void Scroll(Profile* p, const base::DictValue& a,
            base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &ScrollImpl, a, std::move(reply));
}

// ====================================================================
// sessionat_get_dom_outline — structured snapshot of interactive elements
// on the active tab. Returns lists of links, buttons, headings, inputs,
// images, and iframes — only visible (non-zero bounding rect) ones — with
// stable text + attribute identifiers the AI can use as click/type
// locators on the next call.
// ====================================================================
void GetDomOutlineImpl(Profile* profile,
                       const base::DictValue& args,
                       base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* rfh = ResolveTargetFrame(profile, args, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }

  int limit = args.FindInt("limit").value_or(50);
  limit = std::clamp(limit, 1, 200);

  const std::string* root_sel = args.FindString("root_selector");
  const bool has_root = root_sel && !root_sel->empty();
  if (has_root && root_sel->size() > 1024) {
    std::move(reply).Run(ErrorResult(
        "root_selector too long (max 1024 chars)."));
    return;
  }
  const std::string root_js =
      has_root ? EncodeJsStringLiteral(*root_sel) : std::string("null");

  std::u16string js = base::UTF8ToUTF16(base::StringPrintf(
      "(function(){try{"
      "var lim=%d, rootSel=%s;"
      "var root = rootSel ? document.querySelector(rootSel) : document;"
      "if(rootSel && !root) return {error:'root_selector not found: '+rootSel};"
      // Shadow-DOM piercing walker. Used for every category so that
      // sites like Reddit's <shreddit-*> and YouTube's <ytd-*> components
      // are surfaced to the AI client.
      "function deepAll(r,s){"
      "var out=Array.from(r.querySelectorAll(s));"
      "var all=r.querySelectorAll('*');"
      "for(var i=0;i<all.length;i++){"
      "if(all[i].shadowRoot)out=out.concat(deepAll(all[i].shadowRoot,s));"
      "}return out;}"
      "function visible(el){var r=el.getBoundingClientRect();"
      "return r.width>0&&r.height>0&&r.top<window.innerHeight*5;}"
      // `force_include` skips the visibility check — used for iframes,"
      // since captcha frames (Cloudflare Turnstile) are positioned offscreen.
      "function summarize(els,attrs,force_include){"
      "var out=[];"
      "for(var i=0;i<els.length&&out.length<lim;i++){"
      "var el=els[i];"
      "if(!force_include && !visible(el))continue;"
      "var o={tag:el.tagName.toLowerCase()};"
      "for(var j=0;j<attrs.length;j++){"
      "var a=attrs[j];var v=el.getAttribute&&el.getAttribute(a);"
      "if(v)o[a]=String(v).slice(0,200);}"
      "var t=(el.innerText||el.value||'').trim().replace(/\\s+/g,' ');"
      "if(t)o.text=t.slice(0,120);"
      "var hasInfo=!!o.text;"
      "for(var k in o){if(k!=='tag'&&o[k]){hasInfo=true;break;}}"
      "if(!hasInfo)continue;"
      "out.push(o);}"
      "return out;}"
      "return {"
      "url:location.href,"
      "title:document.title,"
      "scoped_to:rootSel||null,"
      "links:summarize(deepAll(root,'a[href]'),"
      "['href','aria-label']),"
      "buttons:summarize(deepAll(root,"
      "'button,[role=\"button\"],input[type=\"submit\"],"
      "input[type=\"button\"]'),"
      "['aria-label','type','name']),"
      "headings:summarize(deepAll(root,'h1,h2,h3'),[]),"
      "inputs:summarize(deepAll(root,"
      "'input:not([type=\"hidden\"]),textarea,select,"
      "[contenteditable=\"true\"]'),"
      "['name','type','placeholder','aria-label']),"
      "images:summarize(deepAll(root,'img[alt]'),"
      "['alt','src']),"
      // iframes always included regardless of visibility — captcha widgets
      // and other zero-size iframes need to be surfaced for navigation.
      "iframes:summarize(deepAll(root,'iframe'),"
      "['src','name','title'],true)"
      "};"
      "}catch(e){return {error:String(e)};}})()",
      limit, root_js.c_str()));

  rfh->ExecuteJavaScriptInIsolatedWorld(
      js,
      base::BindOnce(
          [](base::OnceCallback<void(base::DictValue)> reply,
             base::Value result) {
            if (!result.is_dict()) {
              std::move(reply).Run(
                  ErrorResult("get_dom_outline returned non-dict."));
              return;
            }
            const std::string* e = result.GetDict().FindString("error");
            if (e) {
              std::move(reply).Run(ErrorResult(*e));
              return;
            }
            std::move(reply).Run(TextResult(std::move(result)));
          },
          std::move(reply)),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}
void GetDomOutline(Profile* p, const base::DictValue& a,
                   base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &GetDomOutlineImpl, a, std::move(reply));
}

// ====================================================================
// sessionat_screenshot — capture the active tab's viewport as PNG, return
// it as an MCP image content block. Optional `max_width` scales the image
// down at capture time (so we don't ship multi-MB blobs over JSON-RPC).
// ====================================================================
void ScreenshotImpl(Profile* profile,
                    const base::DictValue& args,
                    base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* rfh = GetActiveMainFrame(profile, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }
  content::WebContents* wc = content::WebContents::FromRenderFrameHost(rfh);
  if (!wc) {
    std::move(reply).Run(ErrorResult("No WebContents."));
    return;
  }
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) {
    std::move(reply).Run(ErrorResult("No RenderWidgetHostView."));
    return;
  }

  gfx::Size view_size = view->GetViewBounds().size();
  if (view_size.IsEmpty()) {
    std::move(reply).Run(ErrorResult("Tab view has zero size."));
    return;
  }

  int max_width = args.FindInt("max_width").value_or(1280);
  max_width = std::clamp(max_width, 200, 2048);
  gfx::Size out_size = view_size;
  if (out_size.width() > max_width) {
    const double scale =
        static_cast<double>(max_width) / out_size.width();
    out_size = gfx::Size(max_width,
                          static_cast<int>(out_size.height() * scale));
  }

  view->CopyFromSurface(
      gfx::Rect(view_size),
      out_size,
      base::Seconds(5),
      base::BindOnce(
          [](base::OnceCallback<void(base::DictValue)> reply,
             const content::CopyFromSurfaceResult& res) {
            if (!res.has_value()) {
              std::move(reply).Run(ErrorResult(
                  "CopyFromSurface failed (tab may be occluded)."));
              return;
            }
            const SkBitmap& bm = res.value().bitmap;
            if (bm.drawsNothing()) {
              std::move(reply).Run(ErrorResult(
                  "Capture returned empty bitmap."));
              return;
            }
            std::optional<std::vector<uint8_t>> png =
                gfx::PNGCodec::EncodeBGRASkBitmap(bm,
                                                   /*discard_alpha=*/true);
            if (!png) {
              std::move(reply).Run(ErrorResult("PNG encode failed."));
              return;
            }
            std::string b64 = base::Base64Encode(*png);
            std::move(reply).Run(ImageResult(b64, "image/png"));
          },
          std::move(reply)));
}
void Screenshot(Profile* p, const base::DictValue& a,
                base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &ScreenshotImpl, a, std::move(reply));
}

// ====================================================================
// sessionat_list_frames — enumerate every RenderFrameHost in the active
// tab (main frame + every same- and cross-origin subframe). Used in
// combination with `frame_url_match` on get_page_text / click / type /
// get_dom_outline to drive cross-origin iframes (Stripe checkout, OAuth
// widgets, embedded YouTube/Twitter cards, reCAPTCHA).
// ====================================================================
void ListFramesImpl(Profile* profile,
                    const base::DictValue& /*args*/,
                    base::OnceCallback<void(base::DictValue)> reply) {
  std::string err;
  content::RenderFrameHost* main = GetActiveMainFrame(profile, &err);
  if (!main) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }
  content::WebContents* wc = content::WebContents::FromRenderFrameHost(main);
  base::ListValue frames;
  if (wc) {
    wc->ForEachRenderFrameHost([&frames](content::RenderFrameHost* rfh) {
      base::DictValue d;
      d.Set("url", rfh->GetLastCommittedURL().spec());
      d.Set("frame_name", rfh->GetFrameName());
      d.Set("is_main", rfh->IsInPrimaryMainFrame());
      d.Set("is_cross_origin", !rfh->GetLastCommittedOrigin().IsSameOriginWith(
                                  rfh->GetMainFrame()->GetLastCommittedOrigin()));
      // Stable identifier: process_id + routing_id. ChildProcessId is a
      // strong-typed wrapper around int32_t (base::IdType) — unwrap it.
      auto id = rfh->GetGlobalId();
      d.Set("process_id", static_cast<int>(id.child_id.GetUnsafeValue()));
      d.Set("routing_id", id.frame_routing_id);
      frames.Append(std::move(d));
    });
  }
  base::DictValue out;
  out.Set("frame_count", static_cast<int>(frames.size()));
  out.Set("frames", std::move(frames));
  std::move(reply).Run(TextResult(base::Value(std::move(out))));
}
void ListFrames(Profile* p, const base::DictValue& a,
                base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &ListFramesImpl, a, std::move(reply));
}

// ====================================================================
// sessionat_press_key — REAL isTrusted=true keyboard event via
// RenderWidgetHost::ForwardKeyboardEvent. v0.5 draft caused a shutdown
// crash but the root cause was WorkspaceService's un-marked
// raw_ptr<Profile> (fixed in v0.5.1 via DanglingUntriaged), not this
// tool. Re-introduced in v0.6. All Chromium pointers are resolved
// synchronously inside the function — nothing is captured across task
// boundaries.
// ====================================================================
struct KeyMapping {
  const char* name;
  ui::KeyboardCode key_code;
  ui::DomCode dom_code;
  ui::DomKey dom_key;
};

const KeyMapping* FindKeyMapping(const std::string& name) {
  static const KeyMapping kKeys[] = {
      {"Enter", ui::VKEY_RETURN, ui::DomCode::ENTER, ui::DomKey::ENTER},
      {"Escape", ui::VKEY_ESCAPE, ui::DomCode::ESCAPE, ui::DomKey::ESCAPE},
      {"Tab", ui::VKEY_TAB, ui::DomCode::TAB, ui::DomKey::TAB},
      {"Backspace", ui::VKEY_BACK, ui::DomCode::BACKSPACE,
       ui::DomKey::BACKSPACE},
      {"Delete", ui::VKEY_DELETE, ui::DomCode::DEL, ui::DomKey::DEL},
      {"ArrowUp", ui::VKEY_UP, ui::DomCode::ARROW_UP, ui::DomKey::ARROW_UP},
      {"ArrowDown", ui::VKEY_DOWN, ui::DomCode::ARROW_DOWN,
       ui::DomKey::ARROW_DOWN},
      {"ArrowLeft", ui::VKEY_LEFT, ui::DomCode::ARROW_LEFT,
       ui::DomKey::ARROW_LEFT},
      {"ArrowRight", ui::VKEY_RIGHT, ui::DomCode::ARROW_RIGHT,
       ui::DomKey::ARROW_RIGHT},
      {"Home", ui::VKEY_HOME, ui::DomCode::HOME, ui::DomKey::HOME},
      {"End", ui::VKEY_END, ui::DomCode::END, ui::DomKey::END},
      {"PageUp", ui::VKEY_PRIOR, ui::DomCode::PAGE_UP, ui::DomKey::PAGE_UP},
      {"PageDown", ui::VKEY_NEXT, ui::DomCode::PAGE_DOWN,
       ui::DomKey::PAGE_DOWN},
      {"Space", ui::VKEY_SPACE, ui::DomCode::SPACE,
       ui::DomKey::FromCharacter(' ')},
  };
  for (const auto& k : kKeys) {
    if (name == k.name) return &k;
  }
  return nullptr;
}

// Some named keys are control characters in the input pipeline — they need
// text[0] set so the renderer's edit handlers (text inputs, contenteditable,
// Google Sheets cell commits) see the Char event with the right code.
char16_t NamedKeyToChar(ui::KeyboardCode kc) {
  switch (kc) {
    case ui::VKEY_RETURN:   return u'\r';
    case ui::VKEY_TAB:      return u'\t';
    case ui::VKEY_BACK:     return u'\b';
    case ui::VKEY_ESCAPE:   return u'\033';
    case ui::VKEY_SPACE:    return u' ';
    default:                return 0;
  }
}

void FillKeyEvent(input::NativeWebKeyboardEvent* event,
                  blink::WebInputEvent::Type type,
                  const KeyMapping& m) {
  event->dom_key = m.dom_key;
  event->dom_code = static_cast<int>(m.dom_code);
  event->native_key_code =
      ui::KeycodeConverter::DomCodeToNativeKeycode(m.dom_code);
  event->windows_key_code = m.key_code;
  event->is_system_key = false;
  event->skip_if_unhandled = true;
  if (type == blink::WebInputEvent::Type::kChar ||
      type == blink::WebInputEvent::Type::kRawKeyDown) {
    if (m.dom_key.IsCharacter()) {
      event->text[0] = m.dom_key.ToCharacter();
      event->unmodified_text[0] = m.dom_key.ToCharacter();
    } else {
      char16_t c = NamedKeyToChar(m.key_code);
      if (c) {
        event->text[0] = c;
        event->unmodified_text[0] = c;
      }
    }
  }
}

// Dispatch a single character as a real RawKeyDown + Char + KeyUp sequence.
// Used to type into apps that don't expose a DOM input target (Google Docs,
// Figma, Lucid — anything canvas-rendered). Browser apps that ARE DOM-based
// should keep using sessionat_type instead since this loses formatting hints.
void DispatchCharToHost(content::RenderWidgetHost* host, char16_t c,
                        int extra_modifiers) {
  // Combine caller-supplied modifiers (e.g. Cmd/Alt for shortcuts) with the
  // auto-shift required for uppercase letters.
  int modifiers = extra_modifiers;
  if (c >= u'A' && c <= u'Z') modifiers |= blink::WebInputEvent::kShiftKey;
  // Best-effort windows_key_code: upper-case the letter, leave digits as-is,
  // anything else falls back to 0 (the Char event still carries the text).
  int wkc = 0;
  if (c >= u'a' && c <= u'z') wkc = ui::VKEY_A + (c - u'a');
  else if (c >= u'A' && c <= u'Z') wkc = ui::VKEY_A + (c - u'A');
  else if (c >= u'0' && c <= u'9') wkc = ui::VKEY_0 + (c - u'0');
  else if (c == u' ') wkc = ui::VKEY_SPACE;

  auto base_event = [&](blink::WebInputEvent::Type t) {
    input::NativeWebKeyboardEvent e(t, modifiers, base::TimeTicks::Now());
    e.windows_key_code = wkc;
    e.dom_key = ui::DomKey::FromCharacter(c);
    e.dom_code = static_cast<int>(ui::DomCode::NONE);
    e.native_key_code = 0;
    e.is_system_key = false;
    e.skip_if_unhandled = false;
    return e;
  };

  // RawKeyDown — frameworks that listen for keydown notice this.
  auto kd = base_event(blink::WebInputEvent::Type::kRawKeyDown);
  host->ForwardKeyboardEvent(kd);

  // Char — actual text insertion. text[] is the printable char.
  auto ch = base_event(blink::WebInputEvent::Type::kChar);
  ch.text[0] = c;
  ch.unmodified_text[0] = c;
  host->ForwardKeyboardEvent(ch);

  // KeyUp.
  auto ku = base_event(blink::WebInputEvent::Type::kKeyUp);
  host->ForwardKeyboardEvent(ku);
}

void PressKeyImpl(Profile* profile,
                  const base::DictValue& args,
                  base::OnceCallback<void(base::DictValue)> reply) {
  const std::string* key_name = args.FindString("key");
  const std::string* text_arg = args.FindString("text");
  const bool has_key = key_name && !key_name->empty();
  const bool has_text = text_arg && !text_arg->empty();
  if (!has_key && !has_text) {
    std::move(reply).Run(ErrorResult(
        "Missing input: provide either 'key' (named key) or 'text' "
        "(string typed char-by-char as real key events). 'text' is the path "
        "for canvas-rendered apps like Google Docs / Figma where there is "
        "no DOM input target."));
    return;
  }
  if (has_text && text_arg->size() > 5000) {
    std::move(reply).Run(ErrorResult("Text too long (max 5000 chars)."));
    return;
  }

  // Resolve modifiers (only relevant for the named-key path).
  int modifiers = blink::WebInputEvent::kNoModifiers;
  const base::ListValue* mods = args.FindList("modifiers");
  if (mods) {
    for (const base::Value& v : *mods) {
      if (!v.is_string()) continue;
      const std::string& s = v.GetString();
      if (s == "shift") modifiers |= blink::WebInputEvent::kShiftKey;
      else if (s == "ctrl" || s == "control")
        modifiers |= blink::WebInputEvent::kControlKey;
      else if (s == "alt") modifiers |= blink::WebInputEvent::kAltKey;
      else if (s == "meta" || s == "cmd")
        modifiers |= blink::WebInputEvent::kMetaKey;
    }
  }

  std::string err;
  content::RenderFrameHost* rfh = GetActiveMainFrame(profile, &err);
  if (!rfh) {
    std::move(reply).Run(ErrorResult(err));
    return;
  }
  content::WebContents* wc = content::WebContents::FromRenderFrameHost(rfh);
  if (!wc) {
    std::move(reply).Run(ErrorResult("No WebContents."));
    return;
  }
  content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
  if (!view) {
    std::move(reply).Run(ErrorResult("No RenderWidgetHostView."));
    return;
  }
  content::RenderWidgetHost* host = view->GetRenderWidgetHost();
  if (!host) {
    std::move(reply).Run(ErrorResult("No RenderWidgetHost."));
    return;
  }

  // ---- TEXT path: type a string char-by-char ----
  if (has_text) {
    // UTF-8 → UTF-16 so we can iterate by char16_t and handle BMP chars.
    std::u16string u16 = base::UTF8ToUTF16(*text_arg);
    for (char16_t c : u16) {
      DispatchCharToHost(host, c, modifiers);
    }
    base::DictValue out;
    out.Set("ok", true);
    out.Set("typed_chars", static_cast<int>(u16.size()));
    std::move(reply).Run(TextResult(base::Value(std::move(out))));
    return;
  }

  // ---- KEY path: single named key (Enter/Escape/Arrow*/etc.) ----
  const KeyMapping* mapping = FindKeyMapping(*key_name);
  if (!mapping) {
    std::move(reply).Run(ErrorResult(
        "Unknown key '" + *key_name +
        "'. Supported: Enter, Escape, Tab, Backspace, Delete, "
        "ArrowUp/Down/Left/Right, Home, End, PageUp, PageDown, Space. "
        "For typing a string, pass `text` instead of `key`."));
    return;
  }

  // RawKeyDown.
  input::NativeWebKeyboardEvent event(blink::WebInputEvent::Type::kRawKeyDown,
                                       modifiers, base::TimeTicks::Now());
  FillKeyEvent(&event, blink::WebInputEvent::Type::kRawKeyDown, *mapping);
  host->ForwardKeyboardEvent(event);

  // Char — only for keys that produce a printable/control character. This is
  // what triggers cell commits in Google Sheets, form submits on Enter for
  // some apps, etc. Skip for pure navigation keys (Arrows, Home/End, etc.)
  // because those don't go through the Char pipeline.
  if (NamedKeyToChar(mapping->key_code) != 0) {
    input::NativeWebKeyboardEvent char_event(
        blink::WebInputEvent::Type::kChar, modifiers, base::TimeTicks::Now());
    FillKeyEvent(&char_event, blink::WebInputEvent::Type::kChar, *mapping);
    host->ForwardKeyboardEvent(char_event);
  }

  // KeyUp.
  input::NativeWebKeyboardEvent up_event(blink::WebInputEvent::Type::kKeyUp,
                                          modifiers, base::TimeTicks::Now());
  FillKeyEvent(&up_event, blink::WebInputEvent::Type::kKeyUp, *mapping);
  host->ForwardKeyboardEvent(up_event);

  base::DictValue out;
  out.Set("ok", true);
  out.Set("key", *key_name);
  out.Set("modifiers", static_cast<int>(modifiers));
  std::move(reply).Run(TextResult(base::Value(std::move(out))));
}
void PressKey(Profile* p, const base::DictValue& a,
              base::OnceCallback<void(base::DictValue)> reply) {
  WriteGateAsync(p, &PressKeyImpl, a, std::move(reply));
}

}  // namespace

void RegisterSessionatTools(
    McpService* /*service*/,
    Profile* profile,
    std::map<std::string, McpService::ToolEntry>* tools) {
  // Sync-handler registration. The inner trampoline adapts a synchronous
  // DictValue-returning handler to the McpService::ToolCallback async
  // signature by replying immediately with the synchronous result.
  auto add = [&](const std::string& name, const std::string& description,
                 base::DictValue schema,
                 base::RepeatingCallback<base::DictValue(
                     Profile*, const base::DictValue&)> handler) {
    McpService::ToolEntry entry;
    entry.name = name;
    entry.description = description;
    entry.input_schema = std::move(schema);
    entry.handler = base::BindRepeating(
        [](Profile* p,
           base::RepeatingCallback<base::DictValue(
               Profile*, const base::DictValue&)> h,
           const base::DictValue& args,
           base::OnceCallback<void(base::DictValue)> reply) {
          std::move(reply).Run(h.Run(p, args));
        },
        profile, std::move(handler));
    (*tools)[name] = std::move(entry);
  };

  // Async-handler registration. The handler keeps the reply callback and
  // invokes it whenever the renderer round-trip finishes.
  auto add_async = [&](const std::string& name, const std::string& description,
                       base::DictValue schema,
                       base::RepeatingCallback<void(
                           Profile*, const base::DictValue&,
                           base::OnceCallback<void(base::DictValue)>)>
                           handler) {
    McpService::ToolEntry entry;
    entry.name = name;
    entry.description = description;
    entry.input_schema = std::move(schema);
    entry.handler = base::BindRepeating(
        [](Profile* p,
           base::RepeatingCallback<void(Profile*, const base::DictValue&,
                                         base::OnceCallback<void(
                                             base::DictValue)>)> h,
           const base::DictValue& args,
           base::OnceCallback<void(base::DictValue)> reply) {
          h.Run(p, args, std::move(reply));
        },
        profile, std::move(handler));
    (*tools)[name] = std::move(entry);
  };

  // ---- list_workspaces ----
  add("sessionat_list_workspaces",
      "List every Sessionat workspace with tab counts and pinned/active "
      "status. Returns the active workspace id.",
      EmptyInputSchema(),
      base::BindRepeating(&ListWorkspaces));

  // ---- get_active_workspace ----
  add("sessionat_get_active_workspace",
      "Return the currently-active workspace and the URLs of every tab in "
      "it (snapshotted by the periodic auto-save up to ~30s ago).",
      EmptyInputSchema(),
      base::BindRepeating(&GetActiveWorkspace));

  // ---- get_workspace ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("workspace_id", StringProp("Workspace UUID."));
    schema.Set("properties", std::move(props));
    base::ListValue required;
    required.Append("workspace_id");
    schema.Set("required", std::move(required));
    schema.Set("additionalProperties", false);
    add("sessionat_get_workspace",
        "Look up a single workspace by id, including every saved tab URL.",
        std::move(schema),
        base::BindRepeating(&GetWorkspace));
  }

  // ---- create_workspace (WRITE tool) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    base::DictValue name_prop;
    name_prop.Set("type", "string");
    name_prop.Set("description",
                  "Workspace name (1-50 characters). Shown in the sidebar.");
    name_prop.Set("minLength", 1);
    name_prop.Set("maxLength", 50);
    props.Set("name", std::move(name_prop));
    props.Set("color",
              StringProp("Optional hex color like '#F97316' for the "
                          "sidebar chip. Defaults to the brand color."));
    props.Set("icon",
              StringProp("Optional emoji or single character to render in "
                          "the sidebar (e.g. '💼')."));
    schema.Set("properties", std::move(props));
    base::ListValue required;
    required.Append("name");
    schema.Set("required", std::move(required));
    schema.Set("additionalProperties", false);
    add("sessionat_create_workspace",
        "WRITE TOOL — create a new workspace in the user's Sessionat "
        "browser. Returns {ok, id, name, color, icon, tab_count}. "
        "Disabled by default; user must enable write tools via "
        "chrome://sessionat-mcp/ first.",
        std::move(schema),
        base::BindRepeating(&CreateWorkspace));
  }

  // ---- get_visits ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("range",
              EnumProp("Time range to query.",
                        {"today", "7d", "30d", "all"}, "today"));
    props.Set("workspace_id", StringProp("Optional workspace filter."));
    props.Set("limit", NumberProp("Max rows (default 50, max 500).", 1, 500));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add("sessionat_get_visits",
        "Page-visit history newest-first for a range, optionally scoped to a "
        "single workspace. Each row carries url, title, host, category, "
        "active_seconds, and timestamp.",
        std::move(schema),
        base::BindRepeating(&GetVisits));
  }

  // ---- get_top_sites ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("range",
              EnumProp("Time range to query.",
                        {"today", "7d", "30d", "all"}, "today"));
    props.Set("workspace_id", StringProp("Optional workspace filter."));
    props.Set("limit", NumberProp("Max hosts (default 10, max 100).", 1, 100));
    props.Set("order_by",
              EnumProp("Ranking metric. \"visits\" ranks by raw visit count "
                       "(the original behavior). \"active_seconds\" ranks by "
                       "total foreground time spent on the host in the range.",
                        {"visits", "active_seconds"}, "visits"));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add("sessionat_get_top_sites",
        "Top hosts in a range, with auto-assigned category. By default ranks "
        "by visit count; pass order_by=\"active_seconds\" to rank by total "
        "foreground time instead. Each row carries host, visit_count, "
        "active_seconds, and category.",
        std::move(schema),
        base::BindRepeating(&GetTopSites));
  }

  // ---- get_category_breakdown ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("range",
              EnumProp("Time range to query.",
                        {"today", "7d", "30d", "all"}, "today"));
    props.Set("workspace_id", StringProp("Optional workspace filter."));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add("sessionat_get_category_breakdown",
        "How the user's browsing splits across categories "
        "(Development / Social / News / Reference / Entertainment / Shopping "
        "/ Work / Finance / Email / Other) for a range. Returns visit "
        "counts, active time, and percentage share per category.",
        std::move(schema),
        base::BindRepeating(&GetCategoryBreakdown));
  }

  // ---- search_visits ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("query",
              StringProp("Case-insensitive substring; matches URL or title."));
    props.Set("range",
              EnumProp("Time range to search.",
                        {"today", "7d", "30d", "all"}, "7d"));
    props.Set("workspace_id", StringProp("Optional workspace filter."));
    props.Set("limit", NumberProp("Max rows (default 50, max 500).", 1, 500));
    base::ListValue required;
    required.Append("query");
    schema.Set("properties", std::move(props));
    schema.Set("required", std::move(required));
    schema.Set("additionalProperties", false);
    add("sessionat_search_visits",
        "Substring search over the user's visit history (URL or title, "
        "case-insensitive) within a time range, optionally scoped to a "
        "workspace. Returns newest-first rows with the same shape as "
        "sessionat_get_visits: url, host, title, workspace_id, "
        "timestamp_ms, active_seconds, category.",
        std::move(schema),
        base::BindRepeating(&SearchVisits));
  }

  // ---- get_visits_for_host ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("host",
              StringProp("Hostname to drill into, e.g. \"github.com\". "
                         "Case-insensitive; a leading \"www.\" is stripped."));
    props.Set("range",
              EnumProp("Time range to query.",
                        {"today", "7d", "30d", "all"}, "30d"));
    props.Set("workspace_id", StringProp("Optional workspace filter."));
    props.Set("limit",
              NumberProp("Max rows (default 100, max 500).", 1, 500));
    schema.Set("properties", std::move(props));
    base::ListValue required;
    required.Append("host");
    schema.Set("required", std::move(required));
    schema.Set("additionalProperties", false);
    add("sessionat_get_visits_for_host",
        "All page visits for a single host within a time range, newest-first. "
        "Use after sessionat_get_top_sites to drill into one domain — returns "
        "the same row shape as sessionat_get_visits (url, title, host, "
        "workspace_id, timestamp_ms, active_seconds, category).",
        std::move(schema),
        base::BindRepeating(&GetVisitsForHost));
  }

  // ---- get_visit_buckets ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("bucket",
              EnumProp("Bucket dimension. 'hour' aggregates by local "
                       "hour-of-day (keys '0'..'23'). 'day_of_week' "
                       "aggregates by weekday (keys '0'..'6', 0=Mon..6=Sun). "
                       "'day' returns one row per calendar date in range "
                       "(keys 'YYYY-MM-DD').",
                       {"hour", "day_of_week", "day"}, "hour"));
    props.Set("range",
              EnumProp("Time range to query.",
                       {"today", "7d", "30d", "all"}, "30d"));
    props.Set("workspace_id", StringProp("Optional workspace filter."));
    schema.Set("properties", std::move(props));
    base::ListValue required;
    required.Append("bucket");
    schema.Set("required", std::move(required));
    schema.Set("additionalProperties", false);
    add("sessionat_get_visit_buckets",
        "Time-distribution of browsing for a range. Three modes: 'hour' "
        "(0-23 local hour-of-day, aggregated — answers 'when during the day "
        "do I browse most?'), 'day_of_week' (0-6, 0=Mon, aggregated — "
        "'what does my Monday vs Friday look like?'), or 'day' (one row per "
        "calendar date in range, max 366 rows; longer ranges are silently "
        "capped to the last 366 days). Each row carries visit_count and "
        "active_seconds. Empty buckets are returned with zeros.",
        std::move(schema),
        base::BindRepeating(&GetVisitBuckets));
  }

  // ---- open_url (WRITE tool) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("url", StringProp("Absolute http:// or https:// URL to open."));
    base::DictValue bg;
    bg.Set("type", "boolean");
    bg.Set("description", "Open in a background tab. Default false.");
    bg.Set("default", false);
    props.Set("background", std::move(bg));
    schema.Set("properties", std::move(props));
    base::ListValue required;
    required.Append("url");
    schema.Set("required", std::move(required));
    schema.Set("additionalProperties", false);
    add("sessionat_open_url",
        "WRITE TOOL — opens a URL in the user's active Sessionat browser "
        "window (new tab). Disabled by default; user must enable write "
        "tools via chrome://sessionat-mcp/ first.",
        std::move(schema),
        base::BindRepeating(&OpenUrl));
  }

  // ---- list_open_tabs (WRITE tool — needs the same gate as other browser
  //      drivers since tab metadata is sensitive) ----
  add("sessionat_list_open_tabs",
      "WRITE TOOL — list every tab in the user's active Sessionat window "
      "with index, url, title, and which is currently focused. Use the "
      "returned indices with sessionat_focus_tab / sessionat_close_tab.",
      EmptyInputSchema(),
      base::BindRepeating(&ListOpenTabs));

  // ---- get_active_tab ----
  add("sessionat_get_active_tab",
      "WRITE TOOL — return the currently-focused tab's index, url, and title.",
      EmptyInputSchema(),
      base::BindRepeating(&GetActiveTab));

  // ---- focus_tab ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("index", NumberProp("Tab index from list_open_tabs.", 0, 1000));
    schema.Set("properties", std::move(props));
    base::ListValue req;
    req.Append("index");
    schema.Set("required", std::move(req));
    schema.Set("additionalProperties", false);
    add("sessionat_focus_tab",
        "WRITE TOOL — bring a tab to the foreground by its index.",
        std::move(schema),
        base::BindRepeating(&FocusTab));
  }

  // ---- close_tab ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("index", NumberProp("Tab index from list_open_tabs.", 0, 1000));
    schema.Set("properties", std::move(props));
    base::ListValue req;
    req.Append("index");
    schema.Set("required", std::move(req));
    schema.Set("additionalProperties", false);
    add("sessionat_close_tab",
        "WRITE TOOL — close a tab by its index.",
        std::move(schema),
        base::BindRepeating(&CloseTab));
  }

  // ---- navigate_active_tab ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("url", StringProp("Absolute http(s):// URL."));
    schema.Set("properties", std::move(props));
    base::ListValue req;
    req.Append("url");
    schema.Set("required", std::move(req));
    schema.Set("additionalProperties", false);
    add("sessionat_navigate_active_tab",
        "WRITE TOOL — navigate the currently-focused tab to a URL "
        "*in place* (no new tab). Replaces the page the user is on.",
        std::move(schema),
        base::BindRepeating(&NavigateActiveTab));
  }

  // ---- get_page_text (WRITE tool — async) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("max_chars",
              NumberProp("Cap on returned text length (default 50000, "
                          "max 200000).",
                          1, 200000));
    props.Set("iframe_selector",
              StringProp("Optional CSS selector for a same-origin iframe to "
                          "read inside. Cross-origin iframes return an error."));
    props.Set("root_selector",
              StringProp("Optional CSS selector to scope text extraction to "
                          "a specific element (e.g. a modal or chat panel). "
                          "Default: document.body."));
    props.Set("frame_url_match",
              StringProp("Optional substring of a subframe's URL. If set, "
                          "reads from the first matching frame (including "
                          "cross-origin iframes). Use sessionat_list_frames "
                          "to enumerate available frames."));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async("sessionat_get_page_text",
              "WRITE TOOL — return the visible text content of the active "
              "tab, capped at max_chars. Pass `root_selector` to scope to a "
              "modal/panel, or `iframe_selector` for same-origin embeds. "
              "Only works on http(s):// pages.",
              std::move(schema),
              base::BindRepeating(&GetPageText));
  }

  // ---- click (WRITE tool — async) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("selector",
              StringProp("CSS selector for the element to click."));
    props.Set("text",
              StringProp("Alternative to selector: case-insensitive contains "
                          "match against visible text / aria-label / title of "
                          "clickable elements (a, button, role=button, etc.)."));
    base::DictValue exact;
    exact.Set("type", "boolean");
    exact.Set("description",
              "If true, `text` must equal the element's visible text "
              "exactly (case-insensitive). Default false (contains).");
    exact.Set("default", false);
    props.Set("text_exact", std::move(exact));
    props.Set("frame_url_match",
              StringProp("Optional substring of a subframe's URL. If set, "
                          "clicks inside the first matching frame "
                          "(cross-origin OK)."));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async("sessionat_click",
              "WRITE TOOL — click the first element matching either a CSS "
              "selector or visible text (contains, or exact with "
              "text_exact:true). Pierces shadow DOM (Reddit, Slack, "
              "Polymer). Returns {ok, tag, text, href?}.",
              std::move(schema),
              base::BindRepeating(&Click));
  }

  // ---- type (WRITE tool — async) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("selector",
              StringProp("CSS selector for the input / textarea / "
                          "contenteditable element."));
    props.Set("field_label",
              StringProp("Alternative to selector: case-insensitive contains "
                          "match against the field's <label>, aria-label, "
                          "placeholder, or name attribute."));
    props.Set("text", StringProp("Text value to set in the field."));
    base::DictValue sub;
    sub.Set("type", "boolean");
    sub.Set("description",
            "After typing, dispatch an Enter keydown and call form."
            "requestSubmit() if the element has a form. Default false.");
    sub.Set("default", false);
    props.Set("submit", std::move(sub));
    props.Set("frame_url_match",
              StringProp("Optional substring of a subframe's URL. If set, "
                          "types inside the first matching frame "
                          "(cross-origin OK)."));
    schema.Set("properties", std::move(props));
    base::ListValue req;
    req.Append("text");
    schema.Set("required", std::move(req));
    schema.Set("additionalProperties", false);
    add_async("sessionat_type",
              "WRITE TOOL — focus the matched element, set its value to "
              "`text`, dispatch input/change events. Provide either selector "
              "OR field_label to locate the input. Optional `submit:true` "
              "presses Enter.",
              std::move(schema),
              base::BindRepeating(&Type));
  }

  // ---- wait_for (WRITE tool — async, polling) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("selector",
              StringProp("CSS selector to wait for."));
    props.Set("text",
              StringProp("Visible text contains (case-insensitive) "
                          "alternative to selector."));
    base::DictValue we;
    we.Set("type", "boolean");
    we.Set("description",
           "Exact-text match instead of contains. Default false.");
    we.Set("default", false);
    props.Set("text_exact", std::move(we));
    props.Set("timeout_ms",
              NumberProp("Max time to wait in milliseconds "
                          "(default 5000, max 30000).",
                          100, 30000));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async("sessionat_wait_for",
              "WRITE TOOL — poll the active tab until an element matching "
              "selector OR text appears (visible, non-zero bounding rect), "
              "or until timeout_ms elapses. Use after a navigate/click to "
              "wait for an SPA to render before reading or clicking.",
              std::move(schema),
              base::BindRepeating(&WaitFor));
  }

  // ---- scroll (WRITE tool — async) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("selector",
              StringProp("If set, scrolls the element matched by this "
                          "selector into view (overrides dx/dy)."));
    base::DictValue dx;
    dx.Set("type", "integer");
    dx.Set("description", "Horizontal scroll delta (px). Default 0.");
    dx.Set("minimum", -100000);
    dx.Set("maximum", 100000);
    props.Set("dx", std::move(dx));
    base::DictValue dy;
    dy.Set("type", "integer");
    dy.Set("description",
           "Vertical scroll delta (px). Positive scrolls down. Default 0.");
    dy.Set("minimum", -100000);
    dy.Set("maximum", 100000);
    props.Set("dy", std::move(dy));
    props.Set("position",
              EnumProp("scrollIntoView block alignment (only used with "
                        "`selector`).",
                        {"start", "center", "end"}, "center"));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async("sessionat_scroll",
              "WRITE TOOL — scroll the active tab. Either scroll an element "
              "into view by selector, or scroll the window by dx/dy pixels. "
              "Returns {ok, scrollY, max_scroll_y} so you can check if you've "
              "reached the bottom of an infinite feed.",
              std::move(schema),
              base::BindRepeating(&Scroll));
  }

  // ---- get_dom_outline (WRITE tool — async) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("limit",
              NumberProp("Max items per category (default 50, max 200).",
                          1, 200));
    props.Set("root_selector",
              StringProp("Optional CSS selector to scope the outline to a "
                          "specific element (e.g. a chat panel or modal). "
                          "Default: whole document."));
    props.Set("frame_url_match",
              StringProp("Optional substring of a subframe's URL. If set, "
                          "outlines the first matching frame (cross-origin "
                          "OK). Use sessionat_list_frames to enumerate."));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async("sessionat_get_dom_outline",
              "WRITE TOOL — structured snapshot of the active tab (or a "
              "section via `root_selector`). Returns {url, title, scoped_to, "
              "links, buttons, headings, inputs, images, iframes} — only "
              "visible elements with text or aria-label / href / name / etc. "
              "(empty noise filtered). Use this when CSS selectors fail on "
              "auto-class SPAs.",
              std::move(schema),
              base::BindRepeating(&GetDomOutline));
  }

  // ---- screenshot (WRITE tool — async) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("max_width",
              NumberProp("Maximum image width in px; aspect ratio preserved "
                          "(default 1280, max 2048).",
                          200, 2048));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async("sessionat_screenshot",
              "WRITE TOOL — capture the active tab's viewport as a PNG and "
              "return it as an MCP image content block (clients like Claude "
              "Desktop display it inline). Scaled down to max_width to keep "
              "payload small.",
              std::move(schema),
              base::BindRepeating(&Screenshot));
  }

  // ---- press_key (WRITE tool — async, real OS-trusted key event) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("key",
              EnumProp("Named key to press.",
                        {"Enter", "Escape", "Tab", "Backspace", "Delete",
                         "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
                         "Home", "End", "PageUp", "PageDown", "Space"}));
    props.Set("text",
              StringProp("Alternative to `key`: a string typed character-by-"
                          "character as real key events. Use this for canvas-"
                          "rendered apps (Google Docs, Figma) where there is "
                          "no DOM input target for sessionat_type. Max 5000 "
                          "chars."));
    base::DictValue mods;
    mods.Set("type", "array");
    mods.Set("description",
             "Held modifier keys: any of \"shift\", \"ctrl\", \"alt\", "
             "\"meta\". Only applies when using `key`. Empty array = "
             "no modifiers.");
    base::DictValue items;
    items.Set("type", "string");
    base::ListValue mod_enum;
    mod_enum.Append("shift");
    mod_enum.Append("ctrl");
    mod_enum.Append("control");
    mod_enum.Append("alt");
    mod_enum.Append("meta");
    mod_enum.Append("cmd");
    items.Set("enum", std::move(mod_enum));
    mods.Set("items", std::move(items));
    props.Set("modifiers", std::move(mods));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async(
        "sessionat_list_frames",
        "WRITE TOOL — enumerate every frame (main + same- AND cross-origin "
        "subframes) in the active tab. Returns {frame_count, frames:["
        "{url, frame_name, is_main, is_cross_origin, process_id, "
        "routing_id}]}. Pair with `frame_url_match` on get_page_text / "
        "get_dom_outline / click / type to drive cross-origin iframes "
        "(Stripe checkout, OAuth, reCAPTCHA, embedded YouTube/Twitter).",
        EmptyInputSchema(), base::BindRepeating(&ListFrames));
  }

  // ---- press_key (WRITE tool — async, real OS-trusted key event) ----
  {
    base::DictValue schema;
    schema.Set("type", "object");
    base::DictValue props;
    props.Set("key",
              EnumProp("Named key to press.",
                        {"Enter", "Escape", "Tab", "Backspace", "Delete",
                         "ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight",
                         "Home", "End", "PageUp", "PageDown", "Space"}));
    props.Set("text",
              StringProp("Alternative to `key`: a string typed character-by-"
                          "character as real key events. Use this for canvas-"
                          "rendered apps (Google Docs, Figma) where there is "
                          "no DOM input target for sessionat_type. Max 5000 "
                          "chars."));
    base::DictValue mods;
    mods.Set("type", "array");
    mods.Set("description",
             "Held modifier keys: any of \"shift\", \"ctrl\", \"alt\", "
             "\"meta\". Empty array = no modifiers.");
    base::DictValue items;
    items.Set("type", "string");
    base::ListValue mod_enum;
    mod_enum.Append("shift");
    mod_enum.Append("ctrl");
    mod_enum.Append("control");
    mod_enum.Append("alt");
    mod_enum.Append("meta");
    mod_enum.Append("cmd");
    items.Set("enum", std::move(mod_enum));
    mods.Set("items", std::move(items));
    props.Set("modifiers", std::move(mods));
    schema.Set("properties", std::move(props));
    schema.Set("additionalProperties", false);
    add_async(
        "sessionat_press_key",
        "WRITE TOOL — send REAL isTrusted=true keyboard events via "
        "RenderWidgetHost::ForwardKeyboardEvent. Two modes: pass `key` "
        "(named: Enter, Escape, ArrowDown, …) with optional `modifiers`, OR "
        "pass `text` to type a string char-by-char (the path for canvas-"
        "rendered apps like Google Docs that have no DOM input target). "
        "Prefer sessionat_type for normal inputs/textareas/contenteditable.",
        std::move(schema), base::BindRepeating(&PressKey));
  }
}

}  // namespace sessionat
