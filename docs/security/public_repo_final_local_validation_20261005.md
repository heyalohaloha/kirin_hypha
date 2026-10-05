# Stage2 final local validation — 2026-10-05

Candidate at start: `1364b9e5e2c31bc643b9a50761d786b1e87ed663`, clean parent worktree before the gate. All functional CI/guard/test source is byte-identical to stage1 B; forward stage2 additions are documentation. The canonical source gate is rerun on this candidate rather than substituting prior PASS. It materializes only the tracked JUCE patch stack for native tests.

| Actually executed command | Stage2 result |
|---|---|
| `CMAKE_BUILD_PARALLEL_LEVEL=2 bash scripts/test_release_source.sh` | exit0,release source contract PASS;35 optimized native targets,CTest57/57,834.06seconds |
| `node --test scripts/ci_security.test.mjs scripts/windows/windows_installer.test.mjs scripts/windows/inno_signing.test.mjs scripts/ls_release/release_metadata.test.mjs` | exit0,45 pass,0 fail,0 skip,including9 security-policy cases |
| `node --test scripts/hypha_workflow_discovery.test.mjs` | exit0,5 pass,0 fail,0 skip |
| `node scripts/test_aax_cmake_gate.mjs` | exit0,10 SDK-free cases |
| `cargo run -p xtask --locked -- windows-preflight` | exit0,actual unsigned installer/verification source gate OK |
| `cargo fmt --all -- --check` | exit0 |
| `bash scripts/check_source_line_budget.sh` | exit0,29 legacy oversized exact ratchets,new source <=500 |
| `/tmp/hypha-actionlint-20261005/actionlint .github/workflows/ci.yml .github/workflows/aax-phase-a.yml` (verified official1.7.12) | exit0; shellcheck unavailable,not represented as passed |
| Ruby YAML parse of both workflows and Dependabot | exit0,3 mappings parsed; actionlint/source negatives provide separate semantics validation |
| Fixed HEAD changed-Markdown local path resolution |102files/354targets,0missing/escaping; external URLs/anchors not all validated |
| Rust/Clang synthetic path-remap fixture |4compiles+4executions succeeded; shipping/Windows/arm64/private signing not tested |
| Fixed-tree legal/audio/image/producers independent preservation checks |7LICENSE texts+license.rs=8identical,10WAVidentical,18RTrows/13producers identical,PNG pixel/content unchanged |
| New reachable commit identity |10commits at tested HEAD,all author/committer noreply; no personal email values output |

Counts above are actual observed results, not invented inventories. The canonical gate's measured ignored/CTest inventory assertions and actual results are recorded below. Upstream JUCE/native and vendored Rust diagnostics are retained in the private raw log; owned clippy uses -D warnings.

## Canonical subcommands and actual results

| Command actually executed within the canonical gate | Result |
|---|---|
| `cargo test -p kirin_measure --locked` | 1701 pass / 0 fail / 15 ignored across 19 result blocks |
| `cargo test -p kirin_hypha_ffi --locked` | 179 pass / 0 fail / 26 ignored across 11 result blocks |
| `cargo test -p hypha_pre -p hypha_post --locked` | 111 pass / 0 fail / 0 ignored across 5 result blocks |
| `cargo test -p vst3-com --locked --test vtable_expression` | 1 pass / 0 fail / 0 ignored across 1 result blocks |
| `cargo test -p kirin_measure --release --locked one_visible_pair_continuous_sharpness_worker_budget_is_quantified --lib -- --ignored --nocapture` | 1 pass / 0 fail / 0 ignored across 1 result blocks |
| `cargo test -p kirin_measure --release --locked two_post_absolute_workers_fit_the_optional_analysis_budget --lib -- --ignored --nocapture` | 1 pass / 0 fail / 0 ignored across 1 result blocks |
| `cargo build -p kirin_hypha_ffi --locked` | exit0 |
| `cargo test -p xtask --locked` | 166 pass / 0 fail / 0 ignored across 1 result blocks |
| `cargo test -p kirin_hypha_ffi --test parity --locked -- --ignored --test-threads=1` | 20 pass / 0 fail / 0 ignored across 1 result blocks |
| `cargo test -p kirin_hypha_ffi --test pairing_candidates --locked -- --ignored --test-threads=1` | 6 pass / 0 fail / 0 ignored across 1 result blocks |
| `cargo clippy -p kirin_measure -p kirin_hypha_ffi -p xtask --all-targets --locked -- -D warnings` | exit0 |
| `cargo clippy -p hypha_pre -p hypha_post --all-targets --locked -- -D warnings` | exit0 |

