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
| `scripts/ls_release/build_kirin_hypha_release_set.mjs` | private_repo_name: 222,313 | Final classification H1 operational origin, retained for existing security/provenance checks; see explicit exception below |
| `scripts/ls_release/hypha_release_local.mjs` | private_repo_name: 1,81,83,84 | H0 public verification/runtime home checks/synthetic fixtures; retain unless specific actual operator metadata |
| `scripts/ls_release/release_metadata.test.mjs` | person: 721; private_repo_name: 392,408 | H0 fixture/author identity; H1 real origin fixtures retained to verify current allowlists, see explicit exception below |
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

## PR-B implementation (local candidate)

The public CI signing inputs/Java/CodeSignTool/eSigner bindings are removed, not merely hidden behind
an unsigned switch. All Windows build/package steps use literal unsigned; external acceptance is
pending. The optional public AAX persistent-runner/SDK-input job is removed. Hosted validation and
existing required job names/artifact paths remain; independently controlled local SDK/signing entry
scripts and exact signature/lifecycle/host/distribution acceptance requirements are retained.
README, the Windows release runbook and AAX readiness guide now describe the same public/trusted boundary.
No product runtime, DSP, audio, Reference, UI, format/schema/API, license, price, version, tag or Release changes.

Actions use the exact commits resolved from their existing refs at audit, with read-only contents permission,
no persisted checkout token, and explicit stable Rust toolchain. No write/OIDC permissions are added.
The new policy regression checks every tracked workflow and negative variants for credentials, token write,
privileged events, persistent/dynamic runners, mutable Actions, token persistence, shell-context injection,
signing dispatch and false external-acceptance status. Its hosted check is part of public-history validation.
These editable source checks complement server settings; they cannot enforce independent collaborator review.

Dependabot covers actual GitHub Actions and Cargo ecosystems only, weekly, with two open PRs per ecosystem
and grouping; no auto-merge. Pin updates remain reviewed changes, including source/security/native gates.
CODEOWNERS/branch-policy changes were not imposed on the sole current maintainer.


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


## Authorized email privacy follow-up

The authenticated owner approved GitHub noreply for this session's new local commits only. Commit
identity was supplied per invocation; the repository/global Git configuration, prior author/committer
metadata and `.mailmap` were not changed. The public profile's email field was null, but a separate
`user/emails` API read showed primary email visibility `public`; a null profile field was therefore
not treated as proof of private email. No address value was printed or copied.

