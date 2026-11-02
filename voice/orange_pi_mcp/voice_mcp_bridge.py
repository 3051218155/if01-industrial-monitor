"""ASR 文字 → 规则调用 MCP 工具（可 import）。"""
from __future__ import annotations

import json
import re
import sys
from typing import Any

from pc_commands import execute_pc_command, parse_pc_command
from tools_impl import ping_system, query_device, show_device_3d

FAULT_WORDS = {
    "轴承": "bearing",
    "轴成": "bearing",
    "轴程": "bearing",
    "轴瓦": "bearing",
    "磨损": "bearing",
    "bearing": "bearing",
    "不平衡": "imbalance",
    "转子": "imbalance",
    "晃": "imbalance",
    "imbalance": "imbalance",
    "地脚": "looseness",
    "地角": "looseness",
    "松动": "looseness",
    "looseness": "looseness",
    "联轴": "coupling",
    "连轴": "coupling",
    "coupling": "coupling",
    "不对中": "misalignment",
    "misalignment": "misalignment",
}

FAULT_LABEL = {
    "bearing": "轴承磨损",
    "imbalance": "转子不平衡",
    "looseness": "地脚松动",
    "coupling": "联轴器异常",
    "misalignment": "不对中",
    "none": "正常",
    "normal": "正常",
}

_LAST_DEVICE_ID = 3


def _selected_device_id() -> int | None:
    """电脑大屏当前选中的设备号；取不到返回 None。"""
    try:
        from pc_commands import get_state  # type: ignore

        resp = get_state()
    except Exception:  # noqa: BLE001
        return None
    if not resp.get("ok"):
        return None
    data = resp.get("data")
    if isinstance(data, dict) and isinstance(data.get("data"), dict):
        data = data["data"]
    if not isinstance(data, dict):
        return None
    try:
        selected = int(data.get("selected_device_id") or 0)
    except (TypeError, ValueError):
        return None
    return selected if 1 <= selected <= 6 else None


def _normalize(text: str) -> str:
    t = (text or "").strip()
    for ch in "。.!！？?，,、":
        t = t.replace(ch, "")
    for a, b in (
        ("三好", "三号"), ("三浩", "三号"), ("二好", "二号"), ("一好", "一号"),
        ("轴成", "轴承"), ("轴程", "轴承"), ("连轴", "联轴"), ("地角", "地脚"),
        ("显示试", "显示"), ("状太", "状态"),
    ):
        t = t.replace(a, b)
    for tail in ("嗯", "啊", "吧", "呀"):
        if t.endswith(tail):
            t = t[: -len(tail)]
    return t.strip()


