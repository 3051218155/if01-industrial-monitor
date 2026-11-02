"""Smoke-test float and quantized condition models on six-device windows."""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import onnxruntime as ort

from condition_pipeline import (
    CONDITIONS,
    FAULT_TYPES,
    SAMPLE_PERIOD_S,
    WINDOW_SIZE,
    simulate_sample,
    window_features,
)


ARTIFACTS = Path(__file__).resolve().parent / "artifacts" / "condition_mlp"


def infer(path: Path, features: np.ndarray, preprocess: dict) -> np.ndarray:
    mean = np.asarray(preprocess["mean"], dtype=np.float32)
    scale = np.asarray(preprocess["scale"], dtype=np.float32)
    normalized = ((features - mean) / scale).astype(np.float32)
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    input_name = session.get_inputs()[0].name
    return session.run(None, {input_name: normalized})[0][0]


def main() -> int:
    preprocess = json.loads(
        (ARTIFACTS / "preprocess.json").read_text(encoding="utf-8")
    )
    float_path = ARTIFACTS / "condition_mlp_float.onnx"
    quant_path = ARTIFACTS / "condition_mlp_qdq_uint8.onnx"
    failures = 0

    for condition_index, condition in enumerate(CONDITIONS):
        fault_type = FAULT_TYPES[0] if condition == "fault" else "none"
        samples = [
            simulate_sample(
                6,
                condition,
                index * SAMPLE_PERIOD_S,
                9000 + condition_index,
                1.0,
                fault_type,
            )
            for index in range(WINDOW_SIZE)
        ]
        features = window_features(samples)[None, :]
        float_logits = infer(float_path, features, preprocess)
        quant_logits = infer(quant_path, features, preprocess)
        float_label = CONDITIONS[int(np.argmax(float_logits))]
        quant_label = CONDITIONS[int(np.argmax(quant_logits))]
        ok = float_label == condition and quant_label == condition
        failures += 0 if ok else 1
        print(
            f"{condition:8s} float={float_label:8s} "
            f"quantized={quant_label:8s} {'PASS' if ok else 'FAIL'}",
            flush=True,
        )

    print("PASS all condition windows" if failures == 0 else "FAIL smoke test")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
