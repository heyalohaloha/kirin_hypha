use anyhow::{bail, Result};

const CI_WORKFLOW: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../.github/workflows/ci.yml"
));
const STABILITY_PLAN: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../docs/hypha_stability_plan_2026-06-26.md"
));
const RELEASE_SOURCE_GATE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../scripts/test_release_source.sh"
));
const PAIRING_CANDIDATES_TEST: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_hypha_ffi/tests/pairing_candidates.rs"
));
const PAIRING_SCOPE_TEST: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_measure/tests/pairing_scope_separation_test.rs"
));
const PAIR_OPERATION_GROUP: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_measure/src/pair_operation_group.rs"
));
const RECORD_SIGNAL_SEPARATION_TEST: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_measure/tests/record_signal_separation_test.rs"
));
const PLATFORM_STORAGE: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_measure/src/storage.rs"
));
const PLATFORM_PATHS_TEST: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../crates/kirin_measure/tests/platform_paths_separation_test.rs"
));
const SHELL_PARITY: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../xtask/src/shell_parity.rs"
));
const RT_SAFETY: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../xtask/src/rt_safety.rs"
));
const WINDOWS_PREFLIGHT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../xtask/src/windows_preflight.rs"
));
const WINDOWS_PREFLIGHT_TESTS: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../xtask/src/windows_preflight/tests.rs"
));
const WINDOWS_VST3_LAYOUT: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../xtask/src/windows_vst3_layout.rs"
));
const CI_USAGE_GUARD: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../xtask/src/ci_usage_guard.rs"
));
const JUCE_PATCH_VERIFY: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../scripts/verify_juce_patch_state.sh"
));
const JUCE_PATCHES_MD: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../juce_shell/PATCHES.md"
));

pub fn run(args: Vec<String>) -> Result<()> {
    match args.as_slice() {
        [] => {}
        [arg] if arg == "-h" || arg == "--help" => {
            print_usage();
            return Ok(());
        }
        [arg] => bail!("unknown argument: {arg}"),
        _ => bail!("unknown arguments: {}", args.join(" ")),
    }

    let checks = readiness_checks();
    let ready = checks.iter().filter(|check| check.ready).count();

    for check in &checks {
        let status = if check.ready { "ready" } else { "blocker" };
        eprintln!("{status:>7}  {:<28} {}", check.id, check.evidence);
        if let Some(detail) = &check.detail {
            eprintln!("         -> {detail}");
        }
    }

    if ready == checks.len() {
        eprintln!(
            "[windows-readiness] ready: {ready}/{} local structural gates for Windows VST3 are present.",
            checks.len()
        );
        return Ok(());
    }

    bail!(
        "Windows readiness blockers: {}/{} gates ready",
        ready,
        checks.len()
    )
}

fn print_usage() {
    eprintln!(
        "Usage: cargo run -p xtask -- windows-readiness\n\n\
         Static audit for the Windows VST3 handoff point. It verifies that the\n\
         pairing, restore-order, shell parity, RT-safety, platform path, JUCE patch,\n\
         CI usage, Windows artifact layout, and Windows preflight gates are all represented\n\
         in source."
    );
}

#[derive(Debug)]
struct Check {
    id: &'static str,
    evidence: &'static str,
    ready: bool,
    detail: Option<String>,
}

