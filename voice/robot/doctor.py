#!/usr/bin/env python3
"""启动自检：集中检查设备节点、网络和运行依赖。

在板子上运行：
    cd ~/robot && source .venv/bin/activate && python doctor.py

退出码：0 = 无致命问题；1 = 有关键项失败（网关/配置/麦克风）。
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent
OK = 0
WARN = 0
BAD = 0


def ok(msg: str) -> None:
    global OK
    OK += 1
    print(f"[ OK ] {msg}")


def warn(msg: str) -> None:
    global WARN
    WARN += 1
    print(f"[ !! ] {msg}")


def bad(msg: str) -> None:
    global BAD
    BAD += 1
    print(f"[ XX ] {msg}")


def load_cfg() -> dict:
    path = Path(os.environ.get("FPAG_CONFIG") or (ROOT / "config.yaml"))
    cfg = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    if host := os.environ.get("FPAG_GATEWAY_HOST"):
        cfg.setdefault("gateway", {})["host"] = host
    if port := os.environ.get("FPAG_GATEWAY_PORT"):
        cfg.setdefault("gateway", {})["port"] = int(port)
    if host := os.environ.get("FPAG_PC_HOST"):
        cfg.setdefault("pc_display", {})["host"] = host
    return cfg


def check_env() -> None:
    ver = sys.version.split()[0]
    if sys.prefix != sys.base_prefix:
        ok(f"Python {ver}（虚拟环境 {sys.prefix}）")
    else:
        warn(f"Python {ver}（未激活虚拟环境，先 source .venv/bin/activate）")
    if shutil.which("sox"):
        ok("sox 已安装（录音依赖）")
    else:
        bad("未找到 sox：sudo apt install -y sox alsa-utils")


def check_audio(cfg: dict) -> None:
    asr_dev = str((cfg.get("asr") or {}).get("device") or "")
    tts_dev = str((cfg.get("tts") or {}).get("device") or "")

    for tool, flag in (("arecord", "-l"), ("aplay", "-l")):
        exe = shutil.which(tool)
        if not exe:
            bad(f"未找到 {tool}：sudo apt install -y alsa-utils")
            continue
        try:
            out = subprocess.run(
                [exe, flag], capture_output=True, text=True, timeout=5
            ).stdout
        except Exception as exc:  # noqa: BLE001
            warn(f"{tool} {flag} 执行失败：{exc}")
            continue
        if not out.strip():
            warn(f"{tool}：没有找到声卡（USB 麦/音箱插好了吗）")
            continue
        ok(f"{tool}：{_summarize_cards(out)}")

    if asr_dev:
        if _device_present(asr_dev):
            ok(f"麦克风节点 {asr_dev} 存在")
        else:
            warn(f"配置里的麦克风 {asr_dev} 不在 arecord -l 里，按实际改 asr.device")
    if tts_dev:
        if _device_present(tts_dev):
            ok(f"扬声器节点 {tts_dev} 存在")
        else:
            warn(f"配置里的扬声器 {tts_dev} 不在 aplay -l 里，按实际改 tts.device")


def _summarize_cards(text: str) -> str:
    cards = [line.strip() for line in text.splitlines() if line.startswith("card ")]
    return "；".join(cards[:4]) if cards else "有声卡"


def _device_present(device: str) -> bool:
    """plughw:3,0 → arecord -l 里是否有 card 3 / device 0。"""
    try:
        card, dev = device.split(":")[1].split(",")
    except (IndexError, ValueError):
        return False
    exe = shutil.which("arecord")
    if not exe:
        return False
    try:
        out = subprocess.run([exe, "-l"], capture_output=True, text=True, timeout=5).stdout
    except Exception:  # noqa: BLE001
        return False
    return f"card {card}:" in out and f"device {dev}:" in out


def _tcp_query(host: str, port: int, payload: dict, timeout: float = 3.0) -> dict:
    raw = (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8")
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(raw)
        chunks = b""
        while True:
            buf = sock.recv(4096)
            if not buf:
                break
            chunks += buf
            if b"\n" in buf or b"}" in buf:
                break
    text = chunks.decode("utf-8", errors="replace").strip()
    return json.loads(text.splitlines()[-1]) if text else {}


def check_gateway(cfg: dict) -> None:
    g = cfg.get("gateway") or {}
    host, port = g.get("host"), int(g.get("port", 9000))
    if not host:
        bad("config.yaml 缺 gateway.host")
        return
    try:
        resp = _tcp_query(host, port, {"cmd": "query_device", "device_id": 3}, timeout=3)
    except Exception as exc:  # noqa: BLE001
        bad(f"网关 {host}:{port} 连不上：{exc}")
        print("       检查：对方 fake_gateway/真网关是否在跑、IP 是否正确、防火墙 9000")
        return

    if resp.get("code") == 0 and isinstance(resp.get("data"), dict):
        d = resp["data"]
        ok(f"网关 {host}:{port} 正常：{d.get('device_name', '3号设备')} {d.get('status_text', d.get('status'))}")
    elif "state" in resp:
        ok(f"网关 {host}:{port} 正常：3号 {resp.get('state')}（PC 格式）")
    else:
        warn(f"网关 {host}:{port} 有响应但格式不认识：{str(resp)[:120]}")


def check_pc(cfg: dict) -> None:
    pc = cfg.get("pc_display") or {}
    host = str(pc.get("host") or "").strip()
    port = int(pc.get("port", 8760))
    if not host:
        warn("pc_display.host 未配置 —— 填 PC IP，或运行时用 --pc IP（会自动启用）")
        return
    if not pc.get("enabled"):
        warn(f"pc_display.enabled=false —— host={host}，但不会控制大屏（要控就改 true）")
        return

    base = f"http://{host}:{port}"
    timeout = float(pc.get("timeout_sec", 3))
    try:
        import requests

        r = requests.get(f"{base}/health", timeout=timeout)
    except Exception as exc:  # noqa: BLE001
        bad(f"PC 大屏 {base} 不可达：{exc}")
        return
    if r.status_code < 400:
        ok(f"PC 大屏 {base} 可达（/health HTTP {r.status_code}）")
    else:
        bad(f"PC 大屏 {base} /health 返回 HTTP {r.status_code}")


def check_llm(cfg: dict, deep: bool) -> None:
    llm = cfg.get("llm") or {}
    key = str(llm.get("api_key") or "")
    if not llm.get("enabled", True):
        warn("LLM 已禁用（llm.enabled=false），只会走规则解析")
        return
    if not key:
        bad("llm.api_key 为空 —— 语音指令无法理解")
        return
    ok(f"LLM {llm.get('model')}，Key {key[:6]}…{key[-4:]}")
    if not deep:
        return
    print("       （--llm 深度测试，正在请求模型…）")
    try:
        sys.path.insert(0, str(ROOT))
        from llm_intent import LlmIntent

        intent = LlmIntent(llm).parse("查3号机器状态")
        ok(f"LLM 调用正常：{intent.get('action')} device={intent.get('device_id')}")
    except Exception as exc:  # noqa: BLE001
        bad(f"LLM 调用失败：{exc}")


def check_display(cfg: dict) -> None:
    d = cfg.get("display") or {}
    assets = ROOT / str(d.get("assets_dir") or "assets/devices")
    try:
        import pygame  # noqa: F401

        ok("pygame 已安装")
    except ImportError:
        bad("pygame 未安装：pip install pygame")
    if not assets.is_dir():
        bad(f"素材目录不存在：{assets}")
        return
    imgs = [p for p in assets.iterdir() if p.suffix.lower() in {".png", ".jpg", ".jpeg"}]
    if imgs:
        ok(f"拓展屏素材 {len(imgs)} 张：{', '.join(p.name for p in imgs[:4])}")
    else:
        warn(f"素材目录为空（{assets}）：拓展屏将显示色块提示")
    if d.get("fullscreen"):
        print("       提示：接第二块屏时设 SDL_VIDEO_FULLSCREEN_DISPLAY=1")


def check_system() -> None:
    try:
        usage = shutil.disk_usage(str(ROOT.anchor or "/"))
        free_gb = usage.free / 1024**3
        if free_gb < 1:
            warn(f"磁盘剩余 {free_gb:.1f}GB，可能不够写日志")
        else:
            ok(f"磁盘剩余 {free_gb:.1f}GB")
    except Exception as exc:  # noqa: BLE001
        warn(f"磁盘检查失败：{exc}")

    meminfo = Path("/proc/meminfo")
    if meminfo.is_file():
        info = {}
        for line in meminfo.read_text().splitlines():
            k, _, v = line.partition(":")
            info[k] = int(v.strip().split()[0]) // 1024
        ok(f"内存 {info.get('MemTotal', 0)}MB（可用 {info.get('MemAvailable', 0)}MB）")


def check_servo(cfg: dict) -> None:
    servo = cfg.get("servo") or {}
    if servo.get("enabled"):
        ok("舵机已启用（确认独立 5V 供电、与板子共地）")
    else:
        warn("servo.enabled=false —— 不会睁眼（接线完再改 true）")


def main() -> int:
    parser = argparse.ArgumentParser(description="机器人启动自检")
    parser.add_argument("--llm", action="store_true", help="额外请求一次大模型验证 Key")
    args = parser.parse_args()

    print("=== 机器人启动自检 ===")
    try:
        cfg = load_cfg()
        ok(f"配置已读取：{os.environ.get('FPAG_CONFIG') or ROOT / 'config.yaml'}")
    except Exception as exc:  # noqa: BLE001
        print(f"[ XX ] 配置读取失败：{exc}")
        return 1

    check_env()
    check_audio(cfg)
    check_gateway(cfg)
    check_pc(cfg)
    check_llm(cfg, args.llm)
    check_display(cfg)
    check_servo(cfg)
    check_system()

    print(f"\n结果：{OK} 项正常，{WARN} 项提醒，{BAD} 项失败")
    if BAD:
        print("先把 [ XX ] 解决，再运行 python main.py")
        return 1
    print("可以直接运行：python main.py（或 python main.py --keyboard 先试键盘）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
