"""Shared data contract and physics-based simulator for condition recognition."""
from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Iterable

import numpy as np


CONDITIONS = ("stopped", "idle", "normal", "overload", "fault")
CONDITION_LABELS_ZH = ("停机", "空载", "正常带载", "过载", "异常")
FAULT_TYPES = ("imbalance", "looseness", "bearing_wear", "coupling_misalignment")
WINDOW_SIZE = 8
SAMPLE_PERIOD_S = 1.0

BASE_FEATURE_NAMES = (
    "point_mean",
    "point_std",
    "point_range",
    "point_max",
    "temperature_c",
    "current_a",
    "vibration_rms",
    "vibration_peak",
    "crest_factor",
    "dominant_point_ratio",
)
FEATURE_NAMES = tuple(
    [f"last_{name}" for name in BASE_FEATURE_NAMES]
    + [f"mean_{name}" for name in BASE_FEATURE_NAMES]
    + [f"std_{name}" for name in BASE_FEATURE_NAMES]
    + [f"trend_{name}" for name in BASE_FEATURE_NAMES]
)
INPUT_DIM = len(FEATURE_NAMES)


@dataclass(frozen=True)
class DeviceBaseline:
    point_scale: float
    current_scale: float
    temperature_offset: float
    ambient_c: float


@dataclass(frozen=True)
class RegisterSample:
    points: tuple[float, float, float, float, float]
    temperature_c: float
    current_a: float
    rms: float
    peak: float

    def registers(self) -> tuple[int, ...]:
        return tuple(
            [max(0, min(65535, round(value * 100))) for value in self.points]
            + [
                max(0, min(65535, round(self.temperature_c * 10))),
                max(0, min(65535, round(self.current_a * 100))),
                max(0, min(65535, round(self.rms * 100))),
                max(0, min(65535, round(self.peak * 100))),
            ]
        )

    def base_features(self) -> np.ndarray:
        points = np.asarray(self.points, dtype=np.float64)
        point_mean = float(points.mean())
        point_std = float(points.std())
        point_range = float(points.max() - points.min())
        point_max = float(points.max())
        crest = float(self.peak / max(self.rms, 0.02))
        dominant = float(point_max / max(point_mean, 0.02))
        return np.asarray(
            [
                point_mean,
                point_std,
                point_range,
                point_max,
                self.temperature_c,
                self.current_a,
                self.rms,
                self.peak,
                crest,
                dominant,
            ],
            dtype=np.float32,
        )


def device_baseline(device_id: int) -> DeviceBaseline:
    """Stable per-machine characteristics without using the ID as a model input."""
    rng = np.random.default_rng(20261019 + device_id)
    return DeviceBaseline(
        point_scale=float(rng.uniform(0.82, 1.18)),
        current_scale=float(rng.uniform(0.90, 1.10)),
        temperature_offset=float(rng.uniform(-2.5, 3.5)),
        ambient_c=float(rng.uniform(24.0, 30.0)),
    )


def _clamp(values: Iterable[float], low: float, high: float) -> tuple[float, ...]:
    return tuple(float(np.clip(value, low, high)) for value in values)


