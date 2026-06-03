// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/visit_analytics_service.h"

#include <algorithm>
#include <unordered_map>

#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace sessionat {

namespace {

constexpr char kVisitsListPref[] = "sessionat.visits";
// Master tracking toggle. Default true.
constexpr char kVisitTrackingEnabledPref[] = "sessionat.visit_tracking_enabled";
// User-managed list of hosts to never record (bare domains match all subdomains).
constexpr char kVisitExcludedHostsPref[] = "sessionat.visit_excluded_hosts";

// Cap on total stored visits — 30 days is reasonable, but we also bound
// by count so a single tab-loading-loop can't blow up the prefs file.
constexpr size_t kMaxStoredVisits = 5000;
constexpr int kRetentionDays = 30;

// Skip chrome://, about:, view-source: etc. We only track actual web pages.
bool IsWebUrl(const GURL& url) {
  return url.is_valid() && (url.SchemeIs("http") || url.SchemeIs("https"));
}

// JSON keys.
constexpr char kKeyUrl[] = "u";
constexpr char kKeyTitle[] = "t";
constexpr char kKeyWorkspace[] = "w";
constexpr char kKeyTimestamp[] = "ts";
constexpr char kKeyActiveMs[] = "a";

base::DictValue VisitToDict(const Visit& v) {
  base::DictValue d;
  d.Set(kKeyUrl, v.url.spec());
  d.Set(kKeyTitle, v.title);
  d.Set(kKeyWorkspace, v.workspace_id);
  d.Set(kKeyTimestamp,
        base::NumberToString(v.timestamp.InMillisecondsFSinceUnixEpoch()));
  d.Set(kKeyActiveMs, v.active_ms);
  return d;
}

std::optional<Visit> VisitFromDict(const base::DictValue& d) {
  const std::string* url_str = d.FindString(kKeyUrl);
  if (!url_str) return std::nullopt;
  GURL url(*url_str);
  if (!IsWebUrl(url)) return std::nullopt;
  Visit v;
  v.url = url;
  v.host = url.host();
  if (const std::string* t = d.FindString(kKeyTitle)) v.title = *t;
  if (const std::string* w = d.FindString(kKeyWorkspace)) v.workspace_id = *w;
  if (const std::string* ts = d.FindString(kKeyTimestamp)) {
    double ms;
    if (base::StringToDouble(*ts, &ms)) {
      v.timestamp = base::Time::FromMillisecondsSinceUnixEpoch(ms);
    }
  }
  v.active_ms = d.FindInt(kKeyActiveMs).value_or(0);
  return v;
}

}  // namespace

// static
void VisitAnalyticsService::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  registry->RegisterListPref(kVisitsListPref);
  registry->RegisterBooleanPref(kVisitTrackingEnabledPref, true);
  registry->RegisterListPref(kVisitExcludedHostsPref);
}

bool VisitAnalyticsService::IsTrackingEnabled() const {
  return prefs_->GetBoolean(kVisitTrackingEnabledPref);
}

void VisitAnalyticsService::SetTrackingEnabled(bool enabled) {
  prefs_->SetBoolean(kVisitTrackingEnabledPref, enabled);
}

std::vector<std::string> VisitAnalyticsService::GetExcludedHosts() const {
  std::vector<std::string> out;
  for (const auto& entry : prefs_->GetList(kVisitExcludedHostsPref)) {
    if (entry.is_string()) out.push_back(entry.GetString());
  }
  return out;
}

void VisitAnalyticsService::SetExcludedHosts(
    std::vector<std::string> hosts) {
  base::ListValue list;
  for (auto& h : hosts) {
    if (!h.empty()) list.Append(std::move(h));
  }
  prefs_->SetList(kVisitExcludedHostsPref, std::move(list));
}

