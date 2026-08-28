#!/usr/bin/env python3
"""Generate the independent 100 ms Perceptual Delta Sharpness reference fixture.

Requires MoSQITo 1.2.1 and NumPy. Rust tests read only the committed JSON fixture.
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
APERTURE = 4_800
P_REF = 2.0e-5


def step_tone() -> np.ndarray:
    sample_count = SAMPLE_RATE * 3
    time = np.arange(sample_count, dtype=np.float64) / SAMPLE_RATE
    peak = P_REF * 10.0 ** (94.0 / 20.0) * math.sqrt(2.0)
    tone = peak * np.sin(2.0 * np.pi * 1_000.0 * time)
    samples = np.zeros(sample_count, dtype=np.float64)
    samples[SAMPLE_RATE // 2 : SAMPLE_RATE * 2] = tone[
        SAMPLE_RATE // 2 : SAMPLE_RATE * 2
    ]
    return samples


def generate() -> dict:
    version = importlib.metadata.version("mosqito")
    if version != "1.2.1":
        raise RuntimeError(f"expected MoSQITo 1.2.1, found {version}")
    loudness, specific, _, _ = loudness_zwtv(
        step_tone(), SAMPLE_RATE, field_type="free"
    )
    with np.errstate(invalid="ignore", divide="ignore"):
        sharpness = np.nan_to_num(
            sharpness_din_from_loudness(loudness, specific, weighting="din"),
            nan=0.0,
        )
    # MoSQITo publishes every 2 ms. Index 49 is the last measured point inside the first
    # half-open [0, 100 ms) aperture, then every 50th point preserves the same endpoint rule.
    endpoint_values = [float(sharpness[50 * endpoint - 1]) for endpoint in range(1, 31)]
    return {
        "schema": 1,
        "reference": {
            "library": "MoSQITo",
            "version": version,
            "sharpness": "DIN 45692:2009 Widmann",
            "sample_rate_hz": SAMPLE_RATE,
            "aperture_samples": APERTURE,
            "state_epoch_samples": 0,
        },
        "tolerance_acum": 0.05,
        "post_minus_pre": endpoint_values,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", type=Path)
    args = parser.parse_args()
    generated = generate()
    if args.check:
        if generated != json.loads(args.check.read_text()):
            raise SystemExit(f"golden fixture differs: {args.check}")
        print(f"golden fixture matches: {args.check}")
    else:
        print(json.dumps(generated, indent=2))


if __name__ == "__main__":
    main()
