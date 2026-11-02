"""Collect a live gateway window and run the quantized condition model."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import time

import numpy as np
import onnxruntime as ort

from condition_pipeline import (
    CONDITION_LABELS_ZH,
    CONDITIONS,
    WINDOW_SIZE,
    RegisterSample,
    window_features,
)


ARTIFACTS = Path(__file__).resolve().parent / "artifacts" / "condition_mlp"


def request(host: str, port: int, payload: dict, timeout: float = 15.0) -> dict:
    with socket.create_connection((host, port), timeout=5.0) as sock:
        sock.settimeout(timeout)
        sock.sendall((json.dumps(payload) + "\n").encode("utf-8"))
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = sock.recv(16384)
            if not chunk:
                break
            data.extend(chunk)
    response = json.loads(data.decode("utf-8"))
    if response.get("code") != 0:
        raise RuntimeError(response.get("msg", "gateway request failed"))
    return response["data"]


def sample_from_gateway(data: dict) -> RegisterSample:
    return RegisterSample(
        points=tuple(
            float(data[f"bus_point{index}"]) / 100.0 for index in range(1, 6)
        ),
        temperature_c=float(data["temperature"]),
        current_a=float(data["current"]),
        rms=float(data["vibration_rms"]),
        peak=float(data["vibration_peak"]),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.137.100")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--device-id", type=int, default=6)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument(
        "--model", type=Path, default=ARTIFACTS / "condition_mlp_qdq_uint8.onnx"
    )
    parser.add_argument(
        "--preprocess", type=Path, default=ARTIFACTS / "preprocess.json"
    )
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    raw_samples: list[dict] = []
    model_samples: list[RegisterSample] = []
    for index in range(WINDOW_SIZE):
        data = request(
            args.host,
            args.port,
            {"cmd": "query_device", "device_id": args.device_id},
        )
        sample = sample_from_gateway(data)
        model_samples.append(sample)
        raw_samples.append(
            {
                "points": list(sample.points),
                "temperature_c": sample.temperature_c,
                "current_a": sample.current_a,
                "rms": sample.rms,
                "peak": sample.peak,
            }
        )
        print(
            f"{index + 1}/{WINDOW_SIZE} rms={sample.rms:.3f} "
            f"current={sample.current_a:.2f} temp={sample.temperature_c:.1f}",
            flush=True,
        )
        if index + 1 < WINDOW_SIZE:
            time.sleep(args.interval)

    preprocess = json.loads(args.preprocess.read_text(encoding="utf-8"))
    features = window_features(model_samples)[None, :]
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
    best = int(np.argmax(probabilities))
    result = {
        "device_id": args.device_id,
        "condition": CONDITIONS[best],
        "condition_zh": CONDITION_LABELS_ZH[best],
        "confidence": float(probabilities[best]),
        "probabilities": {
            name: float(value)
            for name, value in zip(CONDITIONS, probabilities, strict=True)
        },
        "model": str(args.model),
        "runtime": "PC ONNX Runtime CPU validation, not board NPU",
        "samples": raw_samples,
    }
    text = json.dumps(result, indent=2, ensure_ascii=False)
    print(text)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(text + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