bool VisitAnalyticsService::IsHostExcluded(const std::string& host) const {
  if (host.empty()) return false;
  for (const auto& entry : prefs_->GetList(kVisitExcludedHostsPref)) {
    if (!entry.is_string()) continue;
    const std::string& excl = entry.GetString();
    if (excl.empty()) continue;
    // Exact match OR host endswith ".excl" (so "example.com" matches
    // "www.example.com" and "foo.bar.example.com").
    if (host == excl) return true;
    if (host.size() > excl.size() + 1 &&
        host.compare(host.size() - excl.size() - 1, excl.size() + 1,
                     "." + excl) == 0) {
      return true;
    }
  }
  return false;
}

std::string VisitAnalyticsService::ExportToJson() const {
  base::DictValue root;
  root.Set("schema", "sessionat.visits.v1");
  root.Set("exported_at_ms",
           static_cast<double>(
               base::Time::Now().InMillisecondsSinceUnixEpoch()));
  root.Set("count", static_cast<int>(visits_.size()));
  base::ListValue rows;
  for (const auto& v : visits_) {
    base::DictValue d;
    d.Set("url", v.url.spec());
    d.Set("host", v.host);
    d.Set("title", v.title);
    d.Set("workspace_id", v.workspace_id);
    d.Set("timestamp_ms",
          static_cast<double>(v.timestamp.InMillisecondsSinceUnixEpoch()));
    d.Set("active_ms", v.active_ms);
    rows.Append(std::move(d));
  }
  root.Set("visits", std::move(rows));
  std::string out;
  base::JSONWriter::WriteWithOptions(
      root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &out);
  return out;
}

VisitAnalyticsService::VisitAnalyticsService(Profile* profile)
    : profile_(profile), prefs_(profile->GetPrefs()) {
  Load();
}

VisitAnalyticsService::~VisitAnalyticsService() = default;

void VisitAnalyticsService::RecordVisit(const GURL& url,
                                         const std::string& title,
                                         const std::string& workspace_id) {
  if (!IsWebUrl(url)) {
    return;
  }
  // Privacy gates — checked before any state mutation.
  if (!IsTrackingEnabled()) {
    return;
  }
  if (IsHostExcluded(std::string(url.host()))) {
    return;
  }
  // Deduplicate: if the last visit is the same URL within 5 seconds (e.g. an
  // SPA history.pushState followed by a real load), update the existing
  // entry's title rather than appending.
  base::Time now = base::Time::Now();
  if (!visits_.empty()) {
    Visit& last = visits_.back();
    if (last.url == url && (now - last.timestamp) < base::Seconds(5)) {
      if (!title.empty()) last.title = title;
      Save();
      return;
    }
  }

  Visit v;
  v.url = url;
  v.host = url.host();
  v.title = title;
  v.workspace_id = workspace_id;
  v.timestamp = now;
  visits_.push_back(std::move(v));

  TrimOldVisits();
  Save();
}

std::vector<Visit> VisitAnalyticsService::GetVisitsInRange(
    base::Time start,
    base::Time end) const {
  std::vector<Visit> out;
  for (const auto& v : visits_) {
    if (v.timestamp >= start && v.timestamp < end) {
      out.push_back(v);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const Visit& a, const Visit& b) {
              return a.timestamp > b.timestamp;
            });
  return out;
}

std::vector<Visit> VisitAnalyticsService::GetVisitsForDay(
    base::Time local_day) const {
  base::Time start = local_day.LocalMidnight();
  base::Time end = start + base::Days(1);
  return GetVisitsInRange(start, end);
}

std::vector<std::pair<std::string, int>>
VisitAnalyticsService::GetHostCountsForDay(base::Time local_day) const {
  std::unordered_map<std::string, int> counts;
  for (const auto& v : GetVisitsForDay(local_day)) {
    if (v.host.empty()) continue;
    counts[v.host]++;
  }
  std::vector<std::pair<std::string, int>> out(counts.begin(), counts.end());
  std::sort(out.begin(), out.end(),
            [](const auto& a, const auto& b) {
              if (a.second != b.second) return a.second > b.second;
              return a.first < b.first;
            });
  return out;
}

