"""根据 LLM / 规则意图执行查网关、控大屏。"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any


def _ensure_mcp(mcp_dir: str | None) -> Path | None:
    candidates = []
    if mcp_dir:
        candidates.append(Path(mcp_dir))
    here = Path(__file__).resolve().parent
    candidates.extend(
        [
            here.parent / "tools" / "orange_pi_mcp",
            Path.home() / "orange_pi_mcp",
        ]
    )
    for p in candidates:
        p = p.resolve()
        if (p / "tools_impl.py").is_file():
            if str(p) not in sys.path:
                sys.path.insert(0, str(p))
            return p
    return None


_PC_ACTIONS = {
    "pc_state",
    "select_device",
    "start_motor",
    "stop_motor",
    "analyze",
    "generate_report",
    "read_report",
    "refresh_serial",
    "select_serial",
    "metrics",
}

# 工况 / 置信度 / 耗时 / 五类概率 / 寄存器：由 orange_pi_mcp 的中文规则回答
_VOICE_EXTRA_ACTIONS = {"summary", "explain", "register", "registers_all"}


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


def parse_local_command(text: str, mcp_dir: str | None = None) -> dict[str, Any] | None:
    """PC 大屏指令（电机/报告/串口/分析/选设备）走本地规则，命中就不调大模型。"""
    root = _ensure_mcp(mcp_dir)
    if root is None:
        return None
    # 先看工况模型相关问法（当前工况 / 置信度 / 耗时 / 五类概率 / 寄存器），
    # 这些词里含「分析」「停机」，必须先于大屏命令判断，否则会被当成刷新分析或停机。
    try:
        from voice_controller import parse_npu_query  # type: ignore
    except Exception:  # noqa: BLE001
        intent = None
    else:
        intent = parse_npu_query(text)
    if intent and intent.get("action") in _VOICE_EXTRA_ACTIONS:
        intent["_voice_extra"] = True
        return intent

    try:
        from pc_commands import parse_pc_command  # type: ignore
    except Exception:  # noqa: BLE001
        return None
    return parse_pc_command(text)


def execute_intent(
    intent: dict[str, Any],
    mcp_dir: str | None = None,
    *,
    pc_enabled: bool = True,
) -> dict[str, Any]:
    action = intent.get("action") or "ignore"
    if action in {"ignore"}:
        return {"ok": False, "silent": True, "reply": "", "steps": []}
    if action == "wake_only":
        return {"ok": True, "silent": False, "reply": intent.get("reply") or "在", "steps": []}

    root = _ensure_mcp(mcp_dir)
    if root is None:
        return {
            "ok": False,
            "silent": False,
            "reply": "未找到 orange_pi_mcp 工具包",
            "steps": [],
        }

    if action in _PC_ACTIONS:
        try:
            from pc_commands import execute_pc_command  # type: ignore
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "reply": f"PC 大屏模块加载失败：{exc}", "steps": []}
        return execute_pc_command(intent)

    if intent.get("_voice_extra"):
        try:
            from voice_controller import run_npu_intent  # type: ignore
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "reply": f"语音规则模块加载失败：{exc}", "steps": []}
        out = run_npu_intent(intent)
        return {
            "ok": bool(out.get("ok")),
            "reply": str(out.get("reply") or ""),
            "steps": [],
            "data": None,
        }

    from tools_impl import ping_system, query_device, show_device_3d  # type: ignore

    steps: list[dict[str, Any]] = []
    if action == "ping":
        result = ping_system()
        steps.append({"tool": "ping_system", "result": result})
        gw_ok = isinstance(result.get("gateway"), dict) and result["gateway"].get("ok") is not False
        pc_ok = isinstance(result.get("pc_display"), dict) and result["pc_display"].get("ok")
        reply = intent.get("reply") or f"网关{'正常' if gw_ok else '异常'}，大屏{'正常' if pc_ok else '异常'}。"
        return {"ok": True, "reply": reply, "steps": steps, "data": None}

    device_id = intent.get("device_id")
    if device_id in (None, "", 0):
        # 没说几号机：用电脑大屏当前选中的设备，取不到才退回 3 号
        device_id = _selected_device_id() or 3
    try:
        device_id = int(device_id)
    except (TypeError, ValueError):
        device_id = 3

    if action == "query":
        q: dict[str, Any] | None = None

        # 优先从 PC 的 /api/state 取（只依赖 8760）；失败再回退网关 9000
        if pc_enabled:
            try:
                from pc_commands import query_device_via_pc  # type: ignore

                pc_q = query_device_via_pc(device_id)
            except Exception as exc:  # noqa: BLE001
                print(f"[PC] 状态接口异常：{exc}", flush=True)
                pc_q = None
            if pc_q and pc_q.get("code") == 0:
                q = pc_q
                steps.append({"tool": "pc_state", "args": {"device_id": device_id}, "result": "ok"})
            elif pc_q and pc_q.get("msg") == "device not found":
                return {
                    "ok": False,
                    "reply": f"PC 上没有{device_id}号设备的数据。",
                    "steps": steps,
                    "data": None,
                }
            elif pc_q:
                print(f"[PC] 状态接口失败，回退 9000：{pc_q.get('msg')}", flush=True)

        if q is None:
            q = query_device(device_id)
            steps.append({"tool": "query_device", "args": {"device_id": device_id}, "result": q})

        if q.get("ok") is False or q.get("error"):
            host = q.get("host") or "?"
            port = q.get("port") or 9000
            reply = (
                f"连不上网关{host}:{port}，PC 的 8760 也没取到数据。"
                "请确认对方服务已开、防火墙已放行。"
            )
            return {"ok": False, "reply": reply, "steps": steps, "data": None}
        if q.get("code") not in (0, None):
            return {
                "ok": False,
                "reply": f"没有找到{device_id}号设备的数据。",
                "steps": steps,
                "data": None,
            }
        d0 = q.get("data") if isinstance(q.get("data"), dict) else {}
        if str(d0.get("status") or "") == "offline":
            label = str(d0.get("fault_type") or "等待串口数据")
            return {
                "ok": True,
                "reply": f"{device_id}号设备暂无数据（{label}）。",
                "steps": steps,
                "data": None,
            }

        reply = _format_query(q, device_id)
        _state, site, _metrics = _from_query(q)
        data = _data_for_display(q, device_id, site)
        return {"ok": True, "reply": reply, "steps": steps, "data": data}

    if action == "show":
        # PC 端已禁用 /api/show：诊断与故障部位只能由板端数据决定，禁止语音覆盖
        return {
            "ok": False,
            "reply": "不能通过语音修改故障或诊断结果，只能查询、选择设备或控制电机。",
            "steps": [{"tool": "show_device_3d", "skipped": True, "reason": "diagnosis_override_disabled"}],
            "data": None,
        }

    return {"ok": False, "silent": True, "reply": "", "steps": []}


def _num(value: Any) -> str:
    try:
        return f"{float(value):.2f}".rstrip("0").rstrip(".")
    except (TypeError, ValueError):
        return str(value)


def _format_query(q: dict[str, Any], device_id: int) -> str:
    if q.get("ok") and "state" in q:
        name = q.get("name") or f"{device_id}号设备"
        m = q.get("metrics") or {}
        if q.get("state") == "normal":
            return f"{name}运行正常。温度{m.get('temp_c')}度，振动{m.get('vibration_rms')}。"
        return (
            f"{name}当前故障。类型{m.get('fault_label') or q.get('fault_site')}，"
            f"温度{m.get('temp_c')}度，振动{m.get('vibration_rms')}。"
        )
    if q.get("code") == 0 and isinstance(q.get("data"), dict):
        d = q["data"]
        name = d.get("device_name") or f"{device_id}号设备"
        bits: list[str] = []
        if d.get("temperature") is not None:
            bits.append(f"温度{_num(d['temperature'])}度")
        if d.get("vibration_rms") is not None:
            bits.append(f"振动{_num(d['vibration_rms'])}")
        tail = ("，" + "，".join(bits)) if bits else ""
        if d.get("status") == "normal":
            return f"{name}运行正常。{'，'.join(bits)}。" if bits else f"{name}运行正常。"
        fault = str(d.get("fault_type") or "未知")
        loc = str(d.get("fault_location") or "")
        desc = f"故障{fault}" + (f"，位置{loc}" if loc else "")
        return f"{name}当前{d.get('status_text') or '异常'}。{desc}{tail}。"
    return "查询失败。"


_SITE_CODES = (
    (("轴承", "bearing"), "bearing"),
    (("联轴", "coupling"), "coupling"),
    (("不平衡", "转子", "imbalance"), "imbalance"),
    (("地脚", "松动", "底座", "looseness"), "looseness"),
)


def _site_code(d: dict[str, Any]) -> str:
    code = str(d.get("fault_site") or "").lower()
    if code in {"bearing", "coupling", "imbalance", "looseness", "misalignment"}:
        return code
    text = f"{d.get('fault_location') or ''} {d.get('fault_type') or ''}".lower()
    for keys, code in _SITE_CODES:
        if any(k in text for k in keys):
            return code
    return "bearing"


def _from_query(q: dict[str, Any]) -> tuple[str, str, dict[str, Any] | None]:
    if q.get("ok") and q.get("state"):
        state = "normal" if q.get("state") == "normal" else "fault"
        site = "none" if state == "normal" else str(q.get("fault_site") or "bearing")
        return state, site, dict(q.get("metrics") or {})
    if q.get("code") == 0 and isinstance(q.get("data"), dict):
        d = q["data"]
        state = "normal" if d.get("status") == "normal" else "fault"
        site = "none" if state == "normal" else _site_code(d)
        return state, site, {
            "vibration_rms": d.get("vibration_rms"),
            "temp_c": d.get("temperature"),
            "fault_label": d.get("fault_type") or d.get("status_text"),
        }
    return "fault", "bearing", None


def _data_for_display(q: dict[str, Any], device_id: int, site: str) -> dict[str, Any]:
    if q.get("ok") and "device_id" in q:
        return {
            "device_id": q.get("device_id") or device_id,
            "status": "normal" if q.get("state") == "normal" else "fault_major",
            "fault_location": site,
        }
    if q.get("code") == 0 and isinstance(q.get("data"), dict):
        return q["data"]
    return {"device_id": device_id, "status": "fault_major", "fault_location": site}
