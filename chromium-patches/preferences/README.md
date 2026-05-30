# Sessionat Browser - Pre-installed Extensions

This directory contains configuration files for auto-installing extensions in the Sessionat browser.

## Extension: Sessionat

| Property | Value |
|----------|-------|
| **Name** | Sessionat - AI Powered Tab & Session Manager |
| **Extension ID** | `dmoljfchnfkphkmoipdfkgfhlchcoebo` |
| **Web Store URL** | https://chromewebstore.google.com/detail/sessionat-ai-powered-tab/dmoljfchnfkphkmoipdfkgfhlchcoebo |
| **Installation** | Force-installed (cannot be disabled) |
| **Toolbar** | Force-pinned |

## Files

### initial_preferences
JSON configuration file for first-run extension installation.

**Location in built app (macOS):**
```
Sessionat.app/Contents/Resources/initial_preferences
```

**Alternative location:**
```
/Users/[username]/Library/Application Support/Sessionat/initial_preferences
```

### sessionat_policies.json
Reference file showing the policy configuration for ExtensionSettings.

### sessionat_default_extensions.h / .cc
C++ implementation for programmatic extension management.

**Location in Chromium source:**
```
chromium/src/chrome/browser/sessionat/sessionat_default_extensions.h
chromium/src/chrome/browser/sessionat/sessionat_default_extensions.cc
```

## Installation Methods

### Method 1: initial_preferences (First-Run Only)

1. Copy `initial_preferences` to the app bundle:
```bash
cp initial_preferences /path/to/Sessionat.app/Contents/Resources/
```

2. On first launch, the extension will be installed from Chrome Web Store.

**Note:** Users can disable/uninstall extensions installed this way.

### Method 2: Policy-Based Force Install (Recommended)

For extensions that CANNOT be disabled:

1. Add the C++ files to the Chromium source:
```
chrome/browser/sessionat/sessionat_default_extensions.h
chrome/browser/sessionat/sessionat_default_extensions.cc
```

2. Modify `chrome/browser/extensions/external_policy_loader.cc` to include Sessionat defaults.

3. The extension will be force-installed and cannot be disabled.

### Method 3: macOS Managed Preferences

Create a plist file for managed policies:

**File:** `/Library/Managed Preferences/com.anthropic.sessionat.plist`

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>ExtensionInstallForcelist</key>
    <array>
        <string>dmoljfchnfkphkmoipdfkgfhlchcoebo;https://clients2.google.com/service/update2/crx</string>
    </array>
</dict>
</plist>
```

## Quick Setup (macOS)

### TESTED AND WORKING!

On macOS, initial preferences are loaded from:
```
~/Library/Application Support/Chromium/Chromium Initial Preferences
```

**Setup:**
```bash
# Create the directory if it doesn't exist
mkdir -p ~/Library/Application\ Support/Chromium

# Copy initial_preferences to the correct location
cp /Users/dribrahimm/0-Chrome-Custom-Browser/sessionat-browser/chromium-patches/preferences/initial_preferences \
   ~/Library/Application\ Support/Chromium/"Chromium Initial Preferences"

# Launch browser with a fresh profile to trigger first-run
/path/to/Sessionat.app/Contents/MacOS/Sessionat --user-data-dir=/tmp/fresh-profile
```

**Verified Result:**
```
Extension installed: Sessionat - AI-Powered Tab Manager
Version: 4.1.0
Location: /tmp/fresh-profile/Default/Extensions/dmoljfchnfkphkmoipdfkgfhlchcoebo/
```

### Alternative Locations (checked in order)

1. `~/Library/Application Support/Chromium/Chromium Initial Preferences` (preferred)
2. `~/Library/Application Support/Chromium/Chromium Master Preferences` (legacy)
3. `/Library/Application Support/Chromium/Chromium Initial Preferences` (system-wide)
4. `/Library/Application Support/Chromium/Chromium Master Preferences` (system-wide legacy)

## Adding More Extensions

To add more pre-installed extensions:

1. Get the extension ID from Chrome Web Store URL
2. Add to `initial_preferences`:
```json
{
  "initial_extensions": {
    "list": [
      "dmoljfchnfkphkmoipdfkgfhlchcoebo",
      "new_extension_id_here"
    ]
  }
}
```

3. Or add to `sessionat_default_extensions.h`:
```cpp
constexpr PreinstalledExtension kPreinstalledExtensions[] = {
    // Sessionat
    { kSessionatExtensionId, "Sessionat", kWebStoreUpdateUrl, true, true },
    // New extension
    { "new_extension_id", "Extension Name", kWebStoreUpdateUrl, false, false },
};
```

## Troubleshooting

### Extension not installing
1. Delete user data to trigger first-run: `rm -rf ~/Library/Application\ Support/Sessionat`
2. Ensure `initial_preferences` is in correct location
3. Check Chrome Web Store is accessible

### Extension installed but not pinned
Add `toolbar_pin` to ExtensionSettings policy or use Method 2.

### Extension can be disabled
Use Method 2 (policy-based) for force-install that cannot be disabled.