std::vector<Visit> VisitAnalyticsService::GetTopVisitsForDay(
    base::Time local_day,
    size_t n) const {
  // Group visits by host, keep the most-recent representative per host,
  // sort by count desc, then by recency.
  struct Bucket {
    Visit rep;
    int count = 0;
  };
  std::unordered_map<std::string, Bucket> buckets;
  for (const auto& v : GetVisitsForDay(local_day)) {
    auto& b = buckets[v.host];
    b.count++;
    if (b.count == 1 || v.timestamp > b.rep.timestamp) {
      b.rep = v;
    }
  }
  std::vector<Bucket> sorted;
  for (auto& [_, b] : buckets) sorted.push_back(std::move(b));
  std::sort(sorted.begin(), sorted.end(),
            [](const Bucket& a, const Bucket& b) {
              if (a.count != b.count) return a.count > b.count;
              return a.rep.timestamp > b.rep.timestamp;
            });
  std::vector<Visit> out;
  out.reserve(std::min(n, sorted.size()));
  for (size_t i = 0; i < sorted.size() && i < n; ++i) {
    out.push_back(std::move(sorted[i].rep));
  }
  return out;
}

void VisitAnalyticsService::RecordActiveTime(const GURL& url, int delta_ms) {
  if (!IsWebUrl(url) || delta_ms <= 0) return;
  if (!IsTrackingEnabled() || IsHostExcluded(std::string(url.host()))) return;
  // Cap a single contribution to 30 minutes — guards against runaway timers
  // (e.g. system sleep without a visibility change firing).
  delta_ms = std::min(delta_ms, 30 * 60 * 1000);

  base::Time cutoff = base::Time::Now() - base::Hours(24);
  // Find the most-recent matching visit within 24h, scan in reverse.
  for (auto it = visits_.rbegin(); it != visits_.rend(); ++it) {
    if (it->timestamp < cutoff) break;
    if (it->url == url) {
      it->active_ms += delta_ms;
      Save();
      return;
    }
  }
  // No matching visit — silently drop (the navigation must have fallen out
  // of retention or never been recorded, e.g. incognito).
}

