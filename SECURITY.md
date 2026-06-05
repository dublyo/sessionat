# Security Policy

## Supported versions

Only the latest 2.x release line of Sessionat receives security fixes.
Earlier 1.x builds are unmaintained — please upgrade.

| Version | Supported          |
| ------- | ------------------ |
| 2.x     | Yes (latest patch) |
| 1.x     | No                 |

## Reporting a vulnerability

If you believe you've found a security issue in the Sessionat patches in
this repository — for example a problem with the MCP server, the visit
analytics database, the WebUI handlers, or the workspace isolation
patches — please report it privately.

**Preferred channel:** email **info@meditechus.com** with subject line
`Sessionat security: <short description>`.

Please include:

- The affected version (e.g., 2.0.0) and your operating system.
- A clear description of the issue and the impact you believe it has.
- Steps to reproduce, ideally with a minimal proof-of-concept.
- Whether the issue is already public elsewhere, and any disclosure
  deadline you've committed to with other parties.

Do **not** open a public GitHub issue, pull request, or discussion thread
for security reports. Public reports of unfixed issues may put other users
at risk.

## What to expect

- **Acknowledgement** within 72 hours of receipt.
- **Initial assessment** (severity + scope) within 7 days.
- **Status updates** at least every 14 days while a fix is being
  developed.
- **Coordinated disclosure** — once a fix has shipped in a notarized
  release on https://sessionat.com, the report is eligible to be
  published in the release notes with credit to the reporter (opt-in;
  let us know if you prefer to remain anonymous).

If a report turns out to be a duplicate of an upstream Chromium issue
rather than something specific to the Sessionat patches, we'll re-route
it to the appropriate Chromium security channel and let you know.

## Scope

**In scope** — anything in this repository, specifically:

- The patches under `chromium-patches/chrome/browser/sessionat/`
  (MCP server, visit analytics, workspace persistence, the WebUI
  surfaces at `chrome://sessionat-*`).
- The MCP client writers under `chromium-patches/chrome/browser/sessionat/mcp/client_writers/`
  (per-client config-file writes, atomic merge logic, bearer token
  handling).
- Per-client token registry and write-grant enforcement.
- The Sparkle auto-update integration (signature verification, EdDSA
  key handling).

**Out of scope** — issues that are properly reported elsewhere:

- Vulnerabilities in upstream Chromium itself — report via
  https://www.chromium.org/Home/chromium-security/reporting-security-bugs/
- Vulnerabilities in third-party MCP clients (Claude Desktop, Cursor,
  Codex, Claude Code, Windsurf, VS Code) — report to those vendors
  directly.
- Vulnerabilities in third-party npm packages used by the bridge
  layer (e.g., `mcp-remote`) — report to those package maintainers.

## Safe harbor

Researchers acting in good faith under this policy will not be pursued
under the Computer Fraud and Abuse Act or similar laws for testing the
issue. Please don't access data that isn't yours, don't degrade service
for other users, and don't publicly disclose before a fix ships.
