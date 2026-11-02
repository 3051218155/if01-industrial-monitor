"""网关 TCP 客户端 —— 对应项目文档 §5.3。

兼容两种响应：
1) 文档格式：{"code":0,"msg":"ok","data":{...}}
2) PC 假网关格式：{"ok":true,"cmd":"query_device","device_id":3,"state":"fault",...}
"""
from __future__ import annotations

import json
import socket
from typing import Any


FAULT_SITE_CN = {
    "imbalance": "转子不平衡",
    "bearing": "轴承端",
    "coupling": "联轴器",
    "misalignment": "不对中",
    "normal": "",
}


class GatewayClient:
    def __init__(self, host: str, port: int = 9000, timeout_sec: float = 3.0):
        self.host = host
        self.port = port
        self.timeout_sec = timeout_sec

    def _request(self, payload: dict[str, Any]) -> dict[str, Any]:
        data = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
        with socket.create_connection((self.host, self.port), timeout=self.timeout_sec) as sock:
            sock.sendall(data)
            chunks: list[bytes] = []
            while True:
                buf = sock.recv(4096)
                if not buf:
                    break
                chunks.append(buf)
                if b"\n" in buf or b"}" in buf:
                    try:
                        text = b"".join(chunks).decode("utf-8").strip()
                        return json.loads(text.splitlines()[-1])
                    except json.JSONDecodeError:
                        continue
        text = b"".join(chunks).decode("utf-8").strip()
        return json.loads(text.splitlines()[-1] if text else "{}")

    def query_device(self, device_id: int) -> dict[str, Any]:
        raw = self._request({"cmd": "query_device", "device_id": device_id})
        return self._normalize_query_device(raw, device_id)

    def list_devices(self) -> dict[str, Any]:
        return self._request({"cmd": "list_devices"})

    def query_history(self, device_id: int, limit: int = 10) -> dict[str, Any]:
        return self._request(
            {"cmd": "query_history", "device_id": device_id, "limit": limit}
        )

    @staticmethod
    def _normalize_query_device(raw: dict[str, Any], device_id: int) -> dict[str, Any]:
        """统一成文档格式，供 MCP / TTS / 拓展屏使用。"""
        if "code" in raw and "data" in raw:
            return raw

        if raw.get("ok") is False:
            return {"code": -1, "msg": str(raw.get("error") or raw.get("msg") or "查询失败"), "data": {}}

        if "ok" not in raw and "state" not in raw:
            return {"code": -1, "msg": "unknown response", "data": {}}

        metrics = raw.get("metrics") or {}
        state = str(raw.get("state") or "normal")
        fault_site = str(raw.get("fault_site") or "")
        fault_label = str(metrics.get("fault_label") or "")

        if state == "normal":
            status, status_text = "normal", "正常"
        else:
            status, status_text = "fault_major", fault_label or "故障"

        data = {
            "device_id": int(raw.get("device_id") or device_id),
            "device_name": f"{raw.get('device_id') or device_id}号设备",
            "status": status,
            "status_text": status_text,
            "temperature": metrics.get("temp_c"),
            "vibration_rms": metrics.get("vibration_rms"),
            "vibration_peak": metrics.get("vibration_peak"),
            "current_a": metrics.get("current_a"),
            "fault_location": FAULT_SITE_CN.get(fault_site, fault_site),
            "fault_type": fault_label or FAULT_SITE_CN.get(fault_site, fault_site),
            "inference_time_ms": metrics.get("inference_time_ms"),
        }
        return {"code": 0, "msg": "ok", "data": data}


if __name__ == "__main__":
    import argparse
    from pathlib import Path

    import yaml

    cfg_path = Path(__file__).resolve().parents[1] / "config.yaml"
    cfg = yaml.safe_load(cfg_path.read_text(encoding="utf-8"))
    g = cfg["gateway"]

    parser = argparse.ArgumentParser()
    parser.add_argument("--id", type=int, default=3)
    args = parser.parse_args()

    client = GatewayClient(g["host"], g["port"], g.get("timeout_sec", 3))
    print(json.dumps(client.query_device(args.id), ensure_ascii=False, indent=2))
