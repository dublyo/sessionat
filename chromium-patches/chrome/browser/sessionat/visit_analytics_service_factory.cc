// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/visit_analytics_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/visit_analytics_service.h"

namespace sessionat {

// static
VisitAnalyticsService* VisitAnalyticsServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<VisitAnalyticsService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
VisitAnalyticsServiceFactory* VisitAnalyticsServiceFactory::GetInstance() {
  static base::NoDestructor<VisitAnalyticsServiceFactory> instance;
  return instance.get();
}

VisitAnalyticsServiceFactory::VisitAnalyticsServiceFactory()
    : ProfileKeyedServiceFactory(
          "VisitAnalyticsService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {}

VisitAnalyticsServiceFactory::~VisitAnalyticsServiceFactory() = default;

std::unique_ptr<KeyedService>
VisitAnalyticsServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<VisitAnalyticsService>(profile);
}

bool VisitAnalyticsServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  // Eager creation so the prefs are registered and the load happens before
  // any TabHelper tries to record a visit.
  return true;
}

}  // namespace sessionat
