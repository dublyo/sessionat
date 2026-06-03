// Copyright 2026 Sessionat. All rights reserved.

#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/command_line.h"
#include "base/process/launch.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/sessionat/mcp/client_config_manager.h"
#include "chrome/browser/sessionat/mcp/client_writers/writer_common.h"

namespace sessionat {

namespace {

base::FilePath PathImpl() {
  // Claude Code owns its config opaquely via the `claude` CLI; we never
  // touch a file directly.
  return base::FilePath();
}

base::FilePath ResolveClaudeCli() {
  base::FilePath cli = ResolveCliOnPath("claude");
  if (!cli.empty()) return cli;
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  for (const char* p : {"/opt/homebrew/bin/claude", "/usr/local/bin/claude"}) {
    base::FilePath fp(p);
    if (base::PathExists(fp)) return fp;
  }
#endif
  return base::FilePath();
}

bool DetectImpl() { return !ResolveClaudeCli().empty(); }

std::string MakeCommand(int port, const std::string& token) {
  return base::StringPrintf(
      "claude mcp add --transport http sessionat "
      "http://127.0.0.1:%d/mcp "
      "--header \"Authorization: Bearer %s\"",
      port, token.c_str());
}

base::Value BuildEntryImpl(int port, const std::string& token) {
  base::DictValue d;
  d.Set("command", MakeCommand(port, token));
  return base::Value(std::move(d));
}

std::pair<bool, std::string> ShellOut(const std::vector<std::string>& argv) {
  base::LaunchOptions opts;
  base::Process p = base::LaunchProcess(argv, opts);
  if (!p.IsValid()) return {false, "Could not launch process"};
  int exit_code = -1;
  if (!p.WaitForExitWithTimeout(base::Seconds(8), &exit_code)) {
    p.Terminate(/*exit_code=*/1, /*wait=*/false);
    return {false, "Subprocess timed out"};
  }
  if (exit_code != 0) {
    return {false,
            base::StringPrintf("Subprocess exited with code %d", exit_code)};
  }
  return {true, ""};
}

std::pair<bool, std::string> ApplyImpl(bool remove,
                                         int port,
                                         const std::string& token) {
  base::FilePath cli = ResolveClaudeCli();
  if (cli.empty()) {
    if (remove) {
      return {false,
              "claude CLI not found on PATH. Run `claude mcp remove "
              "sessionat` in a terminal where the CLI is available."};
    }
    return {false,
            "claude CLI not found on PATH. Run this in a terminal:\n" +
                MakeCommand(port, token)};
  }
  std::vector<std::string> argv;
  argv.push_back(cli.value());
  argv.push_back("mcp");
  if (remove) {
    argv.push_back("remove");
    argv.push_back("sessionat");
  } else {
    argv.push_back("add");
    argv.push_back("--transport");
    argv.push_back("http");
    argv.push_back("sessionat");
    argv.push_back(base::StringPrintf("http://127.0.0.1:%d/mcp", port));
    argv.push_back("--header");
    argv.push_back(base::StringPrintf("Authorization: Bearer %s", token.c_str()));
  }
  return ShellOut(argv);
}

ClientConfigManager::StatusResult ReadStatusImpl(int port,
                                                  const std::string& token) {
  ClientConfigManager::StatusResult r;
  base::FilePath cli = ResolveClaudeCli();
  if (cli.empty()) {
    r.status = ClientConfigManager::Status::kNotInstalled;
    return r;
  }
  base::CommandLine cmd(cli);
  cmd.AppendArg("mcp");
  cmd.AppendArg("list");
  std::string output;
  int exit_code = -1;
  if (!base::GetAppOutputWithExitCode(cmd, &output, &exit_code) ||
      exit_code != 0) {
    r.status = ClientConfigManager::Status::kInstalledNoEntry;
    r.requires_manual_snippet = true;
    r.manual_snippet = MakeCommand(port, token);
    return r;
  }
  if (output.find("sessionat") == std::string::npos) {
    r.status = ClientConfigManager::Status::kInstalledNoEntry;
    r.requires_manual_snippet = true;
    r.manual_snippet = MakeCommand(port, token);
    return r;
  }
  const std::string url =
      base::StringPrintf("http://127.0.0.1:%d/mcp", port);
  const bool url_ok = output.find(url) != std::string::npos;
  const bool tok_ok = output.find(token) != std::string::npos;
  r.status = (url_ok && tok_ok) ? ClientConfigManager::Status::kConnected
                                  : ClientConfigManager::Status::kStale;
  return r;
}

}  // namespace

const WriterOps& GetClaudeCodeOps() {
  static const WriterOps ops = {
      &PathImpl, &DetectImpl, &BuildEntryImpl, &ApplyImpl, &ReadStatusImpl};
  return ops;
}

}  // namespace sessionat
