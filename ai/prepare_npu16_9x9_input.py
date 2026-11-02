#!/usr/bin/env python3
"""Create board inputs and CPU reference outputs for the 9x9 NPU model."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import numpy_helper

from build_npu16_9x9_model import ARTIFACTS, FLOAT, QDQ, load_windows, reshape_window


LABELS = ("stopped", "idle", "normal", "overload", "fault")
OUTPUT_DIR = ARTIFACTS / "npu16_9x9_inputs"


def main() -> int:
    _, _, test = load_windows()
    selected = {}
    for window in test:
        selected.setdefault(window["expected"], window)

    session = ort.InferenceSession(
        str(FLOAT),
        providers=["CPUExecutionProvider"],
    )
    quantized = onnx.load(str(QDQ))
    quantized_init = {
        item.name: numpy_helper.to_array(item)
        for item in quantized.graph.initializer
    }
    input_scale = float(quantized_init["normalized_features_scale"])
    input_zero = int(quantized_init["normalized_features_zero_point"])
    output_scale = float(quantized_init["conv2_scale"])
    output_zero = int(quantized_init["conv2_zero_point"])
    manifest = []
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for label in LABELS:
        window = selected[label]
        data = reshape_window(window)
        input_path = OUTPUT_DIR / f"{label}.f32"
        data.astype(np.float32).tofile(input_path)
        quantized_input = np.clip(
            np.rint(data / input_scale + input_zero),
            0,
            255,
        ).astype(np.uint8)
        quantized_input_path = OUTPUT_DIR / f"{label}.u8"
        quantized_input.tofile(quantized_input_path)
        output = session.run(
            None,
            {session.get_inputs()[0].name: data},
        )[0].reshape(-1)
        first_five = output[:5]
        prediction = LABELS[int(np.argmax(first_five))]
        manifest.append(
            {
                "expected": label,
                "cpu_prediction": prediction,
                "input": input_path.name,
                "input_bytes": input_path.stat().st_size,
                "quantized_input": quantized_input_path.name,
                "quantized_input_bytes": quantized_input_path.stat().st_size,
                "input_scale": input_scale,
                "input_zero_point": input_zero,
                "output_scale": output_scale,
                "output_zero_point": output_zero,
                "first_five": first_five.astype(float).tolist(),
            }
        )

    manifest_path = OUTPUT_DIR / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(manifest_path)
    return 0 if all(item["expected"] == item["cpu_prediction"] for item in manifest) else 1


if __name__ == "__main__":
    raise SystemExit(main())
