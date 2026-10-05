# Third-party source notices

The project license remains [GPLv3](LICENSE). This file supplements the licenses and copyright
notices retained in dependency and vendored source trees; it is not a complete inventory of every
component included by every build. Release packaging must also account for the actual payload.

## MoSQITo v1.2.1

Upstream: [Eomys/MoSQITo](https://github.com/Eomys/MoSQITo/tree/v1.2.1),
commit `a14daeafcf0a37f36e4a29314ea42bc68b304c0e`.
License: [Apache License 2.0](THIRD_PARTY_LICENSES/MoSQITo-1.2.1-Apache-2.0.txt),
retained verbatim from that public tag. No project-level `NOTICE` file was present in the inspected tag.

The initial Hypha source records references, table extraction and scalar ports associated with
MoSQITo in these `crates/kirin_measure/src/phase_d/` modules:

- `tables.rs`: coefficients and lookup tables.
- `filter_bank.rs`, `core_loudness.rs`, `nonlinear_decay.rs`, `calc_slopes.rs`,
  `temporal_weighting.rs`: loudness calculation stages.
- `sharpness.rs`: sharpness calculation reference.

The Hypha implementations are Rust adaptations rather than unmodified upstream Python files.
Repository history records subsequent integration, streaming, channel and display changes.
The original file-level source/module references are retained. This notice restores a source name
that was omitted from several later comments; it does not change numerical constants or DSP code.
The relationship to public standards in those comments does not replace the OSS attribution.
