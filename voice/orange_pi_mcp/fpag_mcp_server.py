#!/usr/bin/env python3
"""MCP 服务：向 LLM 暴露 query_device / show_device_3d / list_devices / ping_system。

用法（香橙派）：
  pip install -r requirements.txt
  python fpag_mcp_server.py

也可用：
  python -c "from tools_impl import show_device_3d; print(show_device_3d(3))"
"""
from __future__ import annotations

import json
from typing import Any

from mcp.server.fastmcp import FastMCP

from tools_impl import list_devices, ping_system, query_device, show_device_3d

mcp = FastMCP("fpag-orangepi")


@mcp.tool()
def tool_query_device(device_id: int) -> str:
    """查询指定编号工业设备的实时状态与故障信息。device_id 如 1、2、3。"""
    return json.dumps(query_device(device_id), ensure_ascii=False)


@mcp.tool()
def tool_list_devices() -> str:
    """列出全部设备及状态。"""
    return json.dumps(list_devices(), ensure_ascii=False)


@mcp.tool()
def tool_show_device_3d(device_id: int, fault: str = "bearing") -> str:
    """在 PC 大屏切换设备 3D。fault=none/normal 恢复正常；bearing/imbalance/looseness/coupling 高亮部位。"""
    state = "normal" if fault in {"normal", "none"} else "fault"
    site = "none" if state == "normal" else fault
    return json.dumps(
        show_device_3d(device_id, state=state, fault_site=site),
        ensure_ascii=False,
    )


@mcp.tool()
def tool_ping_system() -> str:
    """检查网关 TCP:9000 与 PC 大屏 HTTP:8760 是否可达。"""
    return json.dumps(ping_system(), ensure_ascii=False)


# 给 LLM / 文档用的别名说明
TOOL_ALIASES = {
    "query_device": "tool_query_device",
    "show_device_3d": "tool_show_device_3d",
    "list_devices": "tool_list_devices",
    "ping_system": "tool_ping_system",
}


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
