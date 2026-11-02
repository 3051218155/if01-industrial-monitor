#!/usr/bin/env python3
"""Train a five-class model from fields available in the live Qt stream."""
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
from onnxruntime.quantization import (
    CalibrationMethod,
    QuantFormat,
    QuantType,
    quantize_static,
)
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

from condition_pipeline import CONDITION_LABELS_ZH, CONDITIONS
from train_condition_from_csv import export_npu_onnx, select_runs
from train_condition_mlp import (
    ArrayCalibrationReader,
    FixedTemporaryDirectory,
    benchmark_onnx,
    export_onnx_mlp,
    run_onnx,
    softmax,
    split_metrics,
)


WINDOW_SIZE = 8
BASE_FEATURE_NAMES = (
    "rms",
    "peak",
    "crest_factor",
    "current_a",
    "pwm_normalized",
    "current_to_pwm",
    "rms_to_pwm",
    "peak_to_pwm",
)
FEATURE_NAMES = tuple(
    [f"last_{name}" for name in BASE_FEATURE_NAMES]
    + [f"mean_{name}" for name in BASE_FEATURE_NAMES]
    + [f"trend_{name}" for name in BASE_FEATURE_NAMES]
)
INPUT_DIM = len(FEATURE_NAMES)
REQUIRED_COLUMNS = {
    "timestamp",
    "run_id",
    "condition",
    "rms",
    "peak",
    "temperature_c",
    "current_a",
    "motor_pwm",
}


def base_features(row: pd.Series) -> np.ndarray:
    rms = float(row["rms"])
    peak = float(row["peak"])
    current = float(row["current_a"])
    pwm = float(row["motor_pwm"]) / 1000.0
    return np.asarray(
        [
            rms,
            peak,
            peak / max(rms, 0.02),
            current,
            pwm,
            current / max(pwm, 0.01),
            rms / max(pwm, 0.01),
            peak / max(pwm, 0.01),
        ],
        dtype=np.float32,
    )


def window_features(rows: pd.DataFrame) -> np.ndarray:
    base = np.stack([base_features(row) for _, row in rows.iterrows()])
    return np.concatenate(
        [base[-1], base.mean(axis=0), base[-1] - base[0]]
    ).astype(np.float32, copy=False)


def build_windows(
    frame: pd.DataFrame,
    allowed_runs: set[str],
    stride: int,
) -> tuple[np.ndarray, np.ndarray, list[dict]]:
    features: list[np.ndarray] = []
    labels: list[int] = []
    metadata: list[dict] = []
    label_map = {name: index for index, name in enumerate(CONDITIONS)}
    for run_id, run in frame.groupby("run_id", sort=True):
        run_id = str(run_id)
        if run_id not in allowed_runs:
            continue
        run = run.sort_values("timestamp").reset_index(drop=True)
        condition = str(run["condition"].iloc[0])
        if condition not in label_map:
            continue
        for start in range(0, len(run) - WINDOW_SIZE + 1, max(1, stride)):
            features.append(window_features(run.iloc[start : start + WINDOW_SIZE]))
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--stride", type=int, default=1)
    parser.add_argument("--val-fraction", type=float, default=0.15)
    parser.add_argument("--test-fraction", type=float, default=0.15)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()

    frame = pd.read_csv(args.dataset)
    missing = REQUIRED_COLUMNS - set(frame.columns)
    if missing:
        raise SystemExit(f"dataset missing columns: {sorted(missing)}")
    frame = frame.dropna(subset=list(REQUIRED_COLUMNS)).copy()
    unknown = sorted(set(frame["condition"].astype(str)) - set(CONDITIONS))
    if unknown:
        raise SystemExit(f"unknown conditions: {unknown}")

    train_runs, val_runs, test_runs = select_runs(
        frame, args.val_fraction, args.test_fraction
    )
    x_train, y_train, _ = build_windows(frame, train_runs, args.stride)
    x_val, y_val, _ = build_windows(frame, val_runs, args.stride)
    x_test, y_test, _ = build_windows(frame, test_runs, args.stride)
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
                    hidden_layer_sizes=(8, 4),
                    activation="relu",
                    solver="adam",
                    alpha=1e-1,
                    batch_size=64,
                    learning_rate_init=1e-2,
                    max_iter=3000,
                    early_stopping=False,
                    random_state=args.seed,
                ),
            ),
        ]
    )
    pipeline.fit(x_train, y_train)
    print(f"training iterations: {pipeline.named_steps['model'].n_iter_}")

    args.out.mkdir(parents=True, exist_ok=True)
    float_path = args.out / "condition_runtime_float.onnx"
    npu_path = args.out / "condition_runtime_npu.onnx"
    quant_path = args.out / "condition_runtime_qdq_uint8.onnx"
    preprocess_path = args.out / "runtime_preprocess.json"
    export_onnx_mlp(pipeline, float_path, FEATURE_NAMES, WINDOW_SIZE)
    export_npu_onnx(float_path, npu_path)

    scaler = pipeline.named_steps["scaler"]
    mean = scaler.mean_.astype(np.float32)
    scale = scaler.scale_.astype(np.float32)
    preprocess_path.write_text(
        json.dumps(
            {
                "input": "runtime_window_features",
                "transform": "z_score",
                "mean": mean.astype(float).tolist(),
                "scale": scale.astype(float).tolist(),
                "feature_names": list(FEATURE_NAMES),
                "window_size": WINDOW_SIZE,
                "sample_fields": [
                    "rms",
                    "peak",
                    "temperature_c",
                    "current_a",
                    "pwm_normalized",
                ],
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
        name: split_metrics(labels, values)
        for name, labels, values in (
            ("train", y_train, probabilities["train"]),
            ("validation", y_val, probabilities["validation"]),
            ("test_float", y_test, probabilities["test_float"]),
            ("test_quantized", y_test, probabilities["test_quantized"]),
        )
    }
    report = {
        "dataset": str(args.dataset.resolve()),
        "conditions": list(CONDITIONS),
        "condition_labels_zh": list(CONDITION_LABELS_ZH),
        "feature_count": INPUT_DIM,
        "feature_names": list(FEATURE_NAMES),
        "window_size": WINDOW_SIZE,
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
            "float": benchmark_onnx(float_path, x_test, mean, scale, 1000),
            "quantized": benchmark_onnx(quant_path, x_test, mean, scale, 1000),
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
