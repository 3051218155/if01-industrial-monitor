#!/usr/bin/env python3
"""香橙派语音控制主程序

WAIT_WAKE  静默听唤醒词（小橙/小陈…）
ACK        短应答「在」
LISTEN_CMD 听完整指令（VAD 说完即停）
PROCESS    语音提示「正在解析处理」+ LLM/工具
SPEAK      播报结果
→ 回到 WAIT_WAKE

常用参数：
  python main.py                        # 正常语音模式
  python main.py --gateway 192.168.1.100   # 临时换网关，不改配置文件
  python main.py --keyboard             # 键盘模式（无麦克风调试）
  python main.py --no-llm               # 只用规则解析，不调大模型
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import yaml

EXIT_WORDS = ("退出程序", "退出", "停止运行", "结束程序", "拜拜")
SHUTDOWN_WORDS = ("关机", "关闭电源", "请关机", "系统关机", "关掉机器")

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from asr.engine import AsrEngine
from ext_display.display import ExtDisplay
from intent_exec import execute_intent, parse_local_command
from llm_intent import LlmIntent
from pc_bridge import handle_asr_text
from servo_control.servo import ServoController
from tts.engine import TtsEngine


def load_config() -> dict:
    return yaml.safe_load((ROOT / "config.yaml").read_text(encoding="utf-8"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="香橙派语音控制终端（语音 / 键盘）")
    parser.add_argument("--gateway", help="临时覆盖网关 IP，例如 192.168.1.100")
    parser.add_argument("--port", type=int, help="临时覆盖网关端口（默认读 config.yaml）")
    parser.add_argument("--pc", help="临时覆盖 PC 大屏 IP")
    parser.add_argument("--keyboard", action="store_true", help="键盘模式：不开麦，直接输入指令")
    parser.add_argument("--no-llm", action="store_true", help="禁用大模型，只用规则解析")
    return parser.parse_args()


def apply_overrides(args: argparse.Namespace) -> dict:
    """配置单一来源：robot/config.yaml。

    通过环境变量把覆盖值传给 orange_pi_mcp/tools_impl，避免两份 config 各写一个 IP。
    """
    os.environ["FPAG_CONFIG"] = str(ROOT / "config.yaml")
    if args.gateway:
        os.environ["FPAG_GATEWAY_HOST"] = args.gateway
    if args.pc:
        os.environ["FPAG_PC_HOST"] = args.pc
    if args.port:
        os.environ["FPAG_GATEWAY_PORT"] = str(args.port)

    cfg = load_config()
    if args.gateway:
        cfg["gateway"]["host"] = args.gateway
    if args.port:
        cfg["gateway"]["port"] = args.port
    if args.pc:
        cfg.setdefault("pc_display", {})["host"] = args.pc
        cfg["pc_display"]["enabled"] = True
    if args.no_llm:
        cfg.setdefault("llm", {})["enabled"] = False
    return cfg


def _match_wake(text: str, wake_words: list[str]) -> str | None:
    """唤醒匹配：精确子串 + 谐音/常见误识别。"""
    t = (text or "").strip()
    if not t:
        return None
    for w in wake_words:
        if w and w in t:
            return w
    # 「小X」类谐音（百度常把「橙」认成陈/成/程/城/乘…）
    if re.search(r"小[橙陈成程尘撑乘城承呈]", t):
        return "小橙"
    if re.search(r"你好.?小[橙陈成程尘撑乘城承呈]", t):
        return "你好小橙"
    if re.search(r"晓[橙陈成程]", t):
        return "小橙"
    # 极短口头禅：单独「小橙」被听成近似两字时
    compact = re.sub(r"[，,。.!！？?\s]", "", t)
    if compact in {"小橙", "小陈", "小成", "小程", "小城", "小乘", "晓橙"}:
        return "小橙"
    return None


def _strip_wake(text: str, wake_words: list[str]) -> str:
    text2 = text
    for w in wake_words + ["小橙", "小陈", "小成", "小程", "小尘", "晓橙", "你好"]:
        text2 = text2.replace(w, "")
    # 去掉「在吗」类口头禅，避免当成指令跳过听令
    text2 = re.sub(r"(在吗|在不在|你在吗|你好吗|吗|呀|啊|呢)+", "", text2)
    return text2.strip(" ，,。.!！？?")


def _looks_like_command(text: str) -> bool:
    """同句是否已带可执行指令（否则唤醒后必须再听一轮）。"""
    t = (text or "").strip()
    if len(t) < 2:
        return False
    keys = (
        "显示", "查询", "查一下", "查下", "查", "状态", "怎么样", "怎样", "如何",
        "好不好", "故障", "看看", "打开", "切换", "大屏",
        "轴承", "轴成", "不平衡", "地脚", "松动", "联轴", "正常", "恢复",
        "一号", "二号", "三号", "四号", "五号", "六号",
        "1号", "2号", "3号", "4号", "5号", "6号", "号机", "号设", "号轴",
        "在线", "连通", "退出", "停止", "关机",
        # PC 大屏命令（同句唤醒+指令时也要能识别）
        "启动", "开启", "电机", "马达", "报告", "串口", "分析", "诊断",
        "选择", "选中", "设备列表", "全部设备", "所有设备",
    )
    return any(k in t for k in keys)


def _want_exit(text: str) -> bool:
    return any(k in (text or "") for k in EXIT_WORDS)


def _want_shutdown(text: str) -> bool:
    return any(k in (text or "") for k in SHUTDOWN_WORDS)


def _power_off(say) -> None:
    """播报后系统关机（需 sudo 免密 poweroff）。"""
    print("[状态] SHUTDOWN 关机…", flush=True)
    say("正在关机，再见")
    time.sleep(0.5)
    for cmd in (
        ["sudo", "-n", "poweroff"],
        ["sudo", "-n", "/sbin/poweroff"],
        ["sudo", "-n", "shutdown", "-h", "now"],
    ):
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=8)
            if r.returncode == 0:
                sys.exit(0)
            print(f"[SHUTDOWN] 失败 {cmd}: {(r.stderr or r.stdout).strip()}", flush=True)
        except Exception as exc:
            print(f"[SHUTDOWN] 异常 {cmd}: {exc}", flush=True)
    say("关机失败，请检查 sudo 免密")
    sys.exit(1)


def main() -> None:
    args = parse_args()
    cfg = apply_overrides(args)
    keyboard_mode = bool(args.keyboard)

    baidu_cfg = cfg.get("baidu") or {}
    asr_cfg = dict(cfg.get("asr") or {})
    llm_cfg = dict(cfg.get("llm") or {})
    tts_cfg = dict(cfg.get("tts") or {})

    # 唤醒/指令共用 record_mode（stream=实时 WebSocket；vad/fixed=短语音）
    record_mode = str(asr_cfg.get("record_mode") or "stream").lower()
    asr_wake: AsrEngine | None = None
    asr_cmd: AsrEngine | None = None
    if not keyboard_mode:
        asr_wake = AsrEngine(
            asr_cfg.get("provider", "baidu"),
            baidu_cfg,
            {
                **asr_cfg,
                "record_mode": record_mode,
                "max_record_seconds": float(asr_cfg.get("wake_max_record_seconds", 6)),
                "vad_wait_speech_sec": float(asr_cfg.get("wake_wait_speech_sec", 25)),
                "vad_stop_silence_sec": float(asr_cfg.get("wake_vad_stop_sec", 0.7)),
                "vad_start_sec": float(asr_cfg.get("wake_vad_start_sec", 0.15)),
                "pre_record_delay_sec": float(asr_cfg.get("wake_pre_delay_sec", 0.05)),
            },
        )
        asr_cmd = AsrEngine(
            asr_cfg.get("provider", "baidu"),
            baidu_cfg,
            {
                **asr_cfg,
                "record_mode": record_mode,
                "max_record_seconds": float(asr_cfg.get("max_record_seconds", 15)),
                "vad_wait_speech_sec": float(asr_cfg.get("cmd_wait_speech_sec", 12)),
                "vad_stop_silence_sec": float(asr_cfg.get("cmd_stop_silence_sec", 0.9)),
                "pre_record_delay_sec": float(asr_cfg.get("cmd_pre_delay_sec", 0.1)),
            },
        )
    cmd_listen_retries = int(asr_cfg.get("cmd_listen_retries", 3))

    tts = TtsEngine(tts_cfg.get("provider", "baidu"), baidu_cfg, tts_cfg)
    llm = LlmIntent(llm_cfg)
    servo = ServoController(
        enabled=cfg["servo"].get("enabled", False),
        open_angle=cfg["servo"].get("eye_open_angle", 90),
        close_angle=cfg["servo"].get("eye_close_angle", 0),
    )
    display = ExtDisplay(
        assets_dir=str(ROOT / cfg["display"]["assets_dir"]),
        width=cfg["display"]["width"],
        height=cfg["display"]["height"],
        fullscreen=cfg["display"].get("fullscreen", False),
    )

    pc_cfg = cfg.get("pc_display") or {}
    pc_enabled = bool(pc_cfg.get("enabled", False))
    mcp_dir = pc_cfg.get("mcp_dir")
    post_tts_delay = float(asr_cfg.get("post_tts_delay_sec", 1.0))
    wake_words = [str(w) for w in (asr_cfg.get("wake_words") or []) if str(w).strip()]
    require_wake = bool(asr_cfg.get("require_wake", True)) and bool(wake_words)
    use_llm = bool(llm_cfg.get("enabled", True) and llm_cfg.get("api_key"))

    prompt_enabled = bool(tts_cfg.get("prompt_enabled", True))
    boot_prompt = str(tts_cfg.get("boot_prompt") or "机器人已启动，请说小橙唤醒我")
    ack_prompt = str(tts_cfg.get("ack_prompt") or "在")
    processing_prompt = str(tts_cfg.get("processing_prompt") or "正在解析处理")
    prompt_gap = float(tts_cfg.get("prompt_gap_sec", 0.7))

    def say(msg: str, gap: float | None = None) -> None:
        if not msg:
            return
        tts.speak(msg)
        time.sleep(gap if gap is not None else post_tts_delay)

    def next_command() -> str:
        """再听一轮指令：语音模式走 ASR，键盘模式读一行。"""
        if keyboard_mode:
            try:
                return input("再说一次> ").strip()
            except (EOFError, KeyboardInterrupt):
                return ""
        assert asr_cmd is not None
        return asr_cmd.listen_continuous(max_attempts=cmd_listen_retries)

    def process_command(cmd_text: str) -> tuple[str, dict | None, bool]:
        """PROCESS：大模型或规则解析 → 执行工具。

        返回 (播报文本, 拓展屏数据, 是否退出程序)。
        """
        listen_retries = 0
        answer = ""
        data = None
        quit_app = False

        while True:
            # PC 大屏统一命令（电机/报告/串口/分析/选设备）：命中直接执行，
            # 不念「正在解析处理」，少一轮 TTS，明显更跟手。
            local = parse_local_command(cmd_text, mcp_dir)
            if local:
                out = execute_intent(local, mcp_dir=mcp_dir, pc_enabled=pc_enabled)
                print(f"[EXEC] {out}", flush=True)
                answer = str(out.get("reply") or "")
                data = out.get("data")
                break

            print("[状态] PROCESS 解析中…", flush=True)
            if prompt_enabled and not keyboard_mode and listen_retries == 0:
                say(processing_prompt, gap=prompt_gap)

            if use_llm:
                intent = llm.parse(cmd_text)
                print(f"[LLM] {intent}", flush=True)
                if intent.get("source") == "llm_error":
                    print(f"[LLM] 失败回退规则：{intent.get('error')}", flush=True)
                    if pc_enabled:
                        out = handle_asr_text(cmd_text, mcp_dir=mcp_dir)
                        print(f"[PC/MCP] {out}", flush=True)
                        reply = str(out.get("reply") or "")
                        if out.get("silent") or (not reply) or ("请说例如" in reply):
                            say("模型超时，请再说一遍")
                            break
                        answer = reply
                        data = None
                        break
                    say("模型超时，请再说一遍")
                    break

                # 已唤醒过：模型仍判 wake_only / ignore → 再听指令，不重复念「在」
                if intent.get("action") in {"wake_only", "ignore"} and listen_retries < 1:
                    listen_retries += 1
                    print("[状态] LISTEN_CMD 再听指令…", flush=True)
                    cmd_text = next_command()
                    print(f"[ASR] 指令窗：{cmd_text!r}", flush=True)
                    if not cmd_text:
                        say("没有听清指令")
                        break
                    if _want_shutdown(cmd_text):
                        _power_off(say)
                    if _want_exit(cmd_text):
                        say("已退出")
                        quit_app = True
                        break
                    continue

                out = execute_intent(intent, mcp_dir=mcp_dir, pc_enabled=pc_enabled)
                print(f"[EXEC] {out}", flush=True)
                if out.get("silent") or intent.get("action") in {"ignore", "wake_only"}:
                    say("没有听清指令")
                    break
                answer = str(out.get("reply") or "")
                data = out.get("data")
                if intent.get("action") in {"query", "show", "ping"} and out.get("ok"):
                    print("[状态] NARRATE 模型讲解中…", flush=True)
                    spoken = llm.narrate(cmd_text, out)
                    if spoken:
                        print(f"[LLM] 讲解：{spoken}", flush=True)
                        answer = spoken
                break

            if pc_enabled:
                out = handle_asr_text(cmd_text, mcp_dir=mcp_dir)
                print(f"[PC/MCP] {out}", flush=True)
                reply = str(out.get("reply") or "")
                if out.get("silent") or (not reply) or ("请说例如" in reply):
                    say("没有听清指令")
                    break
                answer = reply
            if not answer:
                say("没有听清指令")
            break

        return answer, data, quit_app

    print("=== 语音状态机（连续聆听）===")
    print("WAIT_WAKE → ACK → LISTEN_CMD → PROCESS → SPEAK → WAIT_WAKE")
    print(f"网关：{cfg['gateway']['host']}:{cfg['gateway']['port']}   大屏：{'开' if pc_enabled else '关'}")
    if keyboard_mode:
        print("聆听：键盘模式（输入指令回车执行，q 退出）")
    else:
        print(f"唤醒词：{' / '.join(wake_words) if require_wake else '关闭'}")
        print(f"聆听：mode={record_mode}（stream=边说边识；vad=录完短语音）")
    print(f"LLM={'开 ' + str(llm_cfg.get('model')) if use_llm else '关'}")
    if not use_llm and not pc_enabled:
        print("[警告] 大模型未启用，且 pc_display.enabled=false —— 指令无法执行。")
        print("       请填 llm.api_key，或在 config.yaml 里把 pc_display.enabled 改成 true。")
    print("Ctrl+C 退出")

    if prompt_enabled and not keyboard_mode:
        say(boot_prompt, gap=prompt_gap)

    try:
        while True:
            if keyboard_mode:
                # ---------- 键盘输入（无麦克风时直接输入指令）----------
                try:
                    text = input("你> ").strip()
                except (EOFError, KeyboardInterrupt):
                    print()
                    break
                if not text:
                    continue
                if text.lower() in {"q", "quit", "exit"}:
                    break
                cmd_text = text
                if _want_shutdown(cmd_text):
                    _power_off(say)
                if _want_exit(cmd_text):
                    say("已退出")
                    break
            else:
                # ---------- WAIT_WAKE：静默等唤醒（不每轮念「请说指令」）----------
                print("[状态] WAIT_WAKE 静默聆听唤醒…", flush=True)
                assert asr_wake is not None
                text = asr_wake.listen()
                if not text:
                    continue
                print(f"[ASR] 唤醒窗：{text!r}", flush=True)
                if _want_shutdown(text):
                    _power_off(say)
                if _want_exit(text):
                    say("已退出")
                    break

                cmd_text = text
                if require_wake:
                    hit = _match_wake(text, wake_words)
                    if not hit:
                        continue
                    cmd_text = _strip_wake(text, wake_words)
                    # ---------- ACK ----------
                    print("[状态] ACK 唤醒应答", flush=True)
                    if prompt_enabled:
                        say(ack_prompt, gap=prompt_gap)
                    # 唤醒后连续听指令（空/噪声自动再听）；同句已带真指令则跳过
                    if not _looks_like_command(cmd_text):
                        print("[状态] LISTEN_CMD 连续听指令…", flush=True)
                        cmd_text = next_command()
                        print(f"[ASR] 指令窗：{cmd_text!r}", flush=True)
                        if not cmd_text:
                            say("没有听清")
                            continue
                else:
                    print("[状态] LISTEN_CMD（无唤醒·连续）…", flush=True)
                    cmd_text = next_command()
                    if not cmd_text:
                        continue

                if _want_shutdown(cmd_text):
                    _power_off(say)
                if _want_exit(cmd_text):
                    say("已退出")
                    break

            # ---------- PROCESS / SPEAK ----------
            answer, data, quit_app = process_command(cmd_text)
            if quit_app:
                break
            if not answer or "请说例如" in answer:
                continue

            print("[状态] SPEAK 播报结果", flush=True)
            say(answer)
            servo.eyes_open()
            if data:
                display.show_status(
                    int(data["device_id"]),
                    str(data.get("status", "normal")),
                    str(data.get("fault_location") or ""),
                )
            # 回到 WAIT_WAKE
    except KeyboardInterrupt:
        print("\n退出")
    finally:
        display.close()


if __name__ == "__main__":
    main()
