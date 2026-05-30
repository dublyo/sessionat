// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/mcp_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/mcp/mcp_service.h"
#include "chrome/browser/sessionat/visit_analytics_service_factory.h"
#include "chrome/browser/sessionat/workspace_service_factory.h"

namespace sessionat {

// static
McpService* McpServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<McpService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
McpServiceFactory* McpServiceFactory::GetInstance() {
  static base::NoDestructor<McpServiceFactory> instance;
  return instance.get();
}

McpServiceFactory::McpServiceFactory()
    : ProfileKeyedServiceFactory(
          "SessionatMcpService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .Build()) {
  DependsOn(WorkspaceServiceFactory::GetInstance());
  DependsOn(VisitAnalyticsServiceFactory::GetInstance());
}

McpServiceFactory::~McpServiceFactory() = default;

std::unique_ptr<KeyedService>
McpServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<McpService>(profile);
}

bool McpServiceFactory::ServiceIsCreatedWithBrowserContext() const {
  // Start the server eagerly so external clients can read mcp.json on launch.
  return true;
}

}  // namespace sessionat
