"""把 ASR 文字接到 orange_pi_mcp 桥接（控 PC 大屏 + 可选查网关）。"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any


def _ensure_mcp_path(mcp_dir: str | None) -> Path | None:
    candidates = []
    if mcp_dir:
        candidates.append(Path(mcp_dir))
    here = Path(__file__).resolve().parent
    candidates.extend(
        [
            here.parent / "tools" / "orange_pi_mcp",
            Path.home() / "orange_pi_mcp",
            Path.home() / "robot" / ".." / "orange_pi_mcp",
        ]
    )
    for p in candidates:
        p = p.resolve()
        if (p / "voice_mcp_bridge.py").is_file():
            if str(p) not in sys.path:
                sys.path.insert(0, str(p))
            return p
    return None


def handle_asr_text(text: str, mcp_dir: str | None = None) -> dict[str, Any]:
    root = _ensure_mcp_path(mcp_dir)
    if root is None:
        return {
            "ok": False,
            "msg": "未找到 orange_pi_mcp，请把 D:\\FPGA\\tools\\orange_pi_mcp 拷到 ~/orange_pi_mcp",
            "reply": None,
        }
    from voice_mcp_bridge import handle_utterance  # type: ignore

    return handle_utterance(text)
