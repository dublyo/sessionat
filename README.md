# Sessionat

**The open-source Mac browser that AI can actually use.**

Sessionat is a Chromium-based browser with Workspaces, auto-saved sessions, local visit analytics, and a built-in connection point so AI assistants (Claude, Cursor, anything that speaks the open MCP standard) can drive the browser for you — to reply on Reddit, post on X, buy from any store, write your Google Docs, find that article you read three weeks ago.

It's a **real** browser. Not headless, not a fake browser pretending to be Chrome. Websites can't tell the difference between your AI and you, because there is no difference.

> ⬇️ Just want to use the browser? **Download from [sessionat.com](https://sessionat.com)** — Mac universal binary, signed, notarized, auto-updating. Free forever.

> ⚠️ Want to build it from source? Read on, but be warned — building Chromium is a serious commitment. **Realistically about 1 in 10,000 people who want this browser will actually build it themselves.** Recommended specs: **32 GB RAM** (linking the framework needs ~28 GB), **200 GB free SSD**, **8+ CPU cores**. First full build takes 3–8 hours on a fast Mac. See [BUILDING.md](BUILDING.md) for the long version.

---

## What's in this repo

This is the **source patches** — every file Sessionat changes or adds on top of vanilla Chromium. Drop them into a fresh Chromium checkout and build.

```
sessionat-opensource/
├── README.md           # This file
├── LICENSE             # MIT
├── ARCHITECTURE.md     # Design rationale, feature specs, technical decisions
├── BUILDING.md         # Step-by-step build guide for Chromium forks
└── chromium-patches/   # 163 files. Mirror of the chromium/src tree.
    ├── chrome/         #   The bulk of Sessionat's code (browser process, UI, WebUI)
    │   ├── app/        #     App-level config: Info.plist, icons, branding, command IDs
    │   ├── browser/    #     The services that DO things:
    │   │   ├── sessionat/             # Our custom services (workspaces, sessions, analytics, MCP)
    │   │   ├── ui/                    # Browser-window UI changes (vertical sidebar, etc.)
    │   │   └── search/                # NTP routing changes
    │   └── common/     #     Shared constants (URLs, command IDs)
    ├── components/     #   Shared icons + resources
    ├── extensions/     #   Auto-install our extension on first run
    ├── preferences/    #   Default-extension wiring
    ├── tools/          #   Build-system grit registrations
    ├── ui/             #   UI-layer changes (color palette, WebUI resources)
    └── workspaces/     #   Workspaces feature module (kept separate for clarity)
```

Each file under `chromium-patches/` is intended to **replace** (or be added next to) the corresponding file in vanilla Chromium. The relative path within `chromium-patches/` is the relative path inside the Chromium source tree.

---

## What Sessionat actually does

### Workspaces (Arc-style vertical sidebar)
Multiple independent tab sets in one window. `Cmd+1` through `Cmd+9` switches workspaces instantly. Right-click any tab → Move to workspace. Comes with three starter workspaces (Work, Personal, Research) and a built-in NTP that shows your current workspace's session.

### Auto-saved sessions
Every 30 seconds + on quit, Sessionat saves a snapshot of every open tab in every workspace. Crash recovery is automatic. You can browse old snapshots like a journal — "what was I researching three Tuesdays ago?" Replaces Session Buddy, OneTab, Toby, Workona, Cluster, Tab Manager Plus, and every other tab-management extension.

### Visit Analytics — local-only
Every page visit is logged to a local SQLite database with the URL, title, host, timestamp, active-engagement seconds (only the time the tab was foregrounded AND you were active), and an auto-assigned category (Work / Social / News / Reference / Shopping / Entertainment / Finance / Email / Other). **No network code path exists in this subsystem.** Data lives at `~/Library/Application Support/Sessionat/visits.db` and that is the only place it exists.

Dashboard at `chrome://sessionat-analytics/` shows time-per-category, top sites, time-of-day patterns, per-workspace breakdowns.

### AI control via MCP (Model Context Protocol)
Sessionat runs a local MCP server (the open standard Anthropic + Cursor + the AI tooling industry have converged on) exposing a growing set of tools an AI client can call:

**Read (browser):** `get_active_tab`, `list_open_tabs`, `list_workspaces`, `get_active_workspace`, `get_visits`, `get_top_sites`, `get_category_breakdown`, `get_page_text`, `get_dom_outline`, `screenshot`, `list_frames`

**Read (analytics, v2.0.0):** `sessionat_search_visits`, `sessionat_get_visits_for_host`, `sessionat_get_visit_buckets`, `sessionat_get_top_sites` (with `order_by=visits|active_seconds`)

**Write:** `open_url`, `navigate_active_tab`, `focus_tab`, `close_tab`, `click`, `type`, `press_key`, `scroll`, `wait_for`, `sessionat_create_workspace`

Every event is a real OS-level keyboard/mouse event going through Chromium's regular input pipeline — there is no automation flag for the website to detect. Cloudflare, hCaptcha, Akamai cannot tell your AI is automating Sessionat from a human using it, because nothing about it is different.

**Multi-client one-click Connect (v2.0.0).** `chrome://sessionat-mcp/` is now a 7-tab dashboard — **Claude Desktop, Cursor, Codex, Claude Code, Windsurf, VS Code, Other** — with a one-click Connect button per client. Each client gets its own 64-hex bearer token (the master token only seeds the per-client registry), so revoking one client doesn't kick the others. The dashboard wires 8 IPC handlers under the hood: `connectClient`, `disconnectClient`, `getClientStatus`, `getAllClientStatuses`, `revealClientConfig`, `setClientWriteGrant`, `testConnection`, `rotateToken`.

Write tools (anything that mutates state) are off by default and gated behind **per-client write grants** — first use of a write tool returns MCP error `-32010` (`kErrWriteRequiresApproval`), Sessionat raises a one-time approval prompt, and only that client's grant is flipped on. Codex is special-cased: Sessionat detects the in-bundle CLI at `/Applications/Codex.app/Contents/Resources/codex` and writes the bearer token directly to `~/.codex/config.toml` (the codex CLI doesn't accept literal tokens on the command line).