The C ABI archive was built and all8 named required exported definitions in the canonical script passed its `nm`/defined-symbol assertions. Normal FFI ignores26 realtime cases; measured inventories were20parity/6pairing and both separate suites ran completely. The3 AAX absence/distribution/submission Node suites executed within the canonical command passed6+14+2=22 tests. SDK-free AAX dry-run, typography/screen/research/release/Windows source checks and pure C++ UI contracts also completed in that same exit0 gate. Raw command boundaries/results are archived; no fixed test count was invented as a substitute for execution.

## First failures / corrections

- Stage2 A initial `git diff --cached --check` flagged one extra EOF blank line. The shell did not stop before the document commit. It was corrected in a new forward B-1213 commit, together with explicit ZIP656 + PKG656 =1,312 count wording; no amend/rewrite occurred. Final diff checks must pass.
- Independent review caught incomplete legal inventory wording (strict bare LICENSE names missed two hyphenated LICENSE files). Complete fixed-tree enumeration is7texts+license.rs,all8byte-identical; public manifest/packet/audit corrected before canonical validation.
- Preliminary read-only review guessed one nonexistent producer filename. Reviewer corrected it using actual tracked inventory and repeated the static check; this was not a product test failure or repository mutation.
- Stage1 failures and authorized reruns are preserved in the pre-change audit. They are not silently reclassified as Stage2 failures or PASS.

## GitHub / platform boundaries

New CI run count0,reused acceptance run count0. Read-only observed old runs belong to other exact SHAs. GitHub4 required checks, arm64 AU and Windows executable/installer/host gates remain pending for this unpublished candidate. Local macOS Intel native source tests are not actual arm64 AU/Windows/private signing verification. No signing/notarization/install/release/version/tag/asset/main change was performed.

Canonical raw log and sanitized machine summaries are preserved outside public Git with private access modes. The final evidence-only documentation commit retains tracked executable source/recipe bytes. Rebuilding a later HEAD changes its generated source/build identity; native artifacts tested here belong to1364b9e5 and are not later-HEAD shipping evidence. Post-recording lightweight/history/diff/status checks apply to the final local HEAD. No later-HEAD GitHub CI is claimed. Final documents cannot be used as evidence that unrun external CI passed.

## Cleanup and decision precision

After the canonical gate, `bash scripts/verify_juce_patch_state.sh` confirmed pinned upstream plus exact11 tracked patches. They were reversed in reverse order with the same flags; only files proven byte-equal to HEAD after CRLF normalization were restored for EOL residue. JUCE and parent worktree were pristine before adding these records. No other checkout or another session process was modified.

Fresh closing GET confirmed main a8f5a4a4, strict4checks/admin enforcement,force/deletionfalse,approval0,publicrepo secrets/runners/environments0,PVRenabled,defaultread/tokenPRapprovalfalse. Main-specific PE01 commit counts areauthor1067/committer1004/uniqueaffected1067; allref PE01 counts1243/1118/1243 in the H1 table use a different declared denominator. No personal email value is included.

Final read-only reviewer verified exactA59f74b30→B1364b9e5 contains17files:original14B+3Bdocuments. All A hygiene outside those overlapping B paths remains identical;17 securitylinks and broader changed-document links resolved. This validation record makes the final B diff18files. Authors-independent Codex A/B reviews found no material new scoped defect; they are not human approval or private-factory/runtime acceptance.
