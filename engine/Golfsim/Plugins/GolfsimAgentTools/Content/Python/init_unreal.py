"""Plugin Python startup (auto-run by UE at editor launch for enabled plugins).

Belt-and-suspenders import so the GolfsimTools ToolsetDefinition uclass is registered
even if the Toolset Registry's auto-scan doesn't import this module on its own. (GOL-215)
"""

import unreal

try:
    import golfsim_mcp_toolset  # noqa: F401  -- import side effect registers the toolset uclass
    unreal.log("GolfsimAgentTools: golfsim_mcp_toolset registered for Unreal MCP")
except Exception as e:
    unreal.log_warning("GolfsimAgentTools: failed to import golfsim_mcp_toolset: {0}".format(e))
