#!/usr/bin/env python3
"""Generate the compact Phase D reference fixture from public MoSQITo APIs.

Requires MoSQITo 1.2.1 and NumPy. The Rust test never invokes Python; this script exists only to
make the independently-produced golden data reproducible and reviewable.
"""

import argparse
import importlib.metadata
import json
import math
from pathlib import Path

import numpy as np
from mosqito.sq_metrics.loudness.loudness_zwtv.loudness_zwtv import loudness_zwtv
from mosqito.sq_metrics.sharpness.sharpness_din.sharpness_din_from_loudness import (
    sharpness_din_from_loudness,
)

SAMPLE_RATE = 48_000
P_REF = 2.0e-5
TONE_PEAK = P_REF * 10.0 ** (94.0 / 20.0) * math.sqrt(2.0)

SIGNALS = {
    "1khz_94db": {
        "sample_count": SAMPLE_RATE,
        # MoSQITo's stationary-signal start boundary differs from a zero-state real-time stream.
        # Validate after its first 10 ms; the step fixture below validates the actual attack path.
        "statistics_from_frame": 5,
        "checkpoints": [5, 10, 25, 50, 100, 250, 499],
    },
    "step_1khz_94db": {
        "sample_count": SAMPLE_RATE * 3,
        "statistics_from_frame": 0,
        "checkpoints": [
            0,
            200,
            249,
            250,
            251,
            255,
            260,
            275,
            300,
            500,
            999,
            1000,
            1001,
            1005,
            1010,
            1025,
            1050,
            1250,
            1499,
        ],
    },
    "silence": {
        "sample_count": SAMPLE_RATE // 2,
        "statistics_from_frame": 0,
        "checkpoints": [0, 1, 10, 100, 249],
    },
}


def sine(sample_count: int) -> np.ndarray:
    time = np.arange(sample_count, dtype=np.float64) / SAMPLE_RATE
    return TONE_PEAK * np.sin(2.0 * np.pi * 1000.0 * time)


def signal(signal_id: str, sample_count: int) -> np.ndarray:
    if signal_id == "1khz_94db":
        return sine(sample_count)
    if signal_id == "step_1khz_94db":
        samples = np.zeros(sample_count, dtype=np.float64)
        tone = sine(sample_count)
        samples[SAMPLE_RATE // 2 : SAMPLE_RATE * 2] = tone[
            SAMPLE_RATE // 2 : SAMPLE_RATE * 2
        ]
        return samples
    if signal_id == "silence":
        return np.zeros(sample_count, dtype=np.float64) + 1.0e-20
    raise ValueError(f"unknown signal: {signal_id}")


def finite(value: float) -> float:
    return float(value) if math.isfinite(float(value)) else 0.0


def measure(signal_id: str, definition: dict) -> dict:
    samples = signal(signal_id, definition["sample_count"])
    loudness, specific, _, _ = loudness_zwtv(samples, SAMPLE_RATE, field_type="free")
    with np.errstate(invalid="ignore", divide="ignore"):
        sharpness = np.atleast_1d(
            sharpness_din_from_loudness(loudness, specific, weighting="din")
        )
    sharpness = np.nan_to_num(sharpness, nan=0.0)
    statistics_from = definition["statistics_from_frame"]
    loudness_stats = loudness[statistics_from:]
    sharpness_stats = sharpness[statistics_from:]
    return {
        "id": signal_id,
        "sample_count": definition["sample_count"],
        "frames": len(loudness),
        "statistics_from_frame": statistics_from,
        "n_mean": finite(np.mean(loudness_stats)),
        "n_max": finite(np.max(loudness_stats)),
        "s_mean": finite(np.mean(sharpness_stats)),
        "s_max": finite(np.max(sharpness_stats)),
        "checkpoints": [
            {
                "frame": frame,
                "n": finite(loudness[frame]),
                "s": finite(sharpness[frame]),
            }
            for frame in definition["checkpoints"]
        ],
    }


def generate() -> dict:
    version = importlib.metadata.version("mosqito")
    if version != "1.2.1":
        raise RuntimeError(f"expected MoSQITo 1.2.1, found {version}")
    return {
        "schema": 1,
        "reference": {
            "library": "MoSQITo",
            "version": version,
            "generator": "scripts/generate_phase_d_mosqito_golden.py",
            "loudness": "ISO 532-1:2017 free-field",
            "sharpness": "DIN 45692:2009 Widmann",
            "sample_rate_hz": SAMPLE_RATE,
            "output_period_ms": 2.0,
        },
        "tolerance": {
            "loudness_relative": 0.001,
            "loudness_absolute_sone": 0.02,
            "sharpness_absolute_acum": 0.05,
        },
        "signals": [measure(signal_id, definition) for signal_id, definition in SIGNALS.items()],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", type=Path, help="fail unless PATH matches regenerated data")
    args = parser.parse_args()
    generated = generate()
    if args.check:
        existing = json.loads(args.check.read_text())
        if generated != existing:
            raise SystemExit(f"golden fixture differs: {args.check}")
        print(f"golden fixture matches: {args.check}")
    else:
        print(json.dumps(generated, indent=2))


if __name__ == "__main__":
    main()
