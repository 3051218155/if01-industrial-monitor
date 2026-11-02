"""香橙派侧工具实现：查 TCP 网关 + HTTP 控 PC 大屏。"""
from __future__ import annotations

import json
import os
import socket
from pathlib import Path
from typing import Any

import requests
import yaml

ROOT = Path(__file__).resolve().parent


def load_config() -> dict[str, Any]:
    """读配置：优先 FPAG_CONFIG（通常指向 robot/config.yaml），否则本目录 config.yaml。

    这样机器人端只需维护一份配置；环境变量可临时覆盖 IP/端口，换场地不用改文件。
    """
    env_path = os.environ.get("FPAG_CONFIG")
    path = Path(env_path) if env_path else ROOT / "config.yaml"
    if not path.is_file():
        raise FileNotFoundError(f"配置文件不存在：{path}")

    cfg: dict[str, Any] = yaml.safe_load(path.read_text(encoding="utf-8")) or {}

    if host := os.environ.get("FPAG_GATEWAY_HOST"):
        cfg.setdefault("gateway", {})["host"] = host
    if port := os.environ.get("FPAG_GATEWAY_PORT"):
        cfg.setdefault("gateway", {})["port"] = int(port)
    if host := os.environ.get("FPAG_PC_HOST"):
        cfg.setdefault("pc_display", {})["host"] = host
    return cfg


def _tcp_json(host: str, port: int, payload: dict[str, Any], timeout: float) -> dict[str, Any]:
    raw = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.sendall(raw)
            chunks: list[bytes] = []
            while True:
                buf = sock.recv(4096)
                if not buf:
                    break
                chunks.append(buf)
                text = b"".join(chunks).decode("utf-8", errors="replace").strip()
                if not text:
                    continue
                try:
                    return json.loads(text.splitlines()[-1])
                except json.JSONDecodeError:
                    if b"\n" in buf:
                        break
        text = b"".join(chunks).decode("utf-8", errors="replace").strip()
        return json.loads(text.splitlines()[-1] if text else "{}")
    except OSError as exc:
        return {
            "ok": False,
            "error": str(exc),
            "host": host,
            "port": port,
            "hint": "网关连不上：对方是否开了 fake_gateway/真网关，IP 是否写在 orange_pi_mcp/config.yaml",
        }


def query_device(device_id: int, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    """查假网关 / 安路板 :9000。"""
    cfg = cfg or load_config()
    g = cfg["gateway"]
    return _tcp_json(
        g["host"],
        int(g["port"]),
        {"cmd": "query_device", "device_id": int(device_id)},
        float(g.get("timeout_sec", 5)),
    )


def list_devices(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    cfg = cfg or load_config()
    g = cfg["gateway"]
    return _tcp_json(
        g["host"],
        int(g["port"]),
        {"cmd": "list_devices"},
        float(g.get("timeout_sec", 5)),
    )


def show_device_3d(
    device_id: int,
    state: str = "fault",
    fault_site: str = "bearing",
    metrics: dict[str, Any] | None = None,
    cfg: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """HTTP POST 到 PC fpag_learn :8760/api/show。"""
    cfg = cfg or load_config()
    pc = cfg["pc_display"]
    url = f"http://{pc['host']}:{int(pc['port'])}{pc.get('path', '/api/show')}"
    body = {
        "device_id": int(device_id),
        "state": state,
        "fault_site": fault_site,
        "metrics": metrics
        or {
            "vibration_rms": 0.45,
            "temp_c": 68.0,
            "fault_label": "轴承磨损" if fault_site not in {"normal", "none"} else "正常",
        },
    }
    # PC MotorScene：正常态 fault_site 用 none
    if state == "normal" or fault_site in {"normal", "none"}:
        body["state"] = "normal"
        body["fault_site"] = "none"
        body["metrics"] = metrics or {
            "vibration_rms": 0.05,
            "temp_c": 42.0,
            "fault_label": "正常",
        }
    resp = requests.post(url, json=body, timeout=float(pc.get("timeout_sec", 5)))
    try:
        data = resp.json()
    except Exception:
        data = {"text": resp.text}
    return {"http_status": resp.status_code, "request": body, "response": data}


def ping_system(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    """测网关 TCP + PC HTTP。"""
    cfg = cfg or load_config()
    out: dict[str, Any] = {"gateway": None, "pc_display": None}
    g = cfg["gateway"]
    try:
        out["gateway"] = _tcp_json(
            g["host"], int(g["port"]), {"cmd": "ping"}, float(g.get("timeout_sec", 5))
        )
    except Exception as exc:
        out["gateway"] = {"ok": False, "error": str(exc)}

    pc = cfg["pc_display"]
    url = f"http://{pc['host']}:{int(pc['port'])}{pc.get('path', '/api/show')}"
    try:
        # 用一次轻量 show 探测；若服务只接受 POST 则状态码非连接错误即算通
        r = requests.post(
            url,
            json={
                "device_id": 1,
                "state": "normal",
                "fault_site": "normal",
                "metrics": {"fault_label": "ping"},
            },
            timeout=float(pc.get("timeout_sec", 5)),
        )
        out["pc_display"] = {"ok": True, "http_status": r.status_code, "body": r.text[:200]}
    except Exception as exc:
        out["pc_display"] = {"ok": False, "error": str(exc)}
    return out
