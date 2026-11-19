# Native test labels and compiler cache: review proposal

Status: source proposal, no hosted CI run or timing improvement measured.

Every registered CTest now receives additive `hypha-all` and one domain label.
Unknown names remain `hypha-general`; no unknown test is excluded. Existing names,
regex selections, storage fixtures, mandatory gates and aggregate checks stay intact.
These labels describe inventory; they are not permission to skip unchanged domains.

Unsigned Makefile/Ninja diagnostics can opt in with `-DKIRIN_HYPHA_USE_CCACHE=ON`
when an approved ccache is already installed. Default OFF preserves current builds.
An existing launcher is rejected rather than silently replaced. Compiler contents
are hashed; relaxed modes are not enabled. Test execution is never cache-hit gated.
Visual Studio/Xcode generators are rejected rather than claiming unsupported reuse.

Proposed workflow change after review: provision an approved ccache, enable the option
only for native macOS phases, and retain every existing phase/CTest invocation.
Persistent cache is deliberately not wired yet: current public CI security explicitly
rejects actions/cache. Do not weaken that rule or introduce a new Action implicitly.
A reviewed cache mechanism must partition OS, architecture, compiler identity, JUCE
revision and build settings, bound cache size, and never store signing material,
release outputs or test results. PR cache trust and invalidation need explicit review.
No CI timing increase or benefit is claimed before cold/warm hosted measurements.

Acceptance: metadata fixture covers safety/reference/record/unknown; source checks
require additive labels and opt-in launcher; existing CI inventory/security and the
full lightweight contract must pass. Actual compilation and cold/warm timing remain
unperformed until the reviewer authorizes the workflow experiment.

## Bounded build-contract review

Rebased onto integration `8ccce1ba53888f6c3955632e4d31030f26d578ba`, including
the existing Reference button build dependency. The CMake delta adds only the
CompilerCache and NativeTestLabels includes and removes two blank lines to keep
the source-line budget unchanged. Removing those includes restores the same token
sequence as the integration CMake file. DATA_SOURCES is byte-identical; all 56
present registered asset files match their existing hashes. Rights, allowed uses,
holds and asset bytes are unchanged. Only embedContractSha256 advances from
`fbe968236bf1bf46a8764ebfe350e90b5123ce9c8fb9d7c443ab5449bc5e7142` to
`5204e8a4897c8038bdcc8d57f12ac42284f66849b73d28f266710be627288823`. The exact-hash guard remains unchanged.
This records a technical build-contract review, not new rights or release approval.
The earlier stale-hash gate failure is retained in validation records.
The workflow cache wiring and new Actions are excluded from this release.
