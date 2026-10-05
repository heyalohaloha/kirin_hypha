# Public repository exposure audit — 2026-10-05

## Snapshot and execution boundary

Audit completed before source changes at remote `main` commit `a8f5a4a4a791cd4a81346e2c5462d08269619b16`. The local main tracking ref matches the GitHub API. The developer checkout contains unrelated work; an isolated checkout is used. No existing source, branch, tag, Release or asset is rewritten. PR-A public hygiene and PR-B CI hardening are separate review units. This report contains locations and categories, not credential or personal values. Recommendations are scoped to actually inspected content; regex candidates are separately described as candidates.

## Findings overview

| Severity | Evidence | Action / side effects | History / spread |
| --- | --- | --- | --- |
| High (latent) | Four eSigner secret bindings in normal CI; same-repository PR and arbitrary manual ref can run code receiving them. API reports zero repository secrets. | Remove public signing route; retain unsigned hosted Windows tests/artifacts and documented independent signing/acceptance gates. Public signed dispatch callers must use the trusted signing boundary. | H0 recipe, no value found; historical workflow/logs remain. |
| Medium (latent) | Optional public AAX self-hosted SDK job; API reports zero registered runners. | Remove persistent-runner route from public workflow; retain hosted source/SDK-absence checks and standalone build instructions. | H0 recipe; historical workflows remain. |
| Medium | Personal home paths, internal operator/session instructions, proprietary repository topology, source-image account/job metadata. | Curate AGENTS/docs, use relative public links and private evidence descriptions, strip only unnecessary image metadata preserving pixels. Detailed file inventory below. | H1; previous commits/PR diffs/source archives may retain them. No rewrite. |
| Medium | Git metadata contains personal email: 1,243 author uses and 1,118 committer uses. | No historical author changes or mailmap. Propose noreply for future commits, separately confirm account privacy UI. | H1; all reachable commits and GitHub mirrors retain metadata. |
| Medium | PVR disabled; no SECURITY.md. Required reviews count 0. | Add truthful policy; enabling private reporting requires owner approval. Review requirements/settings left for owner decision. | Settings independent of source; no secret assumed. |
| Medium | Actions use mutable tags/branch. | Pin exact existing action commits; review-driven update configuration for actual ecosystems. | H0. Pinning requires reviewed updates. |
| Low | Permissions/checkout token persistence rely on repository defaults. | Explicit contents read and no persisted git credentials; no write/OIDC grants. | H0. |
| Informational | GPLv3, product source/contracts/tests, public signatures and checksums, public provenance, synthetic fixture paths. | Preserve transparency, license notices, behavior and verifier identities. | H0. |

## Git inventory and credential method

14 local branch refs, 25 remote-tracking refs (including origin/HEAD), 40 tags; 1,323 reachable commits, 11,386 blobs (486,268,533 bytes), 2,125 changed paths and 110 deleted paths inspected. All GitHub API 24 branch tips and 40 tags are covered by the local scan; no remote history was assumed from a stale local main. Commit bodies (780,105 bytes) also scanned. Provider-token/private-key signatures and contextual generic secret assignments were inspected; 21 generic secret assignment hits were manually verified installer test fixtures. No credential identified in this scope. No secret-like committed .env/key/certificate/credential filename or tracked release_state path found. Binary source-image metadata inspected separately. Unreachable/dangling Git objects, third-party upstream repository histories, and external service state are outside this reachable-history claim.

H0: public technical data, synthetic fixtures, upstream copyright/author notices, public signer identity. H1: individual paths, internal procedures, proprietary topology, image account/job identifiers, personal commit email. H2: none identified in inspected material; this is not a certificate of universal absence. If a credential is found later, report `SECRET DETECTED — value redacted`, stop, prioritize revoke/rotation, and separately decide history/Release/log remediation.

## GitHub public surface and settings

Read-only API inventory: public repository, 24 branches, 40 tags, 73 PRs (the issues endpoint contains those same 73 PR entries, no standalone Issue), 2 issue/PR comments, 0 review comments, 40 Releases / 298 assets (2,679,121,156 bytes), 860 retained Actions runs and 909 artifacts (7,070,848,247 bytes). All 195 small release text/JSON/checksum assets (177,168 bytes) downloaded and scanned; no path/email/provider-key/private-key matches. PR/Issue/Release text and comments scanned with no such matches. Binary installers/plugin ZIP payloads have not all been downloaded/inspected; artifact contents and all 860 logs are not certified clean. Source archives regenerate from historical tags and necessarily retain historical source.

Repository has no Pages (has_pages false; endpoint 404), Discussions false, Wiki false, auto-merge false. Rulesets list empty. Main protection: strict current-base required checks `public history identity`, `release source contract (macos)`, `auval arm64 (AU validation)`, `windows VST3 preflight`; PR reviews configured with count 0, code-owner review/stale-dismissal/last-push approval false; enforce admins true, conversation resolution true, force push/deletion false. Required signatures false. This does not establish independent human review.

Actions: allowed_actions all, enforced SHA pinning false; repository token default read and cannot approve PRs; fork approval first-time contributors. Repository secret names count 0; registered runners 0; environments 0. Secret scanning, push protection, and Dependabot security updates disabled. PVR disabled at audit. No settings changed by audit. Account email privacy/block-push-personal-email UI unconfirmed; local git identity uses a personal email. No automatic identity/config changes.

Public repo Actions logs/artifacts and retained Release material are additional privacy surfaces. Inventory/selected retrieval is evidence only; expired/inaccessible/deleted historical content and private external factory/services remain **unconfirmed — no access or retrieval coverage**. No artifact deletion, CI trigger, budget or retention change.

## Per-item disposition and side effects

The following read-only inventories give original file/line locations and explain public necessity, category, treatment, consequences and historical spread. Public source/contracts/license information is retained even when attackers can read it. Individual-only information is removed forward, without creating a tracked internal.md. Private transfer is required outside the public repository; no private destination is inferred or silently written.

## CI audit (before changes)


Audited source: a8f5a4a4a791cd4a81346e2c5462d08269619b16, exact fetched main. Worktree is managed security checkout. All source findings also appeared unchanged at original working branch 6ba9fe20e681d369c1de08e1c74b1ef2406dc010. Read worktree AGENTS.md and docs/hypha_release_entry.md. No edits, commits, workflow triggers or settings writes were performed by this audit.

## Confirmed workflow inventory

Only tracked workflows are `.github/workflows/ci.yml` (508 lines) and `.github/workflows/aax-phase-a.yml` (100 lines). No tracked Dependabot config or CODEOWNERS. Both support `push` main, `pull_request` targeting main, and `workflow_dispatch`. No `pull_request_target`, `workflow_run`, `workflow_call`, release event, repository_dispatch, schedule, auto-merge, or GitHub publication step appears in these workflow files.

CI jobs are public-history (always), test/auval-arm64/windows-vst3-preflight (PR, manual dispatch, or main push message containing `[ci full]`). CI jobs use GitHub-hosted ubuntu-latest/macos-14/windows-latest. Artifact upload paths are narrowly selected binaries/logs/png/installer/JSON/checksum/ZIP; no arbitrary repo-root upload, credential directory upload or artifact download is present. CI concurrency cancels same workflow/ref in progress; preserve as existing requested budget behavior.

AAX SDK absence checks use hosted Ubuntu for every event. AAX SDK build is an optional two-platform self-hosted matrix, guarded only by manual event and boolean input, with SDK path inputs passed via environment to quoted shell args. It builds unsigned. No named GitHub environment is referenced by either file.

## Exact secrets inventory (names only)

| Name | Location | Receiving step | Event condition |
|---|---|---|---|
| ESIGNER_USERNAME | ci.yml:453 | Build Windows installer and sign all executable surfaces | None on step; Windows job runs for PR, manual, selected main push |
| ESIGNER_PASSWORD | ci.yml:454 | Same | Same |
| ESIGNER_CREDENTIAL_ID | ci.yml:455 | Same | Same |
| ESIGNER_TOTP_SECRET | ci.yml:456 | Same | Same |

No other `${{ secrets.* }}` or secrets-index expression is present in the two tracked workflow files. `ci.yml:390` additionally maps `${{ github.token }}` to GH_TOKEN for official Inno Setup download/asset verification. Both workflow files currently omit explicit `permissions:`; root independently confirmed repository workflow default permission is read and cannot approve PRs.

Root independently confirmed public repo has zero Actions repository secrets, zero registered Actions runners, and zero environments currently. The unconditional eSigner mapping therefore demonstrates a dangerous future boundary, not a proven present secret exposure. Adding eSigner secrets would make same-repository PR code receive these credentials; fork PR secrets are withheld by GitHub. `windows_signing=unsigned` does not prevent access: the untrusted build-installer script receives its process environment regardless of whether it invokes signing. Java and CodeSignTool download (415-447) are signed-dispatch-gated, but the receiving build step is not. No real credential value was detected by this workflow source audit; no rotation requirement established.

## Findings and disposition

### High — Public normal CI has latent signing credential route

Source: ci.yml:448-463 and dispatch inputs 6-21. An untrusted same-repository PR can modify `scripts/windows/build-installer.mjs` and read mapped eSigner credentials, if configured. A manual signed dispatch can run on a selected non-main branch, with no environment/ref/review boundary. Early PR build steps share the signing job workspace and can change later executable/script state. Merely adding a step `if` would not fully separate signing from earlier untrusted code or workflow modifications.

Recommended minimal remedy: remove public CI signed dispatch/input/Java/CodeSignTool/ESIGNER bindings entirely; force unsigned candidate packaging and pending external validation. Preserve public Windows hosted build, audio-transparency/pluginval/installer install/reinstall/uninstall checks and artifacts. Canonical signing is already a separate private factory in docs/ls_release/kirin_hypha_ls_runbook.md:367-416, with same-commit CI verification/private script allowlist/hash-pinned tools/signed candidate promotion. Public signing scripts themselves remain public for reproducibility and independently controlled use. README:989-995 currently describes a legacy optional public signed path and should accurately state its removal.

