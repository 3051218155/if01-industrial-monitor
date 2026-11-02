"""Run the exported condition model on an eight-sample JSON window."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import onnxruntime as ort

from condition_pipeline import (
    CONDITION_LABELS_ZH,
    CONDITIONS,
    RegisterSample,
    window_features,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--samples", type=Path, required=True)
    parser.add_argument("--preprocess", type=Path)
    args = parser.parse_args()

    raw = json.loads(args.samples.read_text(encoding="utf-8"))
    if isinstance(raw, dict):
        raw = raw["samples"]
    samples = [
        RegisterSample(
            points=tuple(float(value) for value in row["points"]),
            temperature_c=float(row["temperature_c"]),
            current_a=float(row["current_a"]),
            rms=float(row["rms"]),
            peak=float(row["peak"]),
        )
        for row in raw
    ]
    features = window_features(samples)[None, :]
    preprocess_path = args.preprocess or args.model.with_name("preprocess.json")
    preprocess = json.loads(preprocess_path.read_text(encoding="utf-8"))
    mean = np.asarray(preprocess["mean"], dtype=np.float32)
    scale = np.asarray(preprocess["scale"], dtype=np.float32)
    normalized = ((features - mean) / scale).astype(np.float32)
    session = ort.InferenceSession(
        str(args.model), providers=["CPUExecutionProvider"]
    )
    input_name = session.get_inputs()[0].name
    logits = session.run(None, {input_name: normalized})[0][0]
    shifted = logits - logits.max()
    probabilities = np.exp(shifted) / np.exp(shifted).sum()
    result = {
        "condition": CONDITIONS[int(np.argmax(probabilities))],
        "condition_zh": CONDITION_LABELS_ZH[int(np.argmax(probabilities))],
        "confidence": float(np.max(probabilities)),
        "probabilities": {
            name: float(value)
            for name, value in zip(CONDITIONS, probabilities, strict=True)
        },
        "runtime": "ONNX Runtime CPU",
    }
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
