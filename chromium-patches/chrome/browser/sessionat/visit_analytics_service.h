// Copyright 2026 Sessionat. All rights reserved.
// VisitAnalyticsService — local-only browsing visit recorder.
//
// PRIVACY GUARANTEE (see CLAUDE.md hard rule): this service NEVER makes a
// network call. All visit data stays in the user's profile prefs JSON.

#ifndef CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_SERVICE_H_
#define CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_SERVICE_H_

#include <map>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "components/keyed_service/core/keyed_service.h"
#include "url/gurl.h"

class PrefRegistrySimple;
class PrefService;
class Profile;

namespace sessionat {

struct Visit {
  GURL url;
  std::string host;          // Derived from url.host() for grouping.
  std::string title;
  std::string workspace_id;  // Empty if no active workspace.
  base::Time timestamp;      // When the visit was recorded.
  int active_ms = 0;         // Reserved for future ActiveTimeTracker.
};

class VisitAnalyticsService : public KeyedService {
 public:
  explicit VisitAnalyticsService(Profile* profile);
  ~VisitAnalyticsService() override;

  VisitAnalyticsService(const VisitAnalyticsService&) = delete;
  VisitAnalyticsService& operator=(const VisitAnalyticsService&) = delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Records a visit. Filters out chrome://, about:, view-source: and other
  // non-web schemes before storing. No-op if `url` is empty or invalid,
  // tracking is disabled, or the host is in the user's exclusion list.
  void RecordVisit(const GURL& url,
                   const std::string& title,
                   const std::string& workspace_id);

  // Adds `delta_ms` of active time to the most-recent visit matching `url`
  // within the last 24 hours. No-op if no matching visit is found. Called
  // from VisitAnalyticsTabHelper as the user spends time on a page.
  void RecordActiveTime(const GURL& url, int delta_ms);

  // ===== Privacy controls =====

  // Master toggle. Default true. When false, RecordVisit + RecordActiveTime
  // are no-ops. Existing data is preserved (use Clear() to wipe).
  bool IsTrackingEnabled() const;
  void SetTrackingEnabled(bool enabled);

  // Host exclusion list — pages on these hosts (or any subdomain when the
  // entry is a bare domain) are never recorded.
  std::vector<std::string> GetExcludedHosts() const;
  void SetExcludedHosts(std::vector<std::string> hosts);
  bool IsHostExcluded(const std::string& host) const;

  // Returns the full visit log as a portable JSON string for the
  // "Export my data" button.
  std::string ExportToJson() const;

  // Returns the category for a host, or "Other" if not in the categories
  // lookup. Categories come from categories.json baked into the binary.
  std::string GetCategoryForHost(const std::string& host) const;

  // Returns {category, total_active_ms, visit_count} aggregated for the
  // range, optionally filtered by workspace.
  struct CategoryStat {
    std::string category;
    int total_visits = 0;
    int total_active_ms = 0;
  };
  std::vector<CategoryStat> GetCategoryStatsInRange(
      base::Time start,
      base::Time end,
      const std::string& workspace_filter_id) const;

  // Per-host aggregation. One pass over visits_ yields both visit_count and
  // total_active_ms so the caller can rank by whichever metric.
  struct HostStat {
    std::string host;
    int visit_count = 0;
    int total_active_ms = 0;
  };
  std::vector<HostStat> GetHostStatsInRange(
      base::Time start,
      base::Time end,
      const std::string& workspace_filter_id) const;

  // Bucket mode for GetVisitBuckets. kHour aggregates by local hour-of-day
  // (24 slots), kDayOfWeek by weekday remapped to 0=Mon..6=Sun (7 slots),
  // kDay by calendar date (one slot per day in range).
  enum class BucketMode { kHour, kDayOfWeek, kDay };
  struct BucketStat {
    std::string key;
    int visit_count = 0;
    int total_active_ms = 0;
  };
  std::vector<BucketStat> GetVisitBuckets(
      base::Time start,
      base::Time end,
      BucketMode mode,
      const std::string& workspace_filter_id) const;

  // Returns all visits with timestamps in [start, end). Sorted newest first.
  std::vector<Visit> GetVisitsInRange(base::Time start, base::Time end) const;

  // Convenience: all visits whose date matches `local_day` (local-time
  // boundary).
  std::vector<Visit> GetVisitsForDay(base::Time local_day) const;

  // Returns {host, count} pairs for `local_day`, sorted by count desc.
  std::vector<std::pair<std::string, int>> GetHostCountsForDay(
      base::Time local_day) const;

  // Returns the most-recent visit per unique host on `local_day`, limited to
  // `n` entries, sorted by total count desc. Useful for the NTP "Top 5 today"
  // widget.
  std::vector<Visit> GetTopVisitsForDay(base::Time local_day, size_t n) const;

  // ===== Range queries (analytics dashboard) =====

  // Like the *ForDay variants but over any [start, end) range.
  std::vector<std::pair<std::string, int>> GetHostCountsInRange(
      base::Time start,
      base::Time end,
      const std::string& workspace_filter_id) const;

  // Top visits in range — one Visit per host (the most-recent rep), sorted
  // by count desc.
  std::vector<Visit> GetTopVisitsInRange(
      base::Time start,
      base::Time end,
      size_t n,
      const std::string& workspace_filter_id) const;

  // Counts per fixed-size bucket. `bucket_size` is the bucket width. Returns
  // a vector with one entry per bucket, in chronological order. The 0th entry
  // covers [start, start+bucket_size). Useful for the analytics bar chart:
  //   - Today: 24 hourly buckets
  //   - 7d: 7 daily buckets
  //   - 30d: 30 daily buckets
  std::vector<int> GetBucketCounts(
      base::Time start,
      base::Time end,
      base::TimeDelta bucket_size,
      const std::string& workspace_filter_id) const;

  // All visits within a host's range — used for the per-host expand-detail.
  // Sorted newest-first.
  std::vector<Visit> GetVisitsForHostInRange(
      const std::string& host,
      base::Time start,
      base::Time end,
      const std::string& workspace_filter_id) const;

  // Case-insensitive substring search over url.spec() OR title within
  // [start, end). workspace_filter_id="" disables workspace filter. Returns
  // at most |limit| visits, newest-first.
  std::vector<Visit> SearchVisitsInRange(
      const std::string& query,
      base::Time start,
      base::Time end,
      size_t limit,
      const std::string& workspace_filter_id) const;

  // Wipes all stored visits. Intended for the upcoming "Privacy → Wipe visit
  // history" Settings entry.
  void Clear();

  // Test/debug accessor — total entries currently in memory.
  size_t total_visits_for_test() const { return visits_.size(); }

 private:
  void Load();
  void Save();
  // Drops visits older than retention_days. Called from RecordVisit.
  void TrimOldVisits();
  // Lazy-loads categories.json on first lookup. Idempotent.
  void EnsureCategoriesLoaded() const;

  // DanglingUntriaged: Profile and PrefService are torn down before this
  // KeyedService's destructor by the DependencyManager. We never deref these
  // pointers in the destructor itself.
  raw_ptr<Profile, DanglingUntriaged> profile_;
  raw_ptr<PrefService, DanglingUntriaged> prefs_;
  std::vector<Visit> visits_;
  // Mutable so const methods (GetCategoryForHost / GetCategoryStatsInRange)
  // can lazy-load the table.
  mutable std::map<std::string, std::string> host_to_category_;
  mutable bool categories_loaded_ = false;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_SERVICE_H_
