# Hypha EBU R128 dependency provenance

Base: `ebur128` 0.1.10, upstream commit `0052abf06d8844f0972657c71e327d7eeee8967c`.
Source: <https://github.com/sdroege/ebur128/tree/0.1.10>.
Published crate SHA-256: `e227cc62d64d6fe01abbef48134b9c1f17d470cef1e7a56337ad05b1f81df7f9`.
License: MIT; upstream notices and LICENSE are retained.

The initial import preserves the 34 published source, manifest, license, documentation,
benchmark, example, and test files byte for byte. Registry metadata and the upstream lockfiles
are omitted; Hypha's workspace lockfile controls the product dependency resolution.
The dependency is imported before the performance change so the local delta is auditable.

The local extension caches raw sums of squared filtered samples in 128-frame/channel tiles
for explicit M/S queries. Touched tiles are recomputed after the original filter writes;
queries combine complete tiles and scalar edges. No running-total subtraction is used.
The input filter, true-peak implementation, integrated/LRA history insertion, channel weighting
and observation cadence are unchanged. Reset clears the cache; reconfiguration rebuilds it.
The original scalar query remains an oracle. Cache readout is opt-in, with scalar fallback.
Local tests cover ring wrap, silence after loud and quiet input, partial blocks, seven sample
rates (including non-round rates), mono/stereo, reset, channel maps and reconfiguration.
The repository S-1 WAV comparison checks M/S within 1e-9 LU and unchanged I/LRA/TP bits.
The standalone manifest has its own empty workspace only to run upstream unit tests; Hypha's
root patch and lockfile govern product builds. The standalone lockfile and target are ignored.
Do not attribute Hypha's local extension to an upstream release or claim upstream certification.