Affected guard consumers: xtask/src/windows_preflight.rs:255-293 requires the current step name and a repository secret string; tests.rs:288-296 specifically enforces the obsolete route. scripts/windows/windows_installer.test.mjs:358-364 checks setup-java@v4 in ci.yml. Renaming installer build step additionally requires scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs:357 and scripts/ls_release/release_metadata.test.mjs:709. Retaining stable job/artifact names prevents private CI consumer breakage. The required private factory and release acceptance gates must not be replaced with unsigned CI artifacts.

History classification: H0 for public workflow recipe; no credential value found. Old workflow revisions remain in history, but currently no repo secrets; continue to keep secrets absent. If credentials ever existed or were used in this public route, logs/runs then need separate incident investigation and rotation decision.

### High/Medium contingent on runner enrollment — Public workflow can address persistent SDK runner

Source: aax-phase-a.yml:48-100, especially 50 and 58. Existing job is manual-only and unsigned, which limits ordinary unchanged external PR access. However, a PR can change workflow source and remove/change its guard; public repo self-hosted runner enrollment must be treated as the actual trust boundary. A main/ref `if` is useful against mistaken manual dispatch but does not protect runners against malicious modified PR workflow. No workflow environment is configured. Root confirmed Actions runner inventory total_count=0. This is latent, not a demonstrated live runner exposure; removing the workflow route prevents accidental later public runner enrollment from reopening the intended SDK pathway.

Preferred remedy when canonical private SDK factory covers releases: remove optional self-hosted build leg from public workflow, retain hosted AAX SDK-absence checks and standalone documented unsigned SDK build scripts. Alternatively only retain it with an administratively enforced runner-group/selected trusted workflow ref and protected environment outside untrusted PR control. Those settings require review and must not be silently changed. The external licensed SDK/build documentation remains necessary OSS contributor information.

### Medium — Action dependencies reference mutable tag/branch

Every Action uses tag or stable branch, not full commit. Unique refs:
- actions/checkout@v6 — ci.yml:42/77/104/195; aax-phase-a.yml:35/64
- dtolnay/rust-toolchain@stable — ci.yml:49/88/109/200; aax-phase-a.yml:69
- Swatinem/rust-cache@v2 — ci.yml:90/111/202; aax-phase-a.yml:71
- actions/upload-artifact@v7 — ci.yml:143/262/350/479/501
- actions/setup-java@v4 — ci.yml:417 (remove with public signing route)

On 2026-10-05, official repository `gh api repos/OWNER/REPO/commits/REF` resolved these exact current pins:
- actions/checkout: d23441a48e516b6c34aea4fa41551a30e30af803
- actions/upload-artifact: 043fb46d1a93c77aae656e7c1c64a875d1fc6a0a
- Swatinem/rust-cache: 6323deb102c322ba6fcbdcafc7e3dddab59af2b6
- dtolnay/rust-toolchain stable: 89b12181fb390509a0842a86cc55eeb8eb928c1d
- actions/setup-java v4: cf277c60eb25467037889841efdb72551f06f6c3

Pinning existing resolved refs prevents code drift and does not intentionally upgrade action behavior. Add explicit `with: toolchain: stable` when changing dtolnay branch-based stable ref to SHA to preserve clear intent. The verified pinned action.yml defaults stable, so this is explicitness rather than a necessary compatibility override. Add readable current-version comments and review-driven Actions dependency updates. Dependabot weekly grouping/low PR limits reduces CI churn; no auto-merge.

### Low — Token permissions/persistence rely on defaults

Source: both workflows omit `permissions`. Repository default is presently read, so no present write grant is proven. Set explicit `contents: read` at workflow level; other GITHUB_TOKEN permissions become none. `gh release verify-asset` official source uses GET release-reference/attestation reads and does not need signing/write token permissions. Preserve this provenance check; contents-only token still requires hosted CI confirmation after merge/publish. Upload-artifact uses artifact service runtime credentials rather than needing blanket repository write. Add `persist-credentials: false` to checkout: build/tests use only local git reads and public submodules, and do not require post-checkout authenticated git writes. Do not introduce write permissions or id-token.

### Low — Dependency update configuration absent

Cargo workspace/Cargo.lock exist. No tracked npm/pnpm/yarn or Python package manifest exists in owned repository; Node scripts use built-ins and are not proof of an npm ecosystem. `.gitmodules` pins JUCE submodule; Cargo.lock pins crates including Git-sourced nih-plug dependencies. Configure review-driven Dependabot for github-actions and Cargo only (git-submodule optional, requiring JUCE patch-stack review). No invented npm or Python blocks. Existing vendored Rust patches must remain aligned and checked before dependency changes.

### Informational — Other supply chain boundaries

CodeSignTool archive in ci.yml:427-434 is release-version+SHA256 pinned, though recommend removing this public signed route. Inno Setup ci.yml:399-400 checks pinned official release asset attestation. Windows pluginval download ci.yml:369-374 is fixed v1.0.4 but lacks expected hash; macOS scripts/validate_macos_pluginval.sh:85-94 permits latest URL or version and lacks archive hash. Their binaries run in hosted test jobs with no signing credential requirement after remediation. Record as follow-up provenance hardening; do not weaken pluginval gate or invent a checksum. Swatinem cache uses PR scopes; public workflow has no privileged downstream artifact importer, but private signing factory cache/import controls were not read here and must remain unconfirmed.

## Dangerous pattern review

No pull_request_target, PR head checkout+privileged event, issue/PR body shell interpolation, eval, workflow_run privileged download, or auto-merge in audited workflow. Source-budget base SHA is assigned via env then quoted when used (ci.yml:53-58). Head-commit message appears only in job expression, not shell. Workflow SDK string inputs are env variables and quoted shell arguments. github.workspace is trusted runner path directly expanded at ci.yml:233; no PR text injection found. Repository arbitrary build/script execution is necessary for PR validation and therefore must be hosted/unprivileged. This audit cannot prove benign contributor code: isolation is the required boundary.

## Settings known via root audit / still outside local fix

Root independently confirmed required strict checks: public history identity/release source contract (macos)/auval arm64/windows VST3 preflight; enforce admins enabled; force push/delete disabled; review count 0; no rulesets. Repository-wide allowed_actions=all and SHA pinning requirement false; first-time contributor fork approval only; PVR disabled. No settings modified. Workflow pinning/unsigned separation cannot stop a same-repository workflow edit from requesting write permission if repository policy permits it, nor prevent an authorized maintainer from bypassing a social review boundary. Review requirements/CODEOWNERS are useful settings-dependent controls; adding CODEOWNERS alone is advisory because require_code_owner_reviews must be enabled. For sole-maintainer repo, requiring self-approval can block every PR; do not silently impose.

## Meaningful local verification for PR-B

- YAML parsing (GitHub uses YAML semantics; ensure `on` key not mistaken for bool by YAML 1.1 parser) and actionlint, if available.
- `cargo test -p xtask --locked windows_preflight` and `cargo run -p xtask --locked -- windows-preflight` after updating unsigned invariant guard.
- `node --test scripts/windows/windows_installer.test.mjs scripts/windows/inno_signing.test.mjs scripts/ls_release/release_metadata.test.mjs`.
- AAX hosted contract exact commands from sdk-absence: `node scripts/test_aax_cmake_gate.mjs`, `node scripts/test_build_aax_universal.mjs`, SDK absence and AAX distribution/submission archive Node tests, SDK absence scanner.
- Existing `scripts/test_lightweight_contract.sh` plus complete release source contract if required by project for changed source. Preserve existing product acceptance checks; no DSP/audio/UI source changes.
- New narrow policy regression should reject signing secret refs/privileged events/public self-hosted runners/write token permissions, verify SHA refs/toolchain input and checksum candidate gates, with negative cases proving failure. This is security behavior, not test mirroring for a cosmetic change.
- No CI dispatch/push performed in audit; required GitHub PR checks still apply when user authorizes publication.

## Official sources checked

- GitHub Secure use reference: https://docs.github.com/en/actions/reference/security/secure-use (immutable SHA pin, untrusted workflow/artifact caution, public self-hosted runner risk).
- Workflow syntax permissions: https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#permissions (explicit least permissions, unspecified become none, fork write caveat).
- Deployment environments: https://docs.github.com/en/actions/reference/workflows-and-actions/deployments-and-environments (environment review and branch policy; self-review tradeoff).
- Dependabot options reference: https://docs.github.com/en/code-security/reference/supply-chain-security/dependabot-options-reference (github-actions directory `/`, actual Cargo support, grouping/open PR control).
- Actual source repository commits: https://github.com/actions/checkout/commit/d23441a48e516b6c34aea4fa41551a30e30af803 ; https://github.com/actions/upload-artifact/commit/043fb46d1a93c77aae656e7c1c64a875d1fc6a0a ; https://github.com/Swatinem/rust-cache/commit/6323deb102c322ba6fcbdcafc7e3dddab59af2b6 ; https://github.com/dtolnay/rust-toolchain/commit/89b12181fb390509a0842a86cc55eeb8eb928c1d .

## Public hygiene audit (before changes)


Candidate inspected: remote main `a8f5a4a4a791cd4a81346e2c5462d08269619b16`, isolated work checkout. The work-checkout AGENTS/release entry were verified byte-identical to the versions read before investigation. No edits, commits, builds, GitHub mutations, history rewrites, or installations were performed by this audit.

## Scope and limits

Tracked-file inventory (1,860 paths including JUCE submodule gitlink; 149 paths under docs), all locally readable tracked bytes scanned for personal home paths, email patterns, personal attribution, internal Notion/SECTION/guardian references, URL/admin patterns, private-repo names, and private-key markers. Actual matched text, README/AGENTS/CLAUDE/.gitignore/Cargo/config, release/signing guides/templates, workflow discovery and packaging/source gates were read to distinguish real data from fixture values and technical contracts. Not every historical technical document was reread cover-to-cover; recommendations below are specific to the matched passages and sections inspected. Git history/branches/tags/GitHub settings/secrets/logs are assigned to other audit workers. A regex hit is not itself a secret.

