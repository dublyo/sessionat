// Copyright 2026 Sessionat. All rights reserved.
//
// Shared helpers for per-client MCP writers — atomic JSON merge, TOML emit,
// .bak rotation, CLI-on-PATH detection, Reveal-in-FileManager.

#ifndef CHROME_BROWSER_SESSIONAT_MCP_CLIENT_WRITERS_WRITER_COMMON_H_
#define CHROME_BROWSER_SESSIONAT_MCP_CLIENT_WRITERS_WRITER_COMMON_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/values.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"

namespace sessionat {

// Resolve $HOME (or platform equivalent) via PathService. Empty on failure.
base::FilePath HomeDir();

// Detect a CLI on PATH. Returns the resolved absolute path or empty if not
// found. Uses the standard PATH env var on all platforms.
base::FilePath ResolveCliOnPath(const std::string& exe_name);

// True if any of the candidate FilePaths exists on disk.
bool AnyPathExists(std::initializer_list<base::FilePath> paths);

// Serialize `root` as pretty JSON and write via ImportantFileWriter. Copies
// the prior file to <path>.bak before overwriting (best-effort).
bool WriteJsonAtomically(const base::FilePath& path,
                          const base::DictValue& root,
                          std::string* err);

// Same for raw text (used by TOML emitters). Same .bak semantics.
bool WriteTextAtomically(const base::FilePath& path,
                          const std::string& body,
                          std::string* err);

// Read+parse JSON file. Returns std::nullopt on read/parse error; sets `err`.
std::optional<base::DictValue> ReadJsonDict(const base::FilePath& path,
                                                std::string* err);

// Worker-thread Reveal-in-Finder / Explorer / Files-app. Fire-and-forget.
void RevealInFileManager(const base::FilePath& path);

// Helper: matches an existing entry's URL substring (host:port path) AND a
// token substring in either a top-level dict value or a "headers"
// sub-dict. Robust against the user adding extra args.
bool EntryMatchesPortAndToken(const base::DictValue& entry,
                                int port,
                                const std::string& token);

// Common shape for { "url": "...", "headers": { "Authorization": ... } }
// used by Cursor / Windsurf / VSCode (the last with a different top key).
base::DictValue BuildHttpHeaderEntry(int port, const std::string& token);

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_CLIENT_WRITERS_WRITER_COMMON_H_
