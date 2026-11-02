#!/usr/bin/env python3
"""Rebuild the condition Conv model using the vendor's QDQ structure."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper
import onnxruntime as ort

from build_conv_condition_model import load_windows


ROOT = Path(__file__).resolve().parent
ARTIFACTS = ROOT / "artifacts" / "condition_runtime_20261026_v5"
FLOAT = ARTIFACTS / "condition_runtime_conv.onnx"
QUANTIZED = ARTIFACTS / "condition_runtime_conv_qdq.onnx"
OUTPUT = ARTIFACTS / "condition_runtime_conv_vendor_qdq.onnx"


def evaluate(path: Path, windows: list[dict]) -> dict:
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    labels = ("stopped", "idle", "normal", "overload", "fault")
    correct = 0
    for window in windows:
        result = session.run(
            None, {"normalized_features": window["input"]})[0].reshape(-1)
        correct += labels[int(np.argmax(result))] == window["expected"]
    return {
        "windows": len(windows),
        "correct": correct,
        "accuracy": correct / len(windows) if windows else 0.0,
    }


def main() -> int:
    float_model = onnx.load(str(FLOAT))
    quantized_model = onnx.load(str(QUANTIZED))
    float_init = {
        item.name: numpy_helper.to_array(item)
        for item in float_model.graph.initializer
    }
    quant_init = {
        item.name: numpy_helper.to_array(item)
        for item in quantized_model.graph.initializer
    }

    nodes = []
    tensors = []

    def add_tensor(name: str, value: np.ndarray) -> str:
        tensors.append(numpy_helper.from_array(value, name))
        return name

    input_scale = float(quant_init["normalized_features_scale"])
    input_zero = np.asarray(
        quant_init["normalized_features_zero_point"], dtype=np.uint8)
    add_tensor("input_scale", np.asarray(input_scale, dtype=np.float32))
    add_tensor("input_zero", input_zero)

    value = "normalized_features"
    for index in range(3):
        weight = float_init[f"conv_w{index}"].astype(np.float32)
        bias = float_init[f"conv_b{index}"].astype(np.float32)
        weight_scale = float(quant_init[f"conv_w{index}_scale"])
        weight_zero = np.asarray(
            quant_init[f"conv_w{index}_zero_point"], dtype=np.uint8)
        weight_q = np.asarray(
            quant_init[f"conv_w{index}_quantized"], dtype=np.uint8)
        bias_scale = float(
            np.asarray(
                quant_init[f"conv_b{index}_quantized_scale"]
            ).reshape(-1)[0]
        )
        bias_zero = int(
            np.asarray(
                quant_init[f"conv_b{index}_quantized_zero_point"]
            ).reshape(-1)[0]
        )
        bias_q = np.rint(bias / bias_scale).astype(np.int32) + bias_zero

        add_tensor(f"w{index}_q", weight_q)
        add_tensor(f"w{index}_scale", np.asarray(weight_scale, np.float32))
        add_tensor(f"w{index}_zero", weight_zero)
        add_tensor(f"b{index}_q", bias_q.reshape(-1).astype(np.int32))
        add_tensor(f"b{index}_scale", np.asarray(bias_scale, np.float32))
        add_tensor(
            f"b{index}_zero", np.asarray([bias_zero], dtype=np.int32))

        input_q = f"input{index}_q"
        input_dq = f"input{index}_dq"
        nodes.extend(
            [
                helper.make_node(
                    "QuantizeLinear",
                    [value, "input_scale", "input_zero"],
                    [input_q],
                ),
                helper.make_node(
                    "DequantizeLinear",
                    [input_q, "input_scale", "input_zero"],
                    [input_dq],
                ),
                helper.make_node(
                    "DequantizeLinear",
                    [f"w{index}_q", f"w{index}_scale", f"w{index}_zero"],
                    [f"w{index}_dq"],
                ),
                helper.make_node(
                    "DequantizeLinear",
                    [f"b{index}_q", f"b{index}_scale", f"b{index}_zero"],
                    [f"b{index}_dq"],
                ),
            ]
        )
        conv_out = f"conv{index}"
        nodes.append(
            helper.make_node(
                "Conv",
                [input_dq, f"w{index}_dq", f"b{index}_dq"],
                [conv_out],
                pads=[0, 0, 0, 0],
                strides=[1, 1],
                group=1,
            )
        )
        if index < 2:
            output_scale_name = f"relu{index}_scale"
            output_zero_name = f"relu{index}_zero"
            output_scale = float(quant_init[output_scale_name])
            output_zero = np.asarray(
                quant_init[f"relu{index}_zero_point"], dtype=np.uint8)
        else:
            output_scale_name = "final_scale"
            output_zero_name = "final_zero"
            output_scale = float(quant_init["conv2_scale"])
            output_zero = np.asarray(
                quant_init["conv2_zero_point"], dtype=np.uint8)
        add_tensor(
            output_scale_name, np.asarray(output_scale, dtype=np.float32))
        add_tensor(output_zero_name, output_zero)

        output_q = f"output{index}_q"
        output_dq = f"output{index}_dq"
        nodes.extend(
            [
                helper.make_node(
                    "QuantizeLinear",
                    [conv_out, output_scale_name, output_zero_name],
                    [output_q],
                ),
                helper.make_node(
                    "DequantizeLinear",
                    [output_q, output_scale_name, output_zero_name],
                    [output_dq],
                ),
            ]
        )
        value = output_dq
        if index < 2:
            relu = f"relu{index}"
            nodes.append(helper.make_node("Relu", [value], [relu]))
            value = relu

    graph = helper.make_graph(
        nodes,
        "fpag-condition-vendor-qdq",
        [
            helper.make_tensor_value_info(
                "normalized_features",
                TensorProto.FLOAT,
                [1, 24, 1, 1],
            )
        ],
        [
            helper.make_tensor_value_info(
                value,
                TensorProto.FLOAT,
                [1, 5, 1, 1],
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
    onnx.save(model, OUTPUT)

    _, validation, test = load_windows()
    report = {
        "path": str(OUTPUT),
        "validation": evaluate(OUTPUT, validation),
        "test": evaluate(OUTPUT, test),
        "nodes": len(model.graph.node),
        "initializers": len(model.graph.initializer),
    }
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