No maintainer personal email or genuine credential was found in inspected current tracked material. This is a finding scoped to the inspection, not a proof of absence. PNG metadata inspection found a personal generation-platform account ID plus job identifiers (below). No real local network address was established: all IPv4-like hits are section numbers or upstream numeric constants. No current admin/private URL credential was found. Public GitHub project URL and official product pages are intended public destinations.

## Decisions and recommended minimum changes

1. AGENTS.md lines 22–34: private Notion/SECTION instructions and individual completion format; remove from public contributor guide, preserve only privately outside this public repository. Lines 143–166, 188–205 contain personalized AI conversation/process instructions mixed with technical quality requirements. Replace individual-role/workroom/guardian instructions with neutral contributor guidance, retaining accurate API facts, RT constraints, transparent A path, explicit comparison limits, filesystem protocol/GPL separation, tests, source line budget, and release safety gates. Do not delete the live comparison, approved attenuation, AUTO ceiling/ramp, mono/stereo/exact-5.1, failure fallback, immutable Reference/Record, or platform transport contracts. Remove individual approval attributions while retaining dates and linked technical decision evidence. Lines 317–321 personal repo folder pair is not necessary; express repository/code/license separation generically. Do not move this content to tracked internal.md.
2. CLAUDE.md: `@AGENTS.md` is a purposeful public integration convention. Retain import and publicly discoverable build/release entry. Remove instructions claiming other sessions/private PC shared globals if unnecessary, not build commands.
3. README.md lines 5–9 lead with AI tool names/internal HP workflow; public readers need build-only contributor entry first, while maintainers still need release guide. Lines 972 onward describe exact private factory operational behavior and public signed CI fallback; the technical trust boundary and artifact verification belong publicly, but private machine workflows/account structure do not. Update accurately alongside PR-B if signed mode leaves public CI. Keep public URL/downloads, channel gates, signer verification, role/format support, reproducibility, source commands, signatures/checksums, and license.
4. .gitignore lines 39–45,59–65: comments expose individual/Notion/B-number process, not secrets. Remove comments/private artifact basename list in favor of precise local state patterns as appropriate; keep tracked `.cargo/config.toml`, public `.env.example`, templates and generated-fixture source. Existing /release_state, /dist, cert/key extensions, .env/.env.* protection is useful. Add `.env*` support while preserving example/template variants; local credentials/private config/signing state/crash dumps should be narrow ignores. Do not blanket-ignore public audits, because the requested security audit belongs tracked.
5. No root SECURITY.md / CONTRIBUTING.md / NOTICE / CODEOWNERS exists at inspected baseline. LICENSE is the GPLv3 text and must remain. Add contributor/security guides where necessary; PVR is currently disabled per root API check, so do not promise `/security/advisories/new` is available. No new personal reporting email. An available reporting method must be established or the disabled private channel explicitly marked a blocker.
6. Same-repository absolute paths should become working relative links, with stale line fragments removed or source existence verified. Other-repository/private Downloads/Music/Windows session/build/backup evidence locations should be described as privately retained evidence, removing user/workstation/worktree topology. Preserve technical outcomes and honest test limits; do not invent public evidence or claim missing private files can be reproduced.
7. Handoff/planning docs are mixed: product contracts, error models, decisions and exact test evidence are necessary. Retain technical portions. Remove private individual permissions, Notion omissions, internal agent-room statuses, exact workstation data and private cross-repo pointers. Do not mechanically delete all handoff/planning documents.
8. `docs/planning/kirin_hypha_reference_growth_integrated_execution_plan_20260925.md`: inspected front matter lines 18–35 individual owner/approval and sections 1.2/1.3 disclose proprietary Kirin OS candidate commits/private-repo file/blob topology. Sections 9–18 include marketing/press outreach, sales/trial campaigns, internal product-media operations and individual campaign approval. These are not needed to build or verify Hypha; retain public feature/compatibility/validation facts and move internal marketing/OS workflow strategy privately or remove those portions. Published prices/URLs already public are not secrets, and changing pricing is forbidden. Section 17.5 includes unnecessary real-name self-introduction template; remove individual name. Public third-party research/source links may stay where relevant.
9. `docs/hypha_implementation_approval_20260906.md` section “現在維持する操作境界” lines 59–74 and “実音源の追加指定” lines 79 onward are individual permission/Windows operator/AI workflow and private media library location. Remove those internal specifics while retaining the principle that licensed audio may be used locally only and is not redistributable. Sections 1–58 hold meaningful technical approval/design contract and should remain without individual quote attribution.
10. `docs/hypha_listening_review_01_20260907.md`: local HTML Download link/ephemeral /tmp evidence, Codex submission process and current private studio output label/rate are internal. Keep general offline annotation workflow, exact generated hash/statistics, fake-sink scope and unverified real-speaker limitation. `docs/hypha_remaining_work_handoff_20260910.md` contains dated branch/PR/run evidence plus private source/Downloads/Windows operation references: retain public commit/run/test outcomes and future technical gates; remove session occupancy, individual authorization, private evidence paths and Notion.
11. PNG `assets_source/IMG_3113.png` contains account ID in `tEXt Author`, generation prompt/style-reference URL/job UUID in `Description`, job UUID in XMP `DigImageGUID`. No secret credential. Personal account/source/job identifiers are unnecessary for OSS usage. If sanitizing, preserve all image data/pixels and AI-generated-source attribution; confirm no visual/product change. This is source artwork and was visually inspected (rendered fern/fungal illustration, no portrait/private workstation). Other owned PNG `eXIf`/`iTXt` chunks inspected contain only color-space/dimensions. Vendor metadata/author/copyright must remain.
12. Corporate signer/team IDs and Developer ID common name in signing scripts identify already-public signatures, not private keys/credentials. Keep verifier trust expectations and do not change signer behavior. `NOTARY_PROFILE` names are local configuration labels, not secret credentials. Public variable names/placeholder templates should remain.
13. Owned source person/guardian comments are internal decision labels but not privacy emergencies; simplify comments only if needed, avoid touching runtime logic or growing large source files. Synthetic test paths are explicitly H0: `crates/kirin_measure/src/storage_tests.rs` lines 32,33,59–61 and `xtask/src/windows_vst3_layout.rs` lines 80,97,140 (confirm current lines in inventory) are fabricated fixture values. `/Users/` code checks in `xtask/src/release_gate.rs`, installer preinstall user-home enumeration and app-data/Library paths are functionality/guardrails, not personal data; preserve.
14. Email pattern hits in `vendor/*` are upstream author/copyright notices. Preserve under license requirements. Owned test hits are synthetic artifact filenames (`...@...`), not email addresses; preserve unless a separate public API test need calls for changing fixture text. `test_signals/source.md` author/provenance attribution is legitimate attribution, not an operator secret; retain ownership/provenance, remove private project folder nickname only if no loss of origin record.

## Regeneration, artifacts, tests

- `scripts/hypha_workflow_discovery.test.mjs:19` asserts guide occurs before `## Notion操作`; deleting private heading will break this source-doc contract. Replace with a public heading or robust bounded startup check. Line 40 expects guide token `強制再読` (AI session behavior); remove that documentation-only token contract if private session paragraph is removed. Keep `@AGENTS.md`, guide/build presence, `--without-aax`, help modes from another cwd, resume/old-checkout/release_state/public approval/RELEASE_COMPLETE boundaries.
- `scripts/ls_release/release_metadata.test.mjs` validates ignored /release_state is absent from public history, ship manifest/paths, exact public media bytes, provenance mappings. Do not modify checksum fixtures or published mappings just to clean personal prose.
- `scripts/test_release_source.sh` is a real broad Rust/native/JS/packaging/source/invariant gate, with additional ignored/parity and budget checks later in file. No need to release/sign/install to validate documentation-only cleanup. Root chooses required tests appropriate to changes.
- `scripts/ls_release/build_kirin_hypha_pkg.mjs` stages only declared plugin bundles plus generated installer scripts; no arbitrary docs directory/archive copy found in inspected path. Installer user's home-path iteration is intended portability and must stay.
- `xtask/src/release_package_metadata.rs` assembles manifest from commit/toolchain/hash facts and public signer identity; license source should remain accessible. Existing .gitignore protects generated target/dist/release_state trees.
- `scripts/release_hypha.mjs` and hp guide files use current root-relative/private-state profile paths; these are intentional configuration interfaces and do not disclose actual private values.
- No public docs generator was identified. Image generation source artwork and public README media are tracked; PNG account metadata would remain until removed explicitly. No release artifact/log/archive payload inspection performed by this worker; no inference of absence there.

## History classification and spread

All changes are forward-only. H0: source/contracts/public verification commands, synthetic fixture paths/URLs, numeric section matches, public signer identity, copyright/author notices, neutral tool integration. H1: real personal home/user paths, generation-platform account/job identifiers, private workstation/session/Downloads/media-source references, individual internal AI/Notion/approval workflow, private proprietary repo/file/commit topology. Ordinary cleanup leaves these in previous commits, existing PR diffs, release notes/source archives and Actions artifacts/logs if they included corresponding content; exact past GitHub spread must be inspected separately. No H2 genuine credential in this worker's inspected current source. Rotation is not indicated by these H0/H1 findings, but no secret-absence certification is claimed.

## Per-file exposure inventory

The table records matched line numbers at the candidate before edits. `person` often means legitimate technical decision/provenance attribution; do not classify all names as secrets. `private_repo_name` includes false positives such as C++ factory classes; inspect actual context. `external_local_path` includes product app-data layout and safe generic home paths. `local_network` hits were verified as section numbers/upstream numerical constants, no live address.

