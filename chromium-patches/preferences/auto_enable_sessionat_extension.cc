// Copyright 2024 Sessionat. All rights reserved.
// Patch to auto-enable Sessionat extension without "Action Required" prompt
//
// This file should be integrated into:
// chrome/browser/extensions/external_install_manager.cc
//
// Add to the IsUnacknowledgedExternalExtension function to skip
// prompting for the Sessionat extension.

// Sessionat extension ID from Chrome Web Store
constexpr char kSessionatExtensionId[] = "dmoljfchnfkphkmoipdfkgfhlchcoebo";

// Add this check at the beginning of IsUnacknowledgedExternalExtension():
//
// bool ExternalInstallManager::IsUnacknowledgedExternalExtension(
//     const Extension& extension) const {
//   // Sessionat: Auto-acknowledge our bundled extension
//   if (extension.id() == kSessionatExtensionId) {
//     return false;  // Not unacknowledged = no prompt needed
//   }
//   // ... rest of original function
// }

// Alternative: Add to AddExternalInstallError() to skip error for Sessionat:
//
// void ExternalInstallManager::AddExternalInstallError(
//     const Extension* extension, bool is_new_profile) {
//   // Sessionat: Skip external install error for our extension
//   if (extension->id() == kSessionatExtensionId) {
//     // Auto-acknowledge and enable the extension
//     extension_prefs_->AcknowledgeExternalExtension(extension->id());
//     return;
//   }
//   // ... rest of original function
// }
