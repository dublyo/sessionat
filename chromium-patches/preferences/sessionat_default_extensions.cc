// Copyright 2024 Sessionat. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sessionat/sessionat_default_extensions.h"

#include <string>
#include <vector>

namespace sessionat {

std::vector<std::string> GetForceInstalledExtensionIds() {
  std::vector<std::string> ids;
  for (const auto& ext : kPreinstalledExtensions) {
    if (ext.force_installed) {
      ids.push_back(ext.id);
    }
  }
  return ids;
}

std::vector<std::string> GetInitialExtensionIds() {
  std::vector<std::string> ids;
  for (const auto& ext : kPreinstalledExtensions) {
    ids.push_back(ext.id);
  }
  return ids;
}

std::vector<std::string> GetExtensionForcelistEntries() {
  std::vector<std::string> entries;
  for (const auto& ext : kPreinstalledExtensions) {
    if (ext.force_installed) {
      // Format: "extension_id;update_url"
      std::string entry = std::string(ext.id) + ";" + ext.update_url;
      entries.push_back(entry);
    }
  }
  return entries;
}

}  // namespace sessionat
