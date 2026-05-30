// Copyright 2024 Sessionat. All rights reserved.
// Factory for WorkspaceService.

#ifndef CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace sessionat {

class WorkspaceService;

// Factory for creating WorkspaceService instances per profile.
class WorkspaceServiceFactory : public ProfileKeyedServiceFactory {
 public:
  // Returns the WorkspaceService for the given profile.
  static WorkspaceService* GetForProfile(Profile* profile);

  // Returns the singleton factory instance.
  static WorkspaceServiceFactory* GetInstance();

  WorkspaceServiceFactory(const WorkspaceServiceFactory&) = delete;
  WorkspaceServiceFactory& operator=(const WorkspaceServiceFactory&) = delete;

 private:
  friend base::NoDestructor<WorkspaceServiceFactory>;

  WorkspaceServiceFactory();
  ~WorkspaceServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_WORKSPACE_SERVICE_FACTORY_H_
