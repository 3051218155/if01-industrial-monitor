"""PC Qt 大屏统一 HTTP 接口（:8760）—— 语音规则解析与执行。

接口定义见 `docs/AI交接_香橙派语音机器人.md` §12：
  GET  /api/state    当前串口、设备、六台设备状态、RMS/阈值/告警
  GET  /api/report   测试报告（markdown）
  POST /api/command  统一命令（select_device / start_motor / ...）
  POST /api/show     兼容原 3D 切换接口

本模块只依赖 `pc_display` 配置；IP 未定时会返回可读错误，不会抛异常。
"""
from __future__ import annotations

import re
from typing import Any

import requests

PC_ACTIONS = {
    "pc_state",
    "select_device",
    "start_motor",
    "stop_motor",
    "analyze",
    "generate_report",
    "read_report",
    "refresh_serial",
    "select_serial",
}

_PUNCT = "。.!！？?，,、；;：:"
_ASR_FIXES = (
    ("三好", "三号"),
    ("二好", "二号"),
    ("一好", "一号"),
    ("四好", "四号"),
    ("五好", "五号"),
    ("六好", "六号"),
    ("轴成", "轴承"),
    ("轴程", "轴承"),
    ("连轴", "联轴"),
    ("地角", "地脚"),
)
_CN_NUM = {"一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5, "六": 6}


# ---------------------------------------------------------------- 规则解析


def _normalize(text: str) -> str:
    t = (text or "").strip()
    for ch in _PUNCT:
        t = t.replace(ch, "")
    for a, b in _ASR_FIXES:
        t = t.replace(a, b)
    return t.strip()


def _parse_pwm(text: str) -> int | None:
    m = re.search(r"pwm\s*[=:：]?\s*(\d{1,4})", text, re.I)
    if not m:
        m = re.search(r"占空比\s*(\d{1,4})", text)
    if not m:
        m = re.search(r"(?<!\d)(\d{2,4})(?!\d)", text)
    if not m:
        return None
    return max(0, min(1000, int(m.group(1))))


def _parse_any_device_id(text: str) -> int | None:
    m = re.search(r"([一二三四五六两123456])\s*号", text)
    if not m:
        return None
    token = m.group(1)
    if token in _CN_NUM:
        return _CN_NUM[token]
    try:
        return int(token)
    except ValueError:
        return None


# 指标关键词 → 内部指标名（长词优先，避免「振动RMS」被「振动」抢先）
_METRIC_RULES: tuple[tuple[tuple[str, ...], str], ...] = (
    (("振动滤波值", "滤波值", "filtered"), "vibration_filtered"),
    (("振动原始值", "原始振动", "原始值", "raw"), "vibration_raw"),
    (("振动有效值", "有效振动值", "振动均方根", "均方根", "振动值", "振动rms", "rms", "有效值"), "vibration_rms"),
    (("振动峰值", "峰值", "peak"), "vibration_peak"),
    (("平均绝对值", "平均幅值", "mean_abs"), "mean_abs"),
    (("过零", "zero_cross"), "zero_crossings"),
    (("告警等级", "alert_level"), "alert_level"),
    (("低阈值", "预警阈值", "alert_low"), "alert_low"),
    (("高阈值", "严重阈值", "alert_high"), "alert_high"),
    (("传感器状态", "sensor_status"), "sensor_status"),
    (("who_am_i", "whoami", "器件编号", "器件id"), "whoami"),
    (("健康度", "health"), "health"),
    (("温度", "temperature"), "temperature"),
    (("电流", "current"), "current"),
    (("数据来源", "来源", "source"), "source"),
)


def parse_metric_query(text: str) -> dict[str, Any] | None:
    """识别「1号电机振动RMS和峰值」这类指标查询；不匹配返回 None。"""
    t = _normalize(text).lower()
    if not t:
        return None
    device_id = _parse_any_device_id(t) or 1
    if any(k in t for k in ("全部诊断", "全部参数", "详细数据", "所有指标")):
        return {
            "action": "metrics",
            "device_id": device_id,
            "metrics": ["__all__"],
            "reply": "正在读取全部诊断参数",
            "source": "pc_rule",
        }
    metrics: list[str] = []
    for keys, name in _METRIC_RULES:
        if any(k in t for k in keys) and name not in metrics:
            metrics.append(name)
    if not metrics:
        return None
    return {
        "action": "metrics",
        "device_id": device_id,
        "metrics": metrics,
        "reply": "正在查询指标",
        "source": "pc_rule",
    }


def _parse_device_id(text: str) -> int | None:
    m = re.search(
        r"(?:选择|选中|切换到|切到|定位到|查看|看看|第)\s*([一二三四五六两123456])\s*号?", text
    )
    if not m:
        return None
    token = m.group(1)
    if token in _CN_NUM:
        return _CN_NUM[token]
    try:
        return int(token)
    except ValueError:
        return None


def parse_pc_command(text: str) -> dict[str, Any] | None:
    """把一句话映射成 PC 命令；不匹配返回 None（交给原有 query/show 流程）。"""
    t = _normalize(text)
    if not t:
        return None

    # 报告：按参考文档，「打开报告」= 生成并打开
    if "报告" in t:
        if re.search(r"(生成|导出|产出|打开|写一?份|做一?份)", t):
            return {"action": "generate_report", "reply": "正在生成测试报告", "source": "pc_rule"}
        if re.search(r"(读取|读|查看|念|播报|内容)", t):
            return {"action": "read_report", "reply": "正在读取测试报告", "source": "pc_rule"}

    # 串口
    m = re.search(r"com\s*(\d+)", t, re.I)
    if m and re.search(r"(连接|连上|接入|切到|选择|打开)", t):
        return {
            "action": "select_serial",
            "port": f"COM{int(m.group(1))}",
            "reply": "正在连接串口",
            "source": "pc_rule",
        }
    if "串口" in t and re.search(r"(刷新|扫描|重新|列表|有哪些)", t):
        return {"action": "refresh_serial", "reply": "正在刷新串口", "source": "pc_rule"}

    # 电机：先判断停止，避免「停止电机」被当成启动
    if "停机" in t:
        return {"action": "stop_motor", "reply": "正在停机", "source": "pc_rule"}
    if re.search(r"(电机|马达)", t):
        if re.search(r"(停止|停下|停掉|停转|关掉|关闭|关|断电)", t):
            return {"action": "stop_motor", "reply": "正在停止电机", "source": "pc_rule"}
        if re.search(r"(启动|开启|开动|转起来|运行|开)(一下|起来)?\s*(电机|马达)", t) or re.search(
            r"(电机|马达).{0,4}(开起来|启动|转起来|运行起来)", t
        ):
            return {
                "action": "start_motor",
                "pwm": _parse_pwm(t),
                "reply": "正在启动电机",
                "source": "pc_rule",
            }

    # 全部诊断参数：优先于「诊断 → analyze」
    if any(k in t for k in ("全部诊断", "全部参数", "详细数据", "所有指标")):
        diag = parse_metric_query(t)
        if diag:
            return diag

    # 分析 / 诊断
    if "分析" in t or (
        "诊断" in t and any(k in t for k in ("刷新", "重新", "板端", "全部", "系统", "一下"))
    ):
        return {"action": "analyze", "reply": "正在刷新分析", "source": "pc_rule"}

    # 指标查询（振动滤波值 / RMS / 温度 …），优先于一般状态查询
    metric_cmd = parse_metric_query(t)
    if metric_cmd:
        return metric_cmd

    # 选择设备
    device_id = _parse_device_id(t)
    if device_id and any(k in t for k in ("选择", "选中", "切换到", "切到", "定位到", "查看", "看看")):
        return {
            "action": "select_device",
            "device_id": device_id,
            "reply": f"正在选择{device_id}号设备",
            "source": "pc_rule",
        }

    # 系统总状态（与单台 query 区分）
    if any(k in t for k in ("系统状态", "全部设备", "所有设备", "六台", "6台", "设备列表", "大屏状态")):
        return {"action": "pc_state", "reply": "正在获取系统状态", "source": "pc_rule"}
    if "状态" in t and any(k in t for k in ("系统", "全部", "所有", "大屏")):
        return {"action": "pc_state", "reply": "正在获取系统状态", "source": "pc_rule"}

    return None


# ---------------------------------------------------------------- HTTP 封装


def _load_cfg(cfg: dict[str, Any] | None) -> dict[str, Any]:
    if cfg:
        return cfg
    from tools_impl import load_config  # 同目录，延迟导入避免循环

    return load_config()


def _pc_conf(cfg: dict[str, Any] | None) -> tuple[str, int, float]:
    c = _load_cfg(cfg)
    pc = c.get("pc_display") or {}
    host = str(pc.get("host") or "").strip()
    port = int(pc.get("port", 8760))
    timeout = float(pc.get("timeout_sec", 5))
    return host, port, timeout


def _base(cfg: dict[str, Any] | None = None) -> str:
    host, port, _ = _pc_conf(cfg)
    if not host:
        raise RuntimeError("pc_display.host 未配置（填实际 IP，或 main.py --pc IP）")
    return f"http://{host}:{port}"


def _wrap(resp: requests.Response) -> dict[str, Any]:
    try:
        data: Any = resp.json()
    except ValueError:
        data = {"text": resp.text[:800]}
    return {"ok": 200 <= resp.status_code < 300, "http_status": resp.status_code, "data": data}


def _err(exc: Exception, path: str, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    host, port, _ = _pc_conf(cfg)
    return {
        "ok": False,
        "error": str(exc),
        "url": f"http://{host}:{port}{path}" if host else path,
        "hint": "PC 大屏没启动 / IP 或端口不对 / 防火墙没放行 8760",
    }


def get_state(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    try:
        return _wrap(requests.get(f"{_base(cfg)}/api/state", timeout=_pc_conf(cfg)[2]))
    except Exception as exc:  # noqa: BLE001
        return _err(exc, "/api/state", cfg)


def get_report(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    try:
        return _wrap(requests.get(f"{_base(cfg)}/api/report", timeout=_pc_conf(cfg)[2]))
    except Exception as exc:  # noqa: BLE001
        return _err(exc, "/api/report", cfg)


def health(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    try:
        return _wrap(requests.get(f"{_base(cfg)}/health", timeout=_pc_conf(cfg)[2]))
    except Exception as exc:  # noqa: BLE001
        return _err(exc, "/health", cfg)


def post_command(payload: dict[str, Any], cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    try:
        return _wrap(
            requests.post(f"{_base(cfg)}/api/command", json=payload, timeout=_pc_conf(cfg)[2])
        )
    except Exception as exc:  # noqa: BLE001
        return _err(exc, "/api/command", cfg)


# ---------------------------------------------------------------- 执行与话术


def _ok(r: dict[str, Any]) -> bool:
    data = r.get("data")
    if isinstance(data, dict) and data.get("ok") is False:
        return False
    return bool(r.get("ok"))


def _fail(intent: dict[str, Any], r: dict[str, Any]) -> dict[str, Any]:
    reason = r.get("error") or (r.get("data") or {}).get("error") or "未知错误"
    return {
        "ok": False,
        "reply": f"PC 大屏操作失败：{reason}",
        "steps": [{"tool": intent.get("action"), "result": r}],
        "data": None,
    }


def _state_reply(r: dict[str, Any]) -> str:
    d = r.get("data")
    if isinstance(d, dict) and isinstance(d.get("data"), dict):
        d = d["data"]
    if not isinstance(d, dict):
        return "已获取系统状态。"
    parts: list[str] = []
    cur = d.get("selected_device_id") or d.get("device_id") or d.get("current_device") or d.get("current")
    if cur:
        parts.append(f"当前{cur}号设备")

    serial = d.get("serial")
    if isinstance(serial, dict):
        port = serial.get("port")
        if port:
            parts.append(f"串口{port}{'在线' if serial.get('online') else '离线'}")
    elif isinstance(serial, str) and serial:
        parts.append(f"串口{serial}")

    devices = d.get("devices") or d.get("device_list")
    if isinstance(devices, list):
        parts.append(f"共{len(devices)}台设备")
        online = [i for i in devices if isinstance(i, dict) and i.get("has_data") is not False]
        faults = [
            i for i in devices
            if isinstance(i, dict) and _device_status(i) not in {"normal", "offline"}
        ]
        parts.append(f"{len(online)}台有数据")
        parts.append(f"{len(faults)}台告警" if faults else "无告警")
    return "，".join(parts) + "。" if parts else "已获取系统状态。"


def _report_reply(r: dict[str, Any]) -> str:
    d = r.get("data")
    if isinstance(d, dict) and isinstance(d.get("data"), dict):
        d = d["data"]
    text = ""
    if isinstance(d, dict):
        text = str(d.get("report") or d.get("markdown") or d.get("content") or d.get("text") or "")
    elif isinstance(d, str):
        text = d
    if not text:
        return "报告已读取。"
    text = text.replace("\r\n", "\n").strip()
    return f"报告内容：{text[:600]}"


# ---------------------------------------------------------------- 单台设备查询
# 走 /api/state 取单台设备，返回文档 §5.3 的 code/data 结构，
# 这样 intent_exec 里原有的格式化/3D 联动都能直接复用。

_DEVICE_LIST_KEYS = ("devices", "device_list", "deviceList", "list", "items")
_STATUS_CN = {
    "normal": "正常",
    "fault_minor": "轻微故障",
    "fault_major": "严重故障",
    "offline": "无数据",
}


def _pick(d: dict[str, Any], *keys: str) -> Any:
    for k in keys:
        if k in d and d[k] not in (None, ""):
            return d[k]
    return None


def _norm_status(raw: Any) -> str:
    if raw is None:
        return "normal"
    s = str(raw).strip().lower()
    if s in {"normal", "ok", "0", "正常", "good", "fine", "none"}:
        return "normal"
    if s in {"1", "minor", "fault_minor", "轻微", "轻微故障", "warning", "warn"}:
        return "fault_minor"
    if s in {"2", "major", "severe", "fault_major", "严重", "严重故障", "alarm", "fault", "error"}:
        return "fault_major"
    return "fault_major"


def _find_device(state: dict[str, Any], device_id: int) -> dict[str, Any] | None:
    items: list[Any] | None = None
    for k in _DEVICE_LIST_KEYS:
        v = state.get(k)
        if isinstance(v, list):
            items = v
            break
    if items is None:
        # 接口只返回当前设备时，顶层就是设备
        did = _pick(state, "device_id", "id", "dev_id")
        try:
            return state if did is not None and int(did) == device_id else None
        except (TypeError, ValueError):
            return None
    for it in items:
        if not isinstance(it, dict):
            continue
        did = _pick(it, "device_id", "id", "dev_id", "index", "no", "num")
        try:
            if did is not None and int(did) == device_id:
                return it
        except (TypeError, ValueError):
            continue
    return None


def _device_status(dev: dict[str, Any]) -> str:
    """优先看 state，其次 alert_level；has_data=false 视为无数据。"""
    if dev.get("has_data") is False:
        return "offline"
    level = dev.get("alert_level")
    if level is not None and str(level) == "-1":
        return "offline"
    state = str(dev.get("state") or "").strip()
    if state:
        mapped = _norm_status(state)
        # state 说正常但 alert_level 给 1/2 时，以告警级别为准
        if mapped == "normal" and str(level) in {"1", "2"}:
            return "fault_minor" if str(level) == "1" else "fault_major"
        return mapped
    return _norm_status(level)


def _fault_short(dev: dict[str, Any]) -> str:
    label = str(_pick(dev, "fault_label", "fault_type", "fault") or "")
    return label.split("·")[0].strip()


def _normalize_device(dev: dict[str, Any], device_id: int) -> dict[str, Any]:
    status = _device_status(dev)
    site_code = str(_pick(dev, "fault_site", "fault_location", "site") or "")
    site_text = str(_pick(dev, "fault_site_text", "fault_location_text") or "")
    if site_code.lower() in {"none", "normal", "无"}:
        site_code = ""
    if site_text in {"无", "none", "normal"}:
        site_text = ""
    return {
        "device_id": device_id,
        "device_name": _pick(dev, "device_name", "name", "title") or f"{device_id}号设备",
        "status": status,
        "status_text": _STATUS_CN[status],
        "temperature": _pick(dev, "temperature_c", "temperature", "temp_c", "temp"),
        "vibration_rms": _pick(dev, "vibration_rms", "rms", "vib_rms", "vibration"),
        "vibration_peak": _pick(dev, "vibration_peak", "peak", "vib_peak"),
        "current_a": _pick(dev, "current_a", "current"),
        "health_percent": _pick(dev, "health_percent", "health"),
        "fault_location": site_text or site_code,
        "fault_site": site_code,
        "fault_type": _fault_short(dev),
        "inference_time_ms": _pick(dev, "inference_time_ms", "infer_ms", "infer_time"),
    }


def query_device_via_pc(device_id: int, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    """从 PC `/api/state` 取单台设备；失败返回 code=-1，由调用方回退 TCP。"""
    r = get_state(cfg)
    if not _ok(r):
        return {"code": -1, "msg": str(r.get("error") or "PC 状态接口不可用"), "data": {}}

    state = r.get("data")
    if isinstance(state, dict) and isinstance(state.get("data"), dict):
        state = state["data"]
    if not isinstance(state, dict):
        return {"code": -1, "msg": "PC /api/state 返回格式不认识", "data": {}}

    dev = _find_device(state, device_id)
    if dev is None:
        print(f"[PC] /api/state 里没有 {device_id} 号设备；顶层键：{list(state)[:12]}", flush=True)
        return {"code": 1, "msg": "device not found", "data": {}}
    return {"code": 0, "msg": "ok", "data": _normalize_device(dev, device_id)}


def _state_dict(cfg: dict[str, Any] | None = None) -> tuple[dict[str, Any] | None, dict[str, Any] | None]:
    r = get_state(cfg)
    if not _ok(r):
        return None, {"ok": False, "error": str(r.get("error") or "PC 状态接口不可用")}
    state = r.get("data")
    if isinstance(state, dict) and isinstance(state.get("data"), dict):
        state = state["data"]
    if not isinstance(state, dict):
        return None, {"ok": False, "error": "PC /api/state 返回格式不认识"}
    return state, None


_METRIC_LABEL = {
    "vibration_raw": "振动原始值",
    "vibration_filtered": "振动滤波值",
    "vibration_rms": "振动有效值",
    "vibration_peak": "振动峰值",
    "mean_abs": "平均绝对值",
    "zero_crossings": "过零次数",
    "temperature": "温度",
    "current": "电流",
    "alert_level": "告警等级",
    "alert_low": "低告警阈值",
    "alert_high": "高告警阈值",
    "sensor_status": "传感器状态",
    "whoami": "器件编号",
    "health": "健康度",
    "source": "通道状态",
}
_SOURCE_CN = {"real": "在线", "simulated": "在线", "remote": "远程", "unknown": "未知"}
_ALERT_CN = {0: "正常", 1: "预警", 2: "严重"}


def _fmt(value: Any) -> str:
    try:
        return f"{float(value):.4f}".rstrip("0").rstrip(".")
    except (TypeError, ValueError):
        return str(value)


def _num(value: Any, digits: int) -> str:
    """播报用数字：固定小数位，去掉多余的 0。"""
    try:
        text = f"{float(value):.{digits}f}"
    except (TypeError, ValueError):
        return str(value)
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text or "0"


def _signed16(value: Any) -> int | None:
    try:
        n = int(value)
    except (TypeError, ValueError):
        return None
    return n - 0x10000 if n >= 0x8000 else n


def _accel(value: Any) -> str | None:
    s = _signed16(value)
    if s is None:
        return None
    return f"{_num(s / 16384 * 9.80665, 3)}米每秒平方（原始码{s}）"


def _source_of(dev: dict[str, Any], metric: str) -> str:
    if metric == "temperature":
        raw = _pick(dev, "temperature_source", "source")
    elif metric == "current":
        raw = _pick(dev, "current_source", "source")
    else:
        raw = _pick(dev, "vibration_source", "source")
    text = str(raw or "").strip()
    if not text:
        return ""
    return _SOURCE_CN.get(text.lower(), text)


def _metric_value(dev: dict[str, Any], metric: str) -> tuple[str | None, str]:
    """返回 (可直接播报的值文本, 来源)；不可用返回 (None, "")。"""
    src = _source_of(dev, metric)
    if metric == "vibration_raw":
        return _accel(_pick(dev, "raw_lsb", "vibration_raw_lsb", "raw")), src
    if metric == "vibration_filtered":
        return _accel(_pick(dev, "filtered_lsb", "vibration_filtered_lsb", "filtered")), src
    if metric == "vibration_rms":
        v = _pick(dev, "vibration_rms", "rms")
        return (f"{_num(v, 3)}米每秒平方" if v is not None else None), src
    if metric == "vibration_peak":
        v = _pick(dev, "vibration_peak", "peak")
        return (f"{_num(v, 3)}米每秒平方" if v is not None else None), src
    if metric == "mean_abs":
        v = _pick(dev, "mean_abs_lsb", "mean_abs")
        return (f"{_num(v, 0)}原始码" if v is not None else None), src
    if metric == "zero_crossings":
        v = _pick(dev, "zero_crossings", "zero_cross")
        return (f"{_num(v, 0)}次" if v is not None else None), src
    if metric == "temperature":
        v = _pick(dev, "temperature_c", "temperature")
        return (f"{_num(v, 1)}摄氏度" if v is not None else None), src
    if metric == "current":
        v = _pick(dev, "current_a", "current")
        return (f"{_num(v, 2)}安" if v is not None else None), src
    if metric == "alert_level":
        v = _pick(dev, "alert_level", "level")
        if v is None:
            return None, ""
        try:
            return _ALERT_CN.get(int(v), str(v)), src
        except (TypeError, ValueError):
            return str(v), src
    if metric == "alert_low":
        return _accel(_pick(dev, "alert_low_raw", "alert_low")), src
    if metric == "alert_high":
        return _accel(_pick(dev, "alert_high_raw", "alert_high")), src
    if metric == "sensor_status":
        v = _pick(dev, "sensor_status")
        return (f"{int(v)}" if v is not None else None), src
    if metric == "whoami":
        v = _pick(dev, "whoami")
        return (f"{int(v)}" if v is not None else None), src
    if metric == "health":
        v = _pick(dev, "health_percent", "health")
        return (f"百分之{_num(v, 1)}" if v is not None else None), src
    if metric == "source":
        return (src or "未知"), ""
    return None, ""


def _execute_metrics(intent: dict[str, Any], cfg: dict[str, Any] | None) -> dict[str, Any]:
    device_id = int(intent.get("device_id") or 1)
    metrics = list(intent.get("metrics") or [])
    state, err = _state_dict(cfg)
    if err or state is None:
        return {
            "ok": False,
            "reply": f"读取设备数据失败：{(err or {}).get('error', '未知')}",
            "steps": [{"tool": "pc_state", "result": err}],
            "data": None,
        }
    dev = _find_device(state, device_id)
    if dev is None:
        return {
            "ok": False,
            "reply": f"没有找到{device_id}号设备。",
            "steps": [{"tool": "pc_state", "result": "device_not_found"}],
            "data": None,
        }

    if metrics == ["__all__"]:
        metrics = [name for _, name in _METRIC_RULES if name != "source"]
        # 去重并保持顺序
        seen: set[str] = set()
        metrics = [m for m in metrics if not (m in seen or seen.add(m))]

    name = f"{device_id}号电机"
    parts: list[str] = []
    for metric in metrics:
        value, _src = _metric_value(dev, metric)
        if value is None:
            return {
                "ok": False,
                "reply": (
                    f"{name}没有可用的{_METRIC_LABEL.get(metric, metric)}数据。"
                    "设备通道可能离线，或原始寄存器尚未上报。"
                ),
                "steps": [{"tool": "metric", "args": metric, "result": "metric_unavailable"}],
                "data": None,
            }
        parts.append(f"{_METRIC_LABEL.get(metric, metric)}为{value}")

    reply = f"{name}当前" + "；".join(parts) + "。"
    return {
        "ok": True,
        "reply": reply,
        "steps": [{"tool": "pc_state", "args": {"device_id": device_id, "metrics": metrics}, "result": "ok"}],
        "data": None,
    }


def query_device_metric(
    device_id: int, metric: str, cfg: dict[str, Any] | None = None
) -> dict[str, Any]:
    """供 MCP 调用：查询单台设备的一个指标。"""
    return _execute_metrics(
        {"action": "metrics", "device_id": device_id, "metrics": [metric]}, cfg
    )


def query_device_diagnostics(device_id: int, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    """供 MCP 调用：查询单台设备的全部可用指标。"""
    return _execute_metrics(
        {"action": "metrics", "device_id": device_id, "metrics": ["__all__"]}, cfg
    )


def execute_pc_command(
    intent: dict[str, Any], cfg: dict[str, Any] | None = None
) -> dict[str, Any]:
    """执行 PC 命令，返回与 intent_exec 一致的结构。"""
    action = str(intent.get("action") or "")

    if action == "metrics":
        return _execute_metrics(intent, cfg)

    if action == "pc_state":
        r = get_state(cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {"ok": True, "reply": _state_reply(r), "steps": [{"tool": action, "result": r}], "data": None}

    if action == "start_motor":
        payload: dict[str, Any] = {"command": "start_motor"}
        pwm = intent.get("pwm")
        if pwm:
            payload["pwm"] = int(pwm)
        r = post_command(payload, cfg)
        if not _ok(r):
            return _fail(intent, r)
        reply = f"电机已启动，PWM {pwm}。" if pwm else "电机已启动。"
        return {"ok": True, "reply": reply, "steps": [{"tool": action, "args": payload, "result": r}], "data": None}

    if action == "stop_motor":
        payload = {"command": "stop_motor"}
        r = post_command(payload, cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {"ok": True, "reply": "电机已停止。", "steps": [{"tool": action, "result": r}], "data": None}

    if action == "analyze":
        r = post_command({"command": "analyze"}, cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {"ok": True, "reply": "已刷新板端分析。", "steps": [{"tool": action, "result": r}], "data": None}

    if action == "generate_report":
        r = post_command({"command": "generate_report"}, cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {"ok": True, "reply": "测试报告已生成。", "steps": [{"tool": action, "result": r}], "data": None}

    if action == "read_report":
        r = get_report(cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {"ok": True, "reply": _report_reply(r), "steps": [{"tool": action, "result": r}], "data": None}

    if action == "refresh_serial":
        r = post_command({"command": "refresh_serial"}, cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {"ok": True, "reply": "已刷新串口列表。", "steps": [{"tool": action, "result": r}], "data": None}

    if action == "select_serial":
        port = str(intent.get("port") or "")
        r = post_command({"command": "select_serial", "port": port}, cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {"ok": True, "reply": f"已连接{port}。", "steps": [{"tool": action, "result": r}], "data": None}

    if action == "select_device":
        device_id = int(intent.get("device_id") or 1)
        r = post_command({"command": "select_device", "device_id": device_id}, cfg)
        if not _ok(r):
            return _fail(intent, r)
        return {
            "ok": True,
            "reply": f"已选择{device_id}号设备。",
            "steps": [{"tool": action, "result": r}],
            "data": None,
        }

    return {"ok": False, "silent": True, "reply": "", "steps": [], "data": None}
