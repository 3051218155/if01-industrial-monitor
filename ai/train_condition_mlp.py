"""Generate six-slave condition data, train, quantize, and evaluate the MLP."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

import numpy as np
import onnx
import onnxruntime as ort
from onnx import TensorProto, helper, numpy_helper
from onnxruntime.quantization import (
    CalibrationDataReader,
    CalibrationMethod,
    QuantFormat,
    QuantType,
    quantize_static,
)
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
)
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

from condition_pipeline import (
    CONDITION_LABELS_ZH,
    CONDITIONS,
    FAULT_TYPES,
    FEATURE_NAMES,
    INPUT_DIM,
    SAMPLE_PERIOD_S,
    WINDOW_SIZE,
    RegisterSample,
    estimate_alert,
    simulate_sample,
    window_features,
)


REPO = Path(__file__).resolve().parents[1]
DEFAULT_OUT = REPO / "ai" / "artifacts" / "condition_mlp"


class FixedTemporaryDirectory:
    """Give ONNX Runtime a normal inherited-ACL directory on Windows sandboxes."""

    def __init__(self, path: Path) -> None:
        self.name = str(path)

    def __enter__(self) -> str:
        return self.name

    def __exit__(self, exc_type, exc, tb) -> None:
        return None


def generate_split(
    device_ids: tuple[int, ...],
    runs_per_condition: int,
    samples_per_run: int,
    seed: int,
) -> tuple[np.ndarray, np.ndarray, list[dict]]:
    features: list[np.ndarray] = []
    labels: list[int] = []
    metadata: list[dict] = []

    for device_id in device_ids:
        for condition_index, condition in enumerate(CONDITIONS):
            for run_index in range(runs_per_condition):
                run_seed = seed + device_id * 100_003 + condition_index * 10_007 + run_index
                rng = np.random.default_rng(run_seed)
                severity = float(rng.uniform(0.72, 1.25))
                fault_type = (
                    FAULT_TYPES[run_index % len(FAULT_TYPES)]
                    if condition == "fault"
                    else "none"
                )
                # The first samples emulate a short transition before the window is valid.
                sequence = [
                    simulate_sample(
                        device_id,
                        condition,
                        index * SAMPLE_PERIOD_S,
                        run_seed,
                        severity,
                        fault_type,
                    )
                    for index in range(samples_per_run)
                ]
                for start in range(0, len(sequence) - WINDOW_SIZE + 1):
                    window = sequence[start : start + WINDOW_SIZE]
                    features.append(window_features(window))
                    labels.append(condition_index)

                    alert_level, alert_site = estimate_alert(window[-1])
                    metadata.append(
                        {
                            "device_id": device_id,
                            "run_id": f"d{device_id}_{condition}_{run_index:03d}",
                            "condition": condition,
                            "fault_type": fault_type,
                            "severity": round(severity, 4),
                            "window_start_s": start,
                            "alert_level": alert_level,
                            "alert_site": alert_site,
                            "registers": list(window[-1].registers()),
                        }
                    )

    return (
        np.stack(features).astype(np.float32),
        np.asarray(labels, dtype=np.int64),
        metadata,
    )


def split_metrics(y_true: np.ndarray, probabilities: np.ndarray) -> dict:
    y_pred = np.argmax(probabilities, axis=1)
    labels = list(range(len(CONDITIONS)))
    return {
        "accuracy": float(accuracy_score(y_true, y_pred)),
        "macro_f1": float(f1_score(y_true, y_pred, average="macro")),
        "confusion_matrix": confusion_matrix(
            y_true, y_pred, labels=labels
        ).tolist(),
        "classification_report": classification_report(
            y_true,
            y_pred,
            labels=labels,
            target_names=list(CONDITIONS),
            output_dict=True,
            zero_division=0,
        ),
    }


def export_onnx_mlp(
    pipeline: Pipeline,
    path: Path,
    feature_names: tuple[str, ...] = FEATURE_NAMES,
    window_size: int = WINDOW_SIZE,
) -> None:
    scaler: StandardScaler = pipeline.named_steps["scaler"]
    model: MLPClassifier = pipeline.named_steps["model"]

    weights = [np.asarray(weight, dtype=np.float32) for weight in model.coefs_]
    biases = [np.asarray(bias, dtype=np.float32) for bias in model.intercepts_]
    input_dim = int(weights[0].shape[0])

    nodes: list[onnx.NodeProto] = []
    initializers: list[onnx.TensorProto] = []
    current = "normalized_features"
    for layer_index, (weight, bias) in enumerate(zip(weights, biases)):
        weight_name = f"W{layer_index}"
        bias_name = f"B{layer_index}"
        matmul_name = f"matmul{layer_index}"
        add_name = f"add{layer_index}"
        initializers.append(numpy_helper.from_array(weight, weight_name))
        initializers.append(numpy_helper.from_array(bias, bias_name))
        nodes.append(
            helper.make_node(
                "MatMul", [current, weight_name], [matmul_name]
            )
        )
        nodes.append(
            helper.make_node("Add", [matmul_name, bias_name], [add_name])
        )
        current = add_name
        if layer_index + 1 < len(weights):
            activation_name = f"relu{layer_index}"
            nodes.append(helper.make_node("Relu", [current], [activation_name]))
            current = activation_name

    nodes.append(helper.make_node("Identity", [current], ["logits"]))
    graph = helper.make_graph(
        nodes,
        "fpag_condition_mlp",
        [
            helper.make_tensor_value_info(
                "normalized_features", TensorProto.FLOAT, ["batch", input_dim]
            )
        ],
        [
            helper.make_tensor_value_info(
                "logits", TensorProto.FLOAT, ["batch", len(CONDITIONS)]
            )
        ],
        initializer=initializers,
    )
    metadata = {
        "schema_version": "1",
        "dataset": "physics_simulated_modbus_six_slaves",
        "conditions": json.dumps(list(CONDITIONS)),
        "condition_labels_zh": json.dumps(list(CONDITION_LABELS_ZH), ensure_ascii=False),
        "feature_names": json.dumps(list(feature_names)),
        "preprocessing": "z_score",
        "window_size": str(window_size),
        "sample_period_s": str(SAMPLE_PERIOD_S),
        "device_id_input": "false",
        "alert_register_input": "false",
    }
    model_proto = helper.make_model(
        graph,
        producer_name="fpag-condition-mlp",
        opset_imports=[helper.make_opsetid("", 13)],
        doc_string="Six-device motor condition recognition trained on physics simulation.",
    )
    model_proto.ir_version = 8
    for key, value in metadata.items():
        entry = model_proto.metadata_props.add()
        entry.key = key
        entry.value = value
    onnx.checker.check_model(model_proto)
    onnx.save(model_proto, path)


class ArrayCalibrationReader(CalibrationDataReader):
    def __init__(self, features: np.ndarray, input_name: str) -> None:
        self._iterator = iter(
            [{input_name: row.reshape(1, -1).astype(np.float32)} for row in features]
        )

    def get_next(self) -> dict | None:
        return next(self._iterator, None)


def normalize_features(
    features: np.ndarray, mean: np.ndarray, scale: np.ndarray
) -> np.ndarray:
    return ((features - mean) / scale).astype(np.float32)


def softmax(logits: np.ndarray) -> np.ndarray:
    shifted = logits - logits.max(axis=1, keepdims=True)
    exp = np.exp(shifted)
    return exp / exp.sum(axis=1, keepdims=True)


def run_onnx(
    path: Path, features: np.ndarray, mean: np.ndarray, scale: np.ndarray
) -> np.ndarray:
    session = ort.InferenceSession(
        str(path), providers=["CPUExecutionProvider"]
    )
    input_name = session.get_inputs()[0].name
    normalized = normalize_features(features, mean, scale)
    output = session.run(None, {input_name: normalized})[0]
    return np.asarray(output, dtype=np.float32)


def benchmark_onnx(
    path: Path,
    features: np.ndarray,
    mean: np.ndarray,
    scale: np.ndarray,
    iterations: int,
) -> dict:
    import time

    session = ort.InferenceSession(
        str(path), providers=["CPUExecutionProvider"]
    )
    input_name = session.get_inputs()[0].name
    batch = normalize_features(features[:1], mean, scale)
    for _ in range(20):
        session.run(None, {input_name: batch})
    durations = []
    for _ in range(iterations):
        start = time.perf_counter_ns()
        session.run(None, {input_name: batch})
        durations.append((time.perf_counter_ns() - start) / 1000.0)
    values = np.asarray(durations)
    return {
        "provider": "CPUExecutionProvider (PC sanity check, not NPU)",
        "iterations": iterations,
        "mean_us": float(values.mean()),
        "p99_us": float(np.percentile(values, 99)),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--train-runs", type=int, default=24)
    parser.add_argument("--eval-runs", type=int, default=12)
    parser.add_argument("--samples", type=int, default=24)
    parser.add_argument("--seed", type=int, default=20261019)
    parser.add_argument("--benchmark-iterations", type=int, default=1000)
    args = parser.parse_args()

    print("[1/8] generating datasets", flush=True)
    args.out.mkdir(parents=True, exist_ok=True)
    quant_tmp = args.out / "tmp"
    quant_tmp.mkdir(parents=True, exist_ok=True)
    os.environ["TEMP"] = str(quant_tmp)
    os.environ["TMP"] = str(quant_tmp)
    tempfile.tempdir = str(quant_tmp)
    x_train, y_train, meta_train = generate_split(
        (1, 2, 3, 4), args.train_runs, args.samples, args.seed
    )
    x_val, y_val, meta_val = generate_split(
        (5,), args.eval_runs, args.samples, args.seed + 1
    )
    x_test, y_test, meta_test = generate_split(
        (6,), args.eval_runs, args.samples, args.seed + 2
    )
    print(
        f"[2/8] datasets ready: train={len(x_train)} "
        f"val={len(x_val)} test={len(x_test)}",
        flush=True,
    )

    pipeline = Pipeline(
        [
            ("scaler", StandardScaler()),
            (
                "model",
                MLPClassifier(
                    hidden_layer_sizes=(32, 16),
                    activation="relu",
                    solver="adam",
                    alpha=1e-4,
                    batch_size=128,
                    learning_rate_init=1e-3,
                    max_iter=600,
                    early_stopping=True,
                    n_iter_no_change=30,
                    validation_fraction=0.12,
                    random_state=42,
                ),
            ),
        ]
    )
    print("[3/8] training sklearn MLP", flush=True)
    pipeline.fit(x_train, y_train)
    print(
        f"[4/8] training done: iterations={pipeline.named_steps['model'].n_iter_}",
        flush=True,
    )

    float_path = args.out / "condition_mlp_float.onnx"
    quant_path = args.out / "condition_mlp_qdq_uint8.onnx"
    preprocess_path = args.out / "preprocess.json"
    print("[5/8] exporting float ONNX", flush=True)
    export_onnx_mlp(pipeline, float_path)
    scaler = pipeline.named_steps["scaler"]
    preprocess_path.write_text(
        json.dumps(
            {
                "input": "raw_window_features",
                "transform": "z_score",
                "mean": scaler.mean_.astype(float).tolist(),
                "scale": scaler.scale_.astype(float).tolist(),
                "feature_names": list(FEATURE_NAMES),
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    print("[6/8] exporting static QDQ ONNX", flush=True)
    calibration_features = x_train[
        np.linspace(0, len(x_train) - 1, min(2048, len(x_train)), dtype=np.int64)
    ]
    quant_work = quant_tmp / "ort_working"
    if quant_work.exists():
        shutil.rmtree(quant_work)
    quant_work.mkdir(parents=True, exist_ok=True)
    original_tempdir = tempfile.TemporaryDirectory
    tempfile.TemporaryDirectory = lambda **_: FixedTemporaryDirectory(quant_work)
    try:
        quantize_static(
            str(float_path),
            str(quant_path),
            ArrayCalibrationReader(calibration_features, "normalized_features"),
            quant_format=QuantFormat.QDQ,
            activation_type=QuantType.QUInt8,
            weight_type=QuantType.QInt8,
            per_channel=True,
            reduce_range=False,
            calibrate_method=CalibrationMethod.MinMax,
        )
    finally:
        tempfile.TemporaryDirectory = original_tempdir
    print("[7/8] evaluating ONNX Runtime models", flush=True)
    onnx.checker.check_model(onnx.load(quant_path))

    mean = scaler.mean_.astype(np.float32)
    scale = scaler.scale_.astype(np.float32)
    logits = {
        "train_float": run_onnx(float_path, x_train, mean, scale),
        "validation_float": run_onnx(float_path, x_val, mean, scale),
        "test_device6_float": run_onnx(float_path, x_test, mean, scale),
        "train_quantized": run_onnx(quant_path, x_train, mean, scale),
        "validation_quantized": run_onnx(quant_path, x_val, mean, scale),
        "test_device6_quantized": run_onnx(quant_path, x_test, mean, scale),
    }
    probabilities = {name: softmax(value) for name, value in logits.items()}
    labels = {
        "train": y_train,
        "validation": y_val,
        "test_device6": y_test,
    }
    metrics = {
        "train": split_metrics(labels["train"], probabilities["train_float"]),
        "validation": split_metrics(
            labels["validation"], probabilities["validation_float"]
        ),
        "test_device6_float": split_metrics(
            labels["test_device6"], probabilities["test_device6_float"]
        ),
        "test_device6_quantized": split_metrics(
            labels["test_device6"], probabilities["test_device6_quantized"]
        ),
    }
    float_predictions = np.argmax(probabilities["test_device6_float"], axis=1)
    quant_predictions = np.argmax(
        probabilities["test_device6_quantized"], axis=1
    )
    parity = float(np.mean(float_predictions == quant_predictions))
    model_size = int(quant_path.stat().st_size)
    size_limit = 256 * 1024
    pass_checks = {
        "validation_accuracy_ge_0_90": metrics["validation"]["accuracy"] >= 0.90,
        "held_out_device6_accuracy_ge_0_85": (
            metrics["test_device6_quantized"]["accuracy"] >= 0.85
        ),
        "quantized_decision_parity_ge_0_98": parity >= 0.98,
        "model_size_le_256_kib": model_size <= size_limit,
    }

    dataset_path = args.out / "dataset.csv"
    import pandas as pd

    all_features = np.concatenate([x_train, x_val, x_test])
    all_labels = np.concatenate([y_train, y_val, y_test])
    all_meta = meta_train + meta_val + meta_test
    split = (
        ["train"] * len(x_train)
        + ["validation_device5"] * len(x_val)
        + ["test_device6"] * len(x_test)
    )
    frame_meta = pd.DataFrame(all_meta)
    frame_meta.insert(0, "split", split)
    frame_meta.insert(1, "label", all_labels)
    frame_features = pd.DataFrame(all_features, columns=FEATURE_NAMES)
    pd.concat([frame_meta, frame_features], axis=1).to_csv(
        dataset_path, index=False
    )
    print("[8/8] benchmarking and writing report", flush=True)

    report = {
        "dataset": "physics_simulated_modbus_six_slaves",
        "caveat": (
            "Metrics prove the simulated pipeline, not real-machine diagnostic accuracy."
        ),
        "conditions": list(CONDITIONS),
        "condition_labels_zh": list(CONDITION_LABELS_ZH),
        "fault_types": list(FAULT_TYPES),
        "feature_count": INPUT_DIM,
        "window_size": WINDOW_SIZE,
        "class_counts": {
            name: int(np.sum(all_labels == index))
            for index, name in enumerate(CONDITIONS)
        },
        "split_counts": {
            "train": int(len(x_train)),
            "validation_device5": int(len(x_val)),
            "test_device6": int(len(x_test)),
        },
        "device_ids_train": [1, 2, 3, 4],
        "device_id_validation": 5,
        "device_id_test": 6,
        "model": {
            "architecture": [INPUT_DIM, 32, 16, len(CONDITIONS)],
            "quantized_parameters": int(
                sum(np.asarray(weight).size for weight in pipeline.named_steps["model"].coefs_)
                + sum(
                    np.asarray(bias).size
                    for bias in pipeline.named_steps["model"].intercepts_
                )
            ),
            "float_bytes": int(float_path.stat().st_size),
            "quantized_bytes": model_size,
            "size_limit_bytes": size_limit,
        },
        "training_iterations": int(pipeline.named_steps["model"].n_iter_),
        "metrics": metrics,
        "quantized_decision_parity": parity,
        "pass_checks": pass_checks,
        "pass": all(pass_checks.values()),
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
            "quantized_onnx": str(quant_path),
            "preprocess_json": str(preprocess_path),
            "dataset_csv": str(dataset_path),
        },
    }
    report_path = args.out / "metrics.json"
    report_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8"
    )
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
