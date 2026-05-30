// Copyright 2026 Sessionat. All rights reserved.
// Factory for VisitAnalyticsService.

#ifndef CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace sessionat {

class VisitAnalyticsService;

class VisitAnalyticsServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static VisitAnalyticsService* GetForProfile(Profile* profile);
  static VisitAnalyticsServiceFactory* GetInstance();

  VisitAnalyticsServiceFactory(const VisitAnalyticsServiceFactory&) = delete;
  VisitAnalyticsServiceFactory& operator=(const VisitAnalyticsServiceFactory&) =
      delete;

 private:
  friend base::NoDestructor<VisitAnalyticsServiceFactory>;

  VisitAnalyticsServiceFactory();
  ~VisitAnalyticsServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_VISIT_ANALYTICS_SERVICE_FACTORY_H_
