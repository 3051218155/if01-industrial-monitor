#!/usr/bin/env python3
"""测试 DeepSeek 官方接口：python tools/test_llm_intent.py \"显示3号轴承故障\""""
from __future__ import annotations

import json
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from llm_intent import LlmIntent


def main() -> None:
    cfg = yaml.safe_load((ROOT / "config.yaml").read_text(encoding="utf-8"))
    text = " ".join(sys.argv[1:]) or "显示3号轴承故障"
    llm = LlmIntent(cfg.get("llm"))
    print("base_url:", cfg.get("llm", {}).get("base_url"))
    print("model:", cfg.get("llm", {}).get("model"))
    print("input:", text)
    print(json.dumps(llm.parse(text), ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