| File | Matches/category | Classification / recommended handling |
| --- | --- | --- |
| `.cargo/config.toml` | internal_ops: 1,11,21 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `.gitignore` | internal_ops: 44,59,60; person: 36 | H1 individual/internal-op comments; keep functional ignores and template exceptions |
| `AGENTS.md` | external_local_path: 278,313; internal_ops: 22,23,24,27,34,203,204,305; person: 149,161,194; private_repo_name: 244,313 | H1 internal individual/Notion workflow mixed with H0 product/contribution contract; curate public guide |
| `README.md` | private_repo_name: 968,969 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `crates/hypha_post/src/editor.rs` | person: 85,2963 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/hypha_post/tests/gui_wiring_test.rs` | person: 698,703 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h` | external_local_path: 315 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_hypha_ffi/src/lib.rs` | external_local_path: 3564 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/Cargo.toml` | internal_ops: 18; person: 36 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/examples/watch_cpu_components.rs` | private_repo_name: 18,22 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/exclusion.rs` | external_local_path: 68; person: 5,37 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/identity_reset_tests.rs` | internal_ops: 48,72,93 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/io_thread_post_json.rs` | internal_ops: 99 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/io_thread_pre.rs` | person: 830,1206 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/io_thread_pre_json.rs` | internal_ops: 100 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/lib.rs` | internal_ops: 508,517 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/measure_thread.rs` | person: 1920 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/plugin_data.rs` | person: 453 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/record_expected_tests.rs` | external_local_path: 92,112,117 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/record_writer.rs` | person: 3321 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/reservation.rs` | external_local_path: 7 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/storage.rs` | external_local_path: 7,8,11,72 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/storage_cleanup.rs` | external_local_path: 6 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `crates/kirin_measure/src/storage_tests.rs` | personal_path: 32,33,59,60,61 | H0 synthetic portability fixtures; preserve test behavior |
| `docs/aax_build_signing_entry.md` | private_repo_name: 25,40,41,45,84,91,95,108 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `docs/aax_windows_build_20260910.md` | private_repo_name: 9,32,76,99 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `docs/attack_perceptual_visual_contract_20260831.md` | person: 21 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_attack_visual_completion_proposal_20260909.md` | person: 571 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_build_entry.md` | private_repo_name: 103,106 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `docs/hypha_c3_development_evaluation_20260909.md` | person: 24,122 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_capture_and_workflow_integrated_plan_20260914.md` | internal_ops: 214 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_ce2226_jungle_visual_system_20260901.md` | person: 63,102,107,110,116,120,180,190,206 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_comparison_integrated_plan_v5_20260914.md` | internal_ops: 99; person: 494,495,496; personal_path: 494,495,496; private_repo_name: 496 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_comparison_v5_execution_contract_20260914.md` | person: 166,167,168,169,170,171; personal_path: 166,167,168,169,170,171 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_completion_plan_20260907.md` | person: 16,51,55,104,106,134,225,248,268; personal_path: 55,225,233; private_repo_name: 225 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_drum_band_view_plan_20260928.md` | person: 6,7,14,165,173,185,191,196,354,355 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_drum_fan_b_implementation_20260906.md` | person: 3,9; personal_path: 143 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_drum_lanes_20260924.md` | internal_ops: 20; person: 9,20,110 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_drum_membrane_implementation_20260907.md` | person: 158; personal_path: 154,158 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_drum_render_diagnosis_20260906.md` | person: 9,122 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_implementation_approval_20260906.md` | internal_ops: 62; person: 4,74 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_implementation_progress_20260906.md` | internal_ops: 144; private_repo_name: 68 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_integrated_implementation_plan_20260907.md` | internal_ops: 379; person: 110,111,112,113,297,308,362,367; personal_path: 110,111,112,113,297,308,362,367; private_repo_name: 362 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_invariants.md` | person: 236,239,241,243,244,245,246,247,248,251 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_jungle_activation_and_visual_plan_20260911.md` | external_local_path: 175; internal_ops: 384; person: 4,350 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_lightweight_runtime_progress_20260906.md` | person: 112; personal_path: 68 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_listening_review_01_20260907.md` | person: 9; personal_path: 9 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_local_blind_runtime_progress_20260906.md` | internal_ops: 95; person: 232; personal_path: 232 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_meter_current_implementation_audit_20260831.md` | person: 23,24,25; personal_path: 23,24,25 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_meter_product_contract_20260831.md` | person: 43,51; personal_path: 51 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_meter_visual_concepts_20260831.md` | person: 13 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_plans_review_and_update_path_20260906.md` | internal_ops: 263; person: 49,50,51,52,53,54,55,56,57; personal_path: 49,50,51,52,53,54,55,56,57 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_post_os_guide_integration_plan_20260831.md` | person: 9 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_pre_post_blind_feasibility_20260906.md` | person: 412,413,414,415,416,417,418,419,420; personal_path: 412,413,414,415,416,417,418,419,420; private_repo_name: 412 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_pre_post_blind_usability_plan_20260914.md` | internal_ops: 415 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_reference_and_audio_research_20260906.md` | internal_ops: 158 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_reference_session_repair_plan_20260907.md` | person: 36; personal_path: 36 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_release_entry.md` | external_local_path: 35; private_repo_name: 24,70,90,92,130 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_remaining_work_handoff_20260910.md` | internal_ops: 30; person: 28,75,208,214,259,282,320,321,322; personal_path: 28,320,321,322; private_repo_name: 28,276 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_responsive_typography_plan_20260910.md` | person: 212 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_space_attack_plan_20260906.md` | person: 86,188,398,407,482,483,484,485,486,487,488,489; personal_path: 482,483,484,485,486,487,488,489 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_space_decay_design_review_20260906.md` | person: 3,179,180,181,182,183,184,185; personal_path: 3,179,180,181,182,183,184,185 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_space_mono_sum_plan_20260918.md` | person: 183,214 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_spectrum_mid_side_overlay_plan_20260911.md` | internal_ops: 451,452 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_structural_repair_plan_20260907.md` | external_local_path: 186; person: 20,153,161,186,202; personal_path: 20,153,161,186,202 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_structural_repair_plan_20260912.md` | person: 9,15,290,564 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_aggregation_candidates_20260918.md` | person: 3,278,438 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_channel_map_findings_20260918.md` | person: 3,12,144,229 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_decisions_20260918.md` | local_network: 249; person: 3,133,203,257,262 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_implementation_plan_20260918.md` | person: 3,18,248,274; private_repo_name: 153 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_ingest_capacity_20260918.md` | person: 3,199,264,292,293,699 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_metric_contracts_20260918.md` | local_network: 403,453; person: 3,341,413,423,451 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_p0_inventory_20260918.md` | person: 18,130 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_surround_review_request_20260919.md` | person: 346 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_trace_record_inbox_recovery_handoff_20260806.md` | internal_ops: 390; person: 371,372 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_true_peak_self_check_20260707.md` | person: 35 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_windows_cpu_psb_diagnosis_20260906.md` | personal_path: 16 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/hypha_windows_exchange_safety_20260906.md` | person: 46 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/hypha_windows_observability_fix_20260906.md` | internal_ops: 207; person: 5; personal_path: 194,196 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/ls_release/kirin_hypha_ls_runbook.md` | internal_ops: 34; private_repo_name: 58,60,62,74,369,373,398 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_live_chain_compare_external_research_20260927.md` | internal_ops: 201 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_live_chain_compare_implementation_plan_20260927.md` | internal_ops: 1191,1323; private_repo_name: 100,168,219,930,968 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_live_chain_compare_implementation_plan_20260927_v7.md` | internal_ops: 944; private_repo_name: 47,114,165,715,751 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_live_chain_compare_review_20260927.md` | internal_ops: 200,267,393,407,501,609,610,623; private_repo_name: 567,602 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_live_chain_compare_review_v6_20260927.md` | internal_ops: 166,167,181 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_live_compare_lifecycle_repair_20260930.md` | internal_ops: 96 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_one_pass_live_blind_implementation_plan_20260929.md` | internal_ops: 483 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_one_pass_live_blind_validation_20260929.md` | internal_ops: 46 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/hypha_post_reference_entry_plan_20260928.md` | internal_ops: 141 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/planning/kirin_hypha_reference_growth_integrated_execution_plan_20260925.md` | person: 18,19,35,343,408,558,586,599,600,613,715; private_repo_name: 131,132,133,134,748,749,750,751 | H1 proprietary candidate/workflow/marketing strategy; retain Hypha technical/public facts only |
| `docs/public_history_identity.md` | person: 14 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/reference_b890_structural_repair_plan_20260914.md` | internal_ops: 276; person: 16,17; personal_path: 16,17; private_repo_name: 17 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/reference_c_tonal_balance_plan_20260914.md` | internal_ops: 734; person: 47,48,49,50,890,892,893,894,895,896,897,898,899,900,901,902,903,904,905,906,907,908,909,910,911,912; personal_path: 47,48,49,50,890,892,893,894,895,896,897,898,899,900,901,902,903,904,905,906,907,908,909,910,911,912; private_repo_name: 49,50,892,893,894,904,905,906,908,909 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/reference_capture_workflow_implementation_20260914.md` | personal_path: 85 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/reference_lazy_presets_handoff_20260910.md` | person: 5; personal_path: 5 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/reference_listening_workflow_plan_20260914.md` | internal_ops: 541; person: 551,552,553,554,555,556,557,558,559,560,561,562; personal_path: 551,552,553,554,555,556,557,558,559,560,561,562; private_repo_name: 42,554,555,556,557,558,559 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/reference_os_preparation_handoff_20260910.md` | person: 3; personal_path: 3 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/reference_publication_refresh_handoff_20260910.md` | person: 20; personal_path: 20 | H1 personal/internal passages; sanitize only those, retain technical evidence/contracts |
| `docs/reference_runtime_v2_handoff_20260905.md` | person: 42,98,246,250,252,254,256,258,260,262,270,276,280,282,286,288,290,292,304,314,316,320,463,469,509,626,628,670,678,696,726,776,798,836,838,848,850,852; private_repo_name: 11,320 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/reference_visual_comparison_plan_20260914.md` | person: 9,208 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/transient_delta_phase2_drum_pilot_report_20260830.md` | person: 192,210 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/watch_hardware_polish_notes_20260903.md` | person: 13 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/watch_hardware_polish_proposal_20260903.md` | person: 185,508,542 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `docs/windows_external_validation.md` | person: 56,230; private_repo_name: 31,44 | H0/H1 decision attribution; remove unnecessary individual name/permission prose, retain technical decision |
| `juce_shell/src/HyphaDepthMaterial.h` | person: 8,11 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/src/HyphaReferenceDisplayText.h` | private_repo_name: 7 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/src/HyphaSpectrumTerrain.h` | person: 19 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/src/PluginMainPOST.cpp` | private_repo_name: 3 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/src/PluginMainPRE.cpp` | private_repo_name: 3 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/src/local_blind/LocalBlindTrial.h` | private_repo_name: 46,60 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/src/reference_audition/ReferenceRuntimeV2PresetParsing.cpp` | private_repo_name: 114 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/tests/FreqHistoryReview.h` | personal_email: 116 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/tests/reference_runtime_os_fixture.h` | private_repo_name: 17,26 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/tests/reference_runtime_v2_manifest_test_support.h` | private_repo_name: 25 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `juce_shell/tests/reference_runtime_v2_workspace_test.cpp` | private_repo_name: 29 | H0 product/runtime/test contract; individual/guardian comment annotations optional neutralization, no runtime edit |
| `scripts/build_hypha.test.mjs` | personal_email: 280,292,300 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `scripts/check_typography_source.mjs` | private_repo_name: 17,98,114,188,208,210 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `scripts/check_typography_source.test.mjs` | private_repo_name: 61,92 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `scripts/hypha_workflow_discovery.test.mjs` | internal_ops: 19 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `scripts/ls_release/aax_distribution.test.mjs` | personal_email: 348 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `scripts/ls_release/aax_notarization_flow.test.mjs` | personal_email: 29 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `scripts/ls_release/build_kirin_hypha_pkg.mjs` | personal_path: 181 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `scripts/ls_release/build_kirin_hypha_release_set.mjs` | private_repo_name: 222,313 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `scripts/ls_release/hypha_release_local.mjs` | private_repo_name: 1,81,83,84 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `scripts/ls_release/release_metadata.test.mjs` | person: 721; private_repo_name: 392,408 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `scripts/release_hypha.mjs` | private_repo_name: 30,189 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `scripts/test_source_line_budget.sh` | personal_email: 96 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `scripts/windows/windows-aax-bundles.mjs` | person: 13 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `test_signals/source.md` | person: 6; private_repo_name: 6 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `tools/capture_hypha_log.sh` | external_local_path: 17 | Review category context; public attribution/product path H0, individual internal workflow H1 |
| `vendor/baseview/Cargo.toml` | personal_email: 5,6,7,8,9,11,12 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/Cargo.toml` | personal_email: 17 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/LICENSE` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/examples/generate_histogram_bins.rs` | personal_email: 2,47 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/ebur128.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/filter.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/histogram_bins.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/history.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/interp.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/lib.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/true_peak.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/ebur128/src/utils.rs` | personal_email: 2 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/egui-baseview/Cargo.toml` | personal_email: 4 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/nih-plug-presentation/Cargo.toml` | personal_email: 6 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/nih-plug-presentation/src/wrapper/clap.rs` | private_repo_name: 13,30,40,69,74,84,123,124,140 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/nih-plug-presentation/src/wrapper/util.rs` | local_network: 14,221 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/nih-plug-presentation/src/wrapper/vst3.rs` | private_repo_name: 5,14,19,74,80,184,187,189 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `vendor/nih-plug-presentation/src/wrapper/vst3/factory.rs` | private_repo_name: 1,31,51 | H0 upstream numeric constants / author metadata; retain license attribution and code |
| `xtask/src/install.rs` | external_local_path: 28,29 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `xtask/src/notarize.rs` | person: 22,108,306 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `xtask/src/release_gate.rs` | personal_path: 148 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `xtask/src/release_package_metadata.rs` | external_local_path: 21,23 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `xtask/src/windows_vst3_layout.rs` | person: 184,190,194; personal_path: 184,190,194 | H0 synthetic portability fixtures; preserve test behavior |
| `設計制約.md` | person: 51,99 | Review category context; public attribution/product path H0, individual internal workflow H1 |

PNG account/job metadata: `assets_source/IMG_3113.png` tEXt Author/Description and XML:com.adobe.xmp; binary chunk locations, H1; preserve pixels and generic AI source tag.

## Authorized settings follow-up

The owner explicitly approved enabling GitHub Private Vulnerability Reporting during this session.
The documented REST PUT completed and a fresh GET returned `enabled: true`. SECURITY.md now directs
reports to the private GitHub form without publishing a personal email. No other repository/account
settings, collaborator permissions, rulesets, Actions policy, budgets or history were changed.

## Additional public-surface evidence and owner controls

Selected Actions log archives were retrieved and manually checked: runs `36860903973` (manual CI,
failed), `36860907827` (manual AAX, successful), `37077361064` (main push, successful), and
`37228898432` (PR, successful); 20 log files / 2,930,107 uncompressed bytes. All 598 home-path regex hits
were GitHub-hosted runner/runneradmin paths (H0), not maintainer personal paths. No provider-token,
private-key, JWT, credential-in-URL or generic secret-assignment candidate was found in these logs.
Other retained logs and Actions artifact payloads remain unverified. No run was triggered/retried/deleted.

Collaborator inventory contains one account with admin role; merge commits enabled, squash/rebase and
auto-merge disabled. CODEOWNERS is not added: the only eligible current owner cannot provide independent
self-review, and code-owner approval is not enforced. If additional maintainers are granted branch write,
review ownership for workflows/signing/packaging and stale-review/last-push approval should be configured
as an owner-approved settings change. A PR can propose workflow/guard changes; source tests and explicit
permissions are not an immutable server-side policy against an authorized write collaborator changing
workflow permissions. The repository's read-token default is a default, not a permission ceiling.
Keep external contributors on fork PRs and public signing credentials/runners absent. Do not claim that
local source hardening alone enforces adversarial same-repository workflow edits or independent review.

The owner approved GitHub noreply for this session's new commits. The authenticated public account ID
and login were obtained from GitHub; the documented ID+login noreply format is used only through
per-command Git identity overrides. Existing Git identity configuration, historical authors and mailmap
are unchanged. Account Email privacy/block-personal-email-push settings remain unconfirmed.

## Final PR-A disposition (local candidate, not yet merged)

The source repository remains public and GPLv3. 82 assigned Markdown documents were curated,
43 assigned documents remained byte-identical. Product contracts, acceptance thresholds, schemas,
public source/hash/test evidence and license notices remain. 63 replacement relative links resolve,
no new fence imbalance, no remaining actual owner home/Notion/session-route marker was identified
in the assigned documents. Individual attribution cleanup does not change explicit end-user permission
semantics. No tracked internal.md was created. Durable private-document migration is pending a private
destination; private working originals are preserved outside the tracked public tree.

PNG source metadata lost 441 bytes (5,805,284 → 5,804,843); 2048×2048 RGB, all IDAT/nonmetadata
chunks and 12,582,912 decoded pixel bytes are identical. Pixel SHA256:
`4148f429e47043e2d1da30c8f6e4b3ccab9c6561a1e510ef72821f463efca3cd`.
AI-generated-source attribution is retained. Independent chunk/CRC verification confirmed the same
picture data. A further Pillow metadata read covered 59 owned PNG/JPEG files: other matched metadata
was color-space/dimensions/encoding, with no personal-path/email/account/private-key candidate.

Root-file treatment: AGENTS retains the full audio/RT/Reference/live/gain/layout/Record safety clauses,
source-line ratchet/tests and three-channel release gates, while removing personal workroom/reporting
instructions. CLAUDE retains the public AGENTS import. README points to public contributor/build guides;
the release runbook retains exact source/payload/signature/lifecycle/host/three-channel gates while
omitting private factory workflow names, machine labels, operator administration and personal session
instructions. CONTRIBUTING/SECURITY are added. .gitignore protects local state/credentials/crash/export
without ignoring the requested public audit or value-free example templates (17 direct ignore checks pass).
Workflow-discovery assertions change only the removed private heading/session phrase requirements.
LICENSE/vendor copyright/NOTICE absence and public attribution are unchanged.


Original line numbers are from pre-edit candidate. H0 retained means the file had no assigned hygiene edit and its technical/provenance function remains. H1 cleaned means private/individual/context metadata was removed or neutralized; it does not imply the whole technical document was confidential. No raw personal path, account ID, private URL or credential value appears in this ledger.

| File | Original changed lines / exposure categories | Final disposition and reason | Side effect |
| --- | --- | --- | --- |
| `docs/aax_build_signing_entry.md` | private_repo_name: 25,40,41,45,84,91,95,108 | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/aax_macos_universal_build_20260910.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/aax_phase_a_readiness_20260907.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/aax_windows_build_20260910.md` | private_repo_name: 9,32,76,99 | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/adr/0001-separation-first-architecture.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/adr/0002-windows-vst3-shell.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/attack_perceptual_visual_contract_20260831.md` | changed 21; person: 21 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_attack_visual_completion_proposal_20260909.md` | changed 571; person: 571 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_b1_host_observation_20260907.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_b453_review_20260729.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_b978_structural_repair_baseline_20260919.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_blind_completion_20260913.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_bs1770_5_r128_v5_audit_20260831.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_build_entry.md` | private_repo_name: 103,106 | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_c3_development_evaluation_20260909.md` | changed 24,122; person: 24,122 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_capture_and_workflow_integrated_plan_20260914.md` | changed 214; internal_ops: 214 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_ce2226_jungle_visual_system_20260901.md` | changed 63,102,107,110,116,120,180,190,206; person: 63,102,107,110,116,120,180,190,206 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_comparison_integrated_plan_v5_20260914.md` | changed 93,99,494–496; internal_ops: 99; person: 494,495,496; personal_path: 494,495,496; private_repo_name: 496 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations; remove unavailable/private Markdown evidence target; preserve named evidence and limitation; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_comparison_safety_contract_20260914.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_comparison_v5_execution_contract_20260914.md` | changed 166–171; person: 166,167,168,169,170,171; personal_path: 166,167,168,169,170,171 | H1 cleaned; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_completion_plan_20260907.md` | changed 16,51,55,104,106,134,225,233,248,268; person: 16,51,55,104,106,134,225,248,268; personal_path: 55,225,233; private_repo_name: 225 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations; remove unavailable/private Markdown evidence target; preserve named evidence and limitation | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_drum_band_view_plan_20260928.md` | changed 6–7,14,165,173,185,191,196,354–355; person: 6,7,14,165,173,185,191,196,354,355 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_drum_fan_b_implementation_20260906.md` | changed 3,9,143; person: 3,9; personal_path: 143 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_drum_lanes_20260924.md` | changed 9,20,110; internal_ops: 20; person: 9,20,110 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_drum_membrane_implementation_20260907.md` | changed 154,158; person: 158; personal_path: 154,158 | H1 cleaned; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_drum_render_diagnosis_20260906.md` | changed 9–10,122; person: 9,122 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_implementation_approval_20260906.md` | changed 4,58,60–68,72,74–77; internal_ops: 62; person: 4,74 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove individual operator/session and private media permissions; retain exact product and reusable validation boundaries | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_implementation_progress_20260906.md` | changed 68,143–145; internal_ops: 144; private_repo_name: 68 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_integrated_implementation_plan_20260907.md` | changed 110–113,297,308,362,367,379; internal_ops: 379; person: 110,111,112,113,297,308,362,367; personal_path: 110,111,112,113,297,308,362,367; private_repo_name: 362 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove unavailable/private Markdown evidence target; preserve named evidence and limitation; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_invariants.md` | changed 236,239,241,243–248,251; person: 236,239,241,243,244,245,246,247,248,251 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_jungle_activation_and_visual_plan_20260911.md` | changed 4–5,86–88,350,384; external_local_path: 175; internal_ops: 384; person: 4,350 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_lightweight_runtime_progress_20260906.md` | changed 68,97,112; person: 112; personal_path: 68 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove actual personal home/Windows machine path; remove individual Windows operator authorization; preserve load-test scope/results; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_listening_review_01_20260907.md` | changed 9,16,38,43; person: 9; personal_path: 9 | H1 cleaned; remove personal delivery, private studio output label/rate and transient evidence location; retain real-speaker limitation and exact test outcomes; remove unavailable/private Markdown evidence target; preserve named evidence and limitation | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_local_blind_runtime_progress_20260906.md` | changed 95,232; internal_ops: 95; person: 232; personal_path: 232 | H1 cleaned; remove internal Notion/SECTION operation or omission record; replace maintainer home prefix with portable repository path | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_meter_current_implementation_audit_20260831.md` | changed 23–25; person: 23,24,25; personal_path: 23,24,25 | H1 cleaned; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_meter_product_contract_20260831.md` | changed 43,51; person: 43,51; personal_path: 51 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove maintainer-specific worktree/branch instruction; retain isolation rationale | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_meter_visual_concepts_20260831.md` | changed 13; person: 13 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_observation_loop_work_attachment_20260901.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_plans_review_and_update_path_20260906.md` | changed 49–57,79,245,247–248,254,256–257,263; internal_ops: 263; person: 49,50,51,52,53,54,55,56,57; personal_path: 49,50,51,52,53,54,55,56,57 | H1 cleaned; remove individual internal handoff routing fields; retain technical What/Why/Next/Ref; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_post_os_guide_integration_plan_20260831.md` | changed 9,20; person: 9 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_pre_post_blind_feasibility_20260906.md` | changed 412–420; person: 412,413,414,415,416,417,418,419,420; personal_path: 412,413,414,415,416,417,418,419,420; private_repo_name: 412 | H1 cleaned; remove unavailable/private Markdown evidence target; preserve named evidence and limitation; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_pre_post_blind_usability_plan_20260914.md` | changed 415; internal_ops: 415 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_pro_tools_full_issue_audit_20260912.md` | changed 128 | H1 cleaned; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_reference_and_audio_research_20260906.md` | changed 158,161; internal_ops: 158 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_reference_connect_action_20260913.md` | changed 3 | H1 cleaned; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_reference_session_repair_plan_20260907.md` | changed 36; person: 36; personal_path: 36 | H1 cleaned; remove unavailable/private Markdown evidence target; preserve named evidence and limitation | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_remaining_work_handoff_20260910.md` | changed 28,30–42,75,208,214,259,282,320–322; internal_ops: 30; person: 28,75,208,214,259,282,320,321,322; personal_path: 28,320,321,322; private_repo_name: 28,276 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove actual personal home/Windows machine path; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations; remove stale individual worktree status and private remote-operating path; preserve public commits/CI/PKG hashes/test limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_responsive_typography_plan_20260910.md` | changed 212; person: 212 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_space_attack_plan_20260906.md` | changed 8,86,188,398,407,431,433–434,440,454,482–489; person: 86,188,398,407,482,483,484,485,486,487,488,489; personal_path: 482,483,484,485,486,487,488,489 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove individual internal handoff routing fields; retain technical What/Why/Next/Ref; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_space_decay_definition_20260910.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_space_decay_design_review_20260906.md` | changed 3,179–185; person: 3,179,180,181,182,183,184,185; personal_path: 3,179,180,181,182,183,184,185 | H1 cleaned; remove unavailable/private Markdown evidence target; preserve named evidence and limitation; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_space_mono_sum_plan_20260918.md` | changed 183,214; person: 183,214 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_spectrum_mid_side_overlay_plan_20260911.md` | changed 451–452; internal_ops: 451,452 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_stability_plan_2026-06-26.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_structural_repair_plan_20260907.md` | changed 20,153,161,186–188,191,202; external_local_path: 186; person: 20,153,161,186,202; personal_path: 20,153,161,186,202 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private music-library topology and inherited individual permission; retain corpus isolation and annotation facts; remove unavailable/private Markdown evidence target; preserve named evidence and limitation | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_structural_repair_plan_20260912.md` | changed 9,11–13,15,24,290,341,348,564; person: 9,15,290,564 | H1 cleaned; neutralize individual/AI operator routing and stash/private UI access; preserve every test result, expected behavior and incomplete host gate; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_aggregation_candidates_20260918.md` | changed 3,278,438; person: 3,278,438 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_channel_map_findings_20260918.md` | changed 3,12,14,144,229; person: 3,12,144,229 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_decisions_20260918.md` | changed 3,133,203,257–258,262; person: 3,133,203,257,262 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_gate_plan_20260919.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/hypha_surround_implementation_plan_20260918.md` | changed 3,18,248,274; person: 3,18,248,274; private_repo_name: 153 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_ingest_capacity_20260918.md` | changed 3,199,264,292–293,699; person: 3,199,264,292,293,699 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_metric_contracts_20260918.md` | changed 3,341–342,413–414,423,451; person: 3,341,413,423,451 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_p0_inventory_20260918.md` | changed 18,130; person: 18,130 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_surround_review_request_20260919.md` | changed 346; person: 346 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_trace_record_inbox_recovery_handoff_20260806.md` | changed 7,9–11,13–14,371–372,390; internal_ops: 390; person: 371,372 | H1 cleaned; neutralize individual/AI operator routing and stash/private UI access; preserve every test result, expected behavior and incomplete host gate; neutralize individual/guardian decision attribution; retain dated technical decisions; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_true_peak_self_check_20260707.md` | changed 35; person: 35 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_windows_cpu_psb_diagnosis_20260906.md` | changed 16; personal_path: 16 | H1 cleaned; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_windows_exchange_safety_20260906.md` | changed 44–48; person: 46 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private DAW occupation/change/authorization state; retain uninstalled status and regression requirements; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/hypha_windows_observability_fix_20260906.md` | changed 4–5,194,196,207; internal_ops: 207; person: 5; personal_path: 194,196 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove actual personal home/Windows machine path; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/media/hypha_meter_concepts/README.md` | changed 14–16 | H1 cleaned; remove AI session/local generated-image storage while retaining generator, references, prompts and output dimensions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/perceptual_delta_design.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/hypha_control_hierarchy_20260911.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/hypha_daw_loop_match_design_20260930.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/hypha_initial_loop_pending_entry_20261002.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/hypha_live_blind_recovery_reveal_20260930.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/hypha_live_chain_compare_contract_draft_20260928.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/hypha_live_chain_compare_external_research_20260927.md` | changed 201; internal_ops: 201 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_live_chain_compare_g1_studio_pro_20260928.md` | changed 29,85,116,485 | H1 cleaned; neutralize individual/AI operator routing and stash/private UI access; preserve every test result, expected behavior and incomplete host gate; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_live_chain_compare_implementation_plan_20260927.md` | changed 1191,1323; internal_ops: 1191,1323; private_repo_name: 100,168,219,930,968 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_live_chain_compare_implementation_plan_20260927_v7.md` | changed 944; internal_ops: 944; private_repo_name: 47,114,165,715,751 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_live_chain_compare_review_20260927.md` | changed 200,267,393–394,396–397,407,501,609–610,615,623; internal_ops: 200,267,393,407,501,609,610,623; private_repo_name: 567,602 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_live_chain_compare_review_v6_20260927.md` | changed 109,166–168,170–171,181; internal_ops: 166,167,181 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_live_compare_lifecycle_repair_20260930.md` | changed 84,86,88,90,96; internal_ops: 96 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private session/daily-log routing fields; preserve technical completion and remaining gates; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_one_pass_live_blind_implementation_plan_20260929.md` | changed 471,483–484; internal_ops: 483 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_one_pass_live_blind_validation_20260929.md` | changed 46; internal_ops: 46 | H1 cleaned; remove internal Notion/SECTION operation or omission record | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_portable_timing_fixture_20261002.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/hypha_post_reference_entry_plan_20260928.md` | changed 140–141; internal_ops: 141 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/planning/hypha_vst3_loop_native_clamp_20261002.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/planning/kirin_hypha_reference_growth_integrated_execution_plan_20260925.md` | changed 2,4–23,26,28–31,33–39,64,66,70–71,107–110,121,124–138,144,164,192,194,207–304,343,345,350–729,744–754,763–779; person: 18,19,35,343,408,558,586,599,600,613,715; private_repo_name: 131,132,133,134,748,749,750,751 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove internal marketing/press/campaign/product-media plan and proprietary repo/commit/blob topology; retain technical product/licensing boundaries, all RT tests, source/public links and release gates; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/post_absolute_timeline_design.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/reference_a_full_capture_plan_20260914.md` | changed 7 | H1 cleaned; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_a_full_capture_validation_20260914.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/reference_b890_structural_repair_plan_20260914.md` | changed 16–17,276; internal_ops: 276; person: 16,17; personal_path: 16,17; private_repo_name: 17 | H1 cleaned; remove actual personal home/Windows machine path; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_c_tonal_balance_plan_20260914.md` | changed 47–50,336,630,734,890,892–912; internal_ops: 734; person: 47,48,49,50,890,892,893,894,895,896,897,898,899,900,901,902,903,904,905,906,907,908,909,910,911,912; personal_path: 47,48,49,50,890,892,893,894,895,896,897,898,899,900,901,902,903,904,905,906,907,908,909,910,911,912; private_repo_name: 49,50,892,893,894,904,905,906,908,909 | H1 cleaned; remove actual personal home/Windows machine path; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations; remove unavailable/private Markdown evidence target; preserve named evidence and limitation; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_capture_b885_structural_repair_plan_20260914.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/reference_capture_b887_implementation_20260914.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/reference_capture_structural_repair_plan_20260914.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/reference_capture_workflow_implementation_20260914.md` | changed 85; personal_path: 85 | H1 cleaned; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_evidence_and_discovery_contract_20260914.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/reference_lazy_candidates_handoff_20260910.md` | changed 13 | H1 cleaned; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_lazy_presets_handoff_20260910.md` | changed 1,5; person: 5; personal_path: 5 | H1 cleaned; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_library_receiver_20260913.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/reference_listening_workflow_plan_20260914.md` | changed 42,541,551–562; internal_ops: 541; person: 551,552,553,554,555,556,557,558,559,560,561,562; personal_path: 551,552,553,554,555,556,557,558,559,560,561,562; private_repo_name: 42,554,555,556,557,558,559 | H1 cleaned; remove internal Notion/SECTION operation or omission record; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations; remove unavailable/private Markdown evidence target; preserve named evidence and limitation; replace private absolute Markdown target with verified public relative target | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_os_preparation_handoff_20260910.md` | changed 1,3,22; person: 3; personal_path: 3 | H1 cleaned; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_publication_refresh_handoff_20260910.md` | changed 1,20,22,38; person: 20; personal_path: 20 | H1 cleaned; remove actual personal home/Windows machine path; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_review_corrections_20260913.md` | changed 1 | H1 cleaned; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_runtime_v2_handoff_20260905.md` | changed 11,42,98,246,250,252,254,256,258,260,262,270,276,280,282,286,288,290,292,304,310,314,316,320,463,469,509,626,628,670,678,696,726,776,798,836,838,848,850,852; person: 42,98,246,250,252,254,256,258,260,262,270,276,280,282,286,288,290,292,304,314,316,320,463,469,509,626,628,670,678,696,726,776,798,836,838,848,850,852; private_repo_name: 11,320 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_visual_comparison_implementation_20260914.md` | changed 5 | H1 cleaned; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_visual_comparison_plan_20260914.md` | changed 7,9,208; person: 9,208 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/reference_whole_song_alignment_20260913.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_design.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_evaluator_v2_report_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_implementation_plan.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_phase0_baseline_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_phase2_audio_provenance_report_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_phase2_candidate_report_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_phase2_drum_pilot_report_20260830.md` | changed 192,210; person: 192,210 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/transient_delta_phase2_formal_development_gate_report_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_phase2_midi_provenance_report_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_phase2_recovery_plan_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/transient_delta_phase2_research_foundation_report_20260830.md` | no assigned personal/internal exposure match | H0 kept; public technical contract, evidence, provenance or contributor verification | None; file byte-identical |
| `docs/watch_hardware_polish_notes_20260903.md` | changed 13; person: 13 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/watch_hardware_polish_proposal_20260903.md` | changed 185,508,542; person: 185,508,542 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `docs/windows_external_validation.md` | changed 56,230; person: 56,230; private_repo_name: 31,44 | H1 cleaned; neutralize individual/guardian decision attribution; retain dated technical decisions; remove private tool/session or proprietary source identifiers; keep technical results and validation limitations | No code/runtime change; private evidence targets now named prose where applicable; dated results/limits kept |
| `assets_source/IMG_3113.png` | PNG tEXt Author/Creation Time/Description; XMP job GUID | H1 cleaned account/job/source-reference metadata; H0 kept image pixels and AI-source attribution | 441 metadata bytes removed; every image-data byte and decoded pixel unchanged |