namespace {
// Inline starter category table — ~150 popular domains. Future polish:
// move this to a separate categories.json baked into the resources pak.
const std::pair<const char*, const char*> kStarterCategories[] = {
    // ---- Development ----
    {"github.com", "Development"},
    {"gitlab.com", "Development"},
    {"bitbucket.org", "Development"},
    {"stackoverflow.com", "Development"},
    {"stackexchange.com", "Development"},
    {"developer.mozilla.org", "Development"},
    {"npmjs.com", "Development"},
    {"www.npmjs.com", "Development"},
    {"pypi.org", "Development"},
    {"crates.io", "Development"},
    {"hub.docker.com", "Development"},
    {"docker.com", "Development"},
    {"kubernetes.io", "Development"},
    {"chromium.googlesource.com", "Development"},
    {"source.chromium.org", "Development"},
    {"go.dev", "Development"},
    {"rust-lang.org", "Development"},
    {"www.rust-lang.org", "Development"},
    {"python.org", "Development"},
    {"www.python.org", "Development"},
    {"nodejs.org", "Development"},
    {"reactjs.org", "Development"},
    {"react.dev", "Development"},
    {"vuejs.org", "Development"},
    {"angular.io", "Development"},
    {"tailwindcss.com", "Development"},
    {"nextjs.org", "Development"},
    {"vercel.com", "Development"},
    {"netlify.com", "Development"},
    {"cloudflare.com", "Development"},
    {"developers.google.com", "Development"},
    {"developer.apple.com", "Development"},
    {"learn.microsoft.com", "Development"},
    {"docs.microsoft.com", "Development"},
    {"console.aws.amazon.com", "Development"},
    {"console.cloud.google.com", "Development"},
    {"portal.azure.com", "Development"},
    {"replit.com", "Development"},
    {"codesandbox.io", "Development"},
    {"codepen.io", "Development"},
    {"jsfiddle.net", "Development"},
    {"regex101.com", "Development"},
    {"caniuse.com", "Development"},
    {"jsbin.com", "Development"},

    // ---- Social ----
    {"twitter.com", "Social"},
    {"x.com", "Social"},
    {"facebook.com", "Social"},
    {"www.facebook.com", "Social"},
    {"instagram.com", "Social"},
    {"www.instagram.com", "Social"},
    {"linkedin.com", "Social"},
    {"www.linkedin.com", "Social"},
    {"reddit.com", "Social"},
    {"www.reddit.com", "Social"},
    {"old.reddit.com", "Social"},
    {"news.ycombinator.com", "Social"},
    {"mastodon.social", "Social"},
    {"bsky.app", "Social"},
    {"threads.net", "Social"},
    {"tiktok.com", "Social"},
    {"www.tiktok.com", "Social"},
    {"discord.com", "Social"},
    {"slack.com", "Social"},
    {"telegram.org", "Social"},
    {"web.telegram.org", "Social"},
    {"web.whatsapp.com", "Social"},

    // ---- News ----
    {"nytimes.com", "News"},
    {"www.nytimes.com", "News"},
    {"theguardian.com", "News"},
    {"www.theguardian.com", "News"},
    {"bbc.com", "News"},
    {"www.bbc.com", "News"},
    {"bbc.co.uk", "News"},
    {"cnn.com", "News"},
    {"www.cnn.com", "News"},
    {"reuters.com", "News"},
    {"www.reuters.com", "News"},
    {"bloomberg.com", "News"},
    {"www.bloomberg.com", "News"},
    {"wsj.com", "News"},
    {"www.wsj.com", "News"},
    {"ft.com", "News"},
    {"www.ft.com", "News"},
    {"techcrunch.com", "News"},
    {"theverge.com", "News"},
    {"www.theverge.com", "News"},
    {"arstechnica.com", "News"},
    {"www.arstechnica.com", "News"},
    {"wired.com", "News"},
    {"www.wired.com", "News"},
    {"economist.com", "News"},
    {"www.economist.com", "News"},
    {"hn.algolia.com", "News"},

    // ---- Reference ----
    {"wikipedia.org", "Reference"},
    {"en.wikipedia.org", "Reference"},
    {"google.com", "Reference"},
    {"www.google.com", "Reference"},
    {"duckduckgo.com", "Reference"},
    {"www.duckduckgo.com", "Reference"},
    {"bing.com", "Reference"},
    {"www.bing.com", "Reference"},
    {"chat.openai.com", "Reference"},
    {"chatgpt.com", "Reference"},
    {"claude.ai", "Reference"},
    {"www.perplexity.ai", "Reference"},
    {"perplexity.ai", "Reference"},
    {"gemini.google.com", "Reference"},
    {"www.bing.com/chat", "Reference"},
    {"scholar.google.com", "Reference"},
    {"arxiv.org", "Reference"},
    {"www.arxiv.org", "Reference"},

    // ---- Entertainment ----
    {"youtube.com", "Entertainment"},
    {"www.youtube.com", "Entertainment"},
    {"music.youtube.com", "Entertainment"},
    {"netflix.com", "Entertainment"},
    {"www.netflix.com", "Entertainment"},
    {"spotify.com", "Entertainment"},
    {"open.spotify.com", "Entertainment"},
    {"twitch.tv", "Entertainment"},
    {"www.twitch.tv", "Entertainment"},
    {"hulu.com", "Entertainment"},
    {"disneyplus.com", "Entertainment"},
    {"hbomax.com", "Entertainment"},
    {"max.com", "Entertainment"},
    {"vimeo.com", "Entertainment"},
    {"www.vimeo.com", "Entertainment"},
    {"primevideo.com", "Entertainment"},
    {"www.primevideo.com", "Entertainment"},
    {"soundcloud.com", "Entertainment"},
    {"www.imdb.com", "Entertainment"},
    {"imdb.com", "Entertainment"},

    // ---- Shopping ----
    {"amazon.com", "Shopping"},
    {"www.amazon.com", "Shopping"},
    {"amazon.co.uk", "Shopping"},
    {"ebay.com", "Shopping"},
    {"www.ebay.com", "Shopping"},
    {"etsy.com", "Shopping"},
    {"www.etsy.com", "Shopping"},
    {"shopify.com", "Shopping"},
    {"target.com", "Shopping"},
    {"www.target.com", "Shopping"},
    {"walmart.com", "Shopping"},
    {"www.walmart.com", "Shopping"},
    {"bestbuy.com", "Shopping"},
    {"www.bestbuy.com", "Shopping"},
    {"alibaba.com", "Shopping"},
    {"aliexpress.com", "Shopping"},

    // ---- Work / Productivity ----
    {"docs.google.com", "Work"},
    {"sheets.google.com", "Work"},
    {"drive.google.com", "Work"},
    {"calendar.google.com", "Work"},
    {"mail.google.com", "Work"},
    {"meet.google.com", "Work"},
    {"notion.so", "Work"},
    {"www.notion.so", "Work"},
    {"linear.app", "Work"},
    {"figma.com", "Work"},
    {"www.figma.com", "Work"},
    {"miro.com", "Work"},
    {"trello.com", "Work"},
    {"asana.com", "Work"},
    {"app.asana.com", "Work"},
    {"atlassian.net", "Work"},
    {"app.clickup.com", "Work"},
    {"app.basecamp.com", "Work"},
    {"zoom.us", "Work"},
    {"www.zoom.us", "Work"},
    {"office.com", "Work"},
    {"www.office.com", "Work"},
    {"outlook.live.com", "Work"},
    {"outlook.office.com", "Work"},
    {"teams.microsoft.com", "Work"},
    {"airtable.com", "Work"},
    {"calendly.com", "Work"},
    {"loom.com", "Work"},
    {"www.loom.com", "Work"},

    // ---- Finance ----
    {"www.paypal.com", "Finance"},
    {"paypal.com", "Finance"},
    {"www.chase.com", "Finance"},
    {"www.bankofamerica.com", "Finance"},
    {"wise.com", "Finance"},
    {"www.wise.com", "Finance"},
    {"stripe.com", "Finance"},
    {"dashboard.stripe.com", "Finance"},
    {"coinbase.com", "Finance"},
    {"www.coinbase.com", "Finance"},

    // ---- Email ----
    {"www.fastmail.com", "Email"},
    {"fastmail.com", "Email"},
    {"www.protonmail.com", "Email"},
    {"protonmail.com", "Email"},
    {"mail.proton.me", "Email"},
};
}  // namespace

