# Exact-distribution provenance gate

This adds to the existing source, signature, notarization, host and three-channel release gates.
It does not change GPLv3 or dependency licenses, certify legal compliance, or qualify an existing
Release retrospectively. Local fixtures and CI success are not actual distribution evidence.

The public asset registry records material hashes and per-use holds. Its Unknown entries describe
missing rights/use evidence, not an infringement finding. Current CMake unconditionally embeds
three GUI PNGs in PRE and POST. The maintainer declared these Codex-created images were generated from
textual instructions, with no third-party image inputs or specific existing-product/design
reproduction instruction. Scoped generation/copy/hash evidence corroborates the known outputs;
a retained input JPEG shows a Hypha-labelled mockup, not evidence of a third-party product. These
three materials are accepted for binary, preview and source use. One documentation image is a
byte-identical alias of the accepted embedded image, giving four accepted paths for three distinct
image hashes. The same material/use decision applies to both paths; this does not approve a fourth
image or extend approval to other materials. Held-byte checks still reject renamed copies of
unapproved material. Missing historical records alone
do not establish a problem or keep the project Freeze in place. Other explicitly unresolved
materials keep individual holds; existing Releases and unrelated OSS work are not stopped.
No product image, executable line or visual behavior is changed to evade this gate.
Individual release requires reviewed rights evidence and allowed uses, or an authorized, reviewed
replacement. A new hash never inherits permission automatically.
The inspected CMake build contract is hash-bound in the registry. Changed embedding declarations,
including appended resources, require review and a corresponding registry update.

### 2026-10-05 optional updater build-contract review

Base: `cd045837b4f4c28c08ade2cdb3ea707560b39ff7` (B-1229). At review time, the updater candidate
was uncommitted; the review did not assert exact-commit or release acceptance. Independent read-only review
and the implementation session compared the CMake difference and inspected
`UpdateChecking.cmake`, `UpdateTrust.cmake` and `UpdateIntegrationTests.cmake`.
The original `KIRIN_HYPHA_DATA_SOURCES` declaration and the three embedded PNG hashes are
unchanged. These helpers add original updater code, system-framework links, trust/plist metadata
and fixture tests, with no image append, replacement or optional-font change.
The reviewed candidate `CMakeLists.txt` SHA-256 is
`35fecd970c53c985e359f08b015f18756661db4401ad2dda06389adec9cd05bf`; only the registry's
build-contract hash is advanced. Existing material hashes, holds, rights evidence and allowed
uses are not changed. Whole-file contract drift continues to fail closed. This bounded technical
review is not new asset permission, extraction/linkage evidence, legal certification or release
approval. Exact-candidate linkage/NOTICE/source delivery and all release-specific gates remain.

### 2026-10-07 observation snapshot native-test build-contract review

Base: `7b0c301cff7d458805ccaea6c936ef84849a8b68` (B-1315). The uncommitted
G1 candidate adds only `include(cmake/SnapshotContracts.cmake)` to the reviewed CMake file.
An independent read-only review and the implementation session inspected that module:
it registers two original native fixture sources, include paths, the existing Rust archive,
system-library links, dependencies and one CTest; it adds no BinaryData or font resources.
The `KIRIN_HYPHA_DATA_SOURCES` declaration, optional licensed-font boundary and all three
embedded PNG bytes are identical to the base. The reviewed CMake SHA-256 is
`0fbc71674d77f3298ecf6fa40555690026dff606418de750f96e881d4aa35fda`.
Only the registry build-contract hash advances; material hashes, holds, rights evidence and
allowed uses retain their prior decisions. This is a bounded source review, not exact-payload
linkage, new asset permission or release acceptance. Distribution-specific gates still apply.

### 2026-10-08 DRUM/PSR G2 build-contract review

