// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/visit_analytics_tab_helper.h"

#include <algorithm>

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/visit_analytics_service.h"
#include "chrome/browser/sessionat/visit_analytics_service_factory.h"
#include "chrome/browser/sessionat/workspace_service.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"

namespace sessionat {

namespace {

bool IsWebUrlForTracking(const GURL& url) {
  return url.is_valid() && (url.SchemeIs("http") || url.SchemeIs("https"));
}

VisitAnalyticsService* ServiceFor(content::WebContents* wc) {
  if (!wc) return nullptr;
  Profile* profile = Profile::FromBrowserContext(wc->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) return nullptr;
  return VisitAnalyticsServiceFactory::GetForProfile(profile);
}

std::string ActiveWorkspaceIdFor(content::WebContents* wc) {
  if (!wc) return std::string();
  Profile* profile = Profile::FromBrowserContext(wc->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) return std::string();
  if (auto* ws_svc = WorkspaceServiceFactory::GetForProfile(profile)) {
    return ws_svc->GetActiveWorkspaceId();
  }
  return std::string();
}

}  // namespace

VisitAnalyticsTabHelper::VisitAnalyticsTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<VisitAnalyticsTabHelper>(*web_contents) {
  // If the tab is born visible (rare for background tabs, common for the
  // foreground tab), start the stopwatch.
  if (web_contents->GetVisibility() == content::Visibility::VISIBLE) {
    StartTrackingForCurrentUrl();
  }
}

VisitAnalyticsTabHelper::~VisitAnalyticsTabHelper() = default;

void VisitAnalyticsTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInPrimaryMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsErrorPage() ||
      navigation_handle->IsSameDocument()) {
    return;
  }

  content::WebContents* contents = web_contents();
  if (!contents) return;

  // Flush time for the previous URL before switching attribution.
  FlushAccumulatedTime();

  Profile* profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) {
    tracked_url_ = GURL();
    visible_since_ = base::TimeTicks();
    return;
  }
  VisitAnalyticsService* service =
      VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (service) {
    service->RecordVisit(navigation_handle->GetURL(),
                          base::UTF16ToUTF8(contents->GetTitle()),
                          ActiveWorkspaceIdFor(contents));
  }

  // Start the stopwatch for the new URL if the tab is currently visible.
  if (contents->GetVisibility() == content::Visibility::VISIBLE) {
    StartTrackingForCurrentUrl();
  } else {
    tracked_url_ = navigation_handle->GetURL();  // remember for later resume
    visible_since_ = base::TimeTicks();
  }
}

void VisitAnalyticsTabHelper::TitleWasSet(content::NavigationEntry*) {
  content::WebContents* contents = web_contents();
  if (!contents) return;
  Profile* profile =
      Profile::FromBrowserContext(contents->GetBrowserContext());
  if (!profile || profile->IsOffTheRecord()) return;
  VisitAnalyticsService* service =
      VisitAnalyticsServiceFactory::GetForProfile(profile);
  if (!service) return;
  service->RecordVisit(contents->GetLastCommittedURL(),
                       base::UTF16ToUTF8(contents->GetTitle()),
                       ActiveWorkspaceIdFor(contents));
}

void VisitAnalyticsTabHelper::OnVisibilityChanged(
    content::Visibility visibility) {
  if (visibility == content::Visibility::VISIBLE) {
    // Tab became foreground — start (or resume) the stopwatch.
    StartTrackingForCurrentUrl();
  } else {
    // Tab moved to background / occluded — flush the accumulated period.
    FlushAccumulatedTime();
    visible_since_ = base::TimeTicks();
  }
}

void VisitAnalyticsTabHelper::WebContentsDestroyed() {
  FlushAccumulatedTime();
  tracked_url_ = GURL();
  visible_since_ = base::TimeTicks();
}

void VisitAnalyticsTabHelper::StartTrackingForCurrentUrl() {
  content::WebContents* contents = web_contents();
  if (!contents) return;
  const GURL url = contents->GetLastCommittedURL();
  if (!IsWebUrlForTracking(url)) {
    tracked_url_ = GURL();
    visible_since_ = base::TimeTicks();
    return;
  }
  // If the URL changed without DidFinishNavigation already flushing, do so.
  if (!visible_since_.is_null() && url != tracked_url_) {
    FlushAccumulatedTime();
  }
  tracked_url_ = url;
  visible_since_ = base::TimeTicks::Now();
}

void VisitAnalyticsTabHelper::FlushAccumulatedTime() {
  // Accumulate the current visible period if we're tracking.
  if (!visible_since_.is_null() && IsWebUrlForTracking(tracked_url_)) {
    const auto delta = base::TimeTicks::Now() - visible_since_;
    int delta_ms = static_cast<int>(delta.InMilliseconds());
    // Cap a single contribution at 30 minutes — protects against a
    // visibility-change event that never fires (system sleep, etc.).
    delta_ms = std::clamp(delta_ms, 0, 30 * 60 * 1000);
    accumulated_ms_ += delta_ms;
    visible_since_ = base::TimeTicks::Now();  // reset the stopwatch base
  }
  // Write what we've got back to the service.
  if (accumulated_ms_ > 0 && IsWebUrlForTracking(tracked_url_)) {
    if (auto* service = ServiceFor(web_contents())) {
      service->RecordActiveTime(tracked_url_, accumulated_ms_);
    }
  }
  accumulated_ms_ = 0;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(VisitAnalyticsTabHelper);

}  // namespace sessionat
