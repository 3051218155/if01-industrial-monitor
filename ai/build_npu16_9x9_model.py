#!/usr/bin/env python3
"""Build a spatially broadcast, NPU-friendly condition model."""

from __future__ import annotations

from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
from onnxruntime.quantization import (
    CalibrationDataReader,
    QuantFormat,
    QuantType,
    quantize_static,
)

from build_conv_condition_model import load_windows


ROOT = Path(__file__).resolve().parent
ARTIFACTS = ROOT / "artifacts" / "condition_runtime_20261026_v5"
SOURCE = ARTIFACTS / "condition_runtime_npu.onnx"
FLOAT = ARTIFACTS / "condition_runtime_npu16_9x9.onnx"
QDQ = ARTIFACTS / "condition_runtime_npu16_9x9_qdq.onnx"


def build_float() -> onnx.ModelProto:
    source = onnx.load(str(SOURCE))
    init = {
        item.name: numpy_helper.to_array(item)
        for item in source.graph.initializer
    }
    first = np.zeros((16, 32, 1, 1), dtype=np.float32)
    first[:8, :24, 0, 0] = init["W0"].T
    first_bias = np.zeros(16, dtype=np.float32)
    first_bias[:8] = init["B0"]

    second = np.zeros((16, 16, 1, 1), dtype=np.float32)
    second[:4, :8, 0, 0] = init["W1"].T
    second_bias = np.zeros(16, dtype=np.float32)
    second_bias[:4] = init["B1"]

    third = np.zeros((16, 16, 1, 1), dtype=np.float32)
    third[:5, :4, 0, 0] = init["W2"].T
    third_bias = np.zeros(16, dtype=np.float32)
    third_bias[:5] = init["B2"]

    nodes = []
    tensors = []
    value = "normalized_features"
    for index, (weight, bias) in enumerate(
        ((first, first_bias), (second, second_bias), (third, third_bias))
    ):
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
                [value, f"conv_w{index}", f"conv_b{index}"],
                [output],
                pads=[0, 0, 0, 0],
                strides=[1, 1],
                group=1,
            )
        )
        value = output
        if index < 2:
            relu = f"relu{index}"
            nodes.append(helper.make_node("Relu", [value], [relu]))
            value = relu

    graph = helper.make_graph(
        nodes,
        "fpag-condition-npu16-9x9",
        [
            helper.make_tensor_value_info(
                "normalized_features",
                TensorProto.FLOAT,
                [1, 32, 9, 9],
            )
        ],
        [
            helper.make_tensor_value_info(
                value,
                TensorProto.FLOAT,
                [1, 16, 9, 9],
            )
        ],
        tensors,
    )
    model = helper.make_model(
        graph,
        opset_imports=[helper.make_opsetid("", 14)],
        producer_name="FPGA",
    )
    model.ir_version = 7
    onnx.checker.check_model(model)
    return model


def reshape_window(window: dict) -> np.ndarray:
    features = np.zeros(32, dtype=np.float32)
    features[:24] = window["input"].reshape(-1)
    return np.broadcast_to(features.reshape(1, 32, 1, 1), (1, 32, 9, 9)).copy()


class Reader(CalibrationDataReader):
    def __init__(self, windows: list[dict]):
        self._items = iter(
            [{"normalized_features": reshape_window(window)}
             for window in windows]
        )

    def get_next(self):
        return next(self._items, None)


def main() -> int:
    train, validation, test = load_windows()
    onnx.save(build_float(), FLOAT)
    quantize_static(
        str(FLOAT),
        str(QDQ),
        Reader(train + validation),
        quant_format=QuantFormat.QDQ,
        per_channel=False,
        reduce_range=False,
        activation_type=QuantType.QUInt8,
        weight_type=QuantType.QUInt8,
    )
    print(FLOAT)
    print(QDQ)
    print(len(validation), len(test))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