After explicit owner approval, `PATCH /user/email/visibility` with `visibility=private` succeeded. A fresh
`GET /user/emails` confirmed one verified primary address with visibility `private`. The account UI's
block-personal-email-push option remains unconfirmed, and old commit email remains H1. This account
setting and the already approved/enabled PVR are the only external settings changed by this task.
See [official email visibility API](https://docs.github.com/en/rest/users/emails#set-primary-email-visibility-for-the-authenticated-user).

## Latest released binary sample — actual H1 persistence

Read-only sample: public `v1.1.50`, source `3d2234ec78ba5924d5db92fd88498bec64233b5c`,
published 2026-09-21. Downloaded the existing macOS Universal PKG, macOS Universal ZIP and Windows
x64 Setup EXE, total 167,223,551 bytes. All three SHA-256 values matched existing published sidecars;
this confirms checksum agreement, not independent signature/notarization acceptance. No installer,
payload or package script was executed; no existing asset was changed.

ZIP: 42 entries, every member CRC validated while reading, 167,985,195 expanded bytes. PKG: installed
`pkgutil --expand` used only for container extraction; gzip/odc cpio payload streamed without writing
or following member paths, 83 entries / 167,910,942 payload bytes with gzip CRC/trailer validation.
Metadata/scripts were read as inert bytes. Windows: raw ASCII/UTF-16LE container checked; installed
`bsdtar` could not recognize Inno format and no installed extractor was available. The compressed
Windows payload is **unconfirmed — no retrieval/extraction coverage**. Across 88 raw/member objects
and 503,193,412 scanned bytes (including repeated containers/payloads), no credential signature
candidate was found. This is scoped inspection, not proof against every encoded/unknown secret.

| Released location | Content / necessity | Classification / recommended action | Persistence / side effects |
| --- | --- | --- | --- |
| Four PRE/POST AU/VST3 Universal Mach-O binaries inside the ZIP and identical PKG counterparts | Owner-specific home component in Rust `.cargo/registry` dependency source-location strings; 164 matches per binary, 656 in ZIP, 1,312 including duplicate PKG packaging. Personal values omitted. Crate/file/line diagnostic identity is useful; the individual home name is unnecessary. | Medium, H1. Keep current assets under the explicit no-asset-change condition. Evaluate future compiler path remapping and verify new unsigned artifacts before normal signing/release gates. No rotation indicated by a path. | Existing Release assets retain the strings; current docs/PNG cleanup cannot remove them. Build flags are unchanged, so forward recurrence is not yet prevented. Older assets/Windows compressed payload/Actions artifacts require separate coverage; do not mark clean. |
| Other generic user-directory matches / binary regex coincidences | 17 generic framework/portable installer matches and five short entropy matches were inspected; not another identified account or personal email | H0, retain portable installer behavior | No cleanup needed |

The ZIP intentionally packages README/LICENSE/generated INSTALL, plugin metadata and bundles. PKG
payload packages installable bundles and its preinstall metadata/script. Neither contains bulk docs,
AGENTS/CLAUDE/private release state/source artwork by inspected member inventory. Complete original
and cleaned source-PNG byte sequences were absent from all inspected objects; CMake embeds three
derived GUI assets, not that source image. This assertion excludes unexpanded Windows/older assets.

[Official rustc source-path remapping documentation](https://doc.rust-lang.org/rustc/remap-source-paths.html)
explains the best-effort boundary and linker/external-tool limitations. Future work must preserve useful
source traceability and exercise the existing source, native/audio and release gates; stripping alone
is not an established remedy. Historical binaries/metadata are H1 pending owner decision; H2 was not
identified and no credential rotation trigger was established by this sample.

## Forward cleanup / regeneration verification boundary

Source inspection of `xtask/src/release_package.rs`, package metadata modules, PKG builder, Inno
manifest, Windows fallback ZIP builder, build-source identity and CMake build identity established:

- Future HP ZIP explicitly copies current README/LICENSE; future Windows fallback ZIP explicitly
  copies `docs/windows_external_validation.md`. Those files are cleaned in this candidate.
- Other curated docs, AGENTS/CLAUDE and ignored private state are not bulk-copied by these packagers.
- Public provenance reads commit/B/hash/source state, not author or committer email. A source commit
  hash remains intentionally public and does not erase metadata from GitHub history.
- No owned doc generator restoring removed operator prose was found in inspected paths. No new
  signed release package was built for this hygiene task, so final binary/package absence is not claimed.
- Ignored private state stays outside public payload lists. The source PNG picture/IDAT bytes are
  unchanged; only unnecessary account/job/source-reference metadata was removed, with AI attribution retained.
- Selected hosted Actions logs contain runner/runneradmin paths (H0), not an identified owner home.
  Future hosted candidate CI has not run. Current local build logs necessarily show the local checkout
  path and are private evidence; they must not be uploaded wholesale.

Remote main, prior commits, tags, source archives, old PR diffs and existing Release assets remain
unchanged. Removal statements apply to the local PR-A/PR-B candidate, not already-published main.


## PR-B affected source guards and operational consequence

| Path | Concrete change / necessity | Preserved behavior / remaining effect |
| --- | --- | --- |
| `.github/workflows/ci.yml` | Remove public signed dispatch inputs, Java/CodeSignTool and every signing-secret binding; literal unsigned/pending Windows candidate, contents read, SHA pins, no persisted checkout token, workspace via env, source-security check | Keep existing required job names, expensive-job event gates, native/pluginval/audio-transparency/install lifecycle and artifact names/paths. Signed acceptance remains external. |
| `.github/workflows/aax-phase-a.yml` | Remove optional public persistent SDK runner and SDK path inputs; hosted SDK-absence suite, readonly token and pinned checkout remain | Licensed local Mac/Windows entry scripts remain; no public runner enrollment or private SDK publication |
| `.github/dependabot.yml` | Review-driven Actions/Cargo weekly updates for actual ecosystems; two open PRs per ecosystem, groups | No npm/Python invented and no auto-merge |
| `scripts/ci_security.test.mjs` | Meaningful negative mutations for secret under unsigned mode, token write, runners, privileged events, Action mutability, checkout persistence and signing status; reject flow mappings outside scalar/script bodies to keep block-source scans defined | Regression aid that an authorized source editor can change; independent server review is still necessary. Current flow sequence branch lists retained. |
| `xtask/src/windows_preflight.rs` / `tests.rs` | Require unsigned/pending, reject credential route/manual signing and mutable uploader; keep package/lifecycle/artifact checks | Immutable SHA format rather than one pinned digest allows reviewed Action updates without another Rust digest edit |
| `xtask/src/windows_readiness.rs` | Final dependency inspection found old positive `upload-artifact@v7` string at baseline line 329; use Action prefix and require immutable-ref negative-test evidence | Readiness remains compatible with pinned CI; other release checks retained |
| `xtask/src/ci_usage_guard.rs` | Final inspection found existing `checkout@v4` step mutation did nothing against baseline v6; target stable named shipping-source step and assert actual mutation | Tests truly exercise rejection of step-only gate; production usage-policy code unchanged |
| `scripts/windows/windows_installer.test.mjs` | Replace obsolete public Java route assertion with public unsigned/credential-free assertion | Keep independent signer/TOTP/redaction/Java/Inno fixture tests |
| `scripts/ls_release/aax_distribution.test.mjs` | Replace obsolete public persistent-runner expectations with hosted/local boundary assertions | Signature/provenance/materialization tests and both local licensed SDK entries retained |
| `scripts/ls_release/build_kirin_hypha_windows_vst3_zip.mjs` / `release_metadata.test.mjs` | Final PR-B leaves these files byte-identical to PR-A. Retain the existing consumed exact CI step identifier. | Same source/signature/hash/provenance schemas and readiness requirements; avoid unnecessary executable-script hash churn in private signer allowlists. Private factory configuration still unconfirmed. |
| `README.md`, `docs/aax_phase_a_readiness_20260907.md`, `docs/ls_release/kirin_hypha_ls_runbook.md` | Describe the actual public hosted unsigned/trusted licensed signing boundary | Build/reproducibility/checksum/signature/three-channel acceptance remain public |
| This audit | Facts, fixes, validation and limits | No credential/personal values republished |

## Future binary path prevention — separate unimplemented work

Read-only build-graph tracing establishes that the current candidate does not enforce source-path
normalization. `scripts/build_hypha.mjs` unsigned plans compile both Apple FFI targets; its `--release`
route instead delegates through `release_hypha.mjs` and `hypha_release_local.mjs` to
`build_juce_universal.sh` and `build_aax_universal.sh --sign`. Both scripts compile Apple FFI archives
before lipo/link; CMake's default FFI fallback also uses Cargo without enforced remapping. Thus a
checkout-only or diagnostic-entry-only fix cannot cover the observed Cargo registry strings. No
ambient private machine config was inspected or changed.

Future policy must normalize actual Cargo-home/registry, checkout and generated-source prefixes
without hardcoding or printing personal FROM values, preserve existing compiler options and encoded
flag precedence, cover both Apple targets/all producers, and inspect final unsigned binary bytes.
Build/source snapshots do not fingerprint ambient flags, and diagnostic build IDs do not alone
isolate shared Cargo archives: old manifest/PASS reuse cannot certify a new mapping policy. The
applicable fixture/source/native/audio/format/provenance gates and normal licensed/signing/host/three-
channel gates remain required. Changing released executable bytes invalidates existing signatures
and hashes; the explicit existing-asset/tag/version prohibition keeps the released H1 unresolved.
No build-flag/product change was silently added to this hygiene/CI task.


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


Additional read-only configuration inventory: repository Actions variables `total_count=0`,
Dependabot secret names `total_count=0`, repository webhooks count/active count both zero. These
GETs returned successfully; no values, deliveries or webhook tests were requested and no setting
was changed. API method/paths verified in official
[Actions variables](https://docs.github.com/en/rest/actions/variables#list-repository-variables),
[repository webhooks](https://docs.github.com/en/rest/repos/webhooks#list-repository-webhooks), and
[Dependabot secrets](https://docs.github.com/en/rest/dependabot/secrets#list-repository-secrets) documentation.
Private external service/factory credentials are still unconfirmed; zero repository inventories do
not certify those systems. Fresh main read still returned original protected base commit.


Final compatibility decision: preserve the existing Windows installer CI step identifier
`Build Windows installer and sign all executable surfaces` because downstream provenance checks
consume it. YAML explicitly marks it as a legacy identifier; actual execution is always unsigned.
The unsigned/pending/no-secret regression and preflight guards are retained. Package verifier and
release-metadata test are byte-identical to PR-A, avoiding unnecessary private script-allowlist hash
changes. The final combined Node security/installer/Inno/metadata suite passed 45/45; actionlint
passed. This preserves a stable interface while strengthening the actual credential boundary.


## Supplementary public branch/tag workflow trust boundaries — 2026-10-05

Scope: all 24 branch tips and 40 tag tips in the existing public API inventory; exact commits were resolved from already-local Git objects. 84 workflow instances reduce to 31 exact blobs (28 ci.yml, 3 aax-phase-a.yml). All 31 YAML blobs parsed; no duplicate YAML keys found. No fetch/checkout/ref change/settings/test/build/CI/private-repository access occurred. Detailed per-step evidence is preserved privately; complete boundary/ref tables follow.

## Findings and trust consequences

- **High, latent if secrets are added:** 13 distinct CI blobs at 21 tips (19 branches including API main; 2 tags) bind four eSigner references to the installer source-execution step. The step has no additional `if`. Its job admits PR, workflow_dispatch, or a main push containing `[ci full]`. Signed-only preparation guards do not protect these env bindings on same-repository PR/unsigned runs. Repo secrets were previously confirmed absent by root; this is a remaining route, not proof of disclosure or rotation need.
- **High, latent if persistent runners are attached:** all 3 AAX blobs at 20 tips (19 branches; 1 tag) retain a manual optional SDK-equipped self-hosted runner route. Its source `if` and license confirmation are workflow-level operator checks, without an environment/immutable trusted-ref boundary. Root previously confirmed repository runners absent. Keep persistent licensed/signing machines inaccessible to this public repository. [GitHub runner security guidance](https://docs.github.com/en/actions/reference/security/secure-use).
- **Medium:** every inventoried blob omits workflow permissions; no job overrides/environment declarations exist. Every referenced Action uses a mutable version tag or `stable`, and every checkout defaults to persisting credentials/event ref. Current root-confirmed default token read is an inherited default, not immutable enforcement against authorized workflow editors. [GitHub permissions](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#permissions).
- **Informational:** only ordinary push-main/PR-to-main and, where declared, manual dispatch events occur. No pull_request_target, workflow_run, repository_dispatch, release, workflow_call, reusable job, privileged artifact downloader or auto-merge route was found in these workflow files. Direct expressions inside run source reference only github.workspace, not PR/Issue text/branch names. No eval found in workflow script bodies; source scripts still execute untrusted candidate code as expected for hosted validation.

Main cleanup does not remove old branch/tag workflow definitions. Manual dispatch requires an eligible default-branch workflow and authorized access; the API accepts a branch or tag as ref. Thus retain the already-confirmed repo-global absence of signing secrets and persistent runners across old refs. Do not assume new main guards, Dependabot or SHA pins apply retroactively. Existing refs/tags were left intact. [Dispatch eligibility](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/manually-run-a-workflow), [branch/tag ref API](https://docs.github.com/en/rest/actions/workflows#create-a-workflow-dispatch-event).

The snapshot has 19 branch tips with secret consumers: 18 non-main branches plus the unchanged remote main snapshot. A normal public fork PR does not receive repository signing secrets; same-repository/authorized dispatch and main push contexts remain distinct. No current secret value is included or newly discovered by this reference inventory. Broader credential/log findings and settings freshness remain root audit responsibilities.

## Actual boundary classes

| Class | Exact blobs | Workflow tip instances | Differing behavior |
|---|---:|---:|---|
| AAX | 3 | 20 | Manual licensed SDK route on persistent runner |
| CI-early | 2 | 6 | Hosted CI; push/PR only; no manual dispatch |
| CI-hosted-v4 | 6 | 29 | Hosted validation; dispatch/PR/full-push gate; checkout v4 |
| CI-hosted-v6 | 7 | 8 | Hosted validation; dispatch/PR/full-push gate; checkout v6 |
| CI-signed-old | 6 | 8 | Public eSigner route; signed-only CodeSignTool preparation |
| CI-signed-java | 7 | 13 | Public eSigner route; signed-only Java and CodeSignTool preparation |

AAX V01/V03/V02 differ only by additional hosted fixture tests: V02 adds three tests versus V01 and submission-archive test versus V03; persistent-runner trust boundary is identical. Hosted CI variants differ in native/product preflight coverage and checkout v4/v6. Signed CI variants additionally differ in whether Java is provisioned; all retain identical unguarded secret-consuming env bindings. Exact blobs/actions/guards remain separately enumerated below and in the private per-step inventory; these are not inferred safe from a no-secret grep.

## Exact workflow blobs

| ID | Filename | Blob SHA | Lines | Tip instances | Class |
|---|---|---|---:|---:|---|
| V01 | .github/workflows/aax-phase-a.yml | `52ac9fbeab315fd702c26dd6a49c81421367ddee` | 97 | 1 | AAX |
| V02 | .github/workflows/aax-phase-a.yml | `763dd96db11f2f37ae816d2593050e094685b2c3` | 100 | 14 | AAX |
| V03 | .github/workflows/aax-phase-a.yml | `b9f8b5626af71a666dbf811ed13cbd99405dd817` | 99 | 5 | AAX |
| V04 | .github/workflows/ci.yml | `05edeef55093ff6b2e4a7d326d7e4bfb53c6861a` | 30 | 5 | CI-early |
| V05 | .github/workflows/ci.yml | `0c98e54a67537e46b3189c46cc86d617654549c8` | 206 | 5 | CI-hosted-v4 |
| V06 | .github/workflows/ci.yml | `11479d35bec0a18716ef2d7e6930724a741804d4` | 508 | 1 | CI-signed-java |
| V07 | .github/workflows/ci.yml | `31c8d7fd917050b6c5ae7cc197421a65139fa5aa` | 92 | 1 | CI-early |
| V08 | .github/workflows/ci.yml | `3396520fc64531379d990509813f8575da287666` | 252 | 1 | CI-hosted-v6 |
| V09 | .github/workflows/ci.yml | `478211a7bc841c363fa43831fb27d4619f65e457` | 225 | 4 | CI-hosted-v4 |
| V10 | .github/workflows/ci.yml | `483e7b7fccd121dfc3a9cd0f734dd363707bcd72` | 504 | 3 | CI-signed-java |
| V11 | .github/workflows/ci.yml | `73f0b1ec90e0f634e2d16f6c9ee7540edee92f5b` | 250 | 7 | CI-hosted-v4 |
| V12 | .github/workflows/ci.yml | `75ea543994768fb96de2ac8bf7b9ecc106b7baa9` | 267 | 1 | CI-hosted-v6 |
| V13 | .github/workflows/ci.yml | `7aff9d9ffd0c2d253f2723b861704333607fcb29` | 434 | 3 | CI-signed-old |
| V14 | .github/workflows/ci.yml | `8173fa56fff28902c73ed930fdf56c2cc4ec3599` | 232 | 7 | CI-hosted-v4 |
| V15 | .github/workflows/ci.yml | `826ace4eb44708985a9ce530c4616fe3a92521f5` | 434 | 1 | CI-signed-old |
| V16 | .github/workflows/ci.yml | `8812d928d7a0f30569bcac9af9100885c53b6bd3` | 451 | 1 | CI-signed-old |
| V17 | .github/workflows/ci.yml | `96f3257d8219e033f0418094e5e328cf5c1de2b1` | 236 | 1 | CI-hosted-v6 |
| V18 | .github/workflows/ci.yml | `9ac31ba9a37fc8ddce67ebc748ba44623764982b` | 504 | 3 | CI-signed-java |
| V19 | .github/workflows/ci.yml | `a615841366fbd393e579c930456432d134656f23` | 246 | 1 | CI-hosted-v6 |
| V20 | .github/workflows/ci.yml | `b2ff392b42759e17acfc8026d1193cb45a33fa01` | 246 | 1 | CI-hosted-v4 |
| V21 | .github/workflows/ci.yml | `c14cd1adeeaa82110389aa6058707243a67ee732` | 375 | 1 | CI-signed-old |
| V22 | .github/workflows/ci.yml | `c69a47e58c634fa9582085614744a776c2951664` | 254 | 5 | CI-hosted-v4 |
| V23 | .github/workflows/ci.yml | `c7c2caaea13a9dcf1688446a8b1dd9545d485baa` | 492 | 3 | CI-signed-java |
| V24 | .github/workflows/ci.yml | `c884bbce007cc8e3dcd0f52825b90f62ea764334` | 240 | 1 | CI-hosted-v6 |
| V25 | .github/workflows/ci.yml | `cbb925a90afa8037732d76f7937dbd4656894e89` | 508 | 1 | CI-signed-java |
| V26 | .github/workflows/ci.yml | `d1c0e3711bc13148d8d161660c91d3aff65c2139` | 508 | 1 | CI-signed-java |
| V27 | .github/workflows/ci.yml | `d44def1bb69858e47bd8025d5b7131aa643675ae` | 236 | 1 | CI-hosted-v6 |
| V28 | .github/workflows/ci.yml | `dcb560169ab813574aca530b4eff440a96c111b1` | 464 | 1 | CI-signed-java |
| V29 | .github/workflows/ci.yml | `dea1dfb9a4c5b361f3ca422765c1f5dfcd013c67` | 427 | 1 | CI-signed-old |
| V30 | .github/workflows/ci.yml | `f37bf39936f935059c2dad7881443ccfee7645c2` | 434 | 1 | CI-signed-old |
| V31 | .github/workflows/ci.yml | `f72e1e9b37ee9ba4e0b73e0e73a6c1f3f5245c8e` | 254 | 2 | CI-hosted-v6 |

## Secret references (identifiers and exact source lines only)

Only dot notation occurs; no secrets[index] or unresolved dynamic index is present. All references are step env in `windows-vst3-preflight`, installer step `Build Windows installer and sign all executable surfaces`. Shared job guard: workflow_dispatch OR pull_request OR head commit `[ci full]`. Shared secret step guard: none. Shared environment declaration: none.

| Variant | ESIGNER_USERNAME | ESIGNER_PASSWORD | ESIGNER_CREDENTIAL_ID | ESIGNER_TOTP_SECRET |
|---|---:|---:|---:|---:|
| V06 | 453 | 454 | 455 | 456 |
| V10 | 449 | 450 | 451 | 452 |
| V13 | 379 | 380 | 381 | 382 |
| V15 | 379 | 380 | 381 | 382 |
| V16 | 396 | 397 | 398 | 399 |
| V18 | 449 | 450 | 451 | 452 |
| V21 | 320 | 321 | 322 | 323 |
| V23 | 437 | 438 | 439 | 440 |
| V25 | 453 | 454 | 455 | 456 |
| V26 | 453 | 454 | 455 | 456 |
| V28 | 409 | 410 | 411 | 412 |
| V29 | 372 | 373 | 374 | 375 |
| V30 | 379 | 380 | 381 | 382 |

CodeSignTool download is guarded by workflow_dispatch AND windows_signing == signed in all secret-consuming variants. Java has the same guard where present. Installer build/verification/ZIP derive signing mode from windows_signing with unsigned default; external validation input can select complete for installer. GITHUB_TOKEN is explicitly passed only to verified Inno Setup download in these 13 CI blobs (JSON records exact lines), and inherited implicit tokens remain available to Actions. Token is not a signing credential. A mutable action or changed candidate script can misuse accessible data; masking is not a trust boundary. [GitHub secret and action guidance](https://docs.github.com/en/actions/reference/security/secure-use).

AAX persistent runner route: V01 job if line47/runner55, V02 if50/runner58, V03 if49/runner57. All use `[self-hosted, aax-sdk, matrix.runner_os]`, default event checkout, and no environment declaration. Manual SDK/path/license inputs have the same names in all three variants (private per-step inventory); no actual SDK path values are recorded.

Historical Action refs: actions/checkout@v4 or @v6, dtolnay/rust-toolchain@stable, Swatinem/rust-cache@v2, actions/upload-artifact@v7 where uploads exist, actions/setup-java@v4 only in CI-signed-java. Full per-step line/guard inventory is in the private per-step inventory. No full-SHA pin exists in these inventoried public tip workflows.

## Every public ref and actual workflow filenames

CI = .github/workflows/ci.yml; AAX = .github/workflows/aax-phase-a.yml. Only API main is marked protected in branch inventory. The local hardened candidate is not one of these published tip snapshots.

| Type | Public ref | Exact commit | Workflow variants |
|---|---|---|---|
| branch | `backup/feature-pre-rebase-20260517_193855` | `04900e93efc4a2736a477d2aa9c160302a5592a6` | CI V04 |
| branch | `backup/feature-snapshot-20260517_193855` | `04900e93efc4a2736a477d2aa9c160302a5592a6` | CI V04 |
| branch | `backup/main-pre-rebase-20260517_193855` | `a318639b98b63ac00deb3245cf2134e678bd5805` | CI V04 |
| branch | `claude/gracious-pasteur-5b5452` | `c71c12e9b1fd08d23982db69cd723975643607e0` | AAX V02, CI V23 |
| branch | `claude/hypha-abcv-h` | `3d74286f7635bdbdaab9b3681948f830aa78351d` | AAX V02, CI V06 |
| branch | `claude/hypha-drum-band-mock` | `283ab7ad42efcecb35e772f45bb318e2822efebe` | AAX V02, CI V18 |
| branch | `claude/hypha-drum-band-summary` | `95da298b9129ee5291b60ccbd411c9b9e0fb9350` | AAX V02, CI V18 |
| branch | `claude/hypha-light-stage2` | `20d7dcd56543430ae51c1532277a94f15a6beec2` | AAX V02, CI V10 |
| branch | `claude/hypha-protools-windows-check` | `37396101f0d13fc4383b2334fd78474409483291` | AAX V02, CI V18 |
| branch | `claude/local-main-reconcile` | `5364e6292aa1326c5e265a8e9cc00d100f2eda88` | AAX V02, CI V23 |
| branch | `codex/b896-observatory-background-recovery` | `5200f8817c8adfd12bfe95531c7409205b0ecb7d` | AAX V03, CI V13 |
| branch | `codex/b897-observatory-background-current` | `e7db39d225fce028f465616f279eb94dcd24ce3a` | AAX V03, CI V13 |
| branch | `codex/hypha-aax-cka` | `0093a4f339d107fd1a4e49cfa9ef8e538b887893` | AAX V01, CI V29 |
| branch | `codex/hypha-aax-qualification` | `7ad511a6556b4d4268a9155b55e9ee8192b87568` | AAX V02, CI V10 |
| branch | `codex/hypha-one-pass-blind` | `91079ae7cf200ac511f26fe1341f5541016d315c` | AAX V02, CI V10 |
| branch | `codex/hypha-perceptual-continuous` | `ad8aad129ede7679b6c9907508818aa51c15e598` | CI V08 |
| branch | `codex/hypha-ref-simple-abc` | `6ba9fe20e681d369c1de08e1c74b1ef2406dc010` | AAX V02, CI V25 |
| branch | `codex/hypha-reference-b-preparation` | `e0f2cc9dbb3517a4a92bc5e40cee8ef5c340192d` | AAX V02, CI V23 |
| branch | `codex/local-recovery-20260915` | `05d347ad19da96917c42ebd68ce55393753b40e8` | AAX V03, CI V15 |
| branch | `codex/reference-abc-delivery` | `0aab301373c511ff6813a01363dbd99630079a69` | AAX V03, CI V30 |
| branch | `codex/reference-library-receiver` | `9b2c827448931fda24abd85b5cd9a570fdc10269` | AAX V03, CI V13 |
| branch | `feature/hypha-pre-post-stabilization` | `05a1a63d25bef1607faafa6978b4ea3467c874e0` | CI V04 |
| branch | `fix/b1007-modern-codesigntool-java` | `99ea232ff94e06e7b73e0b17d07a4a3538c7425d` | AAX V02, CI V28 |
| branch | `main` | `a8f5a4a4a791cd4a81346e2c5462d08269619b16` | AAX V02, CI V26 |
| tag | `v1.1.50` | `3d2234ec78ba5924d5db92fd88498bec64233b5c` | AAX V02, CI V16 |
| tag | `v1.1.49` | `c9e458d1e98c43f9028afd974518a298cca94108` | CI V21 |
| tag | `v1.1.48` | `992cb3094010b6eda44ce0d486ad01a0e94a9b54` | CI V12 |
| tag | `v1.1.47` | `b965ea12a90152038b01949c1ef8e2cea99dc26a` | CI V31 |
| tag | `v1.1.46` | `9d27808dd10d6c859340d0f9aec093d0bd8db4cf` | CI V31 |
| tag | `v1.1.45` | `5ba9bf5e3bcfc1df230f93ca933f39213477d446` | CI V19 |
| tag | `v1.1.44` | `6baa82d81575f0019412c4f9d5da79c8708f3661` | CI V24 |
| tag | `v1.1.43` | `5b1e8a9deb2bc41f7fa5631e92037c0f8a1fdf6f` | CI V27 |
| tag | `v1.1.35` | `547d7767f802b61e447f953d9887ca6253818170` | CI V17 |
| tag | `v1.1.34` | `eb99f1ad17e54dc32b8ad959e1798193b6f39d1b` | CI V14 |
| tag | `v1.1.33` | `019dc3fa85c4932605a2837e6c6b55adfb3c3bb9` | CI V14 |
| tag | `v1.1.30` | `58c975fba5489fb2612674169421bf483ca48fae` | CI V14 |
| tag | `v1.1.29` | `cc54e9ec121de0449d8ff91f99edbc21a47093e5` | CI V14 |
| tag | `v1.1.28` | `3e611b545dc3f1ca2bd85b065b264c702a7bc3dd` | CI V14 |
| tag | `v1.1.27` | `d861b8dba1b5228573b93497e4b5def67f75b2fc` | CI V14 |
| tag | `v1.1.26` | `a5ec2e11a7c933709a7e09e82088e121405b9b38` | CI V14 |
| tag | `v1.1.25` | `fc68e87795b2741e4eaf938c1f36ce1ce7a61a4e` | CI V22 |
| tag | `v1.1.24` | `a34e1e517b02ad7be8ae82327d47e782b7452195` | CI V22 |
| tag | `v1.1.23` | `724de70315f9f21aa754f39fc963070c073acfd0` | CI V22 |
| tag | `v1.1.22` | `1d8a10dc45d72f5bb8df65e8beff2d2fb65046c4` | CI V22 |
| tag | `v1.1.21` | `9a2e6f0062af0aa06f26522ddf00ed7b77c1a2c4` | CI V22 |
| tag | `v1.1.20` | `645fd4f3901428cc655dcf9972070cb16a5da912` | CI V11 |
| tag | `v1.1.19` | `51c7111151894409292c8710f92985b213d1f76d` | CI V11 |
| tag | `v1.1.18` | `c5a32b3c0acc64e5158c1471cbac0ca8e42610bd` | CI V11 |
| tag | `v1.1.17` | `f30051606d1eb9d36d50ae2bbd6f8605f32cca8b` | CI V11 |
| tag | `v1.1.16` | `e2e3e7409ae0bcd7d91dbe89a5aee9ab7b7676f8` | CI V11 |
| tag | `v1.1.15` | `199259360669a6b083a5c4d6dfcbb32f8bb49b90` | CI V11 |
| tag | `v1.1.14` | `aede33bcbb34c796272f8660dfdbbedbb2aa396b` | CI V11 |
| tag | `v1.1.13` | `e31656f9a6d87b151e2dee4ade62e3220af51563` | CI V20 |
| tag | `v1.1.12` | `03597bce44c3483aa146bda0bfa3f368f0b346d3` | CI V09 |
| tag | `v1.1.11` | `7fae45ad57be7b5e754bfa46aa3083c9b73ce173` | CI V09 |
| tag | `v1.1.10` | `bb304753e7a2737d9289cba78966531c4244815a` | CI V09 |
| tag | `v1.1.9` | `5e9c5fda19333ae454b2f61bd2260a0a52ed06f8` | CI V09 |
| tag | `v1.1.7` | `f5ac789e1fdac702de2d1fb51e84b831a415fe38` | CI V05 |
| tag | `v1.1.6` | `2f9554859227cbf4b3dd0e93be80e4c7e21a5b34` | CI V05 |
| tag | `v1.1.5` | `15b2cc7b1f11de7aef40e1652f88bf09f0c680fb` | CI V05 |
| tag | `v1.1.4` | `9aed8ec20fcfa7066f056ee1b5a9b44d1aaa29d6` | CI V05 |
| tag | `v1.1.3` | `6c4d71c462ecd43cb43ab4e18c2b0dfebfe00fd9` | CI V05 |
| tag | `v1.1.1` | `83738961e899f2276487a3353cf8c5820674f38e` | CI V07 |
| tag | `v1.0.0` | `81aefb70fadcca10702065f756f5cc1e01695430` | CI V04 |

## Verification/limits

Commands: local `git rev-parse <API_SHA>^{commit}`, `git ls-tree -r <commit> -- .github/workflows`, `git cat-file blob <workflow_blob>`; YAML.safe_load + Psych AST traversed complete documents, including triggers, job/step guards, env scope and permissions, and exact scalar line ranges. Exact blob de-duplication avoids reclassifying identical source. The first private parser attempt failed because system Ruby lacks Enumerable#tally; a compatible grouping implementation then parsed all 31 blobs. This was a read-only parser correction, not repository test execution.

This pass does not validate runtime behavior of every historical script/tool or past secret configuration, private factory enforcement, all historical commits between tip snapshots, or refreshed GitHub settings. It makes no safe/rotation conclusion from reference absence alone. No branch/tag deletion, history rewrite or mass forward-integration was performed. Any future credential/runner addition or migration of remaining active branches must review these old routes first; immutable tags remain subject to the global isolation boundary.


## Final supplemental link/role findings — 2026-10-05

Five links in one changed technical plan still escape into a historical sibling checkout. They were already broken in baseline main. All five destination documents are tracked publicly in current main/candidate, and the B-885/B-887 headers were read to confirm matching evidence scope and honest DAW/Windows limits. Private directory values are intentionally omitted.

| Source location (baseline/current) | Category / public need | Verified public relative target | Action / side effect | History / other public surfaces |
| --- | --- | --- | --- | --- |
| docs/hypha_pre_post_blind_usability_plan_20260914.md:41 / 41 | Private sibling-checkout path; public technical evidence/contract reference needed | reference_capture_b887_implementation_20260914.md | Replace only target with same-directory relative link; keep displayed label, commit/hash/outcomes and every unverified/acceptance limit | H1 topology remains in previous commits and historical source archives/PR diffs; exact Release/Actions/generated spread unconfirmed |
| docs/hypha_pre_post_blind_usability_plan_20260914.md:44 / 44 | Private sibling-checkout path; public technical evidence/contract reference needed | hypha_capture_and_workflow_integrated_plan_20260914.md | Replace only target with same-directory relative link; keep displayed label, commit/hash/outcomes and every unverified/acceptance limit | H1 topology remains in previous commits and historical source archives/PR diffs; exact Release/Actions/generated spread unconfirmed |
| docs/hypha_pre_post_blind_usability_plan_20260914.md:45 / 45 | Private sibling-checkout path; public technical evidence/contract reference needed | reference_capture_workflow_implementation_20260914.md | Replace only target with same-directory relative link; keep displayed label, commit/hash/outcomes and every unverified/acceptance limit | H1 topology remains in previous commits and historical source archives/PR diffs; exact Release/Actions/generated spread unconfirmed |
| docs/hypha_pre_post_blind_usability_plan_20260914.md:53 / 53 | Private sibling-checkout path; public technical evidence/contract reference needed | reference_capture_b885_structural_repair_plan_20260914.md | Replace only target with same-directory relative link; keep displayed label, commit/hash/outcomes and every unverified/acceptance limit | H1 topology remains in previous commits and historical source archives/PR diffs; exact Release/Actions/generated spread unconfirmed |
| docs/hypha_pre_post_blind_usability_plan_20260914.md:258 / 258 | Private sibling-checkout path; public technical evidence/contract reference needed | hypha_invariants.md | Replace only target with same-directory relative link; keep displayed label, commit/hash/outcomes and every unverified/acceptance limit | H1 topology remains in previous commits and historical source archives/PR diffs; exact Release/Actions/generated spread unconfirmed |

AGENTS.md:305 baseline / 253 current: numeric-font guidance retains an internal individual-operator role label. H1 low-impact internal wording cleanup: use the documented design specification as the font authority. Preserve the default system-font choice and all visual requirements. Historical comments/source archives retain the prior label; no credential/rotation issue.

Recorded before the final five-link/font-authority cleanup. No source/build/test/CI/settings action. All destination paths above are current tracked public paths.


Final supplemental document verification: 95 changed docs/guides and 330 local Markdown targets
checked against the base, including historical sibling-checkout escapes. Broken targets and links
escaping the public repository both zero. The five narrow target replacements preserve labels,
commit/hash/numeric multisets, evidence scope and all DAW/Windows/unverified acceptance limits.
All six tracked legal LICENSE/NOTICE files and ten test WAVs are byte-identical to base; original
creator attribution remains. Invariant numeric multiset and PNG nonmetadata/IDAT are unchanged.
No product behavior, build/CI setting or signature threshold changes in this supplemental hygiene pass.


## Bare historical checkout nickname classification — 2026-10-05

Read-only context inspection of owned tracked docs/root guides found three remaining bare local-checkout nickname occurrences, each next to a public B identifier and full source commit. The prior absolute/sibling-path usages identify the same token as a checkout-directory label. No currently registered Git worktree has that basename; this review does not claim that old checkout still exists. The read-only public API inventory contains 24 branches, with no exact name or branch-basename match. No branch/API inventory is to be altered. Both referenced source commits exist locally and are ancestors of baseline public main.

| File / original and current line | Category / public necessity | Recommended minimal action | Side effects / history / public surfaces |
| --- | --- | --- | --- |
| docs/hypha_comparison_safety_contract_20260914.md:43 / 43 | H1 historical local checkout identity; public source B/full commit is required, checkout nickname is unnecessary | Remove only checkout nickname; preserve B number, full commit and every source/evidence, native-vs-host, unresolved/acceptance condition | Documentation only. Historical commits/source archives/PR diffs may retain the nickname; exact Release binary/Actions/generated spread unconfirmed. Rotation is not indicated; history rewrite remains deferred for the user's judgment. |
| docs/hypha_pre_post_blind_usability_plan_20260914.md:40 / 40 | H1 historical local checkout identity; public source B/full commit is required, checkout nickname is unnecessary | Remove only checkout nickname; preserve B number, full commit and every source/evidence, native-vs-host, unresolved/acceptance condition | Documentation only. Historical commits/source archives/PR diffs may retain the nickname; exact Release binary/Actions/generated spread unconfirmed. Rotation is not indicated; history rewrite remains deferred for the user's judgment. |
| docs/reference_listening_workflow_plan_20260914.md:41 / 41 | H1 historical local checkout identity; public source B/full commit is required, checkout nickname is unnecessary | Remove only checkout nickname; preserve B number, full commit and every source/evidence, native-vs-host, unresolved/acceptance condition | Documentation only. Historical commits/source archives/PR diffs may retain the nickname; exact Release binary/Actions/generated spread unconfirmed. Rotation is not indicated; history rewrite remains deferred for the user's judgment. |

Public branch names, main and their API inventory are H0 public Git evidence; preserve. Public repo/project names and full commit identifiers also stay. These three before-edit rows were saved before the narrow forward cleanup.

No credentials, product/source edits, build/test/CI/settings actions or commits performed. Private scalar values are omitted.


Final bare-nickname cleanup result: three recorded document locations now use neutral public-history
wording; B identifiers and full public source commits are preserved. Combined final supplement changes
four guides/documents and nine lines. Non-link numerical/full-hash token sets remain identical; public
branch inventory remains unchanged. Discovery suite rerun after these edits: 5/5 PASS; diff check PASS.


Final fresh read-only setting checks: remote main still protected at the original `a8f5a4a4`
base; PVR enabled; authenticated account primary email verified with visibility private (values
omitted). Repository Actions secrets, runners and environments each report total_count 0. No further
settings changes, CI trigger or publication occurred. Non-primary email/UI block-push privacy and
private external factory settings remain outside these confirmations.


## Final local validation — executed commands and results

The repository-defined source gate was executed from this isolated candidate checkout:
`CMAKE_BUILD_PARALLEL_LEVEL=2 bash scripts/test_release_source.sh`. Exit **0**, final
`release source contract: PASS`. This includes the real lightweight/source/typography/screen/research/
release-metadata/installer fixtures, two C++ contract builds, the exact tracked JUCE patch stack,
35 optimized native build targets, selected CTest inventory **57/57 PASS** (834.16 seconds), the
following Rust commands, required eight exported C ABI definitions, and both owned clippy gates.
Counts below are actual summed result lines for each command (including integration/doc tests),
not estimates from test source. Normal ignored counts remain explicit. Required FFI ignored suite
inventories were measured as parity 20 and pairing_candidates 6 before their serial runs.

| Actual command | Result |
| --- | --- |
| `cargo fmt --all -- --check` | PASS, exit 0 within completed source gate |
| `cargo test -p kirin_measure --locked` | PASS: 1701 passed / 0 failed / 15 ignored |
| `cargo test -p kirin_hypha_ffi --locked` | PASS: 179 passed / 0 failed / 26 ignored |
| `cargo test -p hypha_pre -p hypha_post --locked` | PASS: 111 passed / 0 failed / 0 ignored |
| `cargo test -p vst3-com --locked --test vtable_expression` | PASS: 1 passed / 0 failed / 0 ignored |
| `cargo test -p kirin_measure --release --locked one_visible_pair_continuous_sharpness_worker_budget_is_quantified --lib -- --ignored --nocapture` | PASS: 1 passed / 0 failed / 0 ignored |
| `cargo test -p kirin_measure --release --locked two_post_absolute_workers_fit_the_optional_analysis_budget --lib -- --ignored --nocapture` | PASS: 1 passed / 0 failed / 0 ignored |
| `cargo build -p kirin_hypha_ffi --locked` | PASS, exit 0 within completed source gate |
| `cargo test -p xtask --locked` | PASS: 166 passed / 0 failed / 0 ignored |
| `cargo test -p kirin_hypha_ffi --test parity --locked -- --ignored --test-threads=1` | PASS: 20 passed / 0 failed / 0 ignored |
| `cargo test -p kirin_hypha_ffi --test pairing_candidates --locked -- --ignored --test-threads=1` | PASS: 6 passed / 0 failed / 0 ignored |
| `cargo clippy -p kirin_measure -p kirin_hypha_ffi -p xtask --all-targets --locked -- -D warnings` | PASS, exit 0 within completed source gate |
| `cargo clippy -p hypha_pre -p hypha_post --all-targets --locked -- -D warnings` | PASS, exit 0 within completed source gate |

Additional targeted checks actually executed:

| Actual command / concrete verification | Result |
| --- | --- |
| `node --test scripts/ci_security.test.mjs scripts/windows/windows_installer.test.mjs scripts/windows/inno_signing.test.mjs scripts/ls_release/release_metadata.test.mjs` | 45/45 PASS, including 9 negative security-policy cases |
| `node --test scripts/check_aax_sdk_absence.test.mjs scripts/ls_release/aax_distribution.test.mjs scripts/ls_release/aax_submission_archive.test.mjs` | 22/22 PASS; also executed within canonical gate |
| `node scripts/test_aax_cmake_gate.mjs` | 10 SDK-free cases PASS |
| `node scripts/test_build_aax_universal.mjs` | SDK-free dry-run PASS; also in canonical gate |
| `node scripts/check_aax_sdk_absence.mjs` | PASS, no licensed SDK committed |
| `node --test scripts/hypha_workflow_discovery.test.mjs` | 5/5 PASS, rerun after final AGENTS/link/nickname edits |
| `cargo run -p xtask --locked -- windows-preflight` | Exit 0, actual unsigned installer/verification source contract OK |
| `cargo fmt --all -- --check` | Final PASS |
| `bash scripts/check_source_line_budget.sh` | Final PASS: 29 legacy oversized files with exact ratchet, new source <=500 |
| `/tmp/hypha-actionlint-20261005/actionlint .github/workflows/ci.yml .github/workflows/aax-phase-a.yml` (verified official v1.7.12 binary) | PASS; shellcheck is unavailable, not represented as passed |
| Ruby YAML parse of both workflows and Dependabot; full historical workflow AST review | PASS, 31 distinct historical blobs parsed; source-policy negative tests complement parser |
| `.gitignore` secret/local-state and public-template exceptions | 17 checks PASS; public examples/audit/.cargo not inadvertently ignored |
| Changed-doc links and source/numeric/provenance checks | 95 guides/docs / 330 local links, 0 broken or repository-escaping; supplemental non-link numeric/full-hash sets identical |
| PNG chunks/CRC/IDAT/decompressed pixels | Metadata only: 441 bytes removed; same 2048×2048 RGB picture and 12,582,912 decoded pixel bytes |
| Latest Release checksums/ZIP CRC/PKG gzip CRC and matching PKG/ZIP binaries | PASS for three downloaded assets; signature/notarization and Windows compressed payload not independently validated |
| `git diff --check` / isolated submodule state | PASS; verified/reversed only the 11 test patch-stack changes, restored only own line-ending residues after logical HEAD equivalence. JUCE pristine, gitlink unchanged. |

Initial attempts and actual failures are not hidden: lightweight fmt-check first failed while guard
edits were in progress; final fmt/canonical gate passed. The first new JavaScript security test had a
syntax error, corrected before its 45/45 final run. Two old AAX assertions expected the removed public
SDK runner/input route; those assertions were updated to the hosted/local boundary and final 22/22
passed. The first serial native compilation was intentionally interrupted with exit 130 after partial
progress, only this task's process group; the bounded two-worker retry reused object files and passed.
No other session/process was canceled. A private read-only Ruby parser initially lacked Enumerable
`#tally`; compatible grouping then covered every blob. A final narrow documentation substitution
assertion stopped on a plain-text occurrence after two code-formatted occurrences; actual formatting
was inspected, the third authorized location corrected, and all token/diff/discovery checks passed.
No threshold/deadline or product acceptance gate was weakened to obtain these results.

No new Actions run/dispatch/rerun or candidate artifact was created or reused for acceptance. Four
pre-existing runs were inspected for log exposure only: 37077361064 / 37228898432 / 36860903973 /
36860907827. Their success is not attributed to the new candidate. No budget/capacity setting or
artifact retention/deletion changed; no claimed cost saving. GitHub required exact-candidate checks,
macOS arm64 AU/pluginval, Windows hosted build/pluginval/lifecycle, licensed AAX host/SDK/signing,
private factory acceptance, actual signature/notarization and public three-channel release are
**unperformed/unconfirmed**, not PASS. LS/HP package readiness is skip because this task did not
execute release/sign/install/publication. Local source-gate success does not substitute those gates.

## Review and outstanding decisions

PR-A is a local forward branch (`codex/hypha-public-hygiene-20261005`, main base → `d27e1638`),
with five commits B-1206 through B-1210: audit before cleanup, public/private contributor separation,
personal cloud operator note removal, private storage-root removal, and repaired public evidence
links/historical checkout nickname removal. PR-B is the separate local hardening branch based on
PR-A; its final commit is recorded by Git/the final report. No direct main push or public PR creation.
Original developer-checkout changes are outside this worktree and preserved.

Owner decisions remain: H1 history remediation and its clone/tag/PR/archive side effects; existing
Release binary home strings and future all-producer path normalization; privacy vs current explicit
trusted-origin allowlists/provenance; independent review/branch/Actions/secret-scanning/push-protection
settings without blocking sole-maintainer development; private factory integration verification;
permanent private operations-document destination and any further broad artifact/log inspection.
No H2 credential identified in inspected scope, so no rotation requirement established. Future actual
credential findings must still stop, redact values and prioritize revoke/rotation.

Originals and private review/evidence copies are outside the public checkout; no internal.md or
private credential file was added. Session-record runbook honors the project Notion-write prohibition:
current/daily/Handoff contents will be preserved privately in that order, and Notion remains unrecorded.
The private originals are a local transfer backup, not a claim of completed permanent private migration.

## Stage2 factual precision and forward decisions

Stage2 adds the [12-ID H1 decision matrix](public_repo_h1_decision_matrix_20261005.md), [full A manifest](public_hygiene_change_manifest_20261005.md), [binary plan](binary_path_normalization_plan_20261005.md), [signing boundary](signing_trust_boundary_20261005.md), [fresh GitHub baseline](github_repository_security_baseline_20261005.md), and [review packet](security_hardening_review_packet_20261005.md). No history rewrite, tag/Release mutation or production flags were applied.

Precision correction: earlier six-file license inventory used bare LICENSE names plus license-engine source. The complete tracked inventory is seven LICENSE texts (including baseview LICENSE-APACHE and LICENSE-MIT) plus `crates/kirin_measure/src/license.rs`, eight relatedfiles total, all byte-identical across a8→d27. There is no tracked NOTICE file. This corrects count wording; no legal file changed.

The prior 24-branch/40-tag snapshot remains the coverage basis. Stage2 cached origin/claude/hypha-abcv-h advanced from snapshot3d74286f to a5bb2234; both objects exist and the H1 table uses the snapshot SHA. Fresh API main remains a8. Do not interpret the snapshot as allrefs current at the later timestamp.