### MIT-licensed, no telemetry, no account
Sessionat doesn't have a server we run. It doesn't phone home. It doesn't have a built-in analytics service that "anonymously" reports usage. The only network connection Sessionat makes that isn't initiated by you is the once-a-day check to `version.sessionat.com/appcast.xml` for auto-updates (a 4 KB XML file). That's it.

---

## Should you build this from source?

**Probably not.** Just download from [sessionat.com](https://sessionat.com) — it's the same code, pre-built, signed, notarized, auto-updating. Saves you 4-8 hours of build time and a ~120 GB Chromium checkout on your SSD.

You should build from source if:

- You want to **fork** Sessionat and add your own features
- You want to **audit** the actual binary you'd run on your machine
- You're a **browser engineer** who finds this fun
- You're on **Linux/Windows** and the official builds aren't out yet (mid-to-late 2026)
- You're building **Sessionat into a downstream product** (allowed by the MIT license — just keep the attribution)

If any of those describe you, read **[BUILDING.md](BUILDING.md)** for the full guide. The TL;DR:

```bash
# 1. Get Chromium's build tools
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PATH:$PWD/depot_tools"

# 2. Fetch Chromium M150 source (~30 GB download, ~80 GB on disk after sync)
mkdir chromium && cd chromium
fetch --no-history chromium
cd src && git checkout 150.0.7856.0
gclient sync --with_branch_heads

# 3. Drop Sessionat's patches on top
rsync -a /path/to/sessionat-opensource/chromium-patches/ ./

# 4. Generate build config and build
gn gen out/release --args='target_cpu="arm64" is_official_build=true is_component_build=false chrome_pgo_phase=0 enable_widevine=false generate_about_credits=false'
autoninja -C out/release chrome chrome/installer/mac
```

For universal (arm64 + x64) builds, build each architecture separately into `out/release_arm64` and `out/release_x64`, then `lipo` the Mach-O binaries together. **Important:** lipo every Mach-O in the bundle (the launcher, the framework, all helpers including the raw ones like `chrome_crashpad_handler`, and every `.dylib` in `Frameworks/.../Libraries/`) — missing any single one ships a "universal" binary that crashes on the other architecture. Also copy `v8_context_snapshot.x86_64.bin` from the x64 build into the universal stage (V8's snapshot file is arch-suffixed and not a Mach-O, so lipo doesn't merge it).

The official Sessionat build scripts handle all of this and live in the upstream private repo. They're shell scripts, not part of this open-source release — but the BUILDING.md guide covers the principles in detail.

---

## Architecture

For the design rationale (why workspaces, why local-only analytics, why MCP and not a custom protocol, why a Chromium fork instead of an Electron app, the KeyedService pattern for our backend, the NTP routing trick, etc.), read **[ARCHITECTURE.md](ARCHITECTURE.md)**.

---

## License

[MIT](LICENSE). You can read, fork, redistribute, modify, sell, do whatever — just include the license notice.

The vanilla Chromium code Sessionat builds on is governed by Google's BSD-style license, which is compatible with MIT distribution.

---

## Credits

Sessionat is built and maintained by **Ibrahim elsherbini** from Kuwait. The browser is dedicated to the people who keep their AI assistants running into "I can't help with that, the browser was blocked" walls.

The download, the changelog, and the waitlist for **Sessionat Anti-Detect** (the upcoming multi-profile / multi-identity browser for running fleets of AI agents in parallel) all live at **[sessionat.com](https://sessionat.com)**.

If you build something with Sessionat, please share it — open an issue, tag the project on Twitter, write a blog post. The MIT license means you don't have to, but it makes the maintainer's day.
