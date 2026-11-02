"""Windows / 香橙派都可跑的假网关 —— 网关未好时先联调机器人。"""
from __future__ import annotations

import json
import socketserver
from datetime import datetime


DEVICES = {
    1: {
        "device_id": 1,
        "device_name": "1号电机",
        "status": "normal",
        "status_text": "正常",
        "temperature": 42.0,
        "vibration_rms": 1.2,
        "vibration_peak": 2.5,
        "fault_location": "",
        "fault_type": "",
        "inference_time_ms": 40,
    },
    2: {
        "device_id": 2,
        "device_name": "2号泵机",
        "status": "fault_minor",
        "status_text": "轻微故障",
        "temperature": 58.0,
        "vibration_rms": 4.5,
        "vibration_peak": 9.0,
        "fault_location": "联轴器",
        "fault_type": "振动偏大",
        "inference_time_ms": 42,
    },
    3: {
        "device_id": 3,
        "device_name": "3号风机",
        "status": "fault_major",
        "status_text": "严重故障",
        "temperature": 78.5,
        "vibration_rms": 12.3,
        "vibration_peak": 25.6,
        "fault_location": "轴承端",
        "fault_type": "轴承磨损",
        "inference_time_ms": 45,
    },
}


class Handler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        raw = self.rfile.readline()
        if not raw:
            return
        try:
            req = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError:
            self._send({"code": -1, "msg": "invalid json", "data": {}})
            return

        cmd = req.get("cmd")
        if cmd == "query_device":
            did = int(req.get("device_id", 0))
            if did not in DEVICES:
                self._send({"code": 1, "msg": "device not found", "data": {}})
                return
            data = dict(DEVICES[did])
            data["timestamp"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            self._send({"code": 0, "msg": "ok", "data": data})
        elif cmd == "list_devices":
            devices = [
                {
                    "device_id": d["device_id"],
                    "device_name": d["device_name"],
                    "status": d["status"],
                }
                for d in DEVICES.values()
            ]
            self._send({"code": 0, "msg": "ok", "data": {"devices": devices}})
        elif cmd == "query_history":
            did = int(req.get("device_id", 0))
            self._send(
                {
                    "code": 0,
                    "msg": "ok",
                    "data": {
                        "records": [
                            {
                                "time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                                "level": "major",
                                "type": "轴承磨损",
                                "vibration": 12.3,
                            }
                        ]
                    },
                }
            )
        else:
            self._send({"code": -1, "msg": f"unknown cmd: {cmd}", "data": {}})

    def _send(self, obj: dict) -> None:
        body = (json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8")
        self.wfile.write(body)


if __name__ == "__main__":
    host, port = "0.0.0.0", 9000
    print(f"[fake_gateway] listening on {host}:{port}")
    with socketserver.ThreadingTCPServer((host, port), Handler) as server:
        server.allow_reuse_address = True
        server.serve_forever()
