# Kirin presentation-latency patch

This directory is a source snapshot of `nih_plug` at upstream revision
`28b149ec4d62757d0b448809148a0c3ca6e09a95` (ISC license).

Kirin's local delta is deliberately limited to the VST3 host contract:

- implement Steinberg `IAudioPresentationLatency` on the VST3 component;
- retain main-input and main-output latency as lock-free atomics;
- expose the latest values as optional fields on `Transport` for `process()`.

The audio thread only performs atomic loads. Hypha subtracts the main-input
value from the host callback sample position when forming its measurement
content clock. The patch does not delay, copy, or modify audio.

Interface reference:
https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioPresentationLatency.html