Base: `eca0b87fd680737c64a8caefea56d2bc82a5c708` (B-1325). The uncommitted G2
candidate adds original snapshot presentation sources and native test helpers through
`HyphaSnapshotSources.cmake` and the existing source lists, plus the existing JUCE graphics
module link for the Capture local-PNG fixture. The complete CMake difference, snapshot-source
module and language-module registrations were inspected independently.
The intermediate CMake hash `a351e73d90eba2f47720ca01a8260f9b0a7dbc2bfa67b137fdbbe44529ef37d0`
was an asset-contract review, not final build acceptance. A subsequent native link failure
identified the omitted TIME/Session source group in `KirinReferenceAuditionComponentTests`.
After that registration was corrected, an independent static dependency audit checked every
`HyphaObservatoryView.cpp` source list: five effective targets (PRE, POST, Reference Preview,
UI Render and Reference Audition Component) include all four TIME/Session implementation
sources. `HyphaSnapshotSource.cpp` belongs only to shipping PRE/POST, and those targets
link the selected `KIRIN_FFI_LIB` with the existing Rust refresh dependency and native libraries.
The reviewed snapshot-source module SHA-256 is
`b894f112f83e462ff432aa134a70b599713950cb5eb13e5e3e16b5a676f04a63`.
The 2,178-byte embedded resource/font declaration is byte-identical to the base; all three
embedded PNG bytes and hashes and `ObservatoryMaterial.cmake` are unchanged. No asset, font,
license, grant, hold or allowed-use decision changes. The reviewed B-1327 CMake SHA-256 is
`da4fc4719057dfa19968f35da25ae399a134a18a674cdbcc00939714b17346af`.
Only the registry build-contract hash advances. Static review evidence is retained as
`cmake-snapshot-dependencies-final.log`, SHA-256
`62a20323294393988bf9f3e3cdb2539deaf92583d373bc4e9ed2d589505cc105`.
This bounded review does not certify native build/fixture execution, exact-payload linkage or
release acceptance; actual build results and distribution-specific gates remain separate.

### 2026-10-08 VU calibration fixture build-contract review

Relative to B-1327, the only CMake change links the existing `juce::juce_cryptography`
module into `KirinUiRenderContractTests` for the shared VU preference scope hash.
The shipping targets already link this module. The full difference and the existing resource/font declarations were inspected;
the declarations, all three embedded PNGs and
`ObservatoryMaterial.cmake` remain byte-identical to B-1325. The DRUM stage reuses
the existing material raster and adds no resource. The current reviewed CMake SHA-256 is
`d69822641db867e712674ce1cb539c2468b3448438df3fc2139cc1f999cb9e61`. Only the registry's build-contract hash
advances. Existing material permissions, holds, licenses and allowed uses are retained;
this source review does not certify actual payload linkage or release acceptance.

### 2026-10-09 Windows native UTF-8 build-contract review

Base: `f422c08ed42e9f3825cb112c7072a5a5e1e79f0c` (B-1350). The implementation session
inspected the complete CMake difference: three added lines apply the already-defined source
encoding options to every owned target. MSVC uses the existing `/utf-8`; other compilers
receive no additional option. This repairs CP932 interpretation of Japanese native-test source
without changing embedded resources, font selection or shipping audio behavior.
Removing those three lines gives the exact base CMake bytes. The 1,109-byte resource/font
assembly block, all three embedded PNG bytes/hashes and `ObservatoryMaterial.cmake` are
byte-identical to the base. The reviewed CMake SHA-256 is
`25e757cfbb52262328ccb95536d5760dc00d81f5a9205133323c3b1d8b87d278`.
Only the registry build-contract hash advances; material permissions, holds, licenses and allowed
uses retain their prior decisions. This source/material audit does not certify actual payload
linkage, independent PR review, formal signing or release acceptance; those gates remain.

