// Copyright 2024 Sessionat. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SESSIONAT_DEFAULT_EXTENSIONS_H_
#define CHROME_BROWSER_SESSIONAT_DEFAULT_EXTENSIONS_H_

#include <string>
#include <vector>

namespace sessionat {

// Sessionat extension from Chrome Web Store
// https://chromewebstore.google.com/detail/sessionat-ai-powered-tab/dmoljfchnfkphkmoipdfkgfhlchcoebo
constexpr char kSessionatExtensionId[] = "dmoljfchnfkphkmoipdfkgfhlchcoebo";

// Chrome Web Store update URL
constexpr char kWebStoreUpdateUrl[] =
    "https://clients2.google.com/service/update2/crx";

// Structure for pre-installed extensions
struct PreinstalledExtension {
  const char* id;
  const char* name;
  const char* update_url;
  bool force_installed;   // Cannot be disabled/uninstalled
  bool force_pinned;      // Pinned to toolbar
};

// List of extensions to pre-install with Sessionat browser
// Add new extensions here for future releases
constexpr PreinstalledExtension kPreinstalledExtensions[] = {
    // CORE: Sessionat - AI Powered Tab & Session Manager
    {
        kSessionatExtensionId,
        "Sessionat",
        kWebStoreUpdateUrl,
        true,   // force_installed - required, cannot be disabled
        true    // force_pinned - pinned to toolbar
    },

    // FUTURE: Add more extensions below
    // Example:
    // {
    //     "extension_id_here",
    //     "Extension Name",
    //     kWebStoreUpdateUrl,
    //     false,  // optional - can be disabled
    //     false   // not pinned
    // },
};

// Returns list of extension IDs for force-install
std::vector<std::string> GetForceInstalledExtensionIds();

// Returns list of extension IDs for initial install (first run)
std::vector<std::string> GetInitialExtensionIds();

// Returns the forcelist string format: "id;update_url"
std::vector<std::string> GetExtensionForcelistEntries();

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_DEFAULT_EXTENSIONS_H_
