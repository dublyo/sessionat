// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sessionat/session_history_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessionat/session_history_service.h"

namespace sessionat {

// static
SessionHistoryService* SessionHistoryServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<SessionHistoryService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
SessionHistoryServiceFactory* SessionHistoryServiceFactory::GetInstance() {
  static base::NoDestructor<SessionHistoryServiceFactory> instance;
  return instance.get();
}

SessionHistoryServiceFactory::SessionHistoryServiceFactory()
    : ProfileKeyedServiceFactory(
          "SessionHistoryService",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .WithSystem(ProfileSelection::kNone)
              .WithAshInternals(ProfileSelection::kNone)
              .Build()) {}

SessionHistoryServiceFactory::~SessionHistoryServiceFactory() = default;

std::unique_ptr<KeyedService>
SessionHistoryServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<SessionHistoryService>(profile);
}

}  // namespace sessionat
