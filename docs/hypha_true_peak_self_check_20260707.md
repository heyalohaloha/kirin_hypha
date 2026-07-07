# Hypha True Peak Self-Check — 2026-07-07

## Purpose

Kirin Hypha is free and open source. This note records a reproducible self-check for the True Peak measurement path that Hypha uses internally:

`WAV test signal -> MeasureEngine::push()` in 100 ms chunks -> `finalize().max_true_peak`

This is a self-check, not third-party certification. It is suitable for a technical post because the command, commit, signal names, expected values, measured values, and scope are all explicit.

## Command

```bash
cargo test -p kirin_measure --test tp_engine_self_check -- --nocapture
```

## Result

Recorded source state: `253360b64fc537be2b573a27ed8f396b56358253` (`[B-288] refresh Hypha 1.1.12 release state`)

| signal | expected | measured | error |
|---|---:|---:|---:|
| 997 Hz 0 dBFS phase 0 | +0.000 dBTP | +0.005 dBTP | +0.005 dB |
| 997 Hz 0 dBFS phase 1 | +0.000 dBTP | +0.005 dBTP | +0.005 dB |
| 997 Hz 0 dBFS phase 2 | +0.000 dBTP | +0.005 dBTP | +0.005 dB |
| 997 Hz 0 dBFS phase 3 | +0.000 dBTP | +0.005 dBTP | +0.005 dB |
| 11760 Hz -6 dBFS near-Nyquist | -6.000 dBTP | -5.940 dBTP | +0.060 dB |

All five signals pass the self-check tolerance of `+/-0.100 dBTP`.

## Scope

- This verifies the Hypha measurement core path, not a DAW screenshot or a third-party certification.
- The True Peak value here is the session maximum (`max_true_peak`), not the live 400 ms recent peak shown in Watch mode.
- The local source note says these True Peak files were copied from Kirin Sense and are Daisuke-created original test signals. If the public post says "EBU Tech 3341 test set", use the actual official EBU files or clearly phrase this as "known True Peak self-check signals based on EBU Tech 3341-style cases."
- The public wording should invite independent verification and avoid certified/compliance language unless a separate formal verification is performed.

## Safe Public Copy

Japanese:

> 無料のKirin Hyphaで、True Peak計測の自己検証を公開します。既知のTrue Peakテスト信号に対して、Hyphaの計測コアがどの値を返すかを、コマンド、commit、測定値つきでそのまま出します。第三者検証歓迎です。

English:

> Kirin Hypha is free and open source. We are publishing a self-check of its True Peak measurement path against known test signals, including the command, commit, and measured values. Independent verification is welcome.

## Avoid

- "EBU certified"
- "officially verified"
- "industry-best"
- competitor comparisons
- saying the local copied signals are the official EBU files unless that source is verified
