"""百度短语音识别与短文本合成。"""
from __future__ import annotations

import base64
import time
from typing import Any

import requests

TOKEN_URL = "https://aip.baidubce.com/oauth/2.0/token"
ASR_URL = "https://vop.baidu.com/server_api"
TTS_URL = "https://tsn.baidu.com/text2audio"


class BaiduSpeech:
    def __init__(self, api_key: str, secret_key: str, cuid: str = "orangepi-zero3"):
        if not api_key or not secret_key:
            raise ValueError("请在 config.yaml 的 baidu.api_key 和 baidu.secret_key 中填写密钥")
        self.api_key = api_key
        self.secret_key = secret_key
        self.cuid = cuid
        self._token = ""
        self._token_deadline = 0.0

    def access_token(self) -> str:
        if self._token and time.time() < self._token_deadline:
            return self._token
        resp = requests.post(
            TOKEN_URL,
            params={
                "grant_type": "client_credentials",
                "client_id": self.api_key,
                "client_secret": self.secret_key,
            },
            timeout=15,
        )
        resp.raise_for_status()
        data = resp.json()
        if "access_token" not in data:
            raise RuntimeError(f"获取百度 token 失败：{data}")
        self._token = data["access_token"]
        self._token_deadline = time.time() + int(data.get("expires_in", 3600)) - 60
        return self._token

    def recognize_wav(self, wav_bytes: bytes, rate: int = 16000) -> str:
        payload: dict[str, Any] = {
            "format": "wav",
            "rate": rate,
            "channel": 1,
            "cuid": self.cuid,
            "token": self.access_token(),
            "dev_pid": 1537,
            "speech": base64.b64encode(wav_bytes).decode("ascii"),
            "len": len(wav_bytes),
        }
        resp = requests.post(ASR_URL, json=payload, timeout=30)
        resp.raise_for_status()
        data = resp.json()
        if data.get("err_no") != 0:
            raise RuntimeError(f"语音识别失败：{data.get('err_no')} {data.get('err_msg')}")
        result = data.get("result") or []
        return (result[0] if result else "").strip()

    def synthesize_wav(self, text: str, per: int = 0) -> bytes:
        resp = requests.post(
            TTS_URL,
            data={
                "tex": text,
                "tok": self.access_token(),
                "cuid": self.cuid,
                "ctp": 1,
                "lan": "zh",
                "spd": 5,
                "pit": 5,
                "vol": 7,
                "per": per,
                "aue": 6,
            },
            timeout=30,
        )
        resp.raise_for_status()
        content_type = resp.headers.get("Content-Type", "")
        if "application/json" in content_type or resp.content[:1] == b"{":
            raise RuntimeError(f"语音合成失败：{resp.text}")
        return resp.content
