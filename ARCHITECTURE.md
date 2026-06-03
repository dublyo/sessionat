# Sessionat Browser v2 — Redevelopment Plan & Specification

| | |
|---|---|
| **Owner** | Sessionat (ibrahim@sessionat.com) |
| **v1 shipped** | 2025-12-25 (macOS only, Chromium 131.0.6778.139) |
| **v2 kickoff** | 2026-05-24 |
| **v2 shipped** | 2026-05-29 (macOS universal arm64+x64, Chromium 150.0.7856.0) |
| **Download** | [sessionat.com](https://sessionat.com) |
| **License** | MIT (free forever) |

---

## 0. Positioning

**Sessionat is a Chromium-based browser built around three co-equal pillars:**

1. **Workspaces** — Arc-style vertical sidebar; switch context with one click, never lose a tab.
2. **Visit Analytics** — Local, private time-tracking dashboard per workspace. See where your hours actually go.
3. **MCP Server** — One-click connect your browser to Claude Desktop / ChatGPT / Cursor. The first browser AI assistants can fully drive.

Targeted at **all three personas equally**: tab-hoarders, researchers/writers, and AI-native developers. No persona is favored — homepage and onboarding emphasize all three pillars co-equally.

---

## 1. Goals

1. ~~Rebase onto current Chromium stable (was M131)~~ — **shipped on Chromium M150 (150.0.7856.0)**
2. ~~Ship signed, notarized universal macOS DMG~~ — **shipped 2026-05-29, arm64+x64, auto-update via Sparkle**
3. ~~v2.0 launches all three pillars on macOS~~ — **shipped: Workspaces, Visit Analytics, MCP Server all in 2.0.0**
4. v2.1 = Linux (.deb/.rpm/AppImage), v2.2 = Windows MSI — in progress
5. Stay open-source under MIT; revenue via Sessionat Anti-Detect (separate paid product, in private beta)

---

## 2. Reality check — Mac App Store is NOT a target

> **Sessionat cannot ship through the Mac App Store. The plan targets notarized DMG instead.**

Why MAS is impossible for a Chromium fork:

- **Sandboxing.** MAS requires Apple App Sandbox. Chromium's multi-process model spawns sandboxed renderer/GPU/utility helpers with custom seatbelt profiles that conflict with the MAS-permitted sandbox profile and parent-child IPC model.
- **Auto-update.** MAS forbids apps that update outside the App Store. Chromium-based browsers ship security patches between full releases — non-negotiable.
- **Runtime code loading.** MAS forbids downloading/executing arbitrary code. Chromium's WebUI + extension model relies on it.
- **Empirical proof.** Chrome, Firefox, Edge, Brave, Vivaldi, Arc, Opera — **none** are on MAS. Only WebKit-based browsers pass review.

**The realistic path** (already documented in [publish.md](publish.md)):

1. Apple Developer ID Application certificate ($99/yr — same membership you'd use for MAS)
2. `codesign` with hardened runtime + entitlements
3. `xcrun notarytool` — Apple's malware scan (required for macOS 10.15+ to launch without warnings)
4. `xcrun stapler staple` — attach notarization ticket
5. Distribute the resulting `.dmg` from `downloads.sessionat.com`

If discoverability is the real concern: Setapp accepts non-sandboxed apps; Homebrew Cask is the de-facto Mac dev channel; strong SEO + a comparison page vs Chrome/Arc/Brave is the real lever.

---

## 3. Distribution strategy

| Platform | Format | Signing | Auto-update | Hosting | Target |
|---|---|---|---|---|---|
| macOS (universal) | Notarized DMG | Apple Developer ID + notarytool | Sparkle 2 | `downloads.sessionat.com` → Cloudflare R2 | **v2.0** |
| Windows 10/11 (x64) | MSI + NSIS .exe | EV Code Signing cert | Omaha or Squirrel.Windows | `downloads.sessionat.com` | **v2.1** |
| Linux (x64) | .deb + .rpm + AppImage | GPG-signed repos | apt/dnf + AppImageUpdate | `apt.sessionat.com` + `downloads.sessionat.com` | **v2.2** |

All hosting fronted by Cloudflare. Version metadata served from existing [sessionat-version/](sessionat-version/) Worker at `version.sessionat.com`.

---

## 4. Core features — the three pillars

All three ship together in **v2.0**. Each pillar gets its own engineering surface but they integrate (workspaces segment analytics; analytics feed MCP; MCP can switch workspaces).

---

### 4.1 Pillar 1 — Workspaces (Arc-style vertical sidebar)

**This is the headline UX change vs v1.** v1 had a tab-strip dropdown for workspaces; v2 has a full vertical sidebar that replaces the top horizontal tab strip as the primary tab UI.

#### 4.1.1 Layout

```
┌────────────────────────────────────────────────────────────────┐
│  [<] [>] [⟳]   [address bar........................]   [⋯]    │  ← slim top bar (no tabs)
├──────────┬─────────────────────────────────────────────────────┤
│ ▼ Work   │                                                     │
│  ▸ Mail  │                                                     │
│  ▸ Slack │                                                     │
│  ▸ ...   │                                                     │
│ ▸ Personal│                  Active page                      │
│ ▸ Research│                                                    │
│ + Add ws │                                                     │
│          │                                                     │
│ ─────────│                                                     │
│ Today: ⏱ │   ← sidebar footer: today's focus score             │
│ 3h 12m   │                                                     │
└──────────┴─────────────────────────────────────────────────────┘
```

- Left sidebar (~220px, collapsible) lists workspaces; each workspace expands to its tab list inline (vertical).
- Top horizontal tab strip is **hidden by default** (toggleable in Settings for users who want classic Chrome layout).
- Address bar is in a slim top toolbar.
- Sidebar footer shows today's active time across all workspaces (link to dashboard).

#### 4.1.2 Engineering surface (heavy)

Heavy native UI work in:
- `chrome/browser/ui/views/frame/browser_view.cc` — relocate `TabStripRegionView` to a new left-side container, hide top tab strip
- `chrome/browser/ui/views/tabs/` — add `VerticalTabStrip` view; preserve existing `TabStripModel` (data model unchanged)
- `chrome/browser/ui/views/frame/` — new `SessionatSidebarView` hosting workspace switcher + vertical tab list
- Existing v1 [workspaces/](sessionat-browser/chromium-patches/workspaces/) patches stay but the UI consumes a sidebar instead of a tab-strip button

#### 4.1.3 Isolation semantics (per Q3 answer)

Workspaces isolate **tabs + visit analytics** only:

| Shared across workspaces | Isolated per workspace |
|---|---|
| Cookies, logins, localStorage | Open tabs (and pinned tabs) |
| Extensions | Visit analytics rollups |
| Bookmarks | Recently closed sessions |
| Browser history (raw URLs) | Saved sessions |
| Passwords, autofill | Last-active state restore |

Rationale: full cookie isolation is Firefox Containers' job and adds significant UX cost (re-logging-in everywhere). Most users want "two contexts, same Google account" — tabs + analytics segmentation gives them that.

#### 4.1.4 Workspace data model

```sql
-- in chrome/browser/sessionat/workspaces_database
CREATE TABLE workspaces (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  icon TEXT,                    -- emoji or material icon name
  color TEXT,                   -- hex
  position INTEGER NOT NULL,    -- sort order
  created_at INTEGER NOT NULL,
  archived INTEGER DEFAULT 0
);

CREATE TABLE workspace_tabs (
  workspace_id INTEGER NOT NULL,
  tab_id INTEGER NOT NULL,      -- TabStripModel handle, ephemeral
  url TEXT NOT NULL,            -- snapshot for restore-on-launch
  title TEXT,
  position INTEGER NOT NULL,
  pinned INTEGER DEFAULT 0,
  last_active INTEGER,          -- unix ms
  PRIMARY KEY (workspace_id, tab_id)
);
```

Tabs persist across browser restarts per workspace — when you re-launch Sessionat, every workspace's tab set is restored exactly.

#### 4.1.5 First-launch defaults

On first run, three starter workspaces are created: **Work**, **Personal**, **Research**. User can rename/delete/add. Each comes with a tip card on the New Tab Page explaining the pillar.

#### 4.1.6 Keyboard shortcuts

- `Cmd+1` … `Cmd+9` — switch to workspace N
- `Cmd+Shift+]` / `Cmd+Shift+[` — next / previous workspace
- `Cmd+Shift+M` — move current tab to another workspace (opens picker)
- `Cmd+Shift+N` — new workspace

---

### 4.2 Pillar 2 — Visit Analytics + Productivity Dashboard

**Goal:** Record every site visited and active time, locally. Surface in a RescueTime-style productivity dashboard with auto-categorization, daily focus scores, and per-workspace breakdowns.

#### 4.2.1 What's tracked (per Q4)

For every visit: **full URL + page title + active time (ms) + timestamps + workspace_id**.

Not tracked: page content text, screenshots, scroll position, keystrokes. (Lighter DB, lower invasiveness.)

#### 4.2.2 Schema

```sql
-- chrome/browser/sessionat/visit_analytics/visit_analytics_database
CREATE TABLE visits (
  id INTEGER PRIMARY KEY,
  workspace_id INTEGER,                -- NULL for tabs not in any workspace
  url TEXT NOT NULL,
  origin TEXT NOT NULL,
  title TEXT,
  visit_start INTEGER NOT NULL,        -- unix ms
  visit_end INTEGER,                   -- unix ms; NULL = still active
  active_time_ms INTEGER DEFAULT 0,    -- foreground+focused only
  category TEXT,                       -- resolved from categories.json
  tab_id INTEGER,
  profile TEXT
);
CREATE INDEX idx_visits_origin ON visits(origin);
CREATE INDEX idx_visits_start ON visits(visit_start);
CREATE INDEX idx_visits_workspace ON visits(workspace_id);

CREATE TABLE daily_rollup (
  date TEXT NOT NULL,                  -- YYYY-MM-DD local
  workspace_id INTEGER,
  origin TEXT NOT NULL,
  category TEXT,
  total_active_ms INTEGER NOT NULL,
  visit_count INTEGER NOT NULL,
  PRIMARY KEY (date, workspace_id, origin)
);
```

Rollups updated incrementally on `visit_end`; full reconcile nightly via `base::ThreadPool`.

#### 4.2.3 Active-time measurement

A visit's `active_time_ms` only accrues when **all three** are true:
1. Tab is the foreground tab in its window
2. Window has OS focus
3. User has produced an input event (mouse / key / scroll) in the last 60 seconds

This matches RescueTime's "active" definition. Idle time (locked screen, AFK) is excluded. Wire up via `SiteEngagementService::Observer` + `PageLoadMetricsObserver` + a new `ActiveTimeTracker` listening to `ui::UserActivityDetector`.

#### 4.2.4 Auto-categorization (per Q8)

Ship a curated `categories.json` (~5,000 popular domains) with the browser:

```json
{
  "version": "2026.05",
  "categories": [
    {"id": "dev", "name": "Development", "color": "#10b981"},
    {"id": "social", "name": "Social", "color": "#f59e0b"},
    {"id": "news", "name": "News", "color": "#6366f1"},
    {"id": "work", "name": "Work", "color": "#ec4899"},
    {"id": "entertainment", "name": "Entertainment", "color": "#ef4444"},
    {"id": "shopping", "name": "Shopping", "color": "#14b8a6"},
    {"id": "education", "name": "Education", "color": "#8b5cf6"},
    {"id": "reference", "name": "Reference", "color": "#0ea5e9"}
  ],
  "domains": {
    "github.com": "dev",
    "stackoverflow.com": "dev",
    "twitter.com": "social",
    "x.com": "social",
    "youtube.com": "entertainment",
    "...": "..."
  }
}
```

- Shipped baked-in to the browser at build time
- Background fetch from the version Worker (`version.sessionat.com/categories.json`) checks weekly for an updated list
- Unknown domains default to `"uncategorized"` and surface in the dashboard for the user to optionally label (writes to a local override table)
- **No ML, no LLM dependency.** Predictable, fast, free.

#### 4.2.5 Dashboard UX — `chrome://sessionat-analytics/`

Sections (RescueTime-inspired):

1. **Today** — Donut chart of time-by-category, top 5 sites bar list, focus score (0-100 based on Work+Dev+Reference categories vs Social+Entertainment).
2. **This week** — 7-day stacked bar, week-over-week delta, "most productive day" callout.
3. **By workspace** — switcher to see metrics for Work / Personal / Research / etc. independently.
4. **Goals** — user sets daily/weekly time targets per category (e.g., "≤1 hour Social"); progress rings with green/red.
5. **Timeline** — chronological visit log (filterable by date / workspace / origin / category), CSV/JSON export.

Plus a tiny **NTP widget** ("Top 5 today") under existing session cards, links to the dashboard.

#### 4.2.6 Default state & privacy (per Q9)

**Tracking is ON by default with a prominent first-run notice:**

> **Sessionat tracks time per site locally** to power your dashboard. Nothing leaves your device. Turn off anytime in Settings → Privacy.
> [ Got it ]   [ Settings ]

Settings → Privacy → Visit Tracking exposes:
- Master toggle (on/off)
- Excluded sites list (URL patterns; tracking skipped for matched URLs)
- "Wipe all visit data" destructive button (confirms twice)
- "Export all visit data" button (JSON download)

Hardcoded rules:
- Incognito profiles never tracked (`IsOffTheRecord()` check at service entry)
- `localhost`, `127.*`, `chrome://`, `chrome-extension://`, `about:` never tracked
- No network egress from `visit_analytics_service` (CI lint enforces)

---

### 4.3 Pillar 3 — MCP Server (full-control surface)

**Goal:** Expose browser data and actions to external AI assistants (Claude Desktop, Cursor, Codex, Claude Code, Windsurf, VS Code, plus an "Other" tab for hand-config) via MCP. **One-click auto-install** for detected clients.

v2.0.0 shipped this as a **multi-client** surface, not a single Claude integration: every connected client gets its own bearer token, its own write-grant set, and its own state-machine row in `chrome://sessionat-mcp/`. The master token in the system keychain only **seeds** per-client tokens — it never travels into a client config.

#### 4.3.1 Architecture — sidecar, not embedded

```
┌──────────────────────────────────────────────────────────────────┐
│                        Sessionat Browser                          │
│                                                                   │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────┐    │
│  │ Visit Analytics  │  │ Session History  │  │  Workspaces  │    │
│  │     Service      │  │     Service      │  │   Service    │    │
│  └────────┬─────────┘  └────────┬─────────┘  └──────┬───────┘    │
│           └─────────────────────┴─────────────────────┘          │
│                                 │                                 │
│                ┌────────────────▼────────────────┐                │
│                │   Sessionat IPC Bridge (Mojo)   │                │
│                │   Unix socket / named pipe      │                │
│                │   + 256-bit token (Keychain)    │                │
│                └────────────────┬────────────────┘                │
└─────────────────────────────────┼────────────────────────────────┘
                                  │ local IPC (token-auth'd)
                                  ▼
                  ┌────────────────────────────────────┐
                  │   sessionat-mcp (sidecar binary)   │
                  │   TypeScript + @mcp/sdk            │
                  │   bundled via `bun build --compile`│
                  └────────────────┬───────────────────┘
                                   │ stdio (MCP 2025-03-26)
                                   ▼
                  ┌────────────────────────────────────────────────────────┐
                  │ Claude Desktop · Cursor · Codex · Claude Code ·        │
                  │ Windsurf · VS Code · Other                             │
                  │   (each holds its own per-client bearer token)         │
                  └────────────────────────────────────────────────────────┘
```

**Why sidecar, not embedded:** A flaw in MCP-handling code running inside the browser process would have full browser-process privilege — access to every tab's cookies, passwords, etc. Running the MCP server as a separate process means a compromise is bounded by the IPC contract instead.

**Why per-client tokens, not one shared token:** if a single client config file leaks, only that client's token rotates — every other client keeps working. The bridge tags every inbound MCP call with the originating `client_id` so write-grant decisions and audit logs are per-client.

#### 4.3.2 cpp-mcp safety check (user requested)

[hkr04/cpp-mcp](https://github.com/hkr04/cpp-mcp) verdict: usable but not the default.

| Signal | Result |
|---|---|
| License | MIT |
| Stars | 283 — moderate, not battle-tested |
| Commits | ~37 — early-stage |
| Spec conformance | MCP 2025-03-26 (current) |
| Transports | stdio + SSE + HTTP |
| Known CVEs | None found |
| Maintenance | One primary maintainer (bus factor risk) |

**Decision: use the official TypeScript SDK (`@modelcontextprotocol/sdk`)** for the sidecar. Mature, larger community, faster bugfix cycle. Bundle with `bun build --compile` → ~10MB single binary per platform. Reserve cpp-mcp for future in-process C++ MCP listener if needed (not v2.0).

#### 4.3.3 Tool surface — full control (per Q6)

**Read tools** (no confirmation):

| Tool | Inputs | Returns |
|---|---|---|
| `list_open_tabs` | `window_id?, workspace_id?` | `[{id, url, title, active, pinned, workspace_id}]` |
| `list_workspaces` | — | `[{id, name, icon, color, tab_count, active_time_today_ms}]` |
| `list_sessions` | `kind: recent\|saved` | array |
| `get_history` | `query?, start?, end?, workspace_id?, limit?` | array of visits |
| `get_top_sites` | `period: today\|7d\|30d\|all, workspace_id?, by: time\|visits, limit?` | aggregated |
| `get_visit_timeline` | `date: YYYY-MM-DD, workspace_id?` | hour-by-hour activity |
| `get_focus_score` | `period: today\|7d` | numeric + category breakdown |
| `search_bookmarks` | `query` | array |
| `get_page_content` | `tab_id, format: text\|markdown\|html` | Readability-extracted content |
| `sessionat_search_visits` | `query?, start?, end?, workspace_id?, limit?` | full-text + filter over `visits` rows |
| `sessionat_get_visits_for_host` | `host, start?, end?, workspace_id?, limit?` | every visit row for an origin |
| `sessionat_get_visit_buckets` | `bucket: hour\|day\|week, start?, end?, workspace_id?` | time-bucketed active-time series |
| `sessionat_get_top_sites` | `period, workspace_id?, order_by: visits\|active_seconds, limit?` | top sites; `order_by` flips between visit count vs accumulated active seconds |

**Write tools** (each requires user confirmation on first use per client):

| Tool | Inputs | Effect |
|---|---|---|
| `open_url` | `url, workspace_id?, background?` | new tab in given (or current) workspace |
| `close_tab` | `tab_id` | close tab |
| `switch_tab` | `tab_id` | focus tab |
| `move_tab_to_workspace` | `tab_id, workspace_id` | move tab between workspaces |
| `switch_workspace` | `workspace_id` | activate workspace |
| `create_workspace` | `name, icon?, color?` | new workspace |
| `sessionat_create_workspace` | `name, icon?, color?` | namespaced alias surfaced under the v2.0.0 multi-client tool list; same effect as `create_workspace`, kept distinct so client UIs that group by `sessionat_*` prefix display analytics + workspace creation in one bucket |
| `delete_workspace` | `workspace_id` | delete + close tabs (confirm) |
| `save_current_session` | `name` | snapshot tabs as a named session |
| `restore_session` | `session_id` | restore session into current/new workspace |
| `delete_session` | `session_id` | destructive |
| `take_screenshot` | `tab_id, full_page?` | PNG (base64) |

**Out of scope for v2.0:** `click_element`, `type_text`, `fill_form`, `scroll` (DOM automation / agent mode). Deferred — too risky without battle-tested permission UI.

**Resources** (read-only):

- `sessionat://visits/today`
- `sessionat://visits/last-7-days`
- `sessionat://sessions/saved`
- `sessionat://workspaces`
- `sessionat://tabs/open`
- `sessionat://focus-score/today`

#### 4.3.4 Connection UX — `chrome://sessionat-mcp/` (v2.0.0 multi-client)

The MCP control surface lives at `chrome://sessionat-mcp/` (Settings → AI Connections deep-links there). It's a **7-tab page**, one tab per supported client plus a catch-all:

| Tab | Client | Default config path |
|---|---|---|
| Claude Desktop | Anthropic Claude Desktop | `~/Library/Application Support/Claude/claude_desktop_config.json` (mac), `%APPDATA%\Claude\claude_desktop_config.json` (win) |
| Cursor | Cursor IDE | `~/.cursor/mcp.json` |
| Codex | OpenAI Codex CLI | `~/.codex/config.toml` (TOML, not JSON) |
| Claude Code | Anthropic Claude Code CLI | written via `claude mcp add` |
| Windsurf | Codeium Windsurf | `~/.codeium/windsurf/mcp_config.json` |
| VS Code | VS Code MCP extension(s) | written via `code --add-mcp` |
| Other | Manual / unsupported | copy-paste snippet only |

Each tab reflects a per-client state machine: **`not_installed` → `installed_no_entry` → `connected` → `stale` → `error`**. The header pill ("Connected" / "Not installed" / "Stale config" / "Error") and the action buttons are driven entirely by that state.

##### Multi-Client MCP Architecture

The browser-side code lives under `chrome/browser/sessionat/mcp/`:

- **`client_config_manager.{h,cc}`** — top-level dispatcher (was `claude_config_manager.{h,cc}` in v1). Holds the `WriterOps` table and routes `connect/disconnect/status` calls to the per-client writer for a given `ClientId`.
- **`client_writers/`** — one writer per supported client:
  - `claude_desktop_writer.cc`
  - `cursor_writer.cc`
  - `codex_writer.cc`
  - `claude_code_writer.cc`
  - `windsurf_writer.cc`
  - `vscode_writer.cc`
  - `writer_common.{h,cc}` — shared helpers (atomic-write-with-`.bak`, JSON merge, "reveal in Finder", CLI probe)
- **`client_token_registry.{h,cc}`** — maps `ClientId → 64-hex bearer token`. Tokens are deterministic (HKDF-derived from the master keychain secret + client id) so they survive reinstall without re-prompting, but each client gets its **own** token. The master token is **never** written into a client config.
- **`write_grants.{h,cc}`** — persistent `(client_id, tool_name) → granted` map. A write tool call from a client that hasn't been granted that tool returns MCP error code **`-32010` (`kErrWriteRequiresApproval`)**; the client surfaces that as a first-use prompt, the user clicks Approve in `chrome://sessionat-mcp/`, the grant is persisted, and the next call succeeds.

The **WriterOps table pattern**: each client writer exposes the same struct of function pointers — `Detect()`, `Connect()`, `Disconnect()`, `ReadStatus()`, `RevealConfig()`. The dispatcher is a flat `std::array<WriterOps, kNumClients>`, so adding a 7th supported client is one struct + one array slot, not a new code path.

##### IPC handlers exposed to `chrome://sessionat-mcp/`

`sessionat_mcp_ui.cc` exposes **8 Mojo IPC handlers** to the WebUI:

1. `connectClient(client_id)` — run that client's writer; mint a per-client token if absent.
2. `disconnectClient(client_id)` — strip the Sessionat entry from the client config; keep the token in the registry (re-Connect is one click).
3. `getClientStatus(client_id)` — return one state-machine row with last-error string.
4. `getAllClientStatuses()` — bulk variant for first paint of the page.
5. `revealClientConfig(client_id)` — opens the client's config file in Finder / Explorer.
6. `setClientWriteGrant(client_id, tool_name, granted)` — flip a row in `write_grants`.
7. `testConnection(client_id)` — round-trip a ping over the IPC bridge using that client's token; surfaces `connected` vs `stale` vs `error`.
8. `rotateToken(client_id)` — re-derive that client's bearer token and re-write the client config in place.

##### Codex specifics

Codex is the awkward client and earns its own paragraph. The writer:

1. **Probes inside the app bundle for the CLI**, not `$PATH`. Codex ships `codex` at `/Applications/Codex.app/Contents/Resources/codex` and does **not** symlink to `/usr/local/bin`, so a Finder-launched Sessionat (which doesn't inherit shell `PATH`) would otherwise fail to find it.
2. **Writes TOML directly to `~/.codex/config.toml`** rather than shelling out to `codex mcp add`. Codex's `mcp add` subcommand only supports bearer tokens via env-var indirection (`--env-from`), not as literal strings, and our per-client token model needs the literal string baked into the config. Writer_common's atomic-write-with-`.bak` handles the TOML merge — preserve every existing `[mcp_servers.*]` block, overwrite only the `[mcp_servers.sessionat]` block.

##### Failure modes covered

- Config file locked / read-only → state → `error`, tab shows "Copy snippet" + step-by-step manual instructions.
- Existing third-party Sessionat-named entry the user hand-wrote → diff dialog before overwrite.
- Client not installed → state → `not_installed`, button becomes "Get app" linking to the vendor download.
- CLI missing for clients that write via CLI (Claude Code, VS Code) → writer falls back to direct-file-write or instructs the user to install the CLI.

#### 4.3.5 Auth & permissions

- On first launch, generate a 256-bit **master** secret, store in macOS Keychain / Windows Credential Manager / Linux libsecret. The master never leaves the keychain and is **never** written into any client config.
- Per-client 64-hex bearer tokens are derived from the master via `client_token_registry`. The IPC endpoint authenticates the inbound `client_id` against its registered token and tags every downstream call with that id.
- Per-client rotation: `chrome://sessionat-mcp/` → client tab → "Rotate token". Only that client's config is rewritten; other clients keep working.
- **Write tools require per-client first-use approval.** A write call from an un-granted `(client_id, tool_name)` pair returns MCP error `-32010` (`kErrWriteRequiresApproval`); the user approves in the client's tab; the grant persists in `write_grants` and is revokable from the same tab.
- Destructive tools (`delete_workspace`, `delete_session`) require an additional in-call confirmation even after the per-client grant is in place.

---

## 5. Implementation phases & milestones

### Phase 0 — Foundation (1 week)

- [x] Pick target Chromium milestone — **shipped on M150 (150.0.7856.0)**
- [ ] Update [depot_tools/](depot_tools/) (`git -C depot_tools pull`)
- [ ] Re-fetch Chromium source at chosen tag
- [ ] Re-apply existing patches against the new milestone, fix conflicts (expect 2-3 per feature folder)
- [ ] Smoke-test build — `autoninja -C out/Default chrome` produces a runnable `Sessionat.app`?
- [ ] Confirm Apple Developer ID is active (renew if lapsed)
- [ ] Start EV Code Signing cert procurement for Windows (1-2 weeks lead — needed by Phase 4)

### Phase 1 — Sidebar workspaces redesign (3-4 weeks) ⚠ heaviest UI work

- [ ] New `SessionatSidebarView` in `chrome/browser/ui/views/frame/`
- [ ] `VerticalTabStrip` view + reuse existing `TabStripModel` data
- [ ] Hide top horizontal tab strip by default; setting toggle to restore
- [ ] Workspaces persistence (`workspaces_database` schema + service)
- [ ] Tab restoration on launch — every workspace's tab set restored exactly
- [ ] Keyboard shortcuts (Cmd+1-9, Cmd+Shift+M move-tab, etc.)
- [ ] Three starter workspaces on first launch (Work / Personal / Research)
- [ ] Sidebar footer "today" widget linking to dashboard
- [ ] Right-click tab → "Move to workspace…" submenu (port v1 `workspace_sub_menu_model.cc`)

### Phase 2 — Visit Analytics + Productivity Dashboard (2-3 weeks)

- [ ] `visit_analytics_service` skeleton + factory + BUILD.gn
- [ ] SQLite schema using `sql::Database` (visits + daily_rollup)
- [ ] `ActiveTimeTracker` combining `SiteEngagementService::Observer` + `PageLoadMetricsObserver` + `ui::UserActivityDetector` (60s idle threshold)
- [ ] Workspace-aware: every visit row carries `workspace_id`
- [ ] Daily rollup background job
- [ ] Ship `categories.json` (~5000 domains) baked into binary
- [ ] Weekly category list fetch from `version.sessionat.com/categories.json`
- [ ] First-run dialog with prominent privacy notice
- [ ] Settings → Privacy → Visit Tracking (toggle, exclusions list, wipe, export)
- [ ] `chrome://sessionat-analytics/` WebUI: Today / This week / By workspace / Goals / Timeline
- [ ] NTP "Top 5 today" widget
- [ ] CI lint: visit_analytics_service has no network egress

### Phase 3 — MCP server (2-3 weeks) — ✅ shipped in v2.0.0

- [x] Mojo IPC bridge in `chrome/browser/sessionat/mcp_bridge/`
- [x] Unix socket (macOS/Linux) + named pipe (Windows) listener with per-client bearer-token auth
- [x] Master secret in Keychain/CredMan/libsecret; per-client tokens via `client_token_registry.{h,cc}`
- [x] `sessionat-mcp` TypeScript project using `@modelcontextprotocol/sdk`
- [x] Implement all read tools (no-confirmation), incl. the 4 analytics tools (`sessionat_search_visits`, `sessionat_get_visits_for_host`, `sessionat_get_visit_buckets`, `sessionat_get_top_sites`)
- [x] Implement all write tools (per-client `write_grants.{h,cc}`; un-granted call → MCP `-32010`); incl. `sessionat_create_workspace`
- [x] `bun build --compile` → single binary per platform
- [x] Bundle binary into `Sessionat.app/Contents/MacOS/sessionat-mcp`
- [x] `chrome://sessionat-mcp/` 7-tab page (Claude Desktop / Cursor / Codex / Claude Code / Windsurf / VS Code / Other) with 8 IPC handlers
- [x] `client_config_manager.{h,cc}` dispatcher + 6 per-client writers under `client_writers/` (Codex writes TOML directly; probes `/Applications/Codex.app/Contents/Resources/codex`)
- [x] Atomic config merge with `.bak` safety, diff-on-overwrite
- [x] In-page approval flow for first-use write-tool grants (replaces OS notification — keeps the trust decision inside the browser)
- [x] Permission management UI (granted-permissions list per tab, revoke)
- [x] NTP empty-state fix — analytics link is always visible (was hidden on first-run zero-data state)
- [x] Docs + 90-second screencast

### Phase 4 — macOS ship (1-2 weeks, overlaps Phase 3)

- [ ] Update [BRANDING](sessionat-browser/chromium-patches/chrome/app/theme/chromium/BRANDING) with `MAC_TEAM_ID`
- [ ] Switch [build_chromium.sh](build_chromium.sh) to universal build (arm64 + x64, then `lipo`)
- [ ] Add `notarytool submit --wait` + `stapler staple` to build script
- [ ] Generate DMG via `create-dmg` with custom background + Applications symlink
- [ ] Wire `downloads.sessionat.com` → Cloudflare R2 bucket
- [ ] Integrate Sparkle 2; serve `appcast.xml` from the version Worker
- [ ] Update [sessionat-version/src/data.js](sessionat-version/src/data.js) with v2.0.0 entries
- [ ] **Ship v2.0.0 macOS** (all three pillars)

### Phase 5 — Windows (3-4 weeks, post-v2.0 = v2.1)

- [ ] Re-apply [sessionat-patches-for-windows/](sessionat-patches-for-windows/) onto current Chromium
- [ ] Run [windows-sessionat/windows-build-script.ps1](windows-sessionat/windows-build-script.ps1) on a Win box (VM or physical)
- [ ] Sign with EV cert
- [ ] MSI via WiX + NSIS .exe alternative
- [ ] Port MCP sidecar named-pipe path
- [ ] Wire Omaha or Squirrel.Windows auto-update
- [ ] **Ship v2.1.0**

### Phase 6 — Linux (2-3 weeks, post-v2.1 = v2.2)

- [ ] Linux build args + tarball
- [ ] .deb (debhelper) + .rpm (rpmbuild) + AppImage (linuxdeploy)
- [ ] GPG keys + Cloudflare-hosted apt + dnf repos
- [ ] MCP sidecar verifies on Linux (Unix socket)
- [ ] **Ship v2.2.0**

**Total estimated time to v2.0 macOS: 9-13 weeks.**

---

## 6. Risks & mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| Chromium rebase: 5 months of upstream drift = patch conflicts | High | Budget full week for Phase 0; expect 2-3 conflicts per feature folder |
| Sidebar redesign harder than estimated (Chromium views/ is dense) | High | Reference existing forks (Brave's vertical tabs, Vivaldi's panel) for known-working patterns |
| MCP spec changes mid-development | Medium | Pin SDK version; abstract behind internal `ToolDispatcher` interface |
| Notarization rejection | Low | Test with `notarytool log` pre-ship; fallback docs (`xattr -d com.apple.quarantine`) |
| EV code signing cert delay (1-2 weeks) | High | Start in Phase 0, blocks Phase 5 only |
| Auto-update safety on Sparkle | Medium | Staged rollout: 5% → 25% → 100% via appcast `phasedRolloutInterval` |
| Visit analytics feels invasive | Medium | First-run dialog with prominent local-only language; per-site exclusions |
| MCP prompt injection (malicious page convinces AI to call `delete_workspace`) | High | Per-client first-use approval for ALL write tools; destructive tools (`delete_*`) always require confirmation; permission revocation UI |
| Bus factor — solo dev, 5-month dormancy precedent | High | Keep specs in [prompts/](prompts/) + this plan up to date; memory system catches drift |
| Categories.json staleness — new sites uncategorized | Low | Weekly fetch from version Worker; uncategorized rows surface in dashboard for manual labeling |
| Free-forever sustainability — Chromium rebases are ongoing labor | Medium | GitHub Sponsors + donation page; consider Open Collective; rebase cadence can stretch (every 3-6 months not 4 weeks) |

---

## 7. Specification — answered decisions (locked)

| # | Decision | Locked answer |
|---|---|---|
| 1 | Target user | **All three personas equally** — tab-hoarders, researchers/writers, AI-native devs. Three co-equal pillars. |
| 2 | Workspace UX | **Vertical sidebar (Arc-style).** Top horizontal tab strip hidden by default. |
| 3 | Workspace isolation | **Tabs + visit history** isolated per workspace. Cookies/extensions/logins shared. |
| 4 | Visit data depth | **URL + title + active time + timestamps + workspace_id.** No content snippets, no screenshots. |
| 5 | Insights UX | **Productivity dashboard** (RescueTime-style). Categories, focus score, goals, timeline. |
| 6 | MCP tool surface | **Full control** — read + writes incl. destructive ops. NOT DOM automation. Per-client first-use confirmation for write tools. |
| 7 | v2.0 scope | **All three pillars ship together** on macOS. ~3 months. |
| 8 | Pricing | **Free forever**, open-source. Donations + GitHub Sponsors. |
| 9 | Categorization | **Bundled `categories.json`** (~5000 domains), weekly auto-update from version Worker. Uncategorized → user can label. |
| 10 | Tracking default state | **On by default with prominent first-run notice.** Honest, local-only, toggle in Settings. |
| 11 | MCP connection UX | **`chrome://sessionat-mcp/` 7-tab multi-client page** (Claude Desktop / Cursor / Codex / Claude Code / Windsurf / VS Code / Other). Per-client bearer tokens, per-client write grants, per-client state machine, atomic config merge + `.bak` safety. |

---

## 8. Open items (not blocking — decide as we go)

1. **Telemetry:** Default zero-telemetry (privacy-first marketing); add opt-in crash reporting via Sentry later if needed.
2. **Cross-device sync of workspaces & visit data:** Not in v2.0 (would need backend infra inconsistent with free-forever model). Could revisit as paid Sessionat Cloud add-on in v2.3+.
3. **Sessionat companion extension:** v1 bundled an external extension (`dmoljfchnfkphkmoipdfkgfhlchcoebo`). With MCP doing the AI integration, decide in Phase 3 whether to keep, deprecate, or merge its features natively.
4. **Goals UX in dashboard:** v2.0 ships basic time-target goals. Streak tracking and notifications can wait.
5. **Mobile:** out of scope indefinitely.

---

## 9. Status (2026-05-29)

This document was written as the **pre-rebase plan** on 2026-05-24. As of 2026-05-29:

1. ✅ Chromium milestone picked — **M150 (150.0.7856.0)**
2. ✅ Universal macOS DMG shipped (arm64 + x64, signed, notarized, EdDSA-signed for Sparkle auto-update)
3. ✅ All three pillars shipped together — Workspaces, Visit Analytics, MCP Server
4. ✅ Auto-update infrastructure live — appcast at `version.sessionat.com`, DMG hosting at `files.sessionat.com`
5. 🟡 EV Code Signing cert for Windows — pending (not blocking macOS)
6. 🟡 Linux build — planned for 2026 Q3
7. 🟡 Windows build — planned for 2026 Q4

The download lives at **[sessionat.com](https://sessionat.com)**.

---

## Appendix A — File map (existing assets ready to reuse)

| Path | Reuse in |
|---|---|
| [build_chromium.sh](build_chromium.sh) | Phase 4 (extend for universal + notarize) |
| [publish.md](publish.md) | Phase 4 (codesigning/notarize reference) |
| [steps.md](steps.md) | Phase 0 (env setup, gn args) |
| [chromium.md](chromium.md) | Phase 0 (Chromium architecture refresher) |
| [sessionat-browser/chromium-patches/](sessionat-browser/chromium-patches/) | Phase 0 rebase baseline |
| [sessionat-browser/chromium-patches/workspaces/](sessionat-browser/chromium-patches/workspaces/) | Phase 1 (workspace_model + service to extend) |
| [sessionat-patches-for-windows/](sessionat-patches-for-windows/) | Phase 5 |
| [windows-sessionat/](windows-sessionat/) | Phase 5 (build script + guide) |
| [sessionat-version/](sessionat-version/) | Phase 4 (version Worker — extend to serve appcast.xml + categories.json) |
| [sessionat_ntp_preview/](sessionat_ntp_preview/) | Phase 2 (NTP starting point for analytics widget) |
| [logo-sessionat/](logo-sessionat/) | All phases (brand assets) |
| [prompts/whitelabel-sessionat-spec.md](prompts/whitelabel-sessionat-spec.md) | Phase 0/1 (existing whitelabel spec) |
| [prompts/workspaces-feature-spec.md](prompts/workspaces-feature-spec.md) | Phase 1 (existing workspace spec — supersede with §4.1 here) |
| [prompts/saving-sessions.txt](prompts/saving-sessions.txt) | Phase 1/2 (session persistence reference) |

## Appendix B — References

- cpp-mcp: https://github.com/hkr04/cpp-mcp
- MCP spec + SDKs: https://modelcontextprotocol.io/docs/sdk
- Chromium Mac sandboxing: https://chromium.googlesource.com/chromium/src/+/HEAD/sandbox/mac/README.md
- Sparkle (macOS auto-update): https://sparkle-project.org/
- Apple notarization: https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution
- Arc Browser sidebar inspiration: https://arc.net/
- RescueTime dashboard inspiration: https://www.rescuetime.com/
