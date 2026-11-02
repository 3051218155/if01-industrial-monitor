"""TTS —— 屏幕打印或百度短文本合成。对应文档 §4.4.3"""
from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path
from typing import Any

from baidu_speech import BaiduSpeech


class TtsEngine:
    def __init__(self, provider: str = "offline_stub", baidu_cfg: dict[str, Any] | None = None, tts_cfg: dict[str, Any] | None = None):
        self.provider = provider
        self.baidu_cfg = baidu_cfg or {}
        self.tts_cfg = tts_cfg or {}
        self._baidu: BaiduSpeech | None = None

    def speak(self, text: str) -> None:
        print(f"[TTS] {text}")
        if self.provider == "offline_stub":
            return
        if self.provider == "baidu":
            self._speak_baidu(text)
            return
        raise NotImplementedError(f"TTS provider not ready: {self.provider}")

    def _client(self) -> BaiduSpeech:
        if self._baidu is None:
            self._baidu = BaiduSpeech(
                self.baidu_cfg.get("api_key", ""),
                self.baidu_cfg.get("secret_key", ""),
            )
        return self._baidu

    def _speak_baidu(self, text: str) -> None:
        audio = self._client().synthesize_wav(text, per=int(self.tts_cfg.get("per", 0)))
        device = str(self.tts_cfg.get("device") or "")
        with tempfile.TemporaryDirectory() as tmp:
            wav_path = Path(tmp) / "out.wav"
            wav_path.write_bytes(audio)
            cmd = ["aplay", str(wav_path)]
            if device:
                cmd[1:1] = ["-D", device]
            subprocess.run(cmd, check=True)
