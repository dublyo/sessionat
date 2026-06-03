// Copyright 2026 Sessionat. All rights reserved.

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"
#include "chrome/browser/sessionat/mcp/client_writers/writer_common.h"

namespace sessionat {

namespace {

base::FilePath PathImpl() {
  base::FilePath home = HomeDir();
  if (home.empty()) return base::FilePath();
  // Informational only — Apply prefers the CLI when available.
  return home.Append(FILE_PATH_LITERAL(".codex"))
      .Append(FILE_PATH_LITERAL("config.toml"));
}

base::FilePath ResolveCodexCli() {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  base::FilePath cli = ResolveCliOnPath("codex");
  if (!cli.empty()) return cli;
#if BUILDFLAG(IS_MAC)
  // OpenAI Codex CLI ships inside the .app bundle, not on $PATH.
  base::FilePath in_bundle(FILE_PATH_LITERAL(
      "/Applications/Codex.app/Contents/Resources/codex"));
  if (base::PathExists(in_bundle)) return in_bundle;
#endif
  base::FilePath brew_arm(FILE_PATH_LITERAL("/opt/homebrew/bin/codex"));
  if (base::PathExists(brew_arm)) return brew_arm;
  base::FilePath usr_local(FILE_PATH_LITERAL("/usr/local/bin/codex"));
  if (base::PathExists(usr_local)) return usr_local;
#elif BUILDFLAG(IS_WIN)
  base::FilePath cli = ResolveCliOnPath("codex");
  if (!cli.empty()) return cli;
#endif
  return base::FilePath();
}

bool DetectImpl() {
#if BUILDFLAG(IS_MAC)
  base::FilePath app(FILE_PATH_LITERAL("/Applications/Codex.app"));
  if (base::PathExists(app)) return true;
#endif
  if (!ResolveCodexCli().empty()) return true;
  base::FilePath p = PathImpl();
  return !p.empty() && base::PathExists(p.DirName());
}

std::string MakeTomlSnippet(int port, const std::string& token) {
  return base::StringPrintf(
      "[mcp_servers.sessionat]\n"
      "transport = \"http\"\n"
      "url = \"http://127.0.0.1:%d/mcp\"\n"
      "# Bearer token below; keep this file readable only by your user.\n"
      "headers = { Authorization = \"Bearer %s\" }\n",
      port, token.c_str());
}

base::Value BuildEntryImpl(int port, const std::string& token) {
  base::DictValue d;
  d.Set("section", "mcp_servers.sessionat");
  d.Set("transport", "http");
  d.Set("url", base::StringPrintf("http://127.0.0.1:%d/mcp", port));
  d.Set("authorization", base::StringPrintf("Bearer %s", token.c_str()));
  d.Set("toml", MakeTomlSnippet(port, token));
  return base::Value(std::move(d));
}

// Drop the `[mcp_servers.sessionat]` block from a TOML body, if present.
// Block ends at EOF or the next line starting with `[` (top-level table).
// Leaves the rest of the file untouched — we don't try to be a full TOML
// parser; just a section-level scalpel.
std::string StripSessionatSection(const std::string& body) {
  std::vector<std::string> lines = base::SplitString(
      body, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  std::string out;
  out.reserve(body.size());
  bool dropping = false;
  for (const std::string& line : lines) {
    std::string trimmed =
        std::string(base::TrimWhitespaceASCII(line, base::TRIM_ALL));
    if (!dropping) {
      if (trimmed == "[mcp_servers.sessionat]") {
        dropping = true;
        continue;
      }
      out.append(line);
      out.push_back('\n');
    } else {
      // Stop dropping when we hit the next top-level `[...]` table header.
      if (!trimmed.empty() && trimmed[0] == '[') {
        dropping = false;
        out.append(line);
        out.push_back('\n');
      }
      // Otherwise: still inside the sessionat block — skip.
    }
  }
  // SplitString on a trailing newline produces an empty tail element; the
  // loop above adds one back. Strip the doubled trailing newline.
  while (out.size() >= 2 && out[out.size() - 1] == '\n' &&
         out[out.size() - 2] == '\n') {
    out.pop_back();
  }
  return out;
}

std::pair<bool, std::string> ApplyImpl(bool remove,
                                         int port,
                                         const std::string& token) {
  // Codex CLI's `mcp add` doesn't accept a literal bearer token (only
  // `--bearer-token-env-var <NAME>`), so we always write TOML directly into
  // ~/.codex/config.toml. The CLI presence is still informational for the UI
  // status — but Apply never invokes it.
  base::FilePath path = PathImpl();
  if (path.empty()) {
    return {false, "Could not resolve $HOME for ~/.codex/config.toml"};
  }

  base::FilePath dir = path.DirName();
  if (!base::DirectoryExists(dir)) {
    base::File::Error err;
    if (!base::CreateDirectoryAndGetError(dir, &err)) {
      return {false, "Could not create " + dir.AsUTF8Unsafe()};
    }
  }

  std::string existing;
  if (base::PathExists(path)) {
    if (!base::ReadFileToString(path, &existing)) {
      return {false, "Could not read " + path.AsUTF8Unsafe()};
    }
  }

  std::string body = StripSessionatSection(existing);
  if (!body.empty() && body.back() != '\n') {
    body.push_back('\n');
  }

  if (!remove) {
    if (!body.empty()) body.push_back('\n');
    body.append(MakeTomlSnippet(port, token));
  }

  std::string werr;
  if (!WriteTextAtomically(path, body, &werr)) {
    return {false, "Could not write " + path.AsUTF8Unsafe() + ": " + werr};
  }
  return {true, ""};
}

ClientConfigManager::StatusResult ReadStatusImpl(int port,
                                                  const std::string& token) {
  ClientConfigManager::StatusResult r;
  r.config_path = PathImpl();
  if (!DetectImpl()) {
    r.status = ClientConfigManager::Status::kNotInstalled;
    return r;
  }
  // No CLI? Surface a manual snippet so the UI can offer a copy button.
  if (ResolveCodexCli().empty()) {
    r.requires_manual_snippet = true;
    r.manual_snippet = MakeTomlSnippet(port, token);
  }
  if (r.config_path.empty() || !base::PathExists(r.config_path)) {
    r.status = ClientConfigManager::Status::kInstalledNoEntry;
    return r;
  }
  std::string body;
  if (!base::ReadFileToString(r.config_path, &body)) {
    r.status = ClientConfigManager::Status::kError;
    r.error_message = "Could not read " + r.config_path.AsUTF8Unsafe();
    return r;
  }
  // Cheap TOML probe — pulling in a full parser is overkill for a single
  // section check. Same robustness trade-off the JSON-arg matcher makes.
  const std::string url_marker =
      base::StringPrintf("http://127.0.0.1:%d/mcp", port);
  const bool has_section =
      body.find("[mcp_servers.sessionat]") != std::string::npos ||
      body.find("mcp_servers.sessionat") != std::string::npos;
  if (!has_section) {
    r.status = ClientConfigManager::Status::kInstalledNoEntry;
    return r;
  }
  const bool url_ok = body.find(url_marker) != std::string::npos;
  const bool tok_ok = body.find(token) != std::string::npos;
  r.status = (url_ok && tok_ok) ? ClientConfigManager::Status::kConnected
                                  : ClientConfigManager::Status::kStale;
  return r;
}

}  // namespace

const WriterOps& GetCodexOps() {
  static const WriterOps ops = {
      &PathImpl, &DetectImpl, &BuildEntryImpl, &ApplyImpl, &ReadStatusImpl};
  return ops;
}

}  // namespace sessionat