def simulate_sample(
    device_id: int,
    condition: str,
    elapsed_s: float,
    seed: int,
    severity: float = 1.0,
    fault_type: str = "imbalance",
) -> RegisterSample:
    if condition not in CONDITIONS:
        raise ValueError(f"unknown condition: {condition}")
    if condition == "fault" and fault_type not in FAULT_TYPES:
        raise ValueError(f"unknown fault type: {fault_type}")

    rng = np.random.default_rng(
        seed + device_id * 1009 + int(elapsed_s * 10) * 9176
    )
    baseline = device_baseline(device_id)
    t = elapsed_s
    common_noise = float(rng.normal(0.0, 0.035))
    slow_load = 1.0 + 0.05 * math.sin(2.0 * math.pi * t / 37.0 + device_id)

    if condition == "stopped":
        points = np.asarray([0.04, 0.03, 0.02, 0.03, 0.04])
        current = 0.0
        temp = baseline.ambient_c
        rms = 0.015
        peak = 0.035
    elif condition == "idle":
        points = np.asarray([0.12, 0.10, 0.08, 0.11, 0.13])
        current = 0.85 * baseline.current_scale * slow_load
        temp = baseline.ambient_c + 4.0 + baseline.temperature_offset
        rms = 0.11
        peak = 0.20
    elif condition == "normal":
        points = np.asarray([0.30, 0.24, 0.22, 0.26, 0.31])
        current = 3.8 * baseline.current_scale * slow_load
        temp = 43.0 + baseline.temperature_offset
        rms = 0.24
        peak = 0.48
    elif condition == "overload":
        points = np.asarray([0.62, 0.55, 0.51, 0.58, 0.64])
        current = (5.3 + 0.9 * severity) * baseline.current_scale * slow_load
        temp = 58.0 + 5.0 * severity + baseline.temperature_offset
        rms = 0.48
        peak = 0.92
    else:
        if fault_type == "imbalance":
            points = np.asarray([1.15, 0.38, 0.32, 0.40, 0.46])
            current = 4.1 * baseline.current_scale
            temp = 47.0 + baseline.temperature_offset
            rms = 0.68
            peak = 1.62
        elif fault_type == "looseness":
            points = np.asarray([0.82, 0.66, 0.58, 0.71, 0.76])
            current = 4.3 * baseline.current_scale
            temp = 49.0 + baseline.temperature_offset
            rms = 0.72
            peak = 2.05
        elif fault_type == "bearing_wear":
            points = np.asarray([0.46, 0.53, 0.61, 0.67, 1.02])
            current = 4.5 * baseline.current_scale
            temp = 53.0 + baseline.temperature_offset
            rms = 0.64
            peak = 1.45
        else:
            points = np.asarray([0.40, 0.44, 0.58, 0.93, 0.86])
            current = 4.4 * baseline.current_scale
            temp = 50.0 + baseline.temperature_offset
            rms = 0.61
            peak = 1.38
        points = points * severity
        rms *= severity
        peak *= severity

    waveform = 1.0 + 0.08 * math.sin(2.0 * math.pi * t / 5.0)
    points = np.asarray(points) * baseline.point_scale * waveform
    points = points + rng.normal(0.0, 0.025 + 0.015 * severity, len(points))
    rms = max(0.005, rms * baseline.point_scale * waveform + common_noise)
    peak = max(rms, peak * baseline.point_scale + rng.normal(0.0, 0.05))
    current = max(0.0, current * (1.0 + rng.normal(0.0, 0.025)))
    temp = temp + rng.normal(0.0, 0.8) + 0.015 * current

    return RegisterSample(
        points=tuple(_clamp(points, 0.0, 12.0)),
        temperature_c=float(np.clip(temp, -20.0, 120.0)),
        current_a=float(np.clip(current, 0.0, 20.0)),
        rms=float(np.clip(rms, 0.0, 12.0)),
        peak=float(np.clip(peak, 0.0, 20.0)),
    )


def window_features(samples: list[RegisterSample]) -> np.ndarray:
    if len(samples) != WINDOW_SIZE:
        raise ValueError(f"expected {WINDOW_SIZE} samples, got {len(samples)}")
    base = np.stack([sample.base_features() for sample in samples]).astype(
        np.float32, copy=False
    )
    return np.concatenate(
        [base[-1], base.mean(axis=0), base.std(axis=0), base[-1] - base[0]]
    ).astype(np.float32, copy=False)


def estimate_alert(sample: RegisterSample) -> tuple[int, int]:
    """Legacy rule output used only for the Modbus alert register."""
    if sample.rms >= 0.60:
        level = 2
    elif sample.rms >= 0.35:
        level = 1
    else:
        level = 0
    if level == 0:
        return 0, 0
    point_index = int(np.argmax(sample.points))
    return level, point_index + 1