void VisitAnalyticsService::EnsureCategoriesLoaded() const {
  if (categories_loaded_) return;
  for (const auto& [host, cat] : kStarterCategories) {
    host_to_category_[host] = cat;
  }
  categories_loaded_ = true;
}

std::string VisitAnalyticsService::GetCategoryForHost(
    const std::string& host) const {
  EnsureCategoriesLoaded();
  auto it = host_to_category_.find(host);
  if (it != host_to_category_.end()) return it->second;
  // Try the base domain (strip leading www.) as a soft fallback.
  if (host.length() > 4 && host.substr(0, 4) == "www.") {
    auto base_it = host_to_category_.find(host.substr(4));
    if (base_it != host_to_category_.end()) return base_it->second;
  }
  return "Other";
}

std::vector<VisitAnalyticsService::CategoryStat>
VisitAnalyticsService::GetCategoryStatsInRange(
    base::Time start,
    base::Time end,
    const std::string& workspace_filter_id) const {
  EnsureCategoriesLoaded();
  std::map<std::string, CategoryStat> agg;
  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    const std::string cat = GetCategoryForHost(v.host);
    auto& s = agg[cat];
    s.category = cat;
    s.total_visits++;
    s.total_active_ms += v.active_ms;
  }
  std::vector<CategoryStat> out;
  for (auto& [_, s] : agg) out.push_back(std::move(s));
  std::sort(out.begin(), out.end(),
            [](const CategoryStat& a, const CategoryStat& b) {
              if (a.total_visits != b.total_visits) {
                return a.total_visits > b.total_visits;
              }
              return a.category < b.category;
            });
  return out;
}

