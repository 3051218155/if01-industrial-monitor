#!/usr/bin/env python3
"""Run the live 8-sample condition model on the HARDNPU."""
from __future__ import annotations

import argparse
import fcntl
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parent
MODEL = ROOT / "condition_runtime_npu16_9x9_qdq_inferred.onnx"
PREPROCESS = ROOT / "runtime_preprocess.json"
BINARY = ROOT / "generic_npu_run_strict"
CONDITIONS = ("stopped", "idle", "normal", "overload", "fault")
CONDITION_LABELS_ZH = ("停机", "空载", "正常带载", "过载", "异常")
WINDOW_SIZE = 8
FIELDS_PER_SAMPLE = 5
CHANNELS = 32
SPATIAL = 9
INPUT_SCALE = 0.06000110134482384
INPUT_ZERO_POINT = 130
OUTPUT_SCALE = 0.1790635883808136
OUTPUT_ZERO_POINT = 116
NPU_READY_MIN_UPTIME_S = 30.0
NPU_LOCK_PATH = Path("/run/fpga-condition-npu.lock")
NPU_INFERENCE_TIMEOUT_S = 10.0


def wait_for_npu_ready() -> None:
    """Avoid programming NPU before the cold-boot clock/reset path settles."""
    try:
        uptime_s = float(Path("/proc/uptime").read_text().split()[0])
    except (OSError, ValueError, IndexError):
        return
    remaining_s = NPU_READY_MIN_UPTIME_S - uptime_s
    if remaining_s > 0:
        time.sleep(remaining_s)


def base_features(values: list[float]) -> list[float]:
    rms, peak, _, current, pwm = values
    return [
        rms,
        peak,
        peak / max(rms, 0.02),
        current,
        pwm,
        current / max(pwm, 0.01),
        rms / max(pwm, 0.01),
        peak / max(pwm, 0.01),
    ]


def window_features(samples: list[list[float]]) -> list[float]:
    if len(samples) != WINDOW_SIZE:
        raise ValueError(f"expected {WINDOW_SIZE} samples, got {len(samples)}")
    rows = [base_features(sample) for sample in samples]
    columns = list(zip(*rows))
    last = rows[-1]
    mean = [sum(column) / len(column) for column in columns]
    trend = [last[index] - rows[0][index] for index in range(len(last))]
    return last + mean + trend


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--values", required=True)
    args = parser.parse_args()

    raw_values = [float(value.strip()) for value in args.values.split(",") if value.strip()]
    expected = WINDOW_SIZE * FIELDS_PER_SAMPLE
    if len(raw_values) != expected or not all(map(math.isfinite, raw_values)):
        raise SystemExit(f"expected {expected} finite values, got {len(raw_values)}")
    samples = [
        raw_values[index : index + FIELDS_PER_SAMPLE]
        for index in range(0, expected, FIELDS_PER_SAMPLE)
    ]
    features = window_features(samples)

    preprocess = json.loads(PREPROCESS.read_text(encoding="utf-8"))
    mean = preprocess["mean"]
    scale = preprocess["scale"]
    normalized = [
        (value - mean[index]) / scale[index]
        for index, value in enumerate(features)
    ]

    quantized = bytearray(CHANNELS * SPATIAL * SPATIAL)
    for channel in range(CHANNELS):
        value = normalized[channel] if channel < len(normalized) else 0.0
        quantized_value = max(
            0,
            min(255, int(round(value / INPUT_SCALE + INPUT_ZERO_POINT))),
        )
        start = channel * SPATIAL * SPATIAL
        quantized[start : start + SPATIAL * SPATIAL] = bytes(
            [quantized_value]
        ) * (SPATIAL * SPATIAL)

    with tempfile.NamedTemporaryFile(suffix=".u8", delete=False) as stream:
        input_path = Path(stream.name)
        stream.write(quantized)

    environment = {
        **os.environ,
        "LD_LIBRARY_PATH": str(ROOT / "lib"),
    }
    lock_fd = os.open(NPU_LOCK_PATH, os.O_CREAT | os.O_RDWR, 0o600)
    try:
        # The gateway and Modbus service poll independently. The NPU driver
        # accepts only one owner at a time, so serialize every strict run.
        fcntl.flock(lock_fd, fcntl.LOCK_EX)
        try:
            wait_for_npu_ready()
            completed = subprocess.run(
                [str(BINARY), str(MODEL), str(input_path)],
                check=True,
                capture_output=True,
                text=True,
                env=environment,
                timeout=NPU_INFERENCE_TIMEOUT_S,
            )
        finally:
            fcntl.flock(lock_fd, fcntl.LOCK_UN)
    finally:
        os.close(lock_fd)
        input_path.unlink(missing_ok=True)

    text = completed.stdout.strip()
    latency_match = re.search(r"average_latency_ms=([0-9.]+)", text)
    values_match = re.search(r"values=([0-9,]+)", text)
    if (
        "backend=Alnpu-strict" not in text
        or latency_match is None
        or values_match is None
    ):
        raise RuntimeError(f"unexpected NPU output: {text}")
    raw = [
        int(value)
        for value in values_match.group(1).split(",")
        if value
    ]
    class_values = [
        raw[index * SPATIAL * SPATIAL]
        for index in range(len(CONDITIONS))
    ]
    logits = [
        (value - OUTPUT_ZERO_POINT) * OUTPUT_SCALE
        for value in class_values
    ]
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
        "runtime": "board Alnpu strict",
        "device": "/dev/hard_npu",
    }
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
