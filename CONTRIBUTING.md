# Contributing to Kirin Hypha

Kirin Hypha remains GPLv3. Preserve LICENSE, dependency copyright notices, source availability,
and independent reproducibility. Read [AGENTS.md](AGENTS.md), [README.md](README.md), and
[the invariants](docs/hypha_invariants.md) before changing product behavior.

## Build and verify

The Rust workspace uses Cargo.lock; the common shipping shell uses the pinned JUCE submodule
and tracked patch stack. Toolchain/platform prerequisites and unsigned build steps are in
[the build guide](docs/hypha_build_entry.md).

```sh
node scripts/build_hypha.mjs --help
node scripts/build_hypha.mjs --without-aax
bash scripts/test_lightweight_contract.sh
cargo test --workspace --locked
cargo clippy --workspace --all-targets --locked
```

The quick build produces PRE/POST AU+VST3 Universal on macOS and VST3 x64 on Windows.
It does not require signing credentials, iLok, or the external licensed AAX SDK. AAX is optional;
its external SDK/license requirements are in [the AAX guide](docs/aax_build_signing_entry.md).
The complete source/packaging/native test gate is `bash scripts/test_release_source.sh`.
FFI changes also require the ignored parity and pairing_candidates suites documented in AGENTS.
Host acceptance and formal release signatures remain separate gates.

## Pull requests

Use a focused branch and describe the trigger, behavior, affected roles/platforms, and actual
verification. Cover failure, boundary and restore paths. Keep normal audio bit-identical with zero
latency, preserve immutable measurement/Record/Reference data, and avoid audio-thread allocation,
locks and blocking I/O. Existing large source files follow the source-line ratchet.

Never commit real credentials, signing keys, account/admin identifiers, personal workstation paths,
private operator state, licensed SDKs, or non-redistributable audio. Use value-free example templates;
release state and generated distributions remain ignored. Public verification contracts, signatures,
checksums, technical evidence and license notices belong publicly.

Public PR CI must be hosted and unsigned with a read-only token. Formal signing and publication
require reviewed exact source and a separately controlled credential boundary. Do not treat a green
PR as permission to merge, sign or release. See [release qualification](docs/hypha_release_entry.md).
Report vulnerabilities according to [SECURITY.md](SECURITY.md), without putting details in an Issue.

## Contribution provenance

Contribute code, documentation and assets that you are entitled to provide under the applicable
project and dependency licenses. Identify third-party sources, versions or commits, license terms,
required notices, and the parts adapted or used as references. Preserve upstream attribution and
record meaningful modifications. Generated code and assets follow the same rule; retain available
source/reference and generation records without committing account identifiers or credentials.

Do not submit leaked source, unauthorized non-public materials, or code obtained by decompiling or
disassembling a third-party proprietary product. Do not commit proprietary product binaries or
assets without a documented redistribution basis. Public product research and normal user-visible
observations should be described separately from implementation sources. If provenance is uncertain,
flag it for review before incorporation; a matching name or a common algorithm alone does not
establish copying. See [PROVENANCE.md](PROVENANCE.md).
Review provenance for changed source, dependencies, patches, assets, generation references and
distribution uses, using [the change review template](docs/provenance/change_review_template.md).
Keep Unknown items visible; do not substitute an unsupported origin or legal guarantee. Private
evidence may be reviewed separately without committing account records, personal values or secrets.
