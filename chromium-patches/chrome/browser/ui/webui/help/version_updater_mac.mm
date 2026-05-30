// Copyright 2026 Sessionat. All rights reserved.
// Originally based on the Chromium Mac VersionUpdater (Omaha path).
//
// Sessionat replaces Chromium's Omaha-based auto-update with Sparkle 2,
// because we ship as a notarized DMG via Apple Developer ID, not via
// Google's Omaha infrastructure. The `chrome://settings/help` WebUI calls
// `VersionUpdater::CheckForUpdate`, which we route into a
// `SPUStandardUpdaterController`. Sparkle's UI takes over from there.
//
// See sessionat-update-plan.md for the full design + audit.

#include "chrome/browser/ui/webui/help/version_updater.h"

#import <Foundation/Foundation.h>
#import <Sparkle/Sparkle.h>

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "content/public/browser/web_contents.h"

namespace {
class VersionUpdaterMac;
}  // namespace

// Obj-C bridge that translates Sparkle's SPUUpdaterDelegate callbacks into
// calls on the C++ VersionUpdaterMac. Sparkle holds its delegate as `weak`,
// so we keep this object alive via a __strong ivar on VersionUpdaterMac.
@interface SessionatSparkleBridge : NSObject <SPUUpdaterDelegate>
- (instancetype)initWithOwner:(base::WeakPtr<VersionUpdaterMac>)owner;
@end

namespace {

class VersionUpdaterMac : public VersionUpdater {
 public:
  VersionUpdaterMac();
  VersionUpdaterMac(const VersionUpdaterMac&) = delete;
  VersionUpdaterMac& operator=(const VersionUpdaterMac&) = delete;
  ~VersionUpdaterMac() override;

  // VersionUpdater:
  void CheckForUpdate(StatusCallback status_callback,
                      PromoteCallback promote_callback) override;
  void PromoteUpdater() override;

  // Called from SessionatSparkleBridge on the main thread (the same thread
  // CheckForUpdate runs on, so no PostTask needed).
  void OnSparkleStatus(Status status,
                       int progress,
                       const std::string& version,
                       const std::u16string& message);

 private:
  void RunStatus(Status status,
                 int progress,
                 const std::string& version,
                 const std::u16string& message);

  StatusCallback status_callback_;
  __strong SPUStandardUpdaterController* updater_controller_;
  __strong SessionatSparkleBridge* sparkle_bridge_;
  base::WeakPtrFactory<VersionUpdaterMac> weak_factory_{this};
};

VersionUpdaterMac::VersionUpdaterMac() {
  sparkle_bridge_ = [[SessionatSparkleBridge alloc]
      initWithOwner:weak_factory_.GetWeakPtr()];
  updater_controller_ = [[SPUStandardUpdaterController alloc]
      initWithStartingUpdater:YES
              updaterDelegate:sparkle_bridge_
           userDriverDelegate:nil];
}

VersionUpdaterMac::~VersionUpdaterMac() = default;

void VersionUpdaterMac::CheckForUpdate(StatusCallback status_callback,
                                        PromoteCallback /*promote_callback*/) {
  status_callback_ = std::move(status_callback);

  // Report CHECKING immediately; Sparkle's check is async, with further
  // status flowing back through SessionatSparkleBridge.
  RunStatus(VersionUpdater::Status::CHECKING, 0, "", std::u16string());

  // `checkForUpdates:` takes `id sender`; `nil` is valid for programmatic
  // invocation (the only consumer is this controller itself).
  [updater_controller_ checkForUpdates:nil];
}

void VersionUpdaterMac::PromoteUpdater() {
  // No-op. Sparkle handles per-user installs in-place; there is no separate
  // "system-wide updater" to promote on macOS.
}

void VersionUpdaterMac::OnSparkleStatus(Status status,
                                         int progress,
                                         const std::string& version,
                                         const std::u16string& message) {
  RunStatus(status, progress, version, message);
}

void VersionUpdaterMac::RunStatus(Status status,
                                   int progress,
                                   const std::string& version,
                                   const std::u16string& message) {
  if (status_callback_) {
    status_callback_.Run(status, progress, /*rollback=*/false,
                          /*powerwash=*/false, version, /*size=*/0, message);
  }
}

}  // namespace

@implementation SessionatSparkleBridge {
  base::WeakPtr<VersionUpdaterMac> _owner;
}

- (instancetype)initWithOwner:(base::WeakPtr<VersionUpdaterMac>)owner {
  if ((self = [super init])) {
    _owner = owner;
  }
  return self;
}

#pragma mark - SPUUpdaterDelegate

- (void)updater:(SPUUpdater*)updater
    didFindValidUpdate:(SUAppcastItem*)item {
  VersionUpdaterMac* owner = _owner.get();
  if (!owner) return;
  std::string version = base::SysNSStringToUTF8(item.displayVersionString);
  owner->OnSparkleStatus(VersionUpdater::Status::UPDATING, 0, version,
                          std::u16string());
}

- (void)updaterDidNotFindUpdate:(SPUUpdater*)updater {
  if (VersionUpdaterMac* owner = _owner.get()) {
    owner->OnSparkleStatus(VersionUpdater::Status::UPDATED, 0, "",
                            std::u16string());
  }
}

- (void)updater:(SPUUpdater*)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck
                                 error:(NSError* _Nullable)error {
  VersionUpdaterMac* owner = _owner.get();
  if (!owner) return;
  if (error) {
    // v2.0: collapse all error subtypes into one FAILED status.
    // TODO(sessionat v2.1): map specific NSError codes to FAILED_OFFLINE /
    // FAILED_HTTP / FAILED_DOWNLOAD for nicer messaging in the WebUI.
    std::u16string msg = base::SysNSStringToUTF16(error.localizedDescription);
    owner->OnSparkleStatus(VersionUpdater::Status::FAILED, 0, "", msg);
    return;
  }
  // No error AND a valid update was previously announced → it's downloaded
  // and ready to apply on relaunch. NEARLY_UPDATED is Chromium's "relaunch
  // to finish update" state.
  owner->OnSparkleStatus(VersionUpdater::Status::NEARLY_UPDATED, 0, "",
                          std::u16string());
}

@end

std::unique_ptr<VersionUpdater> VersionUpdater::Create(
    content::WebContents* /*web_contents*/) {
  return base::WrapUnique(new VersionUpdaterMac());
}
