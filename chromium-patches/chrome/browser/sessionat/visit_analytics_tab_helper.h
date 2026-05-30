// Copyright 2026 Sessionat. All rights reserved.
// VisitAnalyticsTabHelper — per-WebContents observer that forwards primary-
// frame, success-status navigations to VisitAnalyticsService::RecordVisit.

#ifndef CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_TAB_HELPER_H_
#define CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_TAB_HELPER_H_

#include "base/time/time.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "url/gurl.h"

namespace sessionat {

class VisitAnalyticsTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<VisitAnalyticsTabHelper> {
 public:
  ~VisitAnalyticsTabHelper() override;

  VisitAnalyticsTabHelper(const VisitAnalyticsTabHelper&) = delete;
  VisitAnalyticsTabHelper& operator=(const VisitAnalyticsTabHelper&) = delete;

  // content::WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void TitleWasSet(content::NavigationEntry* entry) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<VisitAnalyticsTabHelper>;
  explicit VisitAnalyticsTabHelper(content::WebContents* web_contents);

  // Active-time accounting: a stopwatch that runs while the tab is visible
  // and the URL is a real web page. On every transition (navigation, hide,
  // destroy) we flush the accumulated time to the service for the current
  // tracked URL.
  void StartTrackingForCurrentUrl();
  void FlushAccumulatedTime();

  // The URL the active-time timer is currently attributed to.
  GURL tracked_url_;
  // When the visible period started. If null, the tab is hidden / no URL.
  base::TimeTicks visible_since_;
  // Accumulated ms across multiple visible-periods for tracked_url_.
  int accumulated_ms_ = 0;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_TAB_HELPER_H_