Public CI continues the complete build/test matrix but suppresses new Windows UI/binary/installer/
fallback-ZIP uploads while embedded-input permission is unresolved. Preview permission is checked
separately from binary use. Diagnostic CI also lacks verified actual-payload NOTICE/source delivery,
so input-rights approval alone cannot reenable binary uploads. Logs remain available.
When that diagnostic upload is unavailable, the reviewed private factory may build the exact
green source on `windows-latest` without signing credentials. Its immutable handoff retains
raw PRE/POST hashes, audio transparency/pluginval results, CMake inputs, factory/source-CI identity,
Corresponding Source and legal documents. The signing job verifies this handoff and the complete
source CI before accepting it; it never silently treats a skipped public upload as successful.
The private unsigned build is not public distribution approval. Actual extraction, linkage,
host, signature and three-channel gates remain mandatory.
This also covers generated previews and embedded resource copies, not only loose original files.
Existing artifacts are preserved. Contributor source remains available for inspection; do not
use repository availability as approval to package held material into a new distribution.

The release coordinator checks inputs before build/signing and checks actual distribution evidence
after packaging and before LS/GitHub/HP publication. Supply `--provenance-report` pointing to a
private retained JSON report. It must be schema `hypha-distribution-provenance-v1` with:

- Exact `commit`, `releaseTag`, `kimeraEmbedded: false`. Optional external font rights remain
  unverified; the existing no-font mode is accepted without inventing a grant.
- `sourceArchive`, `sourceSha256`, `sourceDownload`: retained ZIP plus exact-release GitHub delivery
  URL. The archive is uploaded and read back alongside release artifacts, not replaced by a generic
  repository link or GitHub's automatic source ZIP, which omits submodule contents.
- `components`: IDs, actual version/source, license expression, modification notice and archive
  `licenseFiles`. The gate requires MoSQITo, JUCE, its bundled FLAC/Ogg/Vorbis/IJG JPEG/libpng/zlib/Apple AudioUnitSDK/VST3 SDK notices, the pinned Rust runtime notices, and a conservative normal/build FFI Cargo closure;
  additional actual linked/embedded components must be identified in the retained linkage review.
  Every reported component needs a resolved license declaration and retained license files, including
  additional components and those with no manifest license. `Unknown`, `NOASSERTION`, pending markers
  or expressions containing them remain blockers. This does not select JUCE's licensing route or
  infer rights from a nonempty declaration; actual license applicability still requires review.
- `payloads`: exactly `macos-pkg`, `macos-zip`, `windows-exe`, each with exact artifact SHA,
  `root` for actual extracted/installed payload, extractor identity, PASS extraction report file/hash,
  `binaryFiles` path/hash inventory, `legalFiles` mapping and `sourceDeliveryFile`.

Extraction and actual linkage scope are reviewed evidence from the trusted producer and clean
install/package inspection. The gate verifies retained bytes and bindings; it does not fabricate
an extraction PASS or infer whole-binary linkage from Cargo metadata. Keep the extraction command,
artifact hash, output inventory and independent reviewer in the private evidence packet. A forged
self-attestation is not acceptable release evidence. Signing-factory isolation remains a separate
security requirement.
Each payload also needs `buildMaterialEvidence` and its hash: schema
`hypha-build-material-evidence-v1`, exact commit, independent reviewer, matching `binaryFiles`,
and retained `cmakeCaches` path/hash records proving an empty font FILEPATH at the actual producer.
Windows installer/AAX font flags must agree. This verifies the trusted build evidence rather than
accepting `kimeraEmbedded: false` as a sufficient assertion.

Every actual payload must deliver root GPL text, the project component notice, retained MoSQITo
Apache text and the inventory's component licenses. `sourceDeliveryFile` is a JSON file containing
the exact commit, source ZIP SHA256 and download URL. Stage these outside signed plugin bundles
before installer/archive signing; recheck the extracted packages. Do not modify a signed binary
or existing Release to add notices. Current packaging without these files fails this new gate;
it is not automatically deemed ready by older package metadata.