Checks: assigned changed docs 82, assigned H0-kept docs 43, all 63 new relative links resolved, fence parity valid on 82 changed docs, residual scan matches 0, `git diff --check` exit 0.

## Public ref inventory at audit

| Type | Ref | Full tip commit |
| --- | --- | --- |
| branch | `backup/feature-pre-rebase-20260517_193855` | `04900e93efc4a2736a477d2aa9c160302a5592a6` |
| branch | `backup/feature-snapshot-20260517_193855` | `04900e93efc4a2736a477d2aa9c160302a5592a6` |
| branch | `backup/main-pre-rebase-20260517_193855` | `a318639b98b63ac00deb3245cf2134e678bd5805` |
| branch | `claude/gracious-pasteur-5b5452` | `c71c12e9b1fd08d23982db69cd723975643607e0` |
| branch | `claude/hypha-abcv-h` | `3d74286f7635bdbdaab9b3681948f830aa78351d` |
| branch | `claude/hypha-drum-band-mock` | `283ab7ad42efcecb35e772f45bb318e2822efebe` |
| branch | `claude/hypha-drum-band-summary` | `95da298b9129ee5291b60ccbd411c9b9e0fb9350` |
| branch | `claude/hypha-light-stage2` | `20d7dcd56543430ae51c1532277a94f15a6beec2` |
| branch | `claude/hypha-protools-windows-check` | `37396101f0d13fc4383b2334fd78474409483291` |
| branch | `claude/local-main-reconcile` | `5364e6292aa1326c5e265a8e9cc00d100f2eda88` |
| branch | `codex/b896-observatory-background-recovery` | `5200f8817c8adfd12bfe95531c7409205b0ecb7d` |
| branch | `codex/b897-observatory-background-current` | `e7db39d225fce028f465616f279eb94dcd24ce3a` |
| branch | `codex/hypha-aax-cka` | `0093a4f339d107fd1a4e49cfa9ef8e538b887893` |
| branch | `codex/hypha-aax-qualification` | `7ad511a6556b4d4268a9155b55e9ee8192b87568` |
| branch | `codex/hypha-one-pass-blind` | `91079ae7cf200ac511f26fe1341f5541016d315c` |
| branch | `codex/hypha-perceptual-continuous` | `ad8aad129ede7679b6c9907508818aa51c15e598` |
| branch | `codex/hypha-ref-simple-abc` | `6ba9fe20e681d369c1de08e1c74b1ef2406dc010` |
| branch | `codex/hypha-reference-b-preparation` | `e0f2cc9dbb3517a4a92bc5e40cee8ef5c340192d` |
| branch | `codex/local-recovery-20260915` | `05d347ad19da96917c42ebd68ce55393753b40e8` |
| branch | `codex/reference-abc-delivery` | `0aab301373c511ff6813a01363dbd99630079a69` |
| branch | `codex/reference-library-receiver` | `9b2c827448931fda24abd85b5cd9a570fdc10269` |
| branch | `feature/hypha-pre-post-stabilization` | `05a1a63d25bef1607faafa6978b4ea3467c874e0` |
| branch | `fix/b1007-modern-codesigntool-java` | `99ea232ff94e06e7b73e0b17d07a4a3538c7425d` |
| branch | `main` | `a8f5a4a4a791cd4a81346e2c5462d08269619b16` |
| tag | `v1.1.50` | `3d2234ec78ba5924d5db92fd88498bec64233b5c` |
| tag | `v1.1.49` | `c9e458d1e98c43f9028afd974518a298cca94108` |
| tag | `v1.1.48` | `992cb3094010b6eda44ce0d486ad01a0e94a9b54` |
| tag | `v1.1.47` | `b965ea12a90152038b01949c1ef8e2cea99dc26a` |
| tag | `v1.1.46` | `9d27808dd10d6c859340d0f9aec093d0bd8db4cf` |
| tag | `v1.1.45` | `5ba9bf5e3bcfc1df230f93ca933f39213477d446` |
| tag | `v1.1.44` | `6baa82d81575f0019412c4f9d5da79c8708f3661` |
| tag | `v1.1.43` | `5b1e8a9deb2bc41f7fa5631e92037c0f8a1fdf6f` |
| tag | `v1.1.35` | `547d7767f802b61e447f953d9887ca6253818170` |
| tag | `v1.1.34` | `eb99f1ad17e54dc32b8ad959e1798193b6f39d1b` |
| tag | `v1.1.33` | `019dc3fa85c4932605a2837e6c6b55adfb3c3bb9` |
| tag | `v1.1.30` | `58c975fba5489fb2612674169421bf483ca48fae` |
| tag | `v1.1.29` | `cc54e9ec121de0449d8ff91f99edbc21a47093e5` |
| tag | `v1.1.28` | `3e611b545dc3f1ca2bd85b065b264c702a7bc3dd` |
| tag | `v1.1.27` | `d861b8dba1b5228573b93497e4b5def67f75b2fc` |
| tag | `v1.1.26` | `a5ec2e11a7c933709a7e09e82088e121405b9b38` |
| tag | `v1.1.25` | `fc68e87795b2741e4eaf938c1f36ce1ce7a61a4e` |
| tag | `v1.1.24` | `a34e1e517b02ad7be8ae82327d47e782b7452195` |
| tag | `v1.1.23` | `724de70315f9f21aa754f39fc963070c073acfd0` |
| tag | `v1.1.22` | `1d8a10dc45d72f5bb8df65e8beff2d2fb65046c4` |
| tag | `v1.1.21` | `9a2e6f0062af0aa06f26522ddf00ed7b77c1a2c4` |
| tag | `v1.1.20` | `645fd4f3901428cc655dcf9972070cb16a5da912` |
| tag | `v1.1.19` | `51c7111151894409292c8710f92985b213d1f76d` |
| tag | `v1.1.18` | `c5a32b3c0acc64e5158c1471cbac0ca8e42610bd` |
| tag | `v1.1.17` | `f30051606d1eb9d36d50ae2bbd6f8605f32cca8b` |
| tag | `v1.1.16` | `e2e3e7409ae0bcd7d91dbe89a5aee9ab7b7676f8` |
| tag | `v1.1.15` | `199259360669a6b083a5c4d6dfcbb32f8bb49b90` |
| tag | `v1.1.14` | `aede33bcbb34c796272f8660dfdbbedbb2aa396b` |
| tag | `v1.1.13` | `e31656f9a6d87b151e2dee4ade62e3220af51563` |
| tag | `v1.1.12` | `03597bce44c3483aa146bda0bfa3f368f0b346d3` |
| tag | `v1.1.11` | `7fae45ad57be7b5e754bfa46aa3083c9b73ce173` |
| tag | `v1.1.10` | `bb304753e7a2737d9289cba78966531c4244815a` |
| tag | `v1.1.9` | `5e9c5fda19333ae454b2f61bd2260a0a52ed06f8` |
| tag | `v1.1.7` | `f5ac789e1fdac702de2d1fb51e84b831a415fe38` |
| tag | `v1.1.6` | `2f9554859227cbf4b3dd0e93be80e4c7e21a5b34` |
| tag | `v1.1.5` | `15b2cc7b1f11de7aef40e1652f88bf09f0c680fb` |
| tag | `v1.1.4` | `9aed8ec20fcfa7066f056ee1b5a9b44d1aaa29d6` |
| tag | `v1.1.3` | `6c4d71c462ecd43cb43ab4e18c2b0dfebfe00fd9` |
| tag | `v1.1.1` | `83738961e899f2276487a3353cf8c5820674f38e` |
| tag | `v1.0.0` | `81aefb70fadcca10702065f756f5cc1e01695430` |


