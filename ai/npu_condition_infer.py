#!/usr/bin/env python3
"""Run the 40-feature condition model on the on-board Alnpu device."""
from __future__ import annotations

import argparse
from array import array
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent
RUNTIME = ROOT / "runtime_condition_infer.py"
PREPROCESS = ROOT / "preprocess.json"
CONDITIONS = ("stopped", "idle", "normal", "overload", "fault")
CONDITION_LABELS_ZH = ("停机", "空载", "正常带载", "过载", "异常")


def base_features(sample: dict) -> list[float]:
    points = [float(value) for value in sample["points"]]
    point_mean = sum(points) / len(points)
    point_std = math.sqrt(
        sum((value - point_mean) ** 2 for value in points) / len(points)
    )
    point_range = max(points) - min(points)
    point_max = max(points)
    temperature = float(sample["temperature_c"])
    current = float(sample["current_a"])
    rms = float(sample["rms"])
    peak = float(sample["peak"])
    crest = peak / max(rms, 0.02)
    dominant = point_max / max(point_mean, 0.02)
    return [
        point_mean,
        point_std,
        point_range,
        point_max,
        temperature,
        current,
        rms,
        peak,
        crest,
        dominant,
    ]


def window_features(samples: list[dict]) -> list[float]:
    if len(samples) != 8:
        raise ValueError(f"expected 8 samples, got {len(samples)}")
    rows = [base_features(sample) for sample in samples]
    columns = list(zip(*rows))
    last = rows[-1]
    mean = [sum(column) / len(column) for column in columns]
    std = [
        math.sqrt(sum((value - avg) ** 2 for value in column) / len(column))
        for column, avg in zip(columns, mean)
    ]
    trend = [last[index] - rows[0][index] for index in range(len(last))]
    return last + mean + std + trend


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=Path, required=True)
    args = parser.parse_args()

    raw = json.loads(args.samples.read_text(encoding="utf-8"))
    if isinstance(raw, dict):
        run_id = str(raw.get("run_id", ""))
        raw = raw["samples"]
    else:
        run_id = ""
    if len(raw) != 8:
        raise ValueError(f"expected 8 samples, got {len(raw)}")

    condition = run_id.split("_", 1)[0]
    pwm_defaults = {
        "stopped": 0.0,
        "idle": 0.9,
        "normal": 0.95,
        "overload": 0.98,
        "fault": 0.9,
    }
    if condition not in pwm_defaults:
        raise ValueError(f"unknown condition sample: {run_id}")
    values = []
    for sample in raw:
        motor_pwm = sample.get("motor_pwm")
        pwm = (
            float(motor_pwm) / 1000.0
            if motor_pwm is not None
            else pwm_defaults[condition]
        )
        values.extend(
            [
                float(sample["rms"]),
                float(sample["peak"]),
                float(sample["temperature_c"]),
                float(sample["current_a"]),
                pwm,
            ]
        )

    completed = subprocess.run(
        [
            sys.executable,
            str(RUNTIME),
            "--values",
            ",".join(f"{value:.9g}" for value in values),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    print(completed.stdout, end="")
    return 0

    # Legacy helper functions below are retained for model-training tooling.
    features = window_features(raw)
    preprocess = json.loads(PREPROCESS.read_text(encoding="utf-8"))
    mean = preprocess["mean"]
    scale = preprocess["scale"]
    normalized = [
        (value - mean[index]) / scale[index]
        for index, value in enumerate(features)
    ]

    with tempfile.NamedTemporaryFile(suffix=".f32", delete=False) as stream:
        input_path = Path(stream.name)
        array("f", normalized).tofile(stream)

    environment = {
        **__import__("os").environ,
        "LD_LIBRARY_PATH": str(ROOT / "lib"),
    }
    try:
        completed = subprocess.run(
            [str(BINARY), str(MODEL), str(input_path)],
            check=True,
            capture_output=True,
            text=True,
            env=environment,
        )
    finally:
        input_path.unlink(missing_ok=True)

    text = completed.stdout.strip()
    latency_match = re.search(r"average_latency_ms=([0-9.]+)", text)
    values_match = re.search(r"values=([-0-9.eE,]+)", text)
    if latency_match is None or values_match is None:
        raise RuntimeError(f"unexpected NPU output: {text}")
    logits = [float(value) for value in values_match.group(1).split(",")]
    maximum = max(logits)
    exponentials = [math.exp(value - maximum) for value in logits]
    total = sum(exponentials)
    probabilities = [value / total for value in exponentials]
    best = max(range(len(probabilities)), key=probabilities.__getitem__)
    result = {
        "condition": CONDITIONS[best],
        "condition_zh": CONDITION_LABELS_ZH[best],
        "confidence": probabilities[best],
        "probabilities": dict(zip(CONDITIONS, probabilities)),
        "logits": logits,
        "average_latency_ms": float(latency_match.group(1)),
        "runtime": "board Alnpu",
        "device": "/dev/soft_npu",
    }
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
