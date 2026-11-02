"""ASR —— 连续聆听：流式 WebSocket 或 VAD 说完即停。"""
from __future__ import annotations

import audioop
import os
import shutil
import subprocess
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path
from typing import Any

from baidu_speech import BaiduSpeech

COMMAND_HINTS = (
    "号",
    "查",
    "显示",
    "展示",
    "切换",
    "切到",
    "打开",
    "轴承",
    "轴成",
    "不平衡",
    "转子",
    "地脚",
    "松动",
    "联轴",
    "正常",
    "恢复",
    "状态",
    "怎么样",
    "怎样",
    "如何",
    "电机",
    "机器",
    "系统",
    "连通",
    "在线",
    "ping",
    "故障",
    "关机",
    "退出",
)

JUNK_TEXTS = {
    "一个",
    "一个。",
    "嗯",
    "啊",
    "哦",
    "呃",
    "的",
    "了",
    "。",
    ".",
    "",
}


class AsrEngine:
    def __init__(
        self,
        provider: str = "offline_stub",
        baidu_cfg: dict[str, Any] | None = None,
        asr_cfg: dict[str, Any] | None = None,
    ):
        self.provider = provider
        self.baidu_cfg = baidu_cfg or {}
        self.asr_cfg = asr_cfg or {}
        self._baidu: BaiduSpeech | None = None

    def listen(self, prompt: str = "") -> str:
        if self.provider == "offline_stub":
            text = input(prompt or "请输入指令: ").strip()
            print(f"[ASR] 键盘输入：{text}", flush=True)
            return text

        push = bool(self.asr_cfg.get("push_to_talk", False))
        if push:
            tip = prompt or "【回车】说话；或输入指令后回车: "
            typed = input(tip).strip()
            if typed:
                print(f"[ASR] 键盘输入：{typed}", flush=True)
                return typed

        try:
            return self._listen_baidu()
        except Exception as exc:
            print(f"[ASR] 识别出错（已跳过）：{exc}", flush=True)
            return ""

    def listen_continuous(self, *, max_attempts: int = 3, accept_junk: bool = False) -> str:
        """连续多轮：空/噪声则马上再听，像智能音箱一样等你说完。"""
        attempts = max(1, int(max_attempts))
        for i in range(1, attempts + 1):
            print(f"[ASR] 连续聆听 {i}/{attempts}…", flush=True)
            text = self.listen()
            if not text:
                continue
            if accept_junk or not self._is_junk(text):
                return text
            print(f"[ASR] 噪声忽略：{text!r}", flush=True)
        return ""

    @staticmethod
    def _is_junk(text: str) -> bool:
        t = (text or "").strip().rstrip("。.!！？?")
        if not t:
            return True
        # 控制类短词必须放行，否则「退出」「关机」会被当噪声丢掉
        if any(k in t for k in ("退出", "关机", "关闭电源", "停止运行", "拜拜")):
            return False
        if t in JUNK_TEXTS:
            return True
        if len(t) <= 2 and not any(k in t for k in ("号", "查", "显", "机", "轴", "正", "橙", "陈")):
            return True
        return False

    @staticmethod
    def looks_like_command(text: str) -> bool:
        t = (text or "").strip()
        if not t or AsrEngine._is_junk(t):
            return False
        return any(k in t for k in COMMAND_HINTS)

    def _client(self) -> BaiduSpeech:
        if self._baidu is None:
            self._baidu = BaiduSpeech(
                self.baidu_cfg.get("api_key", ""),
                self.baidu_cfg.get("secret_key", ""),
            )
        return self._baidu

    def _listen_baidu(self) -> str:
        rate = int(self.asr_cfg.get("sample_rate", 16000))
        device = str(self.asr_cfg.get("device") or "")
        pre_delay = float(self.asr_cfg.get("pre_record_delay_sec", 0.15))
        mode = str(self.asr_cfg.get("record_mode") or "vad").lower()
        if pre_delay > 0:
            time.sleep(pre_delay)

        if mode == "stream":
            try:
                text = self._listen_stream(rate=rate, device=device)
                if text is not None:
                    print(f"[ASR] 语音识别结果：{text}", flush=True)
                    return text
            except Exception as exc:
                msg = str(exc)
                # 静音时百度会回 -3005，不必再走 12 秒的短语音兜底
                if "-3005" in msg or "no effective speech" in msg:
                    print("[ASR] 静音（无有效语音），继续待命", flush=True)
                    return ""
                print(f"[ASR] 流式识别失败，回退短语音：{msg}", flush=True)

        return self._listen_short(rate=rate, device=device, mode="vad" if mode == "stream" else mode)

    def _listen_stream(self, *, rate: int, device: str) -> str | None:
        """实时 WebSocket：先本地等到开口再连云，边录边传。空串=无语音；None=回退。"""
        app_id = int(self.baidu_cfg.get("app_id") or 0)
        api_key = str(self.baidu_cfg.get("api_key") or "")
        if not app_id:
            print("[ASR] 未配置 baidu.app_id，回退短语音", flush=True)
            return None
        if not shutil.which("arecord"):
            print("[ASR] 未找到 arecord，回退短语音", flush=True)
            return None

        try:
            from baidu_realtime_asr import BaiduRealtimeAsr
        except ImportError as exc:
            print(f"[ASR] 缺少 websocket-client（{exc}），回退短语音", flush=True)
            return None

        frame_ms = int(self.asr_cfg.get("stream_frame_ms", 160))
        frame_bytes = max(320, int(rate * 2 * frame_ms / 1000))
        wait_speech = float(self.asr_cfg.get("vad_wait_speech_sec", 20))
        max_seconds = float(self.asr_cfg.get("max_record_seconds", 15))
        stop_sil = float(self.asr_cfg.get("vad_stop_silence_sec", 0.9))
        energy_th = int(self.asr_cfg.get("stream_energy_threshold", 500))
        start_need = max(1, int(float(self.asr_cfg.get("vad_start_sec", 0.2)) * 1000 / frame_ms))
        stop_need = max(1, int(stop_sil * 1000 / frame_ms))
        max_frames = max(1, int(max_seconds * 1000 / frame_ms))
        wait_frames = max(1, int(wait_speech * 1000 / frame_ms))

        print(
            f"…流式聆听（边说边识，停顿约 {stop_sil:.1f}s 结束；"
            f"等待开口≤{wait_speech:.0f}s，单句≤{max_seconds:.0f}s）…",
            flush=True,
        )

        cmd = [
            "arecord",
            "-q",
            "-f",
            "S16_LE",
            "-r",
            str(rate),
            "-c",
            "1",
            "-t",
            "raw",
            "-",
        ]
        if device:
            cmd[1:1] = ["-D", device]

        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        assert proc.stdout is not None

        def read_frame() -> bytes | None:
            chunk = proc.stdout.read(frame_bytes)
            if not chunk or len(chunk) < frame_bytes // 2:
                return None
            if len(chunk) < frame_bytes:
                chunk = chunk + b"\x00" * (frame_bytes - len(chunk))
            return chunk

        try:
            # 阶段1：本地等开口（不连云）
            speech_run = 0
            preroll: list[bytes] = []
            started = False
            for _ in range(wait_frames):
                frame = read_frame()
                if frame is None:
                    break
                if audioop.rms(frame, 2) >= energy_th:
                    speech_run += 1
                    preroll.append(frame)
                    if speech_run >= start_need:
                        started = True
                        break
                else:
                    speech_run = 0
                    preroll.clear()

            if not started:
                print("[ASR] 本轮无人开口，继续听…", flush=True)
                return ""

            client = BaiduRealtimeAsr(
                app_id,
                api_key,
                cuid=str(self.baidu_cfg.get("cuid") or "orangepi-zero3"),
                dev_pid=int(self.asr_cfg.get("stream_dev_pid", 15372)),
                sample_rate=rate,
            )

            def on_partial(text: str) -> None:
                print(f"[ASR] 部分：{text}", flush=True)

            def pcm_after_start() -> Iterator[bytes]:
                silence_run = 0
                sent = 0
                for frame in preroll:
                    yield frame
                    sent += 1
                while sent < max_frames:
                    frame = read_frame()
                    if frame is None:
                        break
                    yield frame
                    sent += 1
                    if audioop.rms(frame, 2) >= energy_th:
                        silence_run = 0
                    else:
                        silence_run += 1
                        if silence_run >= stop_need:
                            break

            text = client.recognize_stream(pcm_after_start(), on_partial=on_partial)
            return text or ""
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

    def _listen_short(self, *, rate: int, device: str, mode: str) -> str:
        with tempfile.TemporaryDirectory() as tmp:
            wav_path = Path(tmp) / "in.wav"
            if mode == "vad" and shutil.which("rec"):
                ok = self._record_vad(wav_path, rate=rate, device=device)
                if not ok:
                    return ""
            else:
                if mode == "vad" and not shutil.which("rec"):
                    print("[ASR] 未安装 sox(rec)，回退固定时长录音", flush=True)
                self._record_fixed(wav_path, rate=rate, device=device)

            if not wav_path.exists() or wav_path.stat().st_size < 1000:
                print("[ASR] 录音过短/空，跳过", flush=True)
                return ""

            raw = wav_path.read_bytes()
            gain_db = float(self.asr_cfg.get("input_gain_db", 6))
            if gain_db and shutil.which("sox"):
                boosted = Path(tmp) / "boost.wav"
                try:
                    subprocess.run(
                        ["sox", str(wav_path), str(boosted), "gain", str(gain_db)],
                        check=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                    if boosted.exists() and boosted.stat().st_size > 1000:
                        raw = boosted.read_bytes()
                except subprocess.CalledProcessError:
                    pass
            text = self._client().recognize_wav(raw, rate=rate)
        print(f"[ASR] 语音识别结果：{text}", flush=True)
        return text

    def _record_fixed(self, wav_path: Path, rate: int, device: str) -> None:
        seconds = int(self.asr_cfg.get("record_seconds", 8))
        print(f"…聆听中（固定 {seconds}s）…", flush=True)
        cmd = [
            "arecord",
            "-d",
            str(seconds),
            "-f",
            "S16_LE",
            "-r",
            str(rate),
            "-c",
            "1",
            "-t",
            "wav",
            str(wav_path),
        ]
        if device:
            cmd[1:1] = ["-D", device]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def _record_vad(self, wav_path: Path, rate: int, device: str) -> bool:
        """一直等到开口 → 说完静音自动停。超时无语音则返回 False。"""
        wait_speech = float(self.asr_cfg.get("vad_wait_speech_sec", 20))
        max_seconds = float(self.asr_cfg.get("max_record_seconds", 15))
        start_dur = float(self.asr_cfg.get("vad_start_sec", 0.2))
        stop_sil = float(self.asr_cfg.get("vad_stop_silence_sec", 0.9))
        thresh = str(self.asr_cfg.get("vad_threshold", "2%"))
        print(
            f"…连续聆听（开口即录，停顿约 {stop_sil:.1f}s 结束；"
            f"等待开口≤{wait_speech:.0f}s，单句≤{max_seconds:.0f}s）…",
            flush=True,
        )
        cmd = [
            "rec",
            "-q",
            "-r",
            str(rate),
            "-c",
            "1",
            "-b",
            "16",
            str(wav_path),
            "silence",
            "1",
            str(start_dur),
            thresh,
            "1",
            str(stop_sil),
            thresh,
            "trim",
            "0",
            str(max_seconds),
        ]
        env = os.environ.copy()
        if device:
            env["AUDIODEV"] = device
        try:
            subprocess.run(
                cmd,
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env=env,
                timeout=wait_speech + max_seconds + 3,
            )
        except subprocess.TimeoutExpired:
            print("[ASR] 本轮无人开口，继续听…", flush=True)
            return False
        except subprocess.CalledProcessError:
            print("[ASR] VAD 异常，跳过本轮", flush=True)
            return False

        return wav_path.exists() and wav_path.stat().st_size >= 1000