Corresponding Source comparison includes tracked rebuild files, the pinned JUCE source tree,
tracked patches and conservative FFI dependency sources from offline locked Cargo metadata.
Documentation media not needed to rebuild can be omitted; actual embedded inputs cannot be
omitted. Source bytes must match the reviewed checkout, with no unexpected extra files/private
audit. Archive CRC/size/path checks reject malformed or unsafe ZIPs. Missing dependency source,
license or build inputs remain blockers. External licensed SDK conditions and the precise linked
component inventory still require release-specific review; this mechanical gate does not resolve
them or imply that all SDK materials may be redistributed.

The accepted Stage3 audit and detailed private evidence remain outside public commits. Public PRs
contain only the bounded policy, attribution and gate implementation. No competitor records or
historical commits are removed. High-risk provenance or active credential findings stop ordinary
work and keep the implementation Freeze in place.

The unsigned private Windows producer awaits the pinned GUI-subsystem pluginval process,
requires exit code zero, and verifies the complete PRE/POST bundle file sets and SHA-256 values
before and after validation. Its handoff retains the validator receipt, raw owned/JUCE source
inventory and the actual POST MSVC linker map. The importer checks these bytes again before
any signature request. CRLF/LF differences count as different source bytes. Rust 1.94.1 runtime
notices are retained; a different compiler requires a new runtime inventory review.

Legal delivery also includes `Corresponding-Source.txt` with the free exact-release source
archive URL and hash. External AAX SDK and PACE redistribution/source conditions remain a
separate release-specific review; the built-in JUCE inventory does not settle them.


### Producer evidence and runtime identity

All formal Mac AU/VST3/AAX and Windows VST3/AAX producers must check Rust's actual
runtime version against the retained standard-library notices before building, and again
when freezing the candidate. Installing a private producer toolchain selects it within that job;
it must not change a shared machine's default toolchain.
Public CI and contributor diagnostics continue to use Rust stable. The runtime pin belongs
to formal `produceMac`/freeze, Mac AAX `--sign`, Windows AAX `-Distribution` and private
Windows producers. Raw-source qualification tests inject a declared runtime; the real producer
reads the actual compiler and rejects a different version.

The Windows raw-source record compares unfiltered working bytes to the adopted Git blobs.
Approved JUCE patches are applied to a temporary index to derive the expected patched blobs;
the working submodule/index is not reset. Compare this record again after the build.

A Windows VST3 bundle contains its DLL, moduleinfo.json and, if present, the complete
JUCE desktop.ini/Plugin.ico pair. Reject extra files, absent payloads and symlinks at producer,
import and packaging boundaries. Link maps belong in an intermediate evidence directory;
append target link options without replacing platform defaults. Check real map location and
PE continuity in the first unsigned producer run.
The build record defines the complete unsigned upload tree, including both external maps
and all legal/source files. Stage and reimport that tree before upload; upload only that directory.

Retain the unsigned and signed pluginval receipts under separate, exclusively created names.
Bind imported current-factory artifacts to the same reviewed factory commit, repository,
dispatch event, workflow path and unique nonexpired artifact ID/digest. Verify downloaded
archive bytes before extraction. Archive and executable hashes also fix every downloaded
runtime receiving signing secrets; compare its extracted tree to the verified archive before use.
These checks do not replace clean-machine lifecycle, licensed-tool qualification or cost approval.

Current import boundaries include unsigned VST3, signed AAX, signed-candidate promotion
and signed-full delivery to the public coordinator. Promotion preserves the original candidate
manifest and artifact receipt, and records candidate/promotion identity. The coordinator's private
profile pins the reviewed factory repository/commit, promotion run and retained signed-full ZIP. Verify both
runs, separated jobs, unique artifacts, API digests, actual archive/local-delivery bytes and the
original manifest before accepting it. Promotion may only add provenance and mark retained
external validation complete/public-ready; it cannot change payload or qualification metadata.
