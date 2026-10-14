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

## Distribution component inventory

Formal packages deliver `Legal/component-notices.json`, the retained component license texts,
and `Legal/source-delivery.json` beside the signed plugin bundles. The JSON inventory identifies
the pinned JUCE source and its tracked patches, its bundled codecs and SDKs, the Rust standard runtime, the normal/build Rust dependency closure,
and their source and modification notices. This conservative inventory does not replace
review of the actual linked payload, external SDK terms, or a component's applicable license.

Upstream license notices omitted from published Cargo packages are retained under
`THIRD_PARTY_LICENSES/cargo/`. Their upstream commit, blob and file hashes are recorded in
`provenance.json`. The r-efi upstream records its MIT terms and copyrights in `AUTHORS`.
RealFFT's pinned upstream declares MIT in its README; the supplemental MIT standard terms
are explicitly a reference and do not invent a copyright notice or a new grant.

## JUCE bundled components and Rust runtime

JUCE includes FLAC 1.4.3, Ogg (the source pinned with JUCE 7.0.12), Vorbis 1.3.7,
IJG libjpeg 6b, libpng 1.6.37, zlib 1.2.3, Apple AudioUnitSDK (pinned with JUCE),
and VST3 SDK 3.7.8. Their original copyright and license notices are retained in
`Legal/juce_shell/JUCE/modules/`; VST3 uses its GPLv3 option and includes its BSD
base notices. Platform and format linkage is separately checked against each payload.

This software is based in part on the work of the Independent JPEG Group.

Rust 1.94.1 standard-library license texts and upstream dependency notices are retained
in `Legal/THIRD_PARTY_LICENSES/rust-runtime-1.94.1/`. This inventory does not authorize
redistribution of an external AAX SDK or PACE tool. Their applicable terms and the
Corresponding Source boundary require separate verification before an AAX release.

The release includes `Legal/Corresponding-Source.txt` with the exact source archive URL
and SHA-256. The archive is supplied free of charge alongside the release binaries.
