#!/usr/bin/env python3
"""香橙派中文语音服务：ASR 文本 → 全中文播报 / 电脑大屏动作。

交互规范（orange_pi_interaction_reference）：
- 用户只说中文，系统也只播报中文，不念英文参数名、字段名、工具名；
- 设备号支持「一号…六号」，占空比支持「占空比五百」「百分之七十」，
  串口支持「串口二十一」「串口十」；
- 工况统一播报：停机、空载、正常带载、过载、异常；
- 通道状态只播报：在线、远程、不可用、未知；
- 没有数据就说「当前没有可用数据」，不要用零代替缺失值。

启动服务（给板子上的 ASR 进程调用）：
    python3 voice_controller.py --serve --host 0.0.0.0 --port 8780

单句命令行（验收 / 调试）：
    python3 voice_controller.py "查询三号电机状态"
    python3 voice_controller.py --json "五类工况概率"

请求：
    curl -X POST http://127.0.0.1:8780/voice \
      -H "Content-Type: application/json" \
      -d '{"text":"三号电机振动有效值是多少"}'

返回：
    {"ok":true,"text":"...","reply":"...","result":{...}}

不接服务也可以直接从标准输入喂文本（ASR 每行一条）：
    python3 voice_controller.py

数据来源：
- 设备状态 / 指标 / 工况 / 概率：电脑大屏 `GET /api/state`（8760）；
- 电机、串口、报告、分析：电脑大屏 `POST /api/command`（8760）；
- 中文口语规则在本文件内实现，`voice_mcp_bridge` 只作为兜底。
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any

import pc_commands
from tools_impl import load_config, ping_system
from voice_mcp_bridge import handle_utterance

# ---------------------------------------------------------------- 常量

CONDITION_CN = ("停机", "空载", "正常带载", "过载", "异常")

# 五类工况寄存器配置：15 号寄存器单位 0.01 安（A×100）；5 号是温度传感器原始码
REGISTER_PROFILES: dict[str, dict[str, int]] = {
    "停机": {"pwm": 0, "reg15": 0, "reg5": 2003},
    "空载": {"pwm": 900, "reg15": 85, "reg5": 4340},
    "正常带载": {"pwm": 950, "reg15": 380, "reg5": 7345},
    "过载": {"pwm": 980, "reg15": 620, "reg5": 13689},
    "异常": {"pwm": 900, "reg15": 430, "reg5": 9682},
}

REG5_DIVISOR = 333.87
REG5_OFFSET = 21.0

CONDITION_ALIASES: dict[str, str] = {
    "停机": "停机",
    "停止": "停机",
    "stop": "停机",
    "stopped": "停机",
    "shutdown": "停机",
    "halt": "停机",
    "off": "停机",
    "空载": "空载",
    "无载": "空载",
    "idle": "空载",
    "no_load": "空载",
    "no-load": "空载",
    "noload": "空载",
    "unload": "空载",
    "unloaded": "空载",
    "empty": "空载",
    "正常带载": "正常带载",
    "带载": "正常带载",
    "normal": "正常带载",
    "normal_load": "正常带载",
    "normal-load": "正常带载",
    "normalload": "正常带载",
    "load": "正常带载",
    "loaded": "正常带载",
    "running": "正常带载",
    "过载": "过载",
    "超载": "过载",
    "overload": "过载",
    "over_load": "过载",
    "over-load": "过载",
    "overloaded": "过载",
    "heavy_load": "过载",
    "异常": "异常",
    "故障": "异常",
    "abnormal": "异常",
    "anomaly": "异常",
    "fault": "异常",
    "faulty": "异常",
    "error": "异常",
}

# 通道状态：内部枚举统一成面向用户的中性说法
SOURCE_CN: dict[str, str] = {
    "real": "在线",
    "实测": "在线",
    "实测值": "在线",
    "fallback": "在线",
    "备用": "在线",
    "备用数据": "在线",
    "备用通道": "在线",
    "备用通道值": "在线",
    "simulated": "在线",
    "模拟": "在线",
    "模拟值": "在线",
    "simulated_modbus_slave_profile": "在线",
    "mixed": "在线",
    "主备混合": "在线",
    "真实和备用混合": "在线",
    "unavailable": "不可用",
    "没有数据": "不可用",
    "不可用": "不可用",
    "unknown": "未知",
    "无法确定": "未知",
    "未知": "未知",
    "remote": "远程",
    "远程": "远程",
}

_SOURCE_RE = re.compile(
    r"(?<![A-Za-z0-9_])"
    r"(simulated_modbus_slave_profile|real|fallback|simulated|mixed|unavailable|unknown|remote)"
    r"(?![A-Za-z0-9_])",
    re.I,
)

ALERT_CN = {0: "正常", 1: "预警", 2: "严重"}

SITE_CN = {
    "bearing": "轴承端",
    "imbalance": "转子不平衡",
    "looseness": "地脚松动",
    "coupling": "联轴器",
    "body": "机壳",
    "misalignment": "不对中",
}

_CN_DIGITS = {
    "零": 0,
    "〇": 0,
    "一": 1,
    "二": 2,
    "两": 2,
    "三": 3,
    "四": 4,
    "五": 5,
    "六": 6,
    "七": 7,
    "八": 8,
    "九": 9,
}

_DEVICE_RE = re.compile(r"([零〇一二三四五六七八九十两\d]{1,4})\s*号")
_PORT_RE = re.compile(r"(?:串口|com)\s*([零〇一二三四五六七八九十百两\d]{1,4})", re.I)

# 指标：关键词 → 内部名 → 中文播报名（长词在前，避免「振动」抢「振动有效值」）
_METRIC_RULES: tuple[tuple[tuple[str, ...], str, str], ...] = (
    (("振动滤波值", "滤波值", "filtered"), "vibration_filtered", "振动滤波值"),
    (("振动原始值", "原始振动", "原始值", "raw"), "vibration_raw", "振动原始值"),
    (("振动有效值", "有效振动值", "振动均方根", "振动值", "有效值", "振动rms", "rms"), "vibration_rms", "振动有效值"),
    (("振动峰值", "峰值", "peak"), "vibration_peak", "振动峰值"),
    (("平均绝对值", "平均幅值", "mean_abs"), "mean_abs", "平均幅值"),
    (("过零次数", "过零", "zero_cross"), "zero_crossings", "过零次数"),
    (("告警等级", "alert_level"), "alert_level", "告警等级"),
    (("低告警阈值", "低阈值", "预警阈值", "alert_low"), "alert_low", "低告警阈值"),
    (("高告警阈值", "高阈值", "严重阈值", "alert_high"), "alert_high", "高告警阈值"),
    (("传感器状态", "sensor_status"), "sensor_status", "传感器状态"),
    (("器件编号", "who_am_i", "whoami"), "whoami", "器件编号"),
    (("健康度", "health"), "health", "健康度"),
    (("温度", "temperature"), "temperature", "温度"),
    (("电流", "current"), "current", "电流"),
    (("数据来源", "来源", "source"), "source", "数据来源"),
)

_METRIC_LABEL = {name: label for _, name, label in _METRIC_RULES}
_ALL_METRICS = [name for _, name, _ in _METRIC_RULES if name != "source"]

_ALL_PARAM_HINTS = (
    "全部参数",
    "全部诊断",
    "所有参数",
    "所有指标",
    "全部数据",
    "所有数据",
    "详细数据",
    "诊断数据",
)

_WRITE_VERBS = ("显示", "切换为", "切换成", "改成", "改为", "设为", "设置成", "标记为")
_DIAG_WORDS = ("故障", "轴承", "不平衡", "地脚", "松动", "联轴", "异常")


# ---------------------------------------------------------------- 基础工具


def _norm_text(text: str) -> str:
    t = (text or "").strip()
    for ch in "。.!！？?，,、；;：:":
        t = t.replace(ch, "")
    return t


def _pick(data: Any, *keys: str) -> Any:
    if not isinstance(data, dict):
        return None
    for key in keys:
        value = data.get(key)
        if value not in (None, "", [], {}):
            return value
    return None


def _to_float(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _to_int(value: Any) -> int | None:
    number = _to_float(value)
    return int(number) if number is not None else None


def _fmt(value: Any, digits: int = 1) -> str:
    """数字转播报文本：保留 digits 位小数，去掉多余的 0。"""
    number = _to_float(value)
    if number is None:
        return str(value)
    text = f"{number:.{digits}f}"
    if "." in text:
        text = text.rstrip("0").rstrip(".")
    return text or "0"


def _percent(value: Any) -> float | None:
    number = _to_float(value)
    if number is None:
        return None
    return number * 100.0 if 0.0 <= number <= 1.0 else number


def _pct_text(value: Any) -> str:
    """已经是百分数的值（0~100）→ 播报文本，不再乘 100。"""
    number = _to_float(value)
    if number is None:
        return "未知"
    digits = 2 if 0 < number < 1 else 1
    return f"百分之{_fmt(number, digits)}"


def _signed16(value: Any) -> int | None:
    number = _to_int(value)
    if number is None:
        return None
    return number - 0x10000 if number >= 0x8000 else number


def _cn_number(token: str) -> int | None:
    """中文数字转整数：二十一 → 21，一百零五 → 105，五百 → 500。"""
    token = (token or "").strip()
    if not token:
        return None
    if token.isdigit():
        return int(token)
    total = 0
    current = 0
    for ch in token:
        if ch in _CN_DIGITS:
            current = _CN_DIGITS[ch]
        elif ch == "十":
            total += (current or 1) * 10
            current = 0
        elif ch == "百":
            total += (current or 1) * 100
            current = 0
        elif ch == "千":
            total += (current or 1) * 1000
            current = 0
        else:
            return None
    return total + current


def _walk_find(obj: Any, keys: tuple[str, ...], depth: int = 3) -> dict[str, Any] | None:
    """在 state 里广度优先找第一个含指定键的 dict，兼容字段位置变动。"""
    queue: list[tuple[Any, int]] = [(obj, 0)]
    visited = 0
    while queue:
        current, level = queue.pop(0)
        visited += 1
        if visited > 500:
            return None
        if isinstance(current, dict):
            if any(key in current for key in keys):
                return current
            if level < depth:
                queue.extend(
                    (value, level + 1) for value in current.values() if isinstance(value, (dict, list))
                )
        elif isinstance(current, list) and level < depth:
            queue.extend((value, level + 1) for value in current)
    return None


def reg5_to_celsius(reg5: Any) -> float | None:
    """温度原始码 → 摄氏度（不是摄氏度乘 10）。"""
    value = _to_float(reg5)
    if value is None:
        return None
    return value / REG5_DIVISOR + REG5_OFFSET


def _canonical_condition(value: Any) -> str | None:
    raw = str(value or "").strip()
    if not raw:
        return None
    if raw.isdigit() and 0 <= int(raw) < len(CONDITION_CN):
        return CONDITION_CN[int(raw)]
    for name in CONDITION_CN:
        if name in raw:
            return name
    low = raw.lower().replace(" ", "").replace("-", "_")
    if low in CONDITION_ALIASES:
        return CONDITION_ALIASES[low]
    for suffix in ("_class", "_label", "_status", "_zh"):
        if low.endswith(suffix):
            base = low[: -len(suffix)]
            if base in CONDITION_ALIASES:
                return CONDITION_ALIASES[base]
    return None


def _find_condition_in(text: str) -> str | None:
    for name in CONDITION_CN:
        if name in text:
            return name
    low = text.lower()
    for alias in sorted(CONDITION_ALIASES, key=len, reverse=True):
        if not alias:
            continue
        if alias.isascii():
            pattern = rf"(?<![a-z0-9_]){re.escape(alias)}(?![a-z0-9_])"
            if re.search(pattern, low):
                return CONDITION_ALIASES[alias]
        elif alias in text:
            return CONDITION_ALIASES[alias]
    return None


def _source_cn(value: Any) -> str:
    text = str(value or "").strip()
    if not text:
        return ""
    return SOURCE_CN.get(text, SOURCE_CN.get(text.lower(), text))


def translate_sources(text: str) -> str:
    """把回复里残留的英文来源枚举换成中文播报。"""
    if not text:
        return text

    def _repl(match: re.Match[str]) -> str:
        return SOURCE_CN.get(match.group(1).lower(), match.group(0))

    return _SOURCE_RE.sub(_repl, text)


# ---------------------------------------------------------------- 工况 / 寄存器播报


def _register_reply(condition: str) -> str:
    profile = REGISTER_PROFILES[condition]
    temperature = reg5_to_celsius(profile["reg5"])
    return (
        f"{condition}的寄存器配置：占空比{profile['pwm']}，"
        f"15号寄存器{profile['reg15']}，也就是{profile['reg15'] / 100:.2f}安；"
        f"5号寄存器{profile['reg5']}，是温度传感器原始码，约{temperature:.1f}摄氏度。"
        "注意5号寄存器不是摄氏度乘10，换算方法是温度等于原始码除以333.87，再加21。"
    )


def _registers_all_reply() -> str:
    lines = []
    for condition in CONDITION_CN:
        profile = REGISTER_PROFILES[condition]
        temperature = reg5_to_celsius(profile["reg5"])
        lines.append(
            f"{condition}，占空比{profile['pwm']}、15号寄存器{profile['reg15']}"
            f"（{profile['reg15'] / 100:.2f}安）、5号寄存器{profile['reg5']}"
            f"（约{temperature:.1f}摄氏度）"
        )
    return (
        "五个工况的寄存器配置：" + "；".join(lines) + "。"
        "15号寄存器单位是0.01安，5号寄存器是温度传感器原始码，"
        "温度等于原始码除以333.87，再加21。"
    )


# ---------------------------------------------------------------- 电脑大屏状态


def _fetch_state() -> tuple[dict[str, Any] | None, str | None]:
    try:
        resp = pc_commands.get_state()
    except Exception as exc:  # noqa: BLE001
        return None, str(exc)
    if not resp.get("ok"):
        return None, str(resp.get("error") or f"HTTP {resp.get('http_status')}")
    state = resp.get("data")
    if not isinstance(state, dict):
        return None, "电脑大屏返回格式不认识"
    if "npu" not in state and isinstance(state.get("data"), dict):
        state = state["data"]
    return state, None


def _selected_device_id(state: dict[str, Any]) -> int | None:
    selected = _to_int(_pick(state, "selected_device_id", "device_id"))
    return selected if selected and 1 <= selected <= 6 else None


def _device_by_id(state: dict[str, Any], device_id: int) -> dict[str, Any] | None:
    devices = state.get("devices")
    if isinstance(devices, list):
        for dev in devices:
            if not isinstance(dev, dict):
                continue
            if str(_pick(dev, "device_id", "id", "dev_id", "index")) == str(device_id):
                return dev
    dev = state.get("device")
    if isinstance(dev, dict) and str(_pick(dev, "device_id", "id")) == str(device_id):
        return dev
    if device_id == 1 and isinstance(dev, dict):
        return dev
    return None


def _current_device(state: dict[str, Any]) -> dict[str, Any]:
    for key in ("device", "current_device", "selected_device"):
        dev = state.get(key)
        if isinstance(dev, dict):
            return dev
    devices = state.get("devices")
    selected = _selected_device_id(state)
    if isinstance(devices, list):
        if selected:
            for dev in devices:
                if isinstance(dev, dict) and str(
                    _pick(dev, "device_id", "id", "dev_id", "index")
                ) == str(selected):
                    return dev
        for dev in devices:
            if isinstance(dev, dict):
                return dev
    return {}


def _motor_value(state: dict[str, Any], keys: tuple[str, ...]) -> Any:
    value = _pick(_current_device(state), *keys)
    if value is not None:
        return value
    value = _pick(state, *keys)
    if value is not None:
        return value
    hit = _walk_find(state, keys, depth=3)
    return _pick(hit, *keys)


def _motor_snapshot(state: dict[str, Any]) -> dict[str, Any]:
    pwm = _to_int(_motor_value(state, ("pwm", "motor_pwm", "pwm_duty", "duty", "pwm_value")))
    current = _to_float(
        _motor_value(state, ("current_a", "current", "motor_current_a", "current_amp"))
    )
    if current is None:
        reg15 = _to_float(_motor_value(state, ("reg15", "reg_15", "current_reg")))
        if reg15 is not None:
            current = reg15 / 100.0
    source = str(_motor_value(state, ("current_source", "source", "data_source")) or "")
    return {"pwm": pwm, "current_a": current, "source": _source_cn(source)}


# ---------------------------------------------------------------- 指标取值


def _accel_text(lsb: Any) -> str | None:
    signed = _signed16(lsb)
    if signed is None:
        return None
    accel = signed / 16384 * 9.80665
    return f"{_fmt(accel, 3)}米每秒平方（原始码{signed}）"


def _alert_level(dev: dict[str, Any]) -> int | None:
    level = _to_int(_pick(dev, "alert_level", "level"))
    return level if level is not None and 0 <= level <= 2 else None


def _source_of(dev: dict[str, Any], metric: str) -> str:
    if metric == "temperature":
        raw = _pick(dev, "temperature_source")
    elif metric == "current":
        raw = _pick(dev, "current_source")
    else:
        raw = _pick(dev, "vibration_source")
    if raw is None:
        raw = _pick(dev, "source", "data_source")
    return _source_cn(raw)


def _sources_sentence(dev: dict[str, Any]) -> str:
    bits = []
    for metric, label in (("vibration_rms", "振动"), ("temperature", "温度"), ("current", "电流")):
        source = _source_of(dev, metric)
        if source:
            bits.append(f"{label}{source}")
    return "、".join(bits)


def _metric_text(dev: dict[str, Any], metric: str) -> str | None:
    if metric == "vibration_raw":
        return _accel_text(_pick(dev, "raw_lsb", "vibration_raw_lsb", "raw"))
    if metric == "vibration_filtered":
        return _accel_text(_pick(dev, "filtered_lsb", "vibration_filtered_lsb", "filtered"))
    if metric == "vibration_rms":
        value = _pick(dev, "vibration_rms", "rms", "vib_rms")
        return f"{_fmt(value, 3)}米每秒平方" if _to_float(value) is not None else None
    if metric == "vibration_peak":
        value = _pick(dev, "vibration_peak", "peak", "vib_peak")
        return f"{_fmt(value, 3)}米每秒平方" if _to_float(value) is not None else None
    if metric == "mean_abs":
        value = _pick(dev, "mean_abs_lsb", "mean_abs")
        return f"{_fmt(value, 2)}原始码" if _to_float(value) is not None else None
    if metric == "zero_crossings":
        value = _pick(dev, "zero_crossings", "zero_cross")
        return f"{_fmt(value, 0)}次" if _to_float(value) is not None else None
    if metric == "alert_level":
        level = _alert_level(dev)
        return ALERT_CN.get(level, "未知") if level is not None else None
    if metric == "alert_low":
        return _accel_text(_pick(dev, "alert_low_raw", "alert_low"))
    if metric == "alert_high":
        return _accel_text(_pick(dev, "alert_high_raw", "alert_high"))
    if metric == "sensor_status":
        value = _to_int(_pick(dev, "sensor_status"))
        return f"{value}" if value is not None else None
    if metric == "whoami":
        value = _to_int(_pick(dev, "whoami"))
        return f"{value}" if value is not None else None
    if metric == "health":
        value = _to_float(_pick(dev, "health_percent", "health"))
        return f"百分之{_fmt(value, 1)}" if value is not None else None
    if metric == "temperature":
        value = _to_float(_pick(dev, "temperature_c", "temperature"))
        return f"{_fmt(value, 1)}摄氏度" if value is not None else None
    if metric == "current":
        value = _to_float(_pick(dev, "current_a", "current"))
        return f"{_fmt(value, 2)}安" if value is not None else None
    return None


def _no_data_reply(device_id: int, label: str) -> str:
    return f"{device_id}号电机当前没有可用的{label}数据。"


# ---------------------------------------------------------------- 设备播报


def _status_text(dev: dict[str, Any], level: int | None) -> str:
    raw = str(_pick(dev, "state", "status") or "").strip().lower()
    if raw in {"fault_major", "severe", "fault", "error", "2", "严重"} or level == 2:
        return "出现严重故障"
    if raw in {"fault_minor", "warning", "warn", "minor", "1", "预警"} or level == 1:
        return "出现预警"
    if raw in {"offline", "无数据"}:
        return "没有数据"
    return "运行正常"


def _fault_site_cn(dev: dict[str, Any]) -> str:
    code = str(_pick(dev, "fault_site", "fault_location", "site") or "").strip().lower()
    if code in SITE_CN:
        return SITE_CN[code]
    text = str(_pick(dev, "fault_type", "fault_label") or "")
    for key, label in SITE_CN.items():
        if key in text.lower() or label in text:
            return label
    return ""


def _serial_sentence(state: dict[str, Any]) -> str:
    serial = state.get("serial")
    if not isinstance(serial, dict):
        return ""
    port = str(serial.get("port") or "").strip()
    if not port:
        return ""
    return f"串口{_port_text(port)}{'在线' if serial.get('online') else '离线'}"


def _port_text(port: str) -> str:
    text = (port or "").strip()
    match = re.search(r"(\d+)$", text)
    return match.group(1) if match else text


def _device_status_reply(device_id: int, dev: dict[str, Any] | None, state: dict[str, Any]) -> str:
    if dev is None:
        return f"没有找到{device_id}号设备。"
    if dev.get("has_data") is False or str(_pick(dev, "alert_level")) == "-1":
        return f"{device_id}号设备当前没有可用数据。"

    level = _alert_level(dev)
    parts = [f"{device_id}号电机当前{_status_text(dev, level)}"]
    if level is not None:
        parts.append(f"告警等级{ALERT_CN.get(level, '未知')}")
        site = _fault_site_cn(dev) if level > 0 else ""
        if site:
            parts.append(f"故障部位{site}")

    rms = _metric_text(dev, "vibration_rms")
    peak = _metric_text(dev, "vibration_peak")
    if rms:
        parts.append(f"振动有效值{rms}")
    if peak:
        parts.append(f"振动峰值{peak}")
    temperature = _metric_text(dev, "temperature")
    current = _metric_text(dev, "current")
    if temperature:
        parts.append(f"温度{temperature}")
    if current:
        parts.append(f"电流{current}")

    serial = _serial_sentence(state)
    if serial:
        parts.append(serial)

    npu = _npu_snapshot(state)
    if npu and npu.get("condition"):
        parts.append(
            f"工况识别模型判断为{npu['condition']}，置信度{_pct_text(npu.get('confidence'))}"
        )

    if len(parts) == 1:
        return f"{device_id}号设备当前没有可用数据。"
    return "；".join(parts) + "。"


def _metrics_reply(device_id: int, dev: dict[str, Any] | None, metrics: list[str]) -> str:
    if dev is None:
        return f"没有找到{device_id}号设备。"
    if dev.get("has_data") is False:
        return f"{device_id}号设备当前没有可用数据。"

    if metrics == ["source"]:
        sources = _sources_sentence(dev)
        return f"{device_id}号电机通道状态：{sources}。" if sources else f"{device_id}号电机通道状态未知。"

    parts: list[str] = []
    missing: list[str] = []
    for metric in metrics:
        if metric == "source":
            continue
        label = _METRIC_LABEL.get(metric, metric)
        text = _metric_text(dev, metric)
        if text is None:
            missing.append(label)
            continue
        parts.append(f"{label}为{text}")

    if not parts:
        return _no_data_reply(device_id, "、".join(missing) or "指标")

    reply = f"{device_id}号电机当前" + "，".join(parts) + "。"
    if missing:
        reply += f"以下参数没有可用数据：{'、'.join(missing)}。"
    return reply


def _all_params_reply(device_id: int, dev: dict[str, Any] | None) -> str:
    if dev is None:
        return f"没有找到{device_id}号设备。"
    if dev.get("has_data") is False:
        return f"{device_id}号设备当前没有可用数据。"

    parts: list[str] = []
    missing: list[str] = []
    for metric in _ALL_METRICS:
        text = _metric_text(dev, metric)
        if text is None:
            missing.append(_METRIC_LABEL.get(metric, metric))
            continue
        parts.append(f"{_METRIC_LABEL.get(metric, metric)}为{text}")

    if not parts:
        return f"{device_id}号电机当前没有可用数据。"

    reply = f"{device_id}号电机当前的参数：" + "，".join(parts) + "。"
    if missing:
        shown = "、".join(missing[:4])
        extra = f"等共{len(missing)}项" if len(missing) > 4 else ""
        reply += f"以下参数没有可用数据：{shown}{extra}。"
    return reply


def _system_reply(state: dict[str, Any]) -> str:
    parts: list[str] = []
    devices = state.get("devices")
    if isinstance(devices, list) and devices:
        total = len(devices)
        online = sum(1 for dev in devices if isinstance(dev, dict) and dev.get("has_data") is not False)
        alerts = sum(
            1 for dev in devices if isinstance(dev, dict) and (_alert_level(dev) or 0) > 0
        )
        parts.append(f"当前共有{total}台设备，{online}台有数据")
        parts.append(f"{alerts}台告警" if alerts else "没有告警")
    serial = _serial_sentence(state)
    if serial:
        parts.append(serial)
    selected = _selected_device_id(state)
    if selected:
        parts.append(f"当前选中的是{selected}号设备")
    return "；".join(parts) + "。" if parts else "电脑大屏暂时没有设备数据。"


# ---------------------------------------------------------------- 工况识别模型


def _find_npu_dict(state: Any) -> dict[str, Any] | None:
    if not isinstance(state, dict):
        return None
    npu = state.get("npu")
    if isinstance(npu, dict):
        result = npu.get("result") if isinstance(npu.get("result"), dict) else npu
        if isinstance(result.get("data"), dict):
            result = result["data"]
        if any(k in result for k in ("condition", "condition_zh", "probabilities", "confidence")):
            return result
    for key in ("npu_result", "npuResult", "inference"):
        block = state.get(key)
        if isinstance(block, dict) and any(k in block for k in ("condition", "condition_zh", "probabilities")):
            return block
    found = _walk_find(state, ("condition_zh", "probabilities", "average_latency_ms"), depth=4)
    if found is not None:
        return found
    return _walk_find(state, ("condition", "confidence"), depth=4)


def _normalize_probabilities(raw: Any) -> list[tuple[str, float]]:
    pairs: list[tuple[Any, Any]] = []
    if isinstance(raw, dict):
        pairs = list(raw.items())
    elif isinstance(raw, list):
        for item in raw:
            if isinstance(item, dict):
                label = _pick(item, "label", "name", "class", "condition", "condition_zh", "key", "id", "code")
                value = _pick(item, "probability", "prob", "p", "score", "value", "confidence")
                pairs.append((label, value))
            elif isinstance(item, (int, float)):
                pairs.append((None, item))
        if len(pairs) == 5 and all(label is None for label, _ in pairs):
            pairs = list(zip(CONDITION_CN, [value for _, value in pairs]))
    else:
        return []

    numbers = [_to_float(value) for _, value in pairs]
    as_percent = any(number is not None and number > 1.0 for number in numbers)

    out: list[tuple[str, float]] = []
    for (label, _value), number in zip(pairs, numbers):
        if number is None:
            continue
        pct = number if as_percent else number * 100.0
        name = _canonical_condition(label)
        if name is None and label is not None:
            name = str(label)
        out.append((name or "未知", round(pct, 2)))
    out.sort(key=lambda item: item[1], reverse=True)
    return out


def _npu_snapshot(state: Any) -> dict[str, Any] | None:
    result = _find_npu_dict(state)
    if result is None:
        return None

    probabilities = _normalize_probabilities(
        _pick(result, "probabilities", "probs", "probability", "scores", "class_probs")
    )
    condition = _canonical_condition(
        _pick(result, "condition_zh", "condition_text", "label_zh", "condition_en")
    ) or _canonical_condition(_pick(result, "condition", "class", "label", "state"))
    if condition is None and probabilities:
        condition = probabilities[0][0]

    confidence = _percent(_pick(result, "confidence", "conf", "max_prob", "confidence_percent"))
    if confidence is None and probabilities:
        confidence = probabilities[0][1]
    if confidence is not None:
        confidence = round(confidence, 2)

    latency = _to_float(
        _pick(result, "average_latency_ms", "avg_latency_ms", "latency_ms", "inference_time_ms", "latency")
    )
    if latency is not None:
        latency = round(latency, 2)

    return {
        "condition": condition,
        "confidence": confidence,
        "latency_ms": latency,
        "runtime": str(_pick(result, "runtime", "backend", "engine") or "").strip(),
        "source": str(_pick(result, "source", "data_source", "result_source") or "").strip(),
        "probabilities": probabilities,
        "raw": result,
    }


def _npu_summary_reply(npu: dict[str, Any], aspects: list[str]) -> str:
    """只回答被问到的项，不额外附带耗时/概率/来源。"""
    condition = npu.get("condition") or "未知"
    confidence = npu.get("confidence")
    latency = npu.get("latency_ms")
    probabilities = npu.get("probabilities") or []

    parts: list[str] = []
    if "condition" in aspects:
        text = f"工况识别模型当前判断为{condition}"
        if confidence is not None:
            text += f"，置信度{_pct_text(confidence)}"
        parts.append(text)
    elif "confidence" in aspects:
        parts.append(f"当前置信度为{_pct_text(confidence)}" if confidence is not None else "暂时没有置信度数据")

    if "latency" in aspects and "condition" not in aspects:
        parts.append(
            f"单次分析耗时{_fmt(latency, 1)}毫秒" if latency is not None else "暂时没有分析耗时数据"
        )

    if "probabilities" in aspects:
        if probabilities:
            parts.append(
                "五类工况概率从高到低："
                + "、".join(f"{name}{_pct_text(pct)}" for name, pct in probabilities)
            )
        else:
            parts.append("暂时没有五类工况概率数据")

    return "；".join(parts) + "。"


def _cross_check(condition: str, state: dict[str, Any]) -> str:
    """用占空比和通道电流核对输入配置。"""
    profile = REGISTER_PROFILES.get(condition)
    if profile is None:
        return "寄存器档位表里没有这个工况"

    motor = _motor_snapshot(state)
    pwm, current = motor["pwm"], motor["current_a"]

    if pwm == 0 and current is not None and abs(current) > 0.05:
        return (
            f"当前占空比为零，但通道电流仍然不为零（{_fmt(current, 2)}安），"
            "输入配置不一致，请把电流寄存器改为零"
        )
    if pwm is None and current is None:
        return "没有读到占空比和电流，无法核对输入配置"

    measured: list[str] = []
    if pwm is not None:
        measured.append(f"占空比{_fmt(pwm, 0)}")
    if current is not None:
        measured.append(f"电流{_fmt(current, 2)}安（15号寄存器约{int(round(current * 100))}）")
    head = "输入侧" + "、".join(measured) if measured else "输入侧数据不全"

    spec_current = profile["reg15"] / 100.0
    conflicts: list[str] = []
    if pwm is not None and abs(pwm - profile["pwm"]) > 20:
        conflicts.append(f"占空比与{condition}档的{profile['pwm']}不一致")
    if current is not None:
        tolerance = max(0.5, spec_current * 0.5)
        if abs(current - spec_current) > tolerance:
            nearest = min(
                REGISTER_PROFILES.items(), key=lambda item: abs(item[1]["reg15"] / 100.0 - current)
            )
            conflicts.append(f"电流更接近{nearest[0]}档的{nearest[1]['reg15'] / 100:.2f}安")

    if conflicts:
        return (
            head
            + f"，与{condition}档（占空比{profile['pwm']}、15号寄存器{profile['reg15']}）不一致："
            + "；".join(conflicts)
        )
    return head + f"，与{condition}档配置一致，未发现输入矛盾"


def _npu_explain_reply(npu: dict[str, Any], asked: str | None, state: dict[str, Any]) -> str:
    """只解释「为什么判成这个工况」：结论 + 最高概率 + 输入侧是否矛盾。"""
    condition = npu.get("condition") or "未知"
    confidence = npu.get("confidence")
    probabilities = npu.get("probabilities") or []

    if asked and asked != condition:
        head = f"当前实际判断为{condition}，不是{asked}"
    else:
        head = f"当前判断为{condition}"
    if confidence is not None:
        head += f"，置信度{_pct_text(confidence)}"
    parts = [head]

    if probabilities:
        parts.append(
            "概率最高的是"
            + "、".join(f"{name}{_pct_text(pct)}" for name, pct in probabilities[:2])
        )

    parts.append(_cross_check(condition, state))
    return "；".join(part for part in parts if part) + "。"


# ---------------------------------------------------------------- 意图解析


def _parse_pwm(text: str) -> int | None:
    percent = re.search(r"百分之\s*([零〇一二三四五六七八九十百两\d]+)", text)
    if percent:
        value = _cn_number(percent.group(1))
        if value is not None:
            return max(0, min(1000, int(round(value * 10))))

    direct = re.search(
        r"占空比\s*(?:设置到|设到|调到|调成|设为|设成|打到|开到|给到|放到|到|为|是)?\s*"
        r"([零〇一二三四五六七八九十百两\d]{1,5})",
        text,
    )
    if direct:
        value = _cn_number(direct.group(1))
        if value is not None:
            return max(0, min(1000, int(value)))

    english = re.search(r"(?:pwm|占空比)\s*[=:：]?\s*(\d{1,4})", text, re.I)
    if english:
        return max(0, min(1000, int(english.group(1))))

    bare = re.search(r"(?<!\d)(\d{2,4})(?!\d)", text)
    if bare:
        return max(0, min(1000, int(bare.group(1))))
    return None


def _parse_device_id(text: str) -> int | None:
    match = _DEVICE_RE.search(text)
    if not match:
        match = re.search(r"device\s*(\d+)", text, re.I)
    if not match:
        return None
    value = _cn_number(match.group(1))
    if value is None or not 1 <= value <= 6:
        return None
    return value


def _parse_serial_port(text: str) -> str | None:
    match = _PORT_RE.search(text)
    if not match:
        return None
    number = _cn_number(match.group(1))
    if number is None or number <= 0:
        return None
    return f"COM{number}"


def _wants_diagnosis_write(text: str) -> bool:
    return (
        any(verb in text for verb in _WRITE_VERBS)
        and any(word in text for word in _DIAG_WORDS)
        and ("号" in text or "电机" in text or "设备" in text)
    )


def parse_local_command(text: str) -> dict[str, Any] | None:
    """中文口语规则：设备状态、指标、电机、串口、报告、选设备。"""
    t = _norm_text(text)
    if not t:
        return None
    low = t.lower()
    device_id = _parse_device_id(t)

    if _wants_diagnosis_write(t):
        return {"action": "refuse", "source": "local_rule"}

    # 测试报告
    if "报告" in t:
        if re.search(r"(生成|导出|产出|打开|写一?份|做一?份)", t):
            return {"action": "generate_report", "source": "local_rule"}
        if re.search(r"(读取|读|查看|念|播报|内容)", t):
            return {"action": "read_report", "source": "local_rule"}

    # 串口
    port = _parse_serial_port(t)
    if port and any(k in t for k in ("连接", "连上", "接入", "切换", "切到", "选择", "打开", "改到")):
        return {"action": "select_serial", "port": port, "source": "local_rule"}
    if "串口" in t and any(k in t for k in ("刷新", "扫描", "重新", "列表", "有哪些")):
        return {"action": "refresh_serial", "source": "local_rule"}

    # 电机（「把占空比设到百分之七十然后启动」这类不带「电机」二字的说法也要认）
    if "停机" in t:
        return {"action": "stop_motor", "source": "local_rule"}
    wants_motor = "电机" in t or "马达" in t
    has_pwm = "占空比" in t or "pwm" in low
    if wants_motor or has_pwm:
        if any(k in t for k in ("停止", "停下", "停掉", "停转", "关闭", "关掉", "断电")):
            return {"action": "stop_motor", "source": "local_rule"}
        if any(k in t for k in ("启动", "开启", "开动", "转起来", "运行", "开起来")) or (
            has_pwm and any(k in t for k in ("设置", "设到", "调到", "改为", "改到", "调到"))
        ):
            return {"action": "start_motor", "pwm": _parse_pwm(t), "source": "local_rule"}

    # 全部参数（先于切换设备：避免「查看四号电机详细数据」被当成切换）
    if any(hint in t for hint in _ALL_PARAM_HINTS):
        return {"action": "all_params", "device_id": device_id, "source": "local_rule"}

    # 切换设备
    if device_id and any(k in t for k in ("切换", "切到", "选择", "选中", "查看", "看看", "定位")):
        return {"action": "select_device", "device_id": device_id, "source": "local_rule"}

    # 单项 / 多项指标
    metrics = [name for keys, name, _ in _METRIC_RULES if any(k in low for k in keys)]
    if "振动" in t and not any(name.startswith("vibration") for name in metrics):
        # 只说「振动」时按振动有效值播报
        metrics.insert(0, "vibration_rms")
    if metrics:
        return {"action": "metrics", "device_id": device_id, "metrics": metrics, "source": "local_rule"}

    # 系统总览（先于单台状态，避免「六台设备状态」被当单台）
    if any(k in t for k in ("系统状态", "全部设备", "所有设备", "六台", "设备列表", "大屏状态")):
        return {"action": "system", "source": "local_rule"}
    if "状态" in t and any(k in t for k in ("系统", "全部", "所有", "大屏")):
        return {"action": "system", "source": "local_rule"}

    # 单台状态
    status_keys = ("状态", "怎么样", "怎样", "正常吗", "正不正常", "情况", "检查", "看看", "如何", "查一下")
    if any(k in t for k in status_keys) and (device_id or "电机" in t or "设备" in t):
        return {"action": "status", "device_id": device_id, "source": "local_rule"}

    # 刷新板端分析
    if "分析" in t or ("诊断" in t and any(k in t for k in ("刷新", "重新", "板端", "一下"))):
        return {"action": "analyze", "source": "local_rule"}

    # 连通性
    if any(k in t for k in ("连通", "系统在线", "ping", "测网")):
        return {"action": "ping", "source": "local_rule"}

    return None


def parse_npu_query(text: str) -> dict[str, Any] | None:
    """工况模型 / 寄存器说法；不匹配返回 None。"""
    t = _norm_text(text)
    if not t:
        return None
    low = t.lower()
    condition = _find_condition_in(t)

    if "寄存器" in t or "reg15" in low or "reg5" in low:
        if any(k in t for k in ("五类", "五个", "五种", "5类", "5个", "5种", "全部", "所有", "都")):
            return {"action": "registers_all", "source": "npu_rule"}
        if condition:
            return {"action": "register", "condition": condition, "source": "npu_rule"}
        return {"action": "registers_all", "source": "npu_rule"}

    if (
        condition is not None
        and any(k in t for k in ("判成", "判为", "判定为", "判定成", "判断为"))
        and any(k in t for k in ("为什么", "为何", "原因", "解释"))
    ):
        return {"action": "explain", "condition": condition, "source": "npu_rule"}

    wants_prob = "概率" in t
    wants_conf = "置信度" in t or "confidence" in low
    wants_latency = any(k in t for k in ("耗时", "延迟", "推理时间")) or "latency" in low
    # 「五类工况概率」问的是概率，不算问工况
    cond_probe = t.replace("工况概率", "").replace("五类工况", "")
    asks_condition = "工况" in cond_probe or (
        "模型" in t and any(k in t for k in ("判断", "判成", "结果", "结论", "输出"))
    )
    if not asks_condition and ("npu" in low or "工况分析" in t) and not (
        wants_prob or wants_conf or wants_latency
    ):
        asks_condition = True
    if not (wants_prob or wants_conf or wants_latency or asks_condition):
        return None

    aspects: list[str] = []
    if asks_condition:
        aspects.append("condition")
    if wants_conf:
        aspects.append("confidence")
    if wants_latency:
        aspects.append("latency")
    if wants_prob:
        aspects.append("probabilities")
    return {"action": "summary", "aspects": aspects or ["condition"], "source": "npu_rule"}


# ---------------------------------------------------------------- 执行


def _ok(resp: dict[str, Any]) -> bool:
    data = resp.get("data")
    if isinstance(data, dict) and data.get("ok") is False:
        return False
    return bool(resp.get("ok"))


def _error_reply(resp: dict[str, Any], device_id: int | None = None) -> str:
    data = resp.get("data") if isinstance(resp.get("data"), dict) else {}
    error = str(resp.get("error") or data.get("error") or "")
    known = {
        "diagnosis_override_disabled": "不能通过语音修改故障或诊断结果。",
        "simulated_device_has_no_actuator": "该设备未配置电机控制通道。",
        "metric_unavailable": "当前没有可用数据。",
        "device_not_found": "没有找到这台设备。",
        "device_data_unavailable": "这台设备当前没有可用数据。",
        "unknown_command": "电脑大屏不认识这条命令。",
    }
    if error in known:
        return known[error]
    return "电脑大屏、串口和网络连接需要检查。"


def _connection_reply() -> str:
    return "电脑大屏、串口和网络连接需要检查。"


def _report_text(resp: dict[str, Any]) -> str:
    data = resp.get("data")
    if isinstance(data, dict) and isinstance(data.get("data"), dict):
        data = data["data"]
    if isinstance(data, dict):
        for key in ("report", "markdown", "content", "text"):
            if data.get(key):
                return str(data[key])
    elif isinstance(data, str):
        return data
    return ""


def _run_control(action: str, intent: dict[str, Any]) -> dict[str, Any]:
    device_id = intent.get("device_id")

    if action == "start_motor":
        pwm = intent.get("pwm")
        payload: dict[str, Any] = {"command": "start_motor"}
        if pwm is not None:
            payload["pwm"] = int(pwm)
        resp = pc_commands.post_command(payload)
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp), "result": resp}
        reply = f"好的，正在以占空比{pwm}启动电机。" if pwm is not None else "好的，正在启动电机。"
        return {"ok": True, "tool": action, "reply": reply, "result": resp}

    if action == "stop_motor":
        resp = pc_commands.post_command({"command": "stop_motor"})
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp), "result": resp}
        return {"ok": True, "tool": action, "reply": "好的，已停止电机。", "result": resp}

    if action == "select_device":
        resp = pc_commands.post_command({"command": "select_device", "device_id": int(device_id or 1)})
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp, device_id), "result": resp}
        return {
            "ok": True,
            "tool": action,
            "reply": f"已切换到{int(device_id or 1)}号电机。",
            "result": resp,
        }

    if action == "select_serial":
        port = str(intent.get("port") or "")
        resp = pc_commands.post_command({"command": "select_serial", "port": port})
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp), "result": resp}
        return {
            "ok": True,
            "tool": action,
            "reply": f"已切换到串口{_port_text(port)}。",
            "result": resp,
        }

    if action == "refresh_serial":
        resp = pc_commands.post_command({"command": "refresh_serial"})
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp), "result": resp}
        return {"ok": True, "tool": action, "reply": "已刷新串口列表。", "result": resp}

    if action == "generate_report":
        resp = pc_commands.post_command({"command": "generate_report"})
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp), "result": resp}
        return {"ok": True, "tool": action, "reply": "测试报告已生成。", "result": resp}

    if action == "read_report":
        resp = pc_commands.get_report()
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp), "result": resp}
        text = _report_text(resp).replace("\r\n", "\n").strip()
        reply = f"报告内容：{text[:600]}" if text else "报告已经读取，但内容是空的。"
        return {"ok": True, "tool": action, "reply": reply, "result": resp}

    if action == "analyze":
        resp = pc_commands.post_command({"command": "analyze"})
        if not _ok(resp):
            return {"ok": False, "tool": action, "reply": _error_reply(resp), "result": resp}
        return {"ok": True, "tool": action, "reply": "已刷新板端分析。", "result": resp}

    return {"ok": False, "silent": True, "reply": "", "result": None}


def _run_ping() -> dict[str, Any]:
    try:
        result = ping_system()
    except Exception as exc:  # noqa: BLE001
        return {"ok": False, "tool": "ping_system", "reply": _connection_reply(), "error": str(exc)}
    gateway = result.get("gateway") or {}
    pc = result.get("pc_display") or {}
    gateway_ok = isinstance(gateway, dict) and gateway.get("ok") is not False and bool(gateway)
    pc_ok = isinstance(pc, dict) and bool(pc.get("ok"))
    if pc_ok:
        return {
            "ok": True,
            "tool": "ping_system",
            "reply": f"电脑大屏连接正常，串口网关{'正常' if gateway_ok else '暂时没有响应'}。",
            "result": result,
        }
    return {"ok": False, "tool": "ping_system", "reply": _connection_reply(), "result": result}


def run_local_intent(intent: dict[str, Any]) -> dict[str, Any]:
    action = str(intent.get("action") or "")

    if action == "refuse":
        return {
            "ok": False,
            "tool": "refuse",
            "reply": "不能通过语音修改故障或诊断结果，只能查询状态、选择设备或控制电机。",
        }

    if action in {
        "start_motor",
        "stop_motor",
        "select_device",
        "select_serial",
        "refresh_serial",
        "generate_report",
        "read_report",
        "analyze",
    }:
        return _run_control(action, intent)

    if action == "ping":
        return _run_ping()

    state, error = _fetch_state()
    if state is None:
        return {
            "ok": False,
            "tool": "pc_state",
            "reply": _connection_reply(),
            "error": error,
        }

    if action == "system":
        return {"ok": True, "tool": "pc_state", "reply": _system_reply(state), "data": None}

    device_id = int(intent.get("device_id") or _selected_device_id(state) or 1)
    dev = _device_by_id(state, device_id)

    if action == "status":
        reply = _device_status_reply(device_id, dev, state)
        return {"ok": True, "tool": "device_status", "reply": reply, "data": dev}

    if action == "metrics":
        metrics = list(intent.get("metrics") or [])
        reply = _metrics_reply(device_id, dev, metrics)
        return {"ok": True, "tool": "device_metrics", "reply": reply, "data": dev}

    if action == "all_params":
        reply = _all_params_reply(device_id, dev)
        return {"ok": True, "tool": "device_params", "reply": reply, "data": dev}

    return {"ok": False, "silent": True, "reply": ""}


def run_npu_intent(intent: dict[str, Any]) -> dict[str, Any]:
    action = str(intent.get("action") or "")

    if action in {"register", "registers_all"}:
        if action == "register":
            condition = str(intent.get("condition") or "")
            reply = _register_reply(condition)
            data: Any = {"condition": condition, **REGISTER_PROFILES[condition]}
        else:
            reply = _registers_all_reply()
            data = {cond: dict(REGISTER_PROFILES[cond]) for cond in CONDITION_CN}
        return {"ok": True, "tool": "register_profile", "reply": reply, "source": "npu_rule", "data": data}

    state, error = _fetch_state()
    if state is None:
        return {
            "ok": False,
            "tool": "pc_state",
            "reply": _connection_reply(),
            "error": error,
        }

    npu = _npu_snapshot(state)
    if npu is None:
        return {
            "ok": False,
            "tool": "query_model",
            "reply": "电脑大屏暂时没有工况分析结果，请稍后再问。",
            "error": "npu_unavailable",
        }

    if action == "explain":
        reply = _npu_explain_reply(npu, intent.get("condition"), state)
        tool = "explain_condition"
    else:
        reply = _npu_summary_reply(npu, list(intent.get("aspects") or ["condition"]))
        tool = "query_model"

    return {
        "ok": True,
        "tool": tool,
        "reply": translate_sources(reply),
        "source": "pc_rule",
        "data": {
            "condition": npu["condition"],
            "confidence": npu["confidence"],
            "average_latency_ms": npu["latency_ms"],
            "probabilities": npu["probabilities"],
            "source": _source_cn(npu["source"]),
        },
    }


# ---------------------------------------------------------------- 兜底：旧规则桥


_STATUS_HINTS = ("状态", "情况", "怎么样", "怎样", "如何", "检查", "看看")
_FAILURE_PREFIXES = (
    "查询失败",
    "没有找到",
    "读取设备数据失败",
    "PC 大屏操作失败",
    "没有可用",
    "尚未识别",
)


def _should_attach_model(text: str, result: dict[str, Any], reply: str) -> bool:
    if not reply or not result.get("ok"):
        return False
    if "工况识别模型" in reply or "工况" in reply:
        return False
    if reply.startswith(_FAILURE_PREFIXES):
        return False
    normalized = _norm_text(text)
    return any(k in normalized for k in _STATUS_HINTS)


def _attach_model(reply: str) -> str:
    base = reply.rstrip()
    if not base.endswith(("。", "！", "？", ".", "!", "?")):
        base += "。"

    state, _error = _fetch_state()
    npu = _npu_snapshot(state) if state is not None else None
    if npu is None:
        return base + "工况识别模型暂时没有结果。"

    extra = f"工况识别模型判断为{npu.get('condition') or '未知'}"
    if npu.get("confidence") is not None:
        extra += f"，置信度{_pct_text(npu['confidence'])}"
    return base + extra + "。"


def handle_text(text: str) -> dict[str, Any]:
    text = str(text or "").replace("\ufeff", "").strip()

    model_intent = parse_npu_query(text)
    if model_intent is not None:
        out = run_npu_intent(model_intent)
        return {
            "ok": bool(out.get("ok")),
            "text": text,
            "reply": str(out.get("reply") or ""),
            "result": out,
        }

    local_intent = parse_local_command(text)
    if local_intent is not None:
        out = run_local_intent(local_intent)
        if not out.get("silent"):
            reply = translate_sources(str(out.get("reply") or ""))
            out["reply"] = reply
            return {
                "ok": bool(out.get("ok")),
                "text": text,
                "reply": reply,
                "result": out,
            }

    out = handle_utterance(text)
    reply = translate_sources(str(out.get("reply") or ""))
    if _should_attach_model(text, out, reply):
        reply = _attach_model(reply)
    out = dict(out)
    out["reply"] = reply
    return {
        "ok": bool(out.get("ok")),
        "text": text,
        "reply": reply,
        "result": out,
    }


# ---------------------------------------------------------------- HTTP 服务


def _pc_endpoint() -> tuple[str, int]:
    cfg = load_config() or {}
    pc = cfg.get("pc_display") or {}
    return str(pc.get("host") or "").strip(), int(pc.get("port", 8760))


def _health_payload() -> dict[str, Any]:
    payload: dict[str, Any] = {"ok": True, "service": "orange_pi_voice", "version": "1.2"}
    try:
        host, port = _pc_endpoint()
        payload["pc_api"] = f"http://{host}:{port}" if host else None
        payload["pc_configured"] = bool(host)
    except Exception as exc:  # noqa: BLE001
        payload["pc_api"] = None
        payload["pc_configured"] = False
        payload["config_error"] = str(exc)
    return payload


class Handler(BaseHTTPRequestHandler):
    server_version = "OrangePiVoice/1.2"

    def _send(self, code: int, obj: dict[str, Any]) -> None:
        body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        if self.path in {"/health", "/"}:
            self._send(200, _health_payload())
        else:
            self._send(404, {"ok": False, "error": "not found"})

    def do_POST(self) -> None:  # noqa: N802
        if self.path != "/voice":
            self._send(404, {"ok": False, "error": "not found"})
            return
        try:
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length) if length else b"{}"
            body = json.loads(raw.decode("utf-8") or "{}")
        except (ValueError, UnicodeDecodeError) as exc:
            self._send(400, {"ok": False, "error": f"bad json: {exc}"})
            return

        text = str(body.get("text") or "").strip()
        if not text:
            self._send(400, {"ok": False, "error": "empty text"})
            return

        try:
            result = handle_text(text)
        except Exception as exc:  # noqa: BLE001
            self._send(500, {"ok": False, "text": text, "error": str(exc)})
            return
        self._send(200, result)

    def log_message(self, fmt: str, *args: Any) -> None:  # noqa: A003
        print(f"[HTTP] {self.address_string()} {fmt % args}", flush=True)


def serve(host: str, port: int) -> None:
    httpd = ThreadingHTTPServer((host, port), Handler)
    print(f"[voice] listening on http://{host}:{port}  POST /voice  GET /health", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[voice] bye", flush=True)
    finally:
        httpd.server_close()


def stdin_loop() -> None:
    print("[voice] 标准输入模式：每行一句，Ctrl+D 退出", flush=True)
    for line in sys.stdin:
        text = line.strip()
        if not text:
            continue
        try:
            out = handle_text(text)
        except Exception as exc:  # noqa: BLE001
            out = {"ok": False, "text": text, "reply": f"语音指令执行失败：{exc}"}
        print(json.dumps(out, ensure_ascii=False), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description="香橙派中文语音控制 HTTP 服务")
    parser.add_argument("text", nargs="*", help="直接给一句话（省略则读标准输入或启动服务）")
    parser.add_argument("--serve", action="store_true", help="启动 HTTP 服务（否则单句或读标准输入）")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8780)
    parser.add_argument("--json", action="store_true", help="单句模式输出完整 JSON")
    args = parser.parse_args()

    if args.serve:
        serve(args.host, args.port)
        return

    if args.text:
        text = " ".join(args.text).strip()
        try:
            out = handle_text(text)
        except Exception as exc:  # noqa: BLE001
            print(f"语音指令执行失败：{exc}", flush=True)
            return
        if args.json:
            print(json.dumps(out, ensure_ascii=False), flush=True)
        else:
            print(out["reply"] or json.dumps(out, ensure_ascii=False), flush=True)
        return

    stdin_loop()


if __name__ == "__main__":
    main()
