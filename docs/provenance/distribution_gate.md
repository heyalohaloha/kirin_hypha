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
three materials are accepted for binary, preview and source use. Missing historical records alone
do not establish a problem or keep the project Freeze in place. Other explicitly unresolved
materials keep individual holds; existing Releases and unrelated OSS work are not stopped.
No product image, executable line or visual behavior is changed to evade this gate.
Individual release requires reviewed rights evidence and allowed uses, or an authorized, reviewed
replacement. A new hash never inherits permission automatically.
The inspected CMake build contract is hash-bound in the registry. Changed embedding declarations,
including appended resources, require review and a corresponding registry update.

Public CI continues the complete build/test matrix but suppresses new Windows UI/binary/installer/
fallback-ZIP uploads while embedded-input permission is unresolved. Preview permission is checked
separately from binary use. Diagnostic CI also lacks verified actual-payload NOTICE/source delivery,
so input-rights approval alone cannot reenable binary uploads. Logs remain available.
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
  `licenseFiles`. The gate requires MoSQITo, JUCE and a conservative normal/build FFI Cargo closure;
  additional actual linked/embedded components must be identified in the retained linkage review.
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
