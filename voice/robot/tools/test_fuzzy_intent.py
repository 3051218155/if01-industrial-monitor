#!/usr/bin/env python3
"""本地测模糊意图：python tools/test_fuzzy_intent.py"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from llm_intent import fuzzy_parse, normalize_asr

CASES = [
    "三号怎么样",
    "帮我看看三号机",
    "三号轴承故障",
    "轴成坏了显示一下",
    "三好不平衡",
    "把三号恢复正常",
    "连通了吗",
    "三号呢",
    "上课了",
]


def main() -> None:
    extra = " ".join(sys.argv[1:]).strip()
    items = [extra] if extra else CASES
    for c in items:
        print(f"{c!r} -> {normalize_asr(c)!r}")
        print(json.dumps(fuzzy_parse(c), ensure_ascii=False))
        print()


if __name__ == "__main__":
    main()
