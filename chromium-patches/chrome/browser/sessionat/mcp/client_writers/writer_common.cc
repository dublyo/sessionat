// Copyright 2026 Sessionat. All rights reserved.

#include "chrome/browser/sessionat/mcp/client_writers/writer_common.h"

#include <utility>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include "base/strings/utf_string_conversions.h"
#endif

namespace sessionat {

namespace {

constexpr char kHistogramSuffix[] = "SessionatMcp";

#if BUILDFLAG(IS_WIN)
constexpr base::FilePath::CharType kPathSeparator = FILE_PATH_LITERAL(';');
#else
constexpr base::FilePath::CharType kPathSeparator = FILE_PATH_LITERAL(':');
#endif

}  // namespace

base::FilePath HomeDir() {
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home)) return base::FilePath();
  return home;
}

base::FilePath ResolveCliOnPath(const std::string& exe_name) {
  auto env = base::Environment::Create();
  std::optional<std::string> path_var = env->GetVar("PATH");
  if (!path_var) return base::FilePath();
#if BUILDFLAG(IS_WIN)
  std::vector<std::wstring> paths = base::SplitString(
      base::UTF8ToWide(*path_var), std::wstring(1, kPathSeparator),
      base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  std::vector<std::wstring> exts = {L".exe", L".cmd", L".bat", L""};
  std::wstring wexe = base::UTF8ToWide(exe_name);
#else
  std::vector<std::string> paths = base::SplitString(
      *path_var, std::string(1, kPathSeparator), base::TRIM_WHITESPACE,
      base::SPLIT_WANT_NONEMPTY);
#endif
  for (const auto& dir : paths) {
    base::FilePath dir_path(dir);
#if BUILDFLAG(IS_WIN)
    for (const auto& ext : exts) {
      base::FilePath candidate = dir_path.Append(wexe + ext);
      if (base::PathExists(candidate)) return candidate;
    }
#else
    base::FilePath candidate = dir_path.Append(exe_name);
    if (base::PathExists(candidate)) return candidate;
#endif
  }
  return base::FilePath();
}

bool AnyPathExists(std::initializer_list<base::FilePath> paths) {
  for (const auto& p : paths) {
    if (!p.empty() && base::PathExists(p)) return true;
  }
  return false;
}

bool WriteJsonAtomically(const base::FilePath& path,
                          const base::DictValue& root,
                          std::string* err) {
  std::string serialized;
  if (!base::JSONWriter::WriteWithOptions(
          root, base::JSONWriter::OPTIONS_PRETTY_PRINT, &serialized)) {
    if (err) *err = "Serialize failed";
    return false;
  }
  return WriteTextAtomically(path, serialized, err);
}

bool WriteTextAtomically(const base::FilePath& path,
                          const std::string& body,
                          std::string* err) {
  base::FilePath dir = path.DirName();
  if (!base::DirectoryExists(dir)) {
    if (!base::CreateDirectory(dir)) {
      if (err) *err = "Could not create " + dir.AsUTF8Unsafe();
      return false;
    }
  }
  // Best-effort backup of the live file before atomic rename.
  if (base::PathExists(path)) {
    base::FilePath bak = path.AddExtensionASCII(".bak");
    if (!base::CopyFile(path, bak)) {
      LOG(WARNING) << "[Sessionat MCP] could not write " << bak.value();
    }
  }
  if (!base::ImportantFileWriter::WriteFileAtomically(path, body,
                                                       kHistogramSuffix)) {
    if (err) *err = "Atomic write failed for " + path.AsUTF8Unsafe();
    return false;
  }
  return true;
}

std::optional<base::DictValue> ReadJsonDict(const base::FilePath& path,
                                                std::string* err) {
  std::string body;
  if (!base::ReadFileToString(path, &body)) {
    if (err) *err = "Could not read " + path.AsUTF8Unsafe();
    return std::nullopt;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (!parsed) {
    if (err) {
      *err = path.AsUTF8Unsafe() +
              " is invalid JSON; refusing to touch it. Fix or remove the "
              "file and try again.";
    }
    return std::nullopt;
  }
  if (!parsed->is_dict()) {
    if (err) *err = path.AsUTF8Unsafe() + " is not a JSON object.";
    return std::nullopt;
  }
  return std::move(*parsed).TakeDict();
}

void RevealInFileManager(const base::FilePath& path) {
  if (path.empty()) return;
#if BUILDFLAG(IS_MAC)
  std::vector<std::string> argv = {"/usr/bin/open", "-R", path.value()};
  base::LaunchOptions opts;
  base::LaunchProcess(argv, opts);
#elif BUILDFLAG(IS_LINUX)
  // No portable xdg equivalent of `open -R`; open the parent directory.
  std::vector<std::string> argv = {"/usr/bin/xdg-open",
                                     path.DirName().value()};
  base::LaunchOptions opts;
  base::LaunchProcess(argv, opts);
#elif BUILDFLAG(IS_WIN)
  base::CommandLine cmd(
      base::FilePath(FILE_PATH_LITERAL("explorer.exe")));
  cmd.AppendArg(std::string("/select,") + path.AsUTF8Unsafe());
  base::LaunchOptions opts;
  base::LaunchProcess(cmd, opts);
#else
  (void)path;
#endif
}

bool EntryMatchesPortAndToken(const base::DictValue& entry,
                                int port,
                                const std::string& token) {
  const std::string want_url =
      base::StringPrintf("http://127.0.0.1:%d/mcp", port);
  bool url_ok = false;
  bool token_ok = false;
  if (const std::string* url = entry.FindString("url")) {
    url_ok = (url->find(want_url) != std::string::npos);
  }
  if (const base::DictValue* headers = entry.FindDict("headers")) {
    if (const std::string* a = headers->FindString("Authorization")) {
      token_ok = (a->find(token) != std::string::npos);
    }
  }
  // Fallback for clients whose entry shape is the mcp-remote bridge args list.
  if (!url_ok || !token_ok) {
    if (const base::ListValue* args = entry.FindList("args")) {
      for (const base::Value& v : *args) {
        if (!v.is_string()) continue;
        const std::string& s = v.GetString();
        if (s.find(want_url) != std::string::npos) url_ok = true;
        if (s.find(token) != std::string::npos) token_ok = true;
      }
    }
  }
  return url_ok && token_ok;
}

base::DictValue BuildHttpHeaderEntry(int port, const std::string& token) {
  base::DictValue entry;
  entry.Set("url", base::StringPrintf("http://127.0.0.1:%d/mcp", port));
  base::DictValue headers;
  headers.Set("Authorization",
                base::StringPrintf("Bearer %s", token.c_str()));
  entry.Set("headers", std::move(headers));
  return entry;
}

}  // namespace sessionat