## Supplementary contact/cloud review — before final hygiene edit

All 165 submitted phone/address/cloud/admin candidates (153 source lines, 40 files) were inspected in context:
157 apparent phone values are UUID test fixtures or numeric CMake benchmark parameters; the other eight
are a null example admin-URL field, generic local-state schema logic, and a metadata-redaction denylist.
They are H0 and retained. No actual telephone/address/personal cloud path/populated management URL or
credential was found in that candidate set; legal copyright/license contacts are retained.

| File / original location | Content type and evidence | Public necessity / classification | Recommended action / side effect | Persistence / public surfaces |
| --- | --- | --- | --- | --- |
| `docs/hypha_b1_host_observation_20260907.md:234` (pre-edit candidate) | Personal cloud capacity notification and operator account/sync-state note; no account, path or credential value | Unnecessary to build, reproduce or assess adjacent host/PCM findings; internal operator detail, H1 | Remove only this sentence; preserve neighboring host evidence, exact source/binary hashes, restoration and test limits | Already committed: remains in Git history, source archives and any published PR diff; forward cleanup does not alter old Release or logs. No rewrite; owner decision deferred |

This finding was recorded before removing its sentence. The remaining observation document preserves
all technical acceptance evidence; this is an additional PR-A hygiene edit with no code change.


