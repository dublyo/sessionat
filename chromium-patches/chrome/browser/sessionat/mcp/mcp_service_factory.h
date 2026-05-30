// Copyright 2026 Sessionat. All rights reserved.

#ifndef CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace sessionat {

class McpService;

class McpServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static McpService* GetForProfile(Profile* profile);
  static McpServiceFactory* GetInstance();

  McpServiceFactory(const McpServiceFactory&) = delete;
  McpServiceFactory& operator=(const McpServiceFactory&) = delete;

 private:
  friend base::NoDestructor<McpServiceFactory>;

  McpServiceFactory();
  ~McpServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_MCP_SERVICE_FACTORY_H_