std::vector<std::pair<std::string, int>>
VisitAnalyticsService::GetHostCountsInRange(
    base::Time start,
    base::Time end,
    const std::string& workspace_filter_id) const {
  std::unordered_map<std::string, int> counts;
  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    if (v.host.empty()) continue;
    counts[v.host]++;
  }
  std::vector<std::pair<std::string, int>> out(counts.begin(), counts.end());
  std::sort(out.begin(), out.end(),
            [](const auto& a, const auto& b) {
              if (a.second != b.second) return a.second > b.second;
              return a.first < b.first;
            });
  return out;
}

std::vector<Visit> VisitAnalyticsService::GetTopVisitsInRange(
    base::Time start,
    base::Time end,
    size_t n,
    const std::string& workspace_filter_id) const {
  struct Bucket {
    Visit rep;
    int count = 0;
  };
  std::unordered_map<std::string, Bucket> buckets;
  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    auto& b = buckets[v.host];
    b.count++;
    if (b.count == 1 || v.timestamp > b.rep.timestamp) {
      b.rep = v;
    }
  }
  std::vector<Bucket> sorted;
  for (auto& [_, b] : buckets) sorted.push_back(std::move(b));
  std::sort(sorted.begin(), sorted.end(),
            [](const Bucket& a, const Bucket& b) {
              if (a.count != b.count) return a.count > b.count;
              return a.rep.timestamp > b.rep.timestamp;
            });
  std::vector<Visit> out;
  out.reserve(std::min(n, sorted.size()));
  for (size_t i = 0; i < sorted.size() && i < n; ++i) {
    out.push_back(std::move(sorted[i].rep));
  }
  return out;
}

std::vector<int> VisitAnalyticsService::GetBucketCounts(
    base::Time start,
    base::Time end,
    base::TimeDelta bucket_size,
    const std::string& workspace_filter_id) const {
  if (bucket_size.is_zero() || end <= start) return {};
  // Round up so the final partial bucket still gets a slot.
  const int64_t total_us = (end - start).InMicroseconds();
  const int64_t bucket_us = bucket_size.InMicroseconds();
  const size_t bucket_count =
      static_cast<size_t>((total_us + bucket_us - 1) / bucket_us);
  std::vector<int> out(bucket_count, 0);
  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    const int64_t offset_us = (v.timestamp - start).InMicroseconds();
    const size_t idx = static_cast<size_t>(offset_us / bucket_us);
    if (idx < bucket_count) out[idx]++;
  }
  return out;
}

std::vector<Visit> VisitAnalyticsService::GetVisitsForHostInRange(
    const std::string& host,
    base::Time start,
    base::Time end,
    const std::string& workspace_filter_id) const {
  std::vector<Visit> out;
  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    if (v.host != host) continue;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(),
            [](const Visit& a, const Visit& b) {
              return a.timestamp > b.timestamp;
            });
  return out;
}

