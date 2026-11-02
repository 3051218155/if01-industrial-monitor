"""百度实时语音识别（WebSocket）：边传 PCM 边收 MID/FIN。"""
from __future__ import annotations

import json
import threading
import time
import uuid
from collections.abc import Callable, Iterator
from typing import Any

import websocket

WS_URL = "wss://vop.baidu.com/realtime_asr?sn={sn}"


class BaiduRealtimeAsr:
    def __init__(
        self,
        app_id: int,
        api_key: str,
        *,
        cuid: str = "orangepi-zero3",
        dev_pid: int = 15372,
        sample_rate: int = 16000,
    ):
        if not app_id:
            raise ValueError("请在 config.yaml 的 baidu.app_id 中填写控制台 AppID")
        if not api_key:
            raise ValueError("请在 config.yaml 的 baidu.api_key 中填写 API Key")
        self.app_id = int(app_id)
        self.api_key = api_key
        self.cuid = cuid
        self.dev_pid = int(dev_pid)
        self.sample_rate = int(sample_rate)

    def recognize_stream(
        self,
        pcm_iter: Iterator[bytes],
        *,
        on_partial: Callable[[str], None] | None = None,
        finish_timeout_sec: float = 3.0,
    ) -> str:
        """消费 PCM 帧迭代器，结束后返回最终文本（拼接各句 FIN_TEXT）。"""
        sn = str(uuid.uuid4())
        url = WS_URL.format(sn=sn)
        lock = threading.Lock()
        finals: list[str] = []
        last_mid = ""
        err: list[str] = []
        opened = threading.Event()
        finished = threading.Event()
        ws_holder: dict[str, Any] = {}

        def on_open(ws: websocket.WebSocketApp) -> None:
            start = {
                "type": "START",
                "data": {
                    "appid": self.app_id,
                    "appkey": self.api_key,
                    "dev_pid": self.dev_pid,
                    "cuid": self.cuid,
                    "format": "pcm",
                    "sample": self.sample_rate,
                },
            }
            ws.send(json.dumps(start, ensure_ascii=False))
            opened.set()

        def on_message(_ws: websocket.WebSocketApp, message: str) -> None:
            nonlocal last_mid
            try:
                data = json.loads(message)
            except json.JSONDecodeError:
                return
            if data.get("type") == "HEARTBEAT":
                return
            if data.get("err_no", 0) not in (0, None):
                err.append(f"{data.get('err_no')} {data.get('err_msg')}")
                finished.set()
                return
            typ = data.get("type")
            result = (data.get("result") or "").strip()
            if typ == "MID_TEXT" and result:
                with lock:
                    last_mid = result
                if on_partial:
                    on_partial(result)
            elif typ == "FIN_TEXT" and result:
                with lock:
                    finals.append(result)
                    last_mid = result
                if on_partial:
                    on_partial(result)

        def on_error(_ws: websocket.WebSocketApp, error: Any) -> None:
            err.append(str(error))
            finished.set()

        def on_close(_ws: websocket.WebSocketApp, *_args: Any) -> None:
            finished.set()

        ws = websocket.WebSocketApp(
            url,
            on_open=on_open,
            on_message=on_message,
            on_error=on_error,
            on_close=on_close,
        )
        ws_holder["ws"] = ws
        thread = threading.Thread(target=ws.run_forever, kwargs={"ping_interval": 20}, daemon=True)
        thread.start()

        if not opened.wait(timeout=8):
            try:
                ws.close()
            except Exception:
                pass
            raise RuntimeError("实时 ASR WebSocket 连接超时")

        try:
            for frame in pcm_iter:
                if err or finished.is_set():
                    break
                if not frame:
                    continue
                try:
                    ws.send(frame, opcode=websocket.ABNF.OPCODE_BINARY)
                except Exception as exc:  # noqa: BLE001
                    err.append(str(exc))
                    break
            try:
                ws.send(json.dumps({"type": "FINISH"}))
            except Exception:
                pass
            finished.wait(timeout=max(0.5, float(finish_timeout_sec)))
        finally:
            try:
                ws.close()
            except Exception:
                pass
            thread.join(timeout=2)

        if err and not finals:
            raise RuntimeError(f"实时语音识别失败：{err[0]}")

        with lock:
            if finals:
                return "".join(finals).strip()
            return last_mid.strip()