## Supplementary private storage-root audit — before final path edits

Read-only review of 139 owned docs/root guides classified 135 concrete path/numeric candidate lines:
23 H1 private storage-root lines in four documents, 112 H0 public protocol/platform/SDK/output/anonymous
temporary evidence lines retained. Ten further explicit external/absolute contributor placeholders
are H0. No actual private network host/address, UNC share, populated Windows username or named volume
was established in this reviewed document scope. Candidate classification used actual context.

| File / pre-edit locations | Actual category / public necessity | Action and side effects | History / public surfaces |
| --- | --- | --- | --- |
| `docs/hypha_b1_host_observation_20260907.md:110,232,233` | Private crash/host/PDC evidence storage roots; directory topology unnecessary for contributor verification | Remove storage root; preserve report/evidence basenames, test results, hashes, recovery and pending limitations. No invented public download link. | H1; prior Git/source archives/PR diffs may retain; no rewrite or old asset/log removal |
| `docs/planning/hypha_live_chain_compare_g1_studio_pro_20260928.md:91,95,96,185,189,272,370,391,429,430,431,433,434,435,448,480,483` | Private CSV/PCM/analysis/disposable DAW session and removed-fixture directory topology | Use honestly marked private evidence identities/basenames. Preserve every SHA-256, host/clock/PDC metric, source-supplied limitations, disposable/removal/unlink status and public source references. | H1; same forward-only/history scope, no binary package rebuild claimed |
| `docs/planning/hypha_live_chain_compare_review_20260927.md:544,545` | Private storage roots for two supplied review inputs | Keep basenames, attached-input/version status, 189/690 line counts, SHA-256 and exact input-line references; remove storage root only | H1; prior commits/archives/diffs remain |
| `docs/transient_delta_phase2_research_foundation_report_20260830.md:226` | Actual home Trash storage path following dataset isolation incident | Remove home root; retain moved-to-OS-Trash fact, not emptied/recoverability, strict-unopened violation, exact counts/hash and holdout blocker. No research-gate weakening. | H1; prior commits/archives/diffs remain; log/artifact spread unconfirmed |
| `test_signals/source.md:6` | Private source-repository folder nickname embedded in otherwise legitimate source/creator attribution | Remove only folder nickname. Keep source product, original creator and per-wave origin table; no WAV/license change. | H1 nickname; H0 attribution retained; prior commits/archives/diffs remain |

