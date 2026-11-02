#!/usr/bin/env python3
"""Build and quantize an NPU-friendly 1x1-convolution condition model."""

from __future__ import annotations

import csv
import json
from pathlib import Path
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
import onnxruntime as ort
from onnxruntime.quantization import (
    CalibrationDataReader,
    QuantFormat,
    QuantType,
    quantize_static,
)


ROOT = Path(__file__).resolve().parent
ARTIFACTS = ROOT / "artifacts" / "condition_runtime_20261026_v5"
SOURCE = ARTIFACTS / "condition_runtime_npu.onnx"
FLOAT_CONV = ARTIFACTS / "condition_runtime_conv.onnx"
QDQ_CONV = ARTIFACTS / "condition_runtime_conv_qdq.onnx"
PREPROCESS = ARTIFACTS / "runtime_preprocess.json"
METRICS = ARTIFACTS / "metrics.json"
DATASET = ROOT / "datasets" / "condition_live_20261026_clean.csv"

sys.path.insert(0, str(ROOT))
from runtime_condition_infer import window_features  # noqa: E402


def make_float_conv() -> onnx.ModelProto:
    source = onnx.load(str(SOURCE))
    initializer = {
        item.name: numpy_helper.to_array(item)
        for item in source.graph.initializer
    }
    nodes = []
    tensors = []
    values = "normalized_features"
    shapes = [
        (8, 24, 1, 1),
        (4, 8, 1, 1),
        (5, 4, 1, 1),
    ]
    for index, shape in enumerate(shapes):
        weight = initializer[f"W{index}"].T.reshape(shape).astype(np.float32)
        bias = initializer[f"B{index}"].astype(np.float32)
        tensors.extend(
            [
                numpy_helper.from_array(weight, f"conv_w{index}"),
                numpy_helper.from_array(bias, f"conv_b{index}"),
            ]
        )
        output = f"conv{index}"
        nodes.append(
            helper.make_node(
                "Conv",
                [values, f"conv_w{index}", f"conv_b{index}"],
                [output],
                pads=[0, 0, 0, 0],
                strides=[1, 1],
                group=1,
            )
        )
        values = output
        if index < 2:
            relu = f"relu{index}"
            nodes.append(helper.make_node("Relu", [values], [relu]))
            values = relu

    graph = helper.make_graph(
        nodes,
        "fpag-condition-conv",
        [
            helper.make_tensor_value_info(
                "normalized_features",
                TensorProto.FLOAT,
                [1, 24, 1, 1],
            )
        ],
        [
            helper.make_tensor_value_info(
                values,
                TensorProto.FLOAT,
                [1, 5, 1, 1],
            )
        ],
        tensors,
    )
    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 13)],
        producer_name="FPGA",
    )
    model.ir_version = 8
    return model


def load_windows() -> tuple[list[dict], list[dict], list[dict]]:
    metric_data = json.loads(METRICS.read_text(encoding="utf-8"))
    split_runs = {
        split: set(metric_data["runs"][split])
        for split in ("train", "validation", "test")
    }
    grouped: dict[tuple[str, str], list[dict[str, str]]] = {}
    with DATASET.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            for split, runs in split_runs.items():
                if row["run_id"] in runs:
                    grouped.setdefault((split, row["run_id"]), []).append(row)
                    break

    preprocess = json.loads(PREPROCESS.read_text(encoding="utf-8"))
    mean = np.asarray(preprocess["mean"], dtype=np.float32)
    scale = np.asarray(preprocess["scale"], dtype=np.float32)
    windows = {"train": [], "validation": [], "test": []}
    for (split, run_id), rows in sorted(grouped.items()):
        for start in range(0, len(rows) - 7):
            window = rows[start : start + 8]
            labels = {row["condition"] for row in window}
            if len(labels) != 1:
                continue
            samples = [
                [
                    float(row["rms"]),
                    float(row["peak"]),
                    float(row["temperature_c"] or 25.0),
                    float(row["current_a"]),
                    float(row["motor_pwm"]) / 1000.0,
                ]
                for row in window
            ]
            features = np.asarray(window_features(samples), dtype=np.float32)
            normalized = ((features - mean) / scale).reshape(1, 24, 1, 1)
            windows[split].append(
                {
                    "input": normalized.astype(np.float32),
                    "expected": labels.pop(),
                    "run_id": run_id,
                }
            )
    return windows["train"], windows["validation"], windows["test"]


class Reader(CalibrationDataReader):
    def __init__(self, windows: list[dict]):
        self._iterator = iter(
            [{"normalized_features": window["input"]} for window in windows]
        )

    def get_next(self):
        return next(self._iterator, None)


def evaluate(path: Path, windows: list[dict]) -> dict:
    session = ort.InferenceSession(
        str(path),
        providers=["CPUExecutionProvider"],
    )
    correct = 0
    for window in windows:
        logits = session.run(
            None,
            {"normalized_features": window["input"]},
        )[0].reshape(-1)
        predicted = int(np.argmax(logits))
        labels = ("stopped", "idle", "normal", "overload", "fault")
        correct += labels[predicted] == window["expected"]
    return {
        "windows": len(windows),
        "correct": correct,
        "accuracy": correct / len(windows) if windows else 0.0,
    }


def main() -> int:
    train, validation, test = load_windows()
    float_model = make_float_conv()
    onnx.checker.check_model(float_model)
    onnx.save(float_model, FLOAT_CONV)

    quantize_static(
        str(FLOAT_CONV),
        str(QDQ_CONV),
        Reader(train + validation),
        quant_format=QuantFormat.QDQ,
        per_channel=False,
        reduce_range=False,
        activation_type=QuantType.QUInt8,
        weight_type=QuantType.QUInt8,
    )
    quantized = onnx.load(str(QDQ_CONV))
    onnx.checker.check_model(quantized)

    report = {
        "source": str(SOURCE),
        "float_conv": {
            "path": str(FLOAT_CONV),
            "validation": evaluate(FLOAT_CONV, validation),
            "test": evaluate(FLOAT_CONV, test),
        },
        "qdq_conv": {
            "path": str(QDQ_CONV),
            "validation": evaluate(QDQ_CONV, validation),
            "test": evaluate(QDQ_CONV, test),
        },
    }
    (ARTIFACTS / "conv_model_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
