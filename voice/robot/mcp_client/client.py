"""MCP / 工具调用 —— 对应文档 §4.4.2。

初期：本地规则解析「查N号」并调 TCP，不依赖大模型 Key。
有 API Key 后可扩展为真正的 function calling。
"""
from __future__ import annotations

import re
from typing import Any

from tcp_client.gateway_client import GatewayClient


TOOL_NAME = "query_device_status"


class McpClient:
    def __init__(self, gateway: GatewayClient, llm_cfg: dict[str, Any] | None = None):
        self.gateway = gateway
        self.llm_cfg = llm_cfg or {}

    def handle_utterance(self, text: str) -> tuple[str, dict[str, Any] | None]:
        """返回 (播报文案, 设备数据或 None)。"""
        device_id = self._parse_device_id(text)
        if device_id is None:
            return "我可以帮你查询设备，请说例如：查3号机器状态。", None

        resp = self.gateway.query_device(device_id)
        if resp.get("code") != 0:
            return f"查询失败：{resp.get('msg', '未知错误')}", None

        data = resp["data"]
        answer = self._format_answer(data)
        return answer, data

    @staticmethod
    def _parse_device_id(text: str) -> int | None:
        m = re.search(r"([一二三四五六七八九十\d]+)\s*号", text)
        if not m:
            m = re.search(r"device\s*(\d+)", text, re.I)
        if not m:
            return None
        token = m.group(1)
        mapping = {"一": 1, "二": 2, "三": 3, "四": 4, "五": 5}
        if token in mapping:
            return mapping[token]
        try:
            return int(token)
        except ValueError:
            return None

    @staticmethod
    def _format_answer(data: dict[str, Any]) -> str:
        name = data.get("device_name", f"{data.get('device_id')}号设备")
        status = data.get("status", "")
        if status == "normal":
            return f"{name}运行正常。温度{data.get('temperature')}度，振动有效值{data.get('vibration_rms')}。"
        return (
            f"{name}当前{data.get('status_text', '异常')}。"
            f"故障类型{data.get('fault_type') or '未知'}，"
            f"位置{data.get('fault_location') or '未知'}。"
            f"温度{data.get('temperature')}度，振动有效值{data.get('vibration_rms')}。"
        )
