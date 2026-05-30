// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SESSIONAT_SESSION_HISTORY_SERVICE_FACTORY_H_
#define CHROME_BROWSER_SESSIONAT_SESSION_HISTORY_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace sessionat {

class SessionHistoryService;

class SessionHistoryServiceFactory : public ProfileKeyedServiceFactory {
 public:
  static SessionHistoryService* GetForProfile(Profile* profile);
  static SessionHistoryServiceFactory* GetInstance();

  SessionHistoryServiceFactory(const SessionHistoryServiceFactory&) = delete;
  SessionHistoryServiceFactory& operator=(const SessionHistoryServiceFactory&) =
      delete;

 private:
  friend base::NoDestructor<SessionHistoryServiceFactory>;

  SessionHistoryServiceFactory();
  ~SessionHistoryServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_SESSION_HISTORY_SERVICE_FACTORY_H_
