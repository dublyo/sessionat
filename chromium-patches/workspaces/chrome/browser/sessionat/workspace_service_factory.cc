// Copyright 2024 Sessionat. All rights reserved.
// WorkspaceServiceFactory implementation.

#include "chrome/browser/sessionat/workspace_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/workspace_service.h"

namespace sessionat {

// static
WorkspaceService* WorkspaceServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<WorkspaceService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
WorkspaceServiceFactory* WorkspaceServiceFactory::GetInstance() {
  static base::NoDestructor<WorkspaceServiceFactory> instance;
  return instance.get();
}

WorkspaceServiceFactory::WorkspaceServiceFactory()
    : ProfileKeyedServiceFactory(
          "WorkspaceService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {}

WorkspaceServiceFactory::~WorkspaceServiceFactory() = default;

std::unique_ptr<KeyedService>
WorkspaceServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<WorkspaceService>(profile);
}

}  // namespace sessionat