std::vector<Visit> VisitAnalyticsService::SearchVisitsInRange(
    const std::string& query,
    base::Time start,
    base::Time end,
    size_t limit,
    const std::string& workspace_filter_id) const {
  std::vector<Visit> out;
  if (query.empty()) return out;
  const std::string needle = base::ToLowerASCII(query);
  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    const std::string url_lc = base::ToLowerASCII(v.url.spec());
    const std::string title_lc = base::ToLowerASCII(v.title);
    if (url_lc.find(needle) == std::string::npos &&
        title_lc.find(needle) == std::string::npos) {
      continue;
    }
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(),
            [](const Visit& a, const Visit& b) {
              return a.timestamp > b.timestamp;
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

std::vector<VisitAnalyticsService::HostStat>
VisitAnalyticsService::GetHostStatsInRange(
    base::Time start,
    base::Time end,
    const std::string& workspace_filter_id) const {
  std::unordered_map<std::string, HostStat> agg;
  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    if (v.host.empty()) continue;
    auto& s = agg[v.host];
    if (s.host.empty()) s.host = v.host;
    s.visit_count++;
    s.total_active_ms += v.active_ms;
  }
  std::vector<HostStat> out;
  out.reserve(agg.size());
  for (auto& [_, s] : agg) out.push_back(std::move(s));
  std::sort(out.begin(), out.end(),
            [](const HostStat& a, const HostStat& b) {
              if (a.visit_count != b.visit_count) {
                return a.visit_count > b.visit_count;
              }
              return a.host < b.host;
            });
  return out;
}

std::vector<VisitAnalyticsService::BucketStat>
VisitAnalyticsService::GetVisitBuckets(
    base::Time start,
    base::Time end,
    BucketMode mode,
    const std::string& workspace_filter_id) const {
  if (end <= start) return {};

  std::vector<BucketStat> out;
  base::Time start_day = start.LocalMidnight();
  if (mode == BucketMode::kHour) {
    out.resize(24);
    for (int h = 0; h < 24; ++h) out[h].key = base::NumberToString(h);
  } else if (mode == BucketMode::kDayOfWeek) {
    out.resize(7);
    for (int d = 0; d < 7; ++d) out[d].key = base::NumberToString(d);
  } else {
    base::Time end_day = (end - base::Microseconds(1)).LocalMidnight();
    int day_count = (end_day - start_day).InDays() + 1;
    if (day_count < 1) day_count = 1;
    out.resize(day_count);
    for (int i = 0; i < day_count; ++i) {
      base::Time::Exploded e;
      (start_day + base::Days(i)).LocalExplode(&e);
      out[i].key = base::StringPrintf("%04d-%02d-%02d", e.year, e.month,
                                      e.day_of_month);
    }
  }

  for (const auto& v : visits_) {
    if (v.timestamp < start || v.timestamp >= end) continue;
    if (!workspace_filter_id.empty() &&
        v.workspace_id != workspace_filter_id) {
      continue;
    }
    base::Time::Exploded e;
    v.timestamp.LocalExplode(&e);
    size_t idx = 0;
    if (mode == BucketMode::kHour) {
      idx = static_cast<size_t>(e.hour);
    } else if (mode == BucketMode::kDayOfWeek) {
      // Exploded.day_of_week is 0=Sun..6=Sat; remap to 0=Mon..6=Sun.
      int dow_mon0 = (e.day_of_week + 6) % 7;
      idx = static_cast<size_t>(dow_mon0);
    } else {
      base::Time day_t = v.timestamp.LocalMidnight();
      int day_idx = (day_t - start_day).InDays();
      if (day_idx < 0) continue;
      idx = static_cast<size_t>(day_idx);
    }
    if (idx >= out.size()) continue;
    out[idx].visit_count++;
    out[idx].total_active_ms += v.active_ms;
  }
  return out;
}

void VisitAnalyticsService::Clear() {
  visits_.clear();
  Save();
}

void VisitAnalyticsService::Load() {
  visits_.clear();
  const base::ListValue& list = prefs_->GetList(kVisitsListPref);
  visits_.reserve(list.size());
  for (const auto& entry : list) {
    if (!entry.is_dict()) continue;
    if (auto v = VisitFromDict(entry.GetDict())) {
      visits_.push_back(std::move(*v));
    }
  }
  TrimOldVisits();
}

void VisitAnalyticsService::Save() {
  base::ListValue list;
  list.reserve(visits_.size());
  for (const auto& v : visits_) {
    list.Append(VisitToDict(v));
  }
  prefs_->SetList(kVisitsListPref, std::move(list));
}

void VisitAnalyticsService::TrimOldVisits() {
  // Drop anything older than kRetentionDays.
  base::Time cutoff = base::Time::Now() - base::Days(kRetentionDays);
  visits_.erase(
      std::remove_if(visits_.begin(), visits_.end(),
                     [cutoff](const Visit& v) { return v.timestamp < cutoff; }),
      visits_.end());
  // Cap at kMaxStoredVisits — drop oldest.
  if (visits_.size() > kMaxStoredVisits) {
    std::sort(visits_.begin(), visits_.end(),
              [](const Visit& a, const Visit& b) {
                return a.timestamp < b.timestamp;
              });
    visits_.erase(visits_.begin(),
                  visits_.begin() + (visits_.size() - kMaxStoredVisits));
  }
}

}  // namespace sessionat
