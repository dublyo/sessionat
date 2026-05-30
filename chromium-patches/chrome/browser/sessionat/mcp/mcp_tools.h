// Copyright 2026 Sessionat. All rights reserved.
// Sessionat-specific MCP tool implementations. Wires WorkspaceService +
// VisitAnalyticsService through the McpService tool registry.

#ifndef CHROME_BROWSER_SESSIONAT_MCP_MCP_TOOLS_H_
#define CHROME_BROWSER_SESSIONAT_MCP_MCP_TOOLS_H_

#include <map>
#include <string>

#include "chrome/browser/sessionat/mcp/mcp_service.h"

class Profile;

namespace sessionat {

// Populates `tools` with every read-only Sessionat tool. Called once from
// McpService's constructor.
void RegisterSessionatTools(McpService* service,
                             Profile* profile,
                             std::map<std::string, McpService::ToolEntry>* tools);

}  // namespace sessionat

#endif  // CHROME_BROWSER_SESSIONAT_MCP_MCP_TOOLS_H_