fn readiness_checks() -> Vec<Check> {
    vec![
        check_all(
            "pairing-discovery",
            "candidate visibility and pairing separation tests exist",
            &[
                (
                    PAIRING_CANDIDATES_TEST,
                    &[
                        "juce_candidate_abi_bridges_split_shell_claims_and_all_keep",
                        "split-shell PRE choices",
                        "kirin_hypha_enumerate_pre_candidates",
                    ][..],
                ),
                (
                    PAIR_OPERATION_GROUP,
                    &[
                        "operation_group_bridges_au_vst_shelves_by_exact_visible_pre",
                        "operation_group_rejects_other_host_and_nonvisible_exact_claim",
                        "host_process_id",
                    ][..],
                ),
                (
                    PAIRING_SCOPE_TEST,
                    &[
                        "pairing_scope_uses_post_candidates_not_io_thread_post",
                        "crate::post_candidates::scan_post_candidates_in",
                    ][..],
                ),
                (
                    RECORD_SIGNAL_SEPARATION_TEST,
                    &["record_signal_does_not_reexport_pairing_or_pre_candidates"][..],
                ),
            ],
        ),
        check_all(
            "restore-order",
            "PRE/POST state chunk restore is covered before and after enable",
            &[(
                PAIRING_CANDIDATES_TEST,
                &[
                    "restored_pair_target_before_enable_is_written_to_post_watch_json",
                    "restored_pair_target_after_enable_updates_post_watch_json",
                    "restored_pre_name_before_enable_is_written_to_pre_watch_json",
                    "restored_pre_name_after_enable_updates_pre_watch_json",
                ][..],
            )],
        ),
        check_all(
            "ffi-ignored-parity",
            "both slow FFI suites have pinned inventories and run through the CI release gate",
            &[
                (
                    CI_WORKFLOW,
                    &["bash scripts/test_release_source.sh"][..],
                ),
                (
                    RELEASE_SOURCE_GATE,
                    &[
                        "assert_ignored_count parity 20",
                        "assert_ignored_count pairing_candidates 5",
                        "cargo test -p kirin_hypha_ffi --test parity --locked -- --ignored --test-threads=1",
                        "cargo test -p kirin_hypha_ffi --test pairing_candidates --locked -- --ignored --test-threads=1",
                    ][..],
                ),
                (
                    STABILITY_PLAN,
                    &["cargo test -p kirin_hypha_ffi --test parity -- --ignored --test-threads=1"][..],
                ),
            ],
        ),
        check_all(
            "shell-parity",
            "JUCE POST exact-pair menu and fixed Keep slot have static parity tests",
            &[(
                SHELL_PARITY,
                &[
                    "post_controls_keep_slot_is_fixed_and_availability_depends_on_selected_pair",
                    "candidate_menu_enumerates_pre_candidates_independent_of_current_pair",
                    "candidate_selection_commits_exact_instance_and_updates_display_field",
                ][..],
            )],
        ),
        check_all(
            "rt-safety",
            "audio-thread FFI surface and blocking-operation bans are static gated",
            &[(
                RT_SAFETY,
                &[
                    "process_block_calls_only_rt_safe_ffi_surface",
                    "process_block_keeps_audio_thread_free_of_blocking_work",
                    "ffi_push_samples_core_avoids_io_allocation_and_blocking_locks",
                    "audio_thread_c_abi_wrappers_remain_thin",
                ][..],
            )],
        ),
        check_all(
            "platform-paths",
            "macOS and Windows storage roots are split behind PlatformPaths",
            &[
                (
                    PLATFORM_STORAGE,
                    &[
                        "pub fn default_platform()",
                        "pub fn current_kirin_tmp_root()",
                        "pub fn for_windows(",
                        "PlatformKind::Windows",
                    ][..],
                ),
                (
                    PLATFORM_PATHS_TEST,
                    &[
                        "storage_default_macos_is_wrapper_only",
                        "kirin_tmp_root_is_platform_helper_only_in_production_sources",
                    ][..],
                ),
            ],
        ),
        check_all(
            "juce-patch-state",
            "dirty JUCE submodule state is explainable as tracked patch stack",
            &[
                (
                    JUCE_PATCH_VERIFY,
                    &[
                        "EXPECTED_HEAD=\"4f43011b96eb0636104cb3e433894cda98243626\"",
                        "0001-macos15-sdk-cgwindowlistcreateimage-bypass.patch",
                        "0002-drop-webkit-osxframework-from-juce-gui-extra.patch",
                        "0003-au-preferred-channel-layout-tags-for-logic-mono.patch",
                    ][..],
                ),
                (
                    JUCE_PATCHES_MD,
                    &[
                        "bash scripts/verify_juce_patch_state.sh",
                        "dirty `juce_shell/JUCE` checkout is **only** this tracked patch",
                    ][..],
                ),
            ],
        ),
        check_all(
            "ci-usage",
            "full CI cannot resume consuming runner minutes on plain push",
            &[(
                CI_USAGE_GUARD,
                &[
                    "ci_workflow_gates_all_runner_jobs",
                    "github.event_name == 'workflow_dispatch'",
                    "contains(github.event.head_commit.message, '[ci full]')",
                ][..],
            )],
        ),
        check_all(
            "windows-vst3-preflight",
            "Windows VST3 build path and artifact capture are separated from macOS release gates",
            &[
                (
                    WINDOWS_PREFLIGHT,
                    &[
                        "verify_cmake_platform_split",
                        "verify_windows_ci_job",
                        "verify_ffi_staticlib_docs",
                        "verify_ci_uses_layout_artifact_paths",
                        "KirinHyphaPRE_VST3 KirinHyphaPOST_VST3",
                    ][..],
                ),
                (
                    WINDOWS_PREFLIGHT_TESTS,
                    &[
                        "preflight_requires_windows_staticlib_docs",
                        "preflight_requires_cargo_staticlib_docs",
                        "preflight_requires_windows_artifact_presence_check",
                        "preflight_requires_windows_binary_presence_check",
                        "preflight_requires_nonempty_windows_binary_check",
                        "preflight_requires_windows_artifact_upload_error_on_missing_files",
                        "preflight_requires_upload_paths_to_match_layout_paths",
                        "preflight_rejects_macos_release_step_in_windows_ci_job",
                    ][..],
                ),
                (
                    CI_WORKFLOW,
                    &[
                        "windows-vst3-preflight",
                        "runs-on: windows-latest",
                        "cargo run -p xtask --locked -- windows-preflight",
                        "Verify Windows VST3 artifacts",
                        "uses: actions/upload-artifact@v7",
                        "if-no-files-found: error",
                    ][..],
                ),
            ],
        ),
        check_all(
            "windows-vst3-layout",
            "Windows PRE/POST VST3 artifact and install roots are explicit",
            &[(
                WINDOWS_VST3_LAYOUT,
                &[
                    "windows_layout_uses_steinberg_user_dev_folder",
                    "windows_layout_uses_global_common_files_vst3",
                    "windows_layout_exports_ci_artifact_paths",
                    "windows_layout_exports_vst3_binary_paths",
                    "windows_layout_excludes_macos_and_au_paths",
                    "%LOCALAPPDATA%\\\\Programs\\\\Common\\\\VST3",
                    "%ProgramFiles%\\\\Common Files\\\\VST3",
                ][..],
            )],
        ),
    ]
}

fn check_all(id: &'static str, evidence: &'static str, groups: &[(&str, &[&str])]) -> Check {
    let missing: Vec<&str> = groups
        .iter()
        .flat_map(|(source, needles)| {
            needles
                .iter()
                .copied()
                .filter(move |needle| !source.contains(needle))
        })
        .collect();

    Check {
        id,
        evidence,
        ready: missing.is_empty(),
        detail: if missing.is_empty() {
            None
        } else {
            Some(format!("missing {}", missing.join(", ")))
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn windows_readiness_accepts_current_checkout() {
        let checks = readiness_checks();
        assert_eq!(checks.len(), 10);
        let blockers: Vec<_> = checks
            .iter()
            .filter(|check| !check.ready)
            .map(|check| check.id)
            .collect();
        assert!(blockers.is_empty(), "readiness blockers: {blockers:?}");
    }

    #[test]
    fn check_all_reports_missing_evidence() {
        let check = check_all("demo", "evidence", &[("abc", &["abc", "def"])]);
        assert!(!check.ready);
        assert!(check.detail.unwrap().contains("def"));
    }
}