def _parse_device_id(text: str) -> int | None:
    global _LAST_DEVICE_ID
    m = re.search(r"([一二三两四五六七八九十\d]+)\s*号", text)
    if not m:
        m = re.search(r"device\s*(\d+)", text, re.I)
    if not m:
        m = re.search(r"(?<!\d)([123])(?!\d)", text)
    if not m:
        return None
    token = m.group(1)
    mapping = {"一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5}
    if token in mapping:
        did = mapping[token]
    else:
        try:
            did = int(token)
        except ValueError:
            return None
    _LAST_DEVICE_ID = did
    return did


def _parse_fault(text: str) -> str | None:
    if any(k in text for k in ("正常", "恢复", "复位", "好了")):
        return "none"
    for word, site in FAULT_WORDS.items():
        if word in text:
            return site
    if "故障" in text:
        return "bearing"
    return None


def _to_pc_fault(site: str) -> tuple[str, str]:
    if site in {"normal", "none", ""}:
        return "normal", "none"
    return "fault", site


def _reply_from_query(q: dict[str, Any], device_id: int) -> str:
    if q.get("ok") and "state" in q:
        name = q.get("name") or f"{device_id}号设备"
        if q.get("state") == "normal":
            m = q.get("metrics") or {}
            return f"{name}运行正常。温度{m.get('temp_c')}度，振动{m.get('vibration_rms')}。"
        m = q.get("metrics") or {}
        return (
            f"{name}当前故障。"
            f"类型{m.get('fault_label') or q.get('fault_site') or '未知'}，"
            f"温度{m.get('temp_c')}度，振动{m.get('vibration_rms')}。"
        )
    if q.get("code") == 0 and isinstance(q.get("data"), dict):
        d = q["data"]
        name = d.get("device_name", f"{device_id}号设备")
        if d.get("status") == "normal":
            return f"{name}运行正常。温度{d.get('temperature')}度，振动有效值{d.get('vibration_rms')}。"
        return (
            f"{name}当前{d.get('status_text', '异常')}。"
            f"故障类型{d.get('fault_type') or '未知'}，位置{d.get('fault_location') or '未知'}。"
        )
    return f"查询失败：{q.get('msg') or q.get('error') or q}"


def handle_utterance(text: str) -> dict[str, Any]:
    global _LAST_DEVICE_ID
    text = _normalize(text)
    if not text:
        return {"ok": False, "silent": True, "msg": "空文本", "reply": ""}

    # PC 大屏统一命令（电机/报告/串口/分析/选设备）优先匹配，见 pc_commands.py
    pc_cmd = parse_pc_command(text)
    if pc_cmd:
        out = execute_pc_command(pc_cmd)
        out["text"] = text
        return out

    if any(k in text for k in ("检查连通", "系统在线", "ping", "连通", "测网")):
        result = ping_system()
        gw_ok = isinstance(result.get("gateway"), dict) and result["gateway"].get("ok") is not False
        pc_ok = isinstance(result.get("pc_display"), dict) and result["pc_display"].get("ok")
        reply = f"网关{'正常' if gw_ok else '异常'}，大屏{'正常' if pc_ok else '异常'}。"
        return {"ok": True, "tool": "ping_system", "result": result, "reply": reply}

    device_id = _parse_device_id(text)
    fault = _parse_fault(text)
    want_show = any(
        k in text
        for k in ("显示", "大屏", "画面", "切换", "展示", "切到", "打开", "打到", "投屏", "演示")
    ) or (fault is not None)
    want_query = any(
        k in text
        for k in ("查", "怎么样", "怎样", "如何", "状态", "情况", "好不好", "正不正常", "看看", "帮我看")
    )

    # 没听清编号：有故障/查询意图则用电脑大屏当前选中的设备，取不到才沿用上次
    if device_id is None and (want_show or want_query or fault is not None):
        device_id = _selected_device_id() or _LAST_DEVICE_ID
        print(f"[MCP] 未听到编号，改用设备 {device_id}", flush=True)

    if device_id is None:
        return {
            "ok": False,
            "silent": True,
            "msg": "未识别指令",
            "reply": "",
        }

    steps: list[dict[str, Any]] = []

    if want_show and not want_query:
        site = fault or "bearing"
        state, site = _to_pc_fault(site)
        metrics = {
            "vibration_rms": 0.05 if state == "normal" else 0.45,
            "temp_c": 42.0 if state == "normal" else 68.0,
            "fault_label": FAULT_LABEL.get(site, site),
        }
        shown = show_device_3d(device_id, state=state, fault_site=site, metrics=metrics)
        steps.append({"tool": "show_device_3d", "args": {"device_id": device_id, "fault": site}, "result": shown})
        if state == "normal":
            reply = f"已将{device_id}号设备大屏切换为正常。"
        else:
            reply = f"已在大屏显示{device_id}号{FAULT_LABEL.get(site, site)}。"
        return {"ok": True, "text": text, "steps": steps, "reply": reply}

    if not want_query and not want_show:
        return {"ok": False, "silent": True, "msg": "非指令", "reply": ""}

    q = query_device(device_id)
    steps.append({"tool": "query_device", "args": {"device_id": device_id}, "result": q})
    reply = _reply_from_query(q, device_id)

    state, site = "normal", "none"
    metrics: dict[str, Any] = {}
    if q.get("ok") and q.get("state"):
        state = "normal" if q.get("state") == "normal" else "fault"
        site = "none" if state == "normal" else str(q.get("fault_site") or "bearing")
        metrics = dict(q.get("metrics") or {})
    elif q.get("code") == 0 and isinstance(q.get("data"), dict):
        data = q["data"]
        state = "normal" if data.get("status") == "normal" else "fault"
        site = "none" if state == "normal" else "bearing"
        metrics = {
            "vibration_rms": data.get("vibration_rms"),
            "temp_c": data.get("temperature"),
            "fault_label": data.get("fault_type") or data.get("status_text"),
        }
    shown = show_device_3d(device_id, state=state, fault_site=site, metrics=metrics or None)
    steps.append({"tool": "show_device_3d", "args": {"device_id": device_id, "fault": site}, "result": shown})
    return {"ok": True, "text": text, "steps": steps, "reply": reply}


def handle_voice_text(asr_text: str) -> str:
    out = handle_utterance(asr_text)
    return str(out.get("reply") or "")


def main() -> None:
    text = " ".join(sys.argv[1:]).strip() if len(sys.argv) > 1 else input("ASR文字: ").strip()
    out = handle_utterance(text)
    print(json.dumps(out, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
