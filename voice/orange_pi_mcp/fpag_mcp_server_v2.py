#!/usr/bin/env python3
"""MCP v2：把 PC 大屏（:8760）的查询与控制能力暴露给大模型。

用法：
    pip install -r requirements.txt
    python3 fpag_mcp_server_v2.py

接口限制：
- 只能查询/选择设备、控制 1 号电机、刷新分析、报告、串口；
- 不允许远程注入故障或修改 3D 诊断结果（`/api/show` 已禁用）。
"""
from __future__ import annotations

import json
from typing import Any

from mcp.server.fastmcp import FastMCP

import pc_commands

mcp = FastMCP("fpag-orangepi-v2")


def _j(obj: Any) -> str:
    return json.dumps(obj, ensure_ascii=False)


@mcp.tool()
def tool_query_pc_state() -> str:
    """读取 PC Qt 完整状态：串口、当前设备、六台设备、RMS/阈值/告警与板端确认等级。"""
    return _j(pc_commands.get_state())


@mcp.tool()
def tool_query_device(device_id: int, metric: str = "") -> str:
    """查询设备。不传 metric 返回状态摘要；传 metric 返回单项指标（如 vibration_filtered）。"""
    if metric:
        return _j(pc_commands.query_device_metric(device_id, metric))
    return _j(pc_commands.query_device_via_pc(device_id))


@mcp.tool()
def tool_query_device_metric(device_id: int, metric: str) -> str:
    """查询单台设备的一个指标：vibration_raw / vibration_filtered / vibration_rms /
    vibration_peak / mean_abs / zero_crossings / temperature / current / alert_level /
    alert_low / alert_high / sensor_status / whoami / health / source。"""
    return _j(pc_commands.query_device_metric(device_id, metric))


@mcp.tool()
def tool_query_device_diagnostics(device_id: int) -> str:
    """查询单台设备的全部可用指标、原始值、来源与不可用原因。"""
    return _j(pc_commands.query_device_diagnostics(device_id))


@mcp.tool()
def tool_list_devices() -> str:
    """列出 PC Qt 缓存的六台设备摘要。"""
    state = pc_commands.get_state()
    data = state.get("data") if isinstance(state.get("data"), dict) else {}
    devices = data.get("devices") if isinstance(data, dict) else None
    if devices is None:
        return _j(state)
    return _j(
        [
            {
                "device_id": d.get("id"),
                "state": d.get("state"),
                "state_text": d.get("state_text"),
                "fault_label": d.get("fault_label"),
                "has_data": d.get("has_data"),
                "source": d.get("source"),
            }
            for d in devices
            if isinstance(d, dict)
        ]
    )


@mcp.tool()
def tool_select_device(device_id: int) -> str:
    """在 PC 大屏上切换当前查看设备（1-6），不修改测量值或诊断。"""
    return _j(
        pc_commands.post_command({"command": "select_device", "device_id": int(device_id)})
    )


@mcp.tool()
def tool_control_motor(action: str, pwm: int = 500) -> str:
    """启停 1 号机 A 路真实电机。action=start（pwm 0-1000）或 stop。"""
    action = str(action).lower()
    if action in {"start", "on", "run"}:
        payload: dict[str, Any] = {"command": "start_motor", "pwm": max(0, min(1000, int(pwm)))}
        return _j(pc_commands.post_command(payload))
    if action in {"stop", "off"}:
        return _j(pc_commands.post_command({"command": "stop_motor"}))
    return _j({"ok": False, "error": f"action 只能是 start/stop，收到 {action}"})


@mcp.tool()
def tool_connect_serial(port: str) -> str:
    """切换 PC Qt 使用的串口，例如 COM21。"""
    return _j(pc_commands.post_command({"command": "select_serial", "port": str(port)}))


@mcp.tool()
def tool_generate_test_report() -> str:
    """在 PC 上生成并打开测试报告。"""
    return _j(pc_commands.post_command({"command": "generate_report"}))


@mcp.tool()
def tool_read_test_report() -> str:
    """读取 PC 端当前测试报告（markdown）。"""
    return _j(pc_commands.get_report())


@mcp.tool()
def tool_analyze() -> str:
    """刷新板端分析（RMS/阈值判级）。"""
    return _j(pc_commands.post_command({"command": "analyze"}))


@mcp.tool()
def tool_ping_system() -> str:
    """检查香橙派能否访问 PC 的 8760 API。"""
    return _j(pc_commands.health())


def main() -> None:
    mcp.run()


if __name__ == "__main__":
    main()
