# Hypha EBU R128 dependency provenance

Base: `ebur128` 0.1.10, upstream commit `0052abf06d8844f0972657c71e327d7eeee8967c`.
Source: <https://github.com/sdroege/ebur128/tree/0.1.10>.
Published crate SHA-256: `e227cc62d64d6fe01abbef48134b9c1f17d470cef1e7a56337ad05b1f81df7f9`.
License: MIT; upstream notices and LICENSE are retained.

The initial import preserves the 34 published source, manifest, license, documentation,
benchmark, example, and test files byte for byte. Registry metadata and the upstream lockfiles
are omitted; Hypha's workspace lockfile controls the product dependency resolution.
The dependency is imported before the performance change so the local delta is auditable.

The intended extension caches bounded blocks of filtered sample energy for explicit M/S
queries. It must not change the input filter, true-peak implementation, integrated/LRA history
insertion, reset semantics, channel weighting, or measurement cadence. The original scalar
query remains an oracle for numeric regression. Cache readout is opt-in, and tests must cover
wrap, silence, partial blocks, non-round rates, reset, channel-map changes and reconfiguration.
Do not attribute Hypha's local extension to an upstream release or claim upstream certification.