These categories/locations were recorded before the narrow edits. Anonymous temporary evidence
identities, public app-data/protocol/SDK paths and source/author/license provenance remain public.


## Embedded HMAC semantic classification (no key value reproduced)

`crates/kirin_measure/src/identity.rs:30–32,187–190` explicitly states that Phase 1 embeds a fixed
key with GPLv3-public source and limited tamper resistance. The exact default declarations at
`identity.rs:202`, `preset.rs:149`, `preset_v2.rs:167`, `plugin_data.rs:4362` are identical deliberately
public protocol defaults: **H0, retain for compatibility; not a private API/signing/service credential**.
Semantic comparison of 105 relevant historical blobs / 94 declarations found one identical public
variant and no literal override assignment. The known public default is also present in the inspected
Mac release executables. Values/fingerprints are not republished here. No rotation is indicated for
this intentional default. No blanket exception is granted to arbitrary/future/override HMAC values.

Medium existing limitation: anyone already able to supply/alter local files can recompute default
checksums. Identity MAC covers installation/hardware IDs, not every field; the shipping license
reader is explicitly loose. This does not establish remote compromise or adversary file access, and
the existing MAC must not be described as adversarial producer/entitlement authentication. Its
source remains necessary to verify the public protocol. Producer/protocol/license changes are outside
this task and remain unchanged. [HMAC RFC 2104](https://www.rfc-editor.org/rfc/rfc2104.html).

Low existing warning discrepancy: `crates/kirin_measure/build.rs:13` reports an empty override as
default, whereas consumers' `option_env!` select an empty supplied value. The warning alone is not
proof of actual key selection. No private environment/credential store was read; no behavior change
was made. A real private build-time override would be embedded in a client and requires separate
H2 assessment if found; none was identified in inspected source/history/sample binaries.

## Trusted origin identities retained — explicit privacy exception

Final URL/context review found the following real internal origin identities (values omitted):

| Location | Actual role / classification | Public need / disposition | Side effects / persistence |
| --- | --- | --- | --- |
| `scripts/ls_release/build_kirin_hypha_release_set.mjs:222,313` | Signing workflow origin allowlist and exclusive signed-candidate validation origin; H1 internal topology, no credential | Retain existing checks. Replacing with arbitrary/synthetic origin changes security acceptance; independent public provenance verification needs an explicit trusted-origin policy. | No loosening/configuration migration. Public source/history retain this operational identity; a different auditable policy requires separate review. |
| `scripts/ls_release/release_metadata.test.mjs:392,408` | Matching local synthetic artifact fixtures using those real allowed origin identities | Retain to exercise current production acceptance. These fixtures make no external calls. | Retained exception to private-URL residual scan; generic fixture replacement would fail current policy |
| Existing `v1.1.49` verification report + Setup EXE JSON, `v1.1.50` verification report + Setup EXE JSON | Six private signing/candidate run-URL occurrences across four small text assets, expressing trusted artifact provenance | H1 operational origin, useful provenance. Existing assets preserved; no secret value identified. | Source alone cannot remove published URLs. Future verifier/sidecar migration would need coordinated origin-policy review; not an asset rewrite |

Release asset IDs inspected: 541167516 / 541167501 / 579174069 / 579174053. The JSON fields are
`signing.workflow_run` and, for v1.1.50, `external_validation.candidate_workflow_run`; reports state
the signed-candidate/promotion runs. These are provenance run links, not administration/credential
URLs. Keep reporting distinct from secret exposure. A raw residual assertion initially flagged the
fixture file; actual validator/field review established this deliberate retained exception, rather
than making an unjustified all-private-URLs-absent claim.

`test_signals/source.md` retains source product and original creator attribution for the six
measurement WAVs; only the unnecessary source-folder nickname is removed. Author/license provenance
is H0 and must remain. Public verifier/signer identity and copyrighted vendor notices are likewise
retained; real names are not mechanically erased.


Supplemental storage cleanup result: four documents / 23 authorized lines replaced. Numeric-token and
SHA-256 multisets, all Markdown link targets, all private evidence basenames and every unassigned
line are unchanged; no new public evidence links. Authorized storage-root remnants zero. Research
holdout/Trash recoverability, source-supplied review-input version/hash, G1 disposable session/fixture
removal/shared-memory unlink and host acceptance limits retained. `git diff --check` passed.
