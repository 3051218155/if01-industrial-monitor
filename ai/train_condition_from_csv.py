#!/usr/bin/env python3
"""Train the condition MLP from labeled live samples collected by Qt."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import tempfile
from pathlib import Path

import numpy as np
import onnx
import pandas as pd
from onnx import helper
from onnxruntime.quantization import (
    CalibrationMethod,
    QuantFormat,
    QuantType,
    quantize_static,
)
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.neural_network import MLPClassifier

from condition_pipeline import (
    CONDITION_LABELS_ZH,
    CONDITIONS,
    FEATURE_NAMES,
    INPUT_DIM,
    RegisterSample,
    window_features,
)
from train_condition_mlp import (
    ArrayCalibrationReader,
    FixedTemporaryDirectory,
    benchmark_onnx,
    export_onnx_mlp,
    run_onnx,
    softmax,
    split_metrics,
)


REQUIRED_COLUMNS = {
    "run_id",
    "condition",
    "point1",
    "point2",
    "point3",
    "point4",
    "point5",
    "temperature_c",
    "current_a",
    "rms",
    "peak",
}


def select_runs(
    frame: pd.DataFrame,
    val_fraction: float,
    test_fraction: float,
) -> tuple[set[str], set[str], set[str]]:
    train: set[str] = set()
    validation: set[str] = set()
    test: set[str] = set()
    for condition in CONDITIONS:
        runs = sorted(
            frame.loc[frame["condition"] == condition, "run_id"]
            .dropna()
            .astype(str)
            .unique()
        )
        if len(runs) < 3:
            raise SystemExit(
                f"{condition}: need at least 3 independent runs, got {len(runs)}"
            )
        test_count = max(1, round(len(runs) * test_fraction))
        val_count = max(1, round(len(runs) * val_fraction))
        if test_count + val_count >= len(runs):
            test_count = 1
            val_count = 1
        test.update(runs[:test_count])
        validation.update(runs[test_count : test_count + val_count])
        train.update(runs[test_count + val_count :])
    return train, validation, test


def build_windows(
    frame: pd.DataFrame, allowed_runs: set[str], stride: int
) -> tuple[np.ndarray, np.ndarray, list[dict]]:
    features: list[np.ndarray] = []
    labels: list[int] = []
    metadata: list[dict] = []
    label_map = {name: index for index, name in enumerate(CONDITIONS)}

    for run_id, run in frame.groupby("run_id", sort=True):
        run_id = str(run_id)
        if run_id not in allowed_runs:
            continue
        run = run.sort_values("timestamp") if "timestamp" in run.columns else run
        condition = str(run["condition"].iloc[0])
        if condition not in label_map:
            continue
        samples = [
            RegisterSample(
                points=tuple(float(row[f"point{i}"]) for i in range(1, 6)),
                temperature_c=float(row["temperature_c"]),
                current_a=float(row["current_a"]),
                rms=float(row["rms"]),
                peak=float(row["peak"]),
            )
            for _, row in run.iterrows()
        ]
        for start in range(0, len(samples) - 8 + 1, max(1, stride)):
            window = samples[start : start + 8]
            features.append(window_features(window))
            labels.append(label_map[condition])
            metadata.append(
                {
                    "run_id": run_id,
                    "condition": condition,
                    "window_start": start,
                }
            )

    if not features:
        raise SystemExit("no complete 8-sample windows were built")
    return (
        np.stack(features).astype(np.float32),
        np.asarray(labels, dtype=np.int64),
        metadata,
    )


def export_npu_onnx(float_path: Path, npu_path: Path) -> None:
    model = onnx.load(str(float_path))
    model.graph.input[0].type.tensor_type.shape.dim[0].dim_value = 1
    model.graph.output[0].type.tensor_type.shape.dim[0].dim_value = 1
    if model.graph.node and model.graph.node[-1].op_type == "Identity":
        producer = model.graph.node[-1].input[0]
        model.graph.node.pop()
        model.graph.output[0].name = producer
    onnx.checker.check_model(model)
    onnx.save(model, str(npu_path))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Train the five-class condition model from live CSV data."
    )
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument(
        "--out",
        type=Path,
        default=Path(__file__).resolve().parent
        / "artifacts"
        / "condition_mlp_live",
    )
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument(
        "--hidden-layers",
        default="32,16",
        help="comma-separated hidden layer sizes, for example 64,32",
    )
    parser.add_argument("--learning-rate", type=float, default=1e-3)
    parser.add_argument("--max-iter", type=int, default=600)
    parser.add_argument("--no-early-stopping", action="store_true")
    parser.add_argument("--val-fraction", type=float, default=0.15)
    parser.add_argument("--test-fraction", type=float, default=0.15)
    parser.add_argument("--seed", type=int, default=20261023)
    parser.add_argument("--benchmark-iterations", type=int, default=1000)
    args = parser.parse_args()
    hidden_layers = tuple(
        int(value.strip())
        for value in args.hidden_layers.split(",")
        if value.strip()
    )
    if not hidden_layers or any(value <= 0 for value in hidden_layers):
        raise SystemExit("--hidden-layers must contain positive integers")

    frame = pd.read_csv(args.dataset)
    missing = REQUIRED_COLUMNS - set(frame.columns)
    if missing:
        raise SystemExit(f"dataset missing columns: {sorted(missing)}")
    frame = frame.dropna(subset=list(REQUIRED_COLUMNS)).copy()
    frame["condition"] = frame["condition"].astype(str)
    unknown = sorted(set(frame["condition"]) - set(CONDITIONS))
    if unknown:
        raise SystemExit(f"unknown conditions: {unknown}")

    train_runs, val_runs, test_runs = select_runs(
        frame, args.val_fraction, args.test_fraction
    )
    x_train, y_train, meta_train = build_windows(frame, train_runs, args.stride)
    x_val, y_val, meta_val = build_windows(frame, val_runs, args.stride)
    x_test, y_test, meta_test = build_windows(frame, test_runs, args.stride)
    print(
        f"datasets: train={len(x_train)} val={len(x_val)} test={len(x_test)}",
        flush=True,
    )

    pipeline = Pipeline(
        [
            ("scaler", StandardScaler()),
            (
                "model",
                MLPClassifier(
                    hidden_layer_sizes=hidden_layers,
                    activation="relu",
                    solver="adam",
                    alpha=1e-4,
                    batch_size=128,
                    learning_rate_init=args.learning_rate,
                    max_iter=args.max_iter,
                    early_stopping=not args.no_early_stopping,
                    n_iter_no_change=30,
                    validation_fraction=0.12,
                    random_state=args.seed,
                ),
            ),
        ]
    )
    pipeline.fit(x_train, y_train)
    print(f"training iterations: {pipeline.named_steps['model'].n_iter_}")

    args.out.mkdir(parents=True, exist_ok=True)
    float_path = args.out / "condition_mlp_float.onnx"
    npu_path = args.out / "condition_mlp_npu.onnx"
    quant_path = args.out / "condition_mlp_qdq_uint8.onnx"
    preprocess_path = args.out / "preprocess.json"
    export_onnx_mlp(pipeline, float_path)
    export_npu_onnx(float_path, npu_path)

    scaler = pipeline.named_steps["scaler"]
    mean = scaler.mean_.astype(np.float32)
    scale = scaler.scale_.astype(np.float32)
    preprocess_path.write_text(
        json.dumps(
            {
                "input": "raw_window_features",
                "transform": "z_score",
                "mean": mean.astype(float).tolist(),
                "scale": scale.astype(float).tolist(),
                "feature_names": list(FEATURE_NAMES),
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    quant_tmp = args.out / "tmp"
    if quant_tmp.exists():
        shutil.rmtree(quant_tmp)
    quant_tmp.mkdir(parents=True, exist_ok=True)
    os.environ["TEMP"] = str(quant_tmp)
    os.environ["TMP"] = str(quant_tmp)
    tempfile.tempdir = str(quant_tmp)
    original_tempdir = tempfile.TemporaryDirectory
    tempfile.TemporaryDirectory = lambda **_: FixedTemporaryDirectory(quant_tmp)
    try:
        calibration = x_train[
            np.linspace(0, len(x_train) - 1, min(2048, len(x_train)), dtype=np.int64)
        ]
        quantize_static(
            str(float_path),
            str(quant_path),
            ArrayCalibrationReader(calibration, "normalized_features"),
            quant_format=QuantFormat.QDQ,
            activation_type=QuantType.QUInt8,
            weight_type=QuantType.QInt8,
            per_channel=True,
            reduce_range=False,
            calibrate_method=CalibrationMethod.MinMax,
        )
    finally:
        tempfile.TemporaryDirectory = original_tempdir

    probabilities = {
        "train": softmax(run_onnx(float_path, x_train, mean, scale)),
        "validation": softmax(run_onnx(float_path, x_val, mean, scale)),
        "test_float": softmax(run_onnx(float_path, x_test, mean, scale)),
        "test_quantized": softmax(run_onnx(quant_path, x_test, mean, scale)),
    }
    metrics = {
        "train": split_metrics(y_train, probabilities["train"]),
        "validation": split_metrics(y_val, probabilities["validation"]),
        "test_float": split_metrics(y_test, probabilities["test_float"]),
        "test_quantized": split_metrics(y_test, probabilities["test_quantized"]),
    }

    report = {
        "dataset": str(args.dataset.resolve()),
        "source": "live Qt API samples with explicit simulated point/current fields",
        "conditions": list(CONDITIONS),
        "condition_labels_zh": list(CONDITION_LABELS_ZH),
        "feature_count": INPUT_DIM,
        "window_size": 8,
        "runs": {
            "train": sorted(train_runs),
            "validation": sorted(val_runs),
            "test": sorted(test_runs),
        },
        "windows": {
            "train": len(x_train),
            "validation": len(x_val),
            "test": len(x_test),
        },
        "metrics": metrics,
        "pass_checks": {
            "validation_accuracy_ge_0_90": metrics["validation"]["accuracy"] >= 0.90,
            "test_accuracy_ge_0_85": metrics["test_quantized"]["accuracy"] >= 0.85,
        },
        "benchmark_pc": {
            "float": benchmark_onnx(
                float_path, x_test, mean, scale, args.benchmark_iterations
            ),
            "quantized": benchmark_onnx(
                quant_path, x_test, mean, scale, args.benchmark_iterations
            ),
        },
        "artifacts": {
            "float_onnx": str(float_path),
            "npu_onnx": str(npu_path),
            "quantized_onnx": str(quant_path),
            "preprocess_json": str(preprocess_path),
        },
    }
    report["pass"] = all(report["pass_checks"].values())
    (args.out / "metrics.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
