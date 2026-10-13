#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

UI_CONTRACT_BIN="${TMPDIR:-/tmp}/kirin-hypha-ui-contract-$$"
OBSERVATORY_CONTRACT_BIN="${TMPDIR:-/tmp}/kirin-hypha-observatory-contract-$$"
ABI_INCLUDE_DIR="$ROOT/crates/kirin_hypha_ffi/include"
# Keep native objects under the already-ignored Cargo target tree. Re-running this gate now
# recompiles only changed JUCE sources; CI workspaces are fresh, so release verification remains
# independent there. Set KIRIN_HYPHA_NATIVE_TEST_BUILD to isolate a diagnostic run if needed.
PRE_DISPLAY_BUILD="${KIRIN_HYPHA_NATIVE_TEST_BUILD:-${CARGO_TARGET_DIR:-$ROOT/target}/hypha-release-native-tests}"
cleanup() {
  cmake -E rm -f "$UI_CONTRACT_BIN"
  cmake -E rm -f "$OBSERVATORY_CONTRACT_BIN"
}
trap cleanup EXIT

run() {
  echo "==> $*"
  "$@"
}

count_ignored() {
  local test_target="$1"
  cargo test -p kirin_hypha_ffi --test "$test_target" --locked -- --ignored --list 2>/dev/null \
    | grep -c ': test' \
    || true
}

assert_ignored_count() {
  local test_target="$1"
  local expected="$2"
  local actual
  actual="$(count_ignored "$test_target")"
  if [[ "$actual" != "$expected" ]]; then
    echo "release gate inventory mismatch: $test_target ignored tests=$actual expected=$expected" >&2
    exit 1
  fi
}

# Shipping producer/consumer contract. This includes measurement, Record writer, generation,
# pairing, TRACE publication, and error-path integration tests without treating the retired
# nih-plug editors as the AU/VST3 release shell.
run bash scripts/test_lightweight_contract.sh
run node --test scripts/public_history.test.mjs
run node scripts/check_public_history.mjs --tip HEAD
run node --test scripts/check_aax_sdk_absence.test.mjs
run node scripts/check_aax_sdk_absence.mjs
run node --test scripts/ls_release/aax_distribution.test.mjs
run node --test scripts/ls_release/aax_submission_archive.test.mjs
run node --test scripts/ls_release/aax_notarization_flow.test.mjs
run node scripts/test_build_aax_universal.mjs
run node --test scripts/check_typography_source.test.mjs
run node scripts/check_typography_source.mjs
run node --test scripts/check_screen_text.test.mjs
run node scripts/check_screen_text.mjs
run node --test scripts/research/review/review.test.mjs
run node --test scripts/research/review/evaluate_review_answers.test.mjs
run node --test scripts/ls_release/release_metadata.test.mjs scripts/ls_release/windows_primary_metadata.test.mjs
run node --test scripts/windows/windows_installer.test.mjs
run node --test scripts/windows/inno_signing.test.mjs
run node --test scripts/updates/update_manifest.test.mjs scripts/updates/update_key_binding.test.mjs scripts/updates/update_binary_slices.test.mjs scripts/updates/winhttp_transport.test.mjs scripts/updates/macos_transport.test.mjs scripts/check_hypha_update_ui.mjs scripts/check_hypha_update_isolation.mjs

# Pure C++ contract used by the common AU/VST3 editor. This deliberately runs before any JUCE
# bundle build and blocks mismatched dimensions, bounds, ABI contracts, fonts, colours, or shared
# UI constants while remaining independent of host/plugin-format wrappers.
run "${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -I "$ABI_INCLUDE_DIR" \
  juce_shell/tests/ui_contract_test.cpp -o "$UI_CONTRACT_BIN"
run "$UI_CONTRACT_BIN"

# New shell anatomy is independent of the legacy Meters layout. Pin all five PRE/POST sizes and
# the optional Guide context before either transport or JUCE rendering is connected to it.
run "${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  juce_shell/tests/observatory_contract_test.cpp -o "$OBSERVATORY_CONTRACT_BIN"
run "$OBSERVATORY_CONTRACT_BIN"

# The pinned JUCE submodule is intentionally pristine in a clean checkout. Both the runtime
# build below and xtask's wrapper parity checks consume the tracked build-time patch stack, so
# materialize that exact state here instead of relying on a developer's existing submodule tree.
run bash scripts/apply_juce_patches.sh

# Execute the file-backed PRE consumer and native UI contract in the same optimized configuration
# that ships. The UI contract includes hard paint-time ceilings, so a Debug build would measure
# assertion/instrumentation overhead instead of the production renderer and vary by runner class.
PRE_DISPLAY_CMAKE_ARGS=(
  -S juce_shell
  -B "$PRE_DISPLAY_BUILD"
  -DKIRIN_HYPHA_BUILD_PRE_DISPLAY_TESTS=ON
  -DKIRIN_HYPHA_BUILD_UI_RENDER_TESTS=ON
  -DKIRIN_HYPHA_BUILD_REFERENCE_AUDITION_TESTS=ON
  -DKIRIN_HYPHA_BUILD_UPDATE_TESTS=ON
  -DCMAKE_BUILD_TYPE=Release
)
if [[ "$(uname -s)" == "Darwin" ]]; then
  PRE_DISPLAY_CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=$(uname -m)")
  # Evidence for clock-proof loss on the ARM64 runner. Test executable only: shipping
  # processor flags, the 60-second deadline, PCM oracle and safety criteria stay unchanged.
  PRE_DISPLAY_CMAKE_ARGS+=("-DKIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC=ON")
fi
run cmake "${PRE_DISPLAY_CMAKE_ARGS[@]}"
JUCE_TEST_TARGETS=(
  Kirin_reaper_clock_Tests
  Kirin_reaper_correspondence_Tests
  Kirin_reaper_fault_Tests
  KirinReaperListenProductTests
  KirinAppearanceContractTests
  KirinGuideFrequencyOverlayContractTests
  KirinRendererLeaseTests
  KirinFutureHostStatusTests
  KirinIntegratedHostStatusTests
  KirinAuTransportResetTests
  KirinPreContentTimingTests
  KirinPostContentTimingTests
  KirinContentTimingProductTests
  KirinUpdate_manifest_Tests
  KirinUpdate_store_Tests
  KirinUpdate_checker_Tests
  KirinHostIdentityTests
  KirinPreDisplayRuntimeTests
  KirinCaptureWorkAttachmentTests
  KirinUiRenderContractTests
  KirinSnapshotAbiContractTests
  KirinAttackUiContractTests
  KirinReferenceAuditionRuntimeTests
  KirinPairPreviewLifetimeTests
  KirinReferenceContentCorrelationTests
  KirinOutputOwnershipTests
  KirinReferenceAudioPagesTests
  KirinReferenceAudioStreamingTests
  KirinLocalBlindCaptureTests
  KirinLocalBlindTrialTests
  KirinLocalBlindHostContextTests
  KirinLocalBlindPreparationTests
  KirinLocalBlindCaptureServiceTests
  KirinLocalBlindCapturePairComparisonTests
  KirinLocalBlindPdcValidationDelayTests
  KirinLocalBlindProductTests
  KirinEditorSurfaceProductTests
  KirinReferenceAuditionComponentTests
  KirinHyphaReferencePreviewRenderer
  KirinLiveCompareCorrespondenceTests
  KirinChainTraceTests
  KirinLiveCompareTimingTests
  KirinLiveTimingProductTests
  KirinLiveCompareLoopTests
  KirinLiveCompareLoopFeasibilityTests
  KirinLiveCompare_reentry_Tests
  KirinLiveCompare_gain_snapshot_Tests
  Kirin_live_blind_session_Tests
  Kirin_live_compare_completion_Tests
  Kirin_live_compare_authority_Tests
  KirinLiveBlindProductTests
  KirinLiveRecoveryProductTests
  KirinLiveChainTimingProductTests
  KirinLiveReentryProductTests
  KirinLiveCompareLifecycleTests
  KirinLivePublicationPRETests
  KirinLivePublicationPOSTTests
  KirinStoragePathsTests KirinAtomicFileTests
  KirinReferenceLowerAProductTests
  KirinLiveCompareSessionTests
  KirinLiveCompareMatchTests
  KirinLiveComparePinProductTests
  KirinLiveCompareOffsetProductTests
  KirinLiveCompareAaxGroupProductTests
  KirinAudioUnitAnalysisClockTests
  KirinReaperAnalysisClockTests
  KirinHostContentClockPolicyTests
  KirinListenTextLayoutTests
  KirinContentAnalysisClockTests
  KirinContentAnalysisProductTests
  KirinProductWallClockTests
  KirinShippingWallClockTests
  KirinProcessorWallClockTests
  KirinAnalysisProductWaitTests
  KirinAnalysisProductTimingTests
  KirinAaxAnalysisClockTests
  KirinAaxAnalysisProductTests
)
JUCE_TEST_REGEX='^(kirin_native_path_budget_setup|kirin_native_path_budget_cleanup|kirin_atomic_file|kirin_atomic_file_boundaries|kirin_native_path_budget_contract|kirin_product_wall_clock|kirin_shipping_wall_clock|kirin_processor_wall_clock|kirin_reaper_clock|kirin_reaper_correspondence|kirin_reaper_fault|kirin_reaper_listen_mono_product|kirin_reaper_listen_stereo_product|kirin_product_entry_contract|kirin_ui_typography_contract|kirin_appearance_contract|kirin_guide_frequency_overlay_contract|kirin_renderer_lease|kirin_future_host_status|kirin_integrated_host_status|kirin_shell_repair_ui|kirin_reference_source_repairs|kirin_reference_event_repairs|kirin_editor_shell_repair|kirin_live_compare_memory|kirin_live_close_fade|kirin_au_reset_transport|kirin_pre_content_timing|kirin_post_content_timing|kirin_content_timing_product|kirin_listen_text_layout|kirin_chain_trace|kirin_storage_paths_environment|kirin_storage_paths_sandbox|kirin_storage_paths_defaults|kirin_storage_paths_relative|kirin_storage_paths_unwritable|kirin_reaper_analysis_clock|kirin_host_content_clock_policy|kirin_content_analysis_stereo_missing_linear_product|kirin_content_analysis_stereo_missing_loop_product|kirin_content_analysis_stereo_missing_recovery_product|kirin_content_analysis_stereo_omitted_linear_product|kirin_content_analysis_stereo_omitted_loop_product|kirin_content_analysis_stereo_omitted_recovery_product|kirin_content_analysis_stereo_included_linear_product|kirin_content_analysis_stereo_included_loop_product|kirin_content_analysis_stereo_included_recovery_product|kirin_content_analysis_stereo_zero_linear_product|kirin_content_analysis_stereo_zero_loop_product|kirin_content_analysis_stereo_zero_recovery_product|kirin_au_analysis_stereo_missing_linear_product|kirin_au_analysis_stereo_missing_loop_product|kirin_au_analysis_stereo_missing_recovery_product|kirin_au_analysis_stereo_missing_cycle_listen_product|kirin_au_analysis_stereo_missing_cycle_blind_product|kirin_au_analysis_stereo_omitted_linear_product|kirin_au_analysis_stereo_omitted_loop_product|kirin_au_analysis_stereo_omitted_recovery_product|kirin_au_analysis_stereo_omitted_cycle_listen_product|kirin_au_analysis_stereo_omitted_cycle_blind_product|kirin_au_analysis_stereo_included_linear_product|kirin_au_analysis_stereo_included_loop_product|kirin_au_analysis_stereo_included_recovery_product|kirin_au_analysis_stereo_zero_linear_product|kirin_au_analysis_stereo_zero_loop_product|kirin_au_analysis_stereo_zero_recovery_product|kirin_au_analysis_stereo_zero_cycle_listen_product|kirin_au_analysis_stereo_zero_cycle_blind_product|kirin_reaper_analysis_stereo_missing_linear_product|kirin_reaper_analysis_stereo_missing_linear_jitter_product|kirin_reaper_analysis_stereo_missing_loop_product|kirin_reaper_analysis_stereo_missing_loop_jitter_product|kirin_reaper_analysis_stereo_missing_recovery_product|kirin_reaper_analysis_stereo_missing_recovery_jitter_product|kirin_content_analysis_mono_missing_linear_product|kirin_content_analysis_mono_missing_loop_product|kirin_content_analysis_mono_missing_recovery_product|kirin_content_analysis_mono_omitted_linear_product|kirin_content_analysis_mono_omitted_loop_product|kirin_content_analysis_mono_omitted_recovery_product|kirin_content_analysis_mono_included_linear_product|kirin_content_analysis_mono_included_loop_product|kirin_content_analysis_mono_included_recovery_product|kirin_content_analysis_mono_zero_linear_product|kirin_content_analysis_mono_zero_loop_product|kirin_content_analysis_mono_zero_recovery_product|kirin_au_analysis_mono_missing_linear_product|kirin_au_analysis_mono_missing_loop_product|kirin_au_analysis_mono_missing_recovery_product|kirin_au_analysis_mono_missing_cycle_listen_product|kirin_au_analysis_mono_missing_cycle_blind_product|kirin_au_analysis_mono_omitted_linear_product|kirin_au_analysis_mono_omitted_loop_product|kirin_au_analysis_mono_omitted_recovery_product|kirin_au_analysis_mono_omitted_cycle_listen_product|kirin_au_analysis_mono_omitted_cycle_blind_product|kirin_au_analysis_mono_included_linear_product|kirin_au_analysis_mono_included_loop_product|kirin_au_analysis_mono_included_recovery_product|kirin_au_analysis_mono_zero_linear_product|kirin_au_analysis_mono_zero_loop_product|kirin_au_analysis_mono_zero_recovery_product|kirin_au_analysis_mono_zero_cycle_listen_product|kirin_au_analysis_mono_zero_cycle_blind_product|kirin_reaper_analysis_mono_missing_linear_product|kirin_reaper_analysis_mono_missing_linear_jitter_product|kirin_reaper_analysis_mono_missing_loop_product|kirin_reaper_analysis_mono_missing_loop_jitter_product|kirin_reaper_analysis_mono_missing_recovery_product|kirin_reaper_analysis_mono_missing_recovery_jitter_product|kirin_analysis_product_wait|kirin_analysis_product_timing|kirin_au_analysis_clock|kirin_content_analysis_clock|kirin_aax_analysis_clock|kirin_aax_analysis_stereo_certified_product|kirin_aax_analysis_stereo_uncertified_product|kirin_aax_analysis_stereo_loop_product|kirin_aax_analysis_stereo_short_loops_product|kirin_aax_analysis_stereo_per_insert_product|kirin_aax_analysis_stereo_measured_loop_product|kirin_aax_analysis_mono_certified_product|kirin_aax_analysis_mono_uncertified_product|kirin_aax_analysis_mono_loop_product|kirin_aax_analysis_mono_short_loops_product|kirin_aax_analysis_mono_per_insert_product|kirin_aax_analysis_mono_measured_loop_product|kirin_snapshot_abi_contract|kirin_update_manifest|kirin_update_store|kirin_update_checker|kirin_host_identity|kirin_pre_display_runtime|kirin_capture_work_attachment|kirin_ui_render_contract|kirin_time_history_contract|kirin_analysis_demand_contract|kirin_attack_ui_contract|kirin_reference_audition_runtime|kirin_reference_audition_component|kirin_hypha_reference_preview_renderer|kirin_reference_audio_pages|kirin_reference_audio_streaming|kirin_reference_content_correlation|kirin_output_ownership|kirin_local_blind_capture|kirin_local_blind_trial|kirin_local_blind_host_context|kirin_local_blind_preparation|kirin_local_blind_capture_service|kirin_local_blind_capture_pair_comparison|kirin_local_blind_pdc_validation_delay|kirin_local_blind_product|kirin_local_blind_product_track|kirin_local_blind_product_aax|kirin_local_blind_product_aax_track|kirin_editor_surface_product|kirin_pair_preview_lifetime|kirin_live_compare_correspondence|kirin_live_compare_timing|kirin_live_timing_product|kirin_live_chain_timing_product|kirin_live_initial_loop_product|kirin_live_compare_session|kirin_live_compare_match|kirin_live_compare_pin_product|kirin_live_compare_offset_product|kirin_live_compare_aax_group_product|kirin_live_compare_loop|kirin_live_compare_loop_feasibility|kirin_live_blind_session|kirin_live_compare_completion|kirin_live_compare_authority|kirin_live_recovery_product|kirin_live_blind_product|kirin_live_blind_reuse_product|kirin_live_blind_loop_product|kirin_live_blind_loop_stall_product|kirin_live_blind_fault_product|kirin_live_blind_fault_gap_product|kirin_live_blind_fault_content_product|kirin_live_blind_fault_stop_product|kirin_live_blind_approval_product|kirin_live_lifecycle_restore-active|kirin_live_lifecycle_restore-invalid|kirin_live_lifecycle_restore-preparing|kirin_live_lifecycle_restore-approval|kirin_live_lifecycle_held|kirin_live_lifecycle_finishing|kirin_live_lifecycle_out-of-range|kirin_live_lifecycle_reuse-held|kirin_live_lifecycle_restore-matched-listen|kirin_reference_lower_a_product|kirin_live_compare_reentry|kirin_live_compare_gain_snapshot|kirin_live_reentry_stop_product|kirin_live_reentry_seek_product|kirin_live_reentry_clock_product|kirin_live_reentry_restore_product|kirin_live_reentry_end_product|kirin_live_reentry_blind-stop_product|kirin_live_reentry_compensation_product|kirin_live_reentry_seek-odd_product|kirin_live_reentry_pending-odd_product|kirin_live_reentry_proof-odd_product|kirin_live_reentry_pair-clear_product|kirin_live_reentry_pair-set_product|kirin_live_reentry_clock-blind_product|kirin_live_reentry_preparing-clock_product|kirin_live_reentry_preparing-stop_product|kirin_live_reentry_reuse_product|kirin_live_reentry_pending-end_product|kirin_live_reentry_restore-blind_product|kirin_live_reentry_failure-named_product|kirin_live_reentry_preparing-dc_product|kirin_live_reentry_preparing-content_product|kirin_live_reentry_preparing-seek_product|kirin_live_publication_PRE_state-only|kirin_live_publication_PRE_shared-ring|kirin_live_publication_POST_state-only|kirin_live_publication_POST_shared-ring|kirin_live_publication_PRE_storage-only|kirin_live_publication_POST_storage-only|kirin_native_storage_guard_setup|kirin_native_storage_guard_cleanup|kirin_native_storage_guard_contract)$'
# Include every live comparison contract, including LOOP and invalidation/recovery, not only
# the original named-listen fixtures. Inventory is asserted rather than silently skipping one.
JUCE_TEST_COUNT=227

# Actual receiver/license/paired Record run while the update worker is held. These
# are not sentinel files and must be present in every supported native test build.
UPDATE_INTEGRATION_TARGETS=(KirinUpdateRuntimeIntegrationTests KirinUpdate_http_response_Tests)
UPDATE_INTEGRATION_REGEX='^(kirin_update_runtime_(off|success|failure|cancel|lifetime)|kirin_update_http_response)$'
# CTest also selects real-storage and path-budget setup/cleanup for every native test;
# the inventory counts all four fixture tests with the selected native bodies.
UPDATE_INTEGRATION_COUNT=10
if [[ "$(uname -s)" == "Darwin" ]]; then
  UPDATE_INTEGRATION_TARGETS+=(KirinUpdate_transport_mac_Tests)
  UPDATE_INTEGRATION_REGEX='^(kirin_update_runtime_(off|success|failure|cancel|lifetime)|kirin_update_http_response|kirin_update_transport_mac)$'
  UPDATE_INTEGRATION_COUNT=11
fi
# Compare the entire configured CTest registry before building or running any selected test.
# Exclusions are exact names with reasons, never a broad regex that could hide new tests.
run node scripts/check_native_test_inventory.mjs --build-dir "$PRE_DISPLAY_BUILD" --config Release \
  --exclusions scripts/release_native_test_exclusions.json \
  --group "$JUCE_TEST_COUNT" "$JUCE_TEST_REGEX" \
  --group "$UPDATE_INTEGRATION_COUNT" "$UPDATE_INTEGRATION_REGEX"

run cmake --build "$PRE_DISPLAY_BUILD" --target "${JUCE_TEST_TARGETS[@]}" --config Release
# Keep the existing one-retry policy for real GUI/IPC deadlines. Callback continuity now uses
# the fixture clock; deliberate host gaps remain explicit clock advances and must still pass
# kirin_live_blind_loop_stall_product. Two failures still fail the gate.
LIVE_RETRY_REGEX="^($(ctest --test-dir "$PRE_DISPLAY_BUILD" --build-config Release -N -R "$JUCE_TEST_REGEX" \
  | sed -n 's/^ *Test *#[0-9]*: \(kirin_live_[^ ]*_product\)$/\1/p' | paste -sd '|' -))\$"
run ctest --test-dir "$PRE_DISPLAY_BUILD" --build-config Release \
  --output-on-failure --no-tests=error -R "$JUCE_TEST_REGEX" -E "$LIVE_RETRY_REGEX"
run ctest --test-dir "$PRE_DISPLAY_BUILD" --build-config Release \
  --output-on-failure --no-tests=error --repeat until-pass:2 -R "$LIVE_RETRY_REGEX"

run cmake --build "$PRE_DISPLAY_BUILD" --target "${UPDATE_INTEGRATION_TARGETS[@]}" --config Release
run ctest --test-dir "$PRE_DISPLAY_BUILD" --build-config Release \
  --output-on-failure --no-tests=error -R "$UPDATE_INTEGRATION_REGEX"

run cargo test -p kirin_measure --locked
run cargo test -p kirin_hypha_ffi --locked
# Keep legacy VST3 identity and Record styling covered without using these editors as shipping
# AU/VST3 bundles. Their compatibility modules remain separate from the JUCE product surface.
run cargo test -p hypha_pre -p hypha_post --locked
run cargo test -p vst3-com --locked --test vtable_expression
# Each paired SHARP view runs one exact PRE/POST pair. The local LIVE view runs one POST analyzer;
# quantify both allowed LIVE slots in the same optimized configuration that ships.
run cargo test -p kirin_measure --release --locked \
  one_visible_pair_continuous_sharpness_worker_budget_is_quantified \
  --lib -- --ignored --nocapture
run cargo test -p kirin_measure --release --locked \
  two_post_absolute_workers_fit_the_optional_analysis_budget \
  --lib -- --ignored --nocapture

# The C++ shell consumes the static C ABI, not Rust's rlib symbols. Build that exact archive and
# require the restart locator entry points to be exported definitions before any plugin bundle.
run cargo build -p kirin_hypha_ffi --locked
FFI_ARCHIVE="${CARGO_TARGET_DIR:-target}/debug/libkirin_hypha_ffi.a"
# Apple nm from the installed Xcode can be older than rustc's LLVM and report unsupported debug
# attributes for unrelated dependency objects. It still emits the public symbol table for this
# crate; discard those diagnostic-only failures and require our entries to be defined (`T`).
FFI_SYMBOLS="$(nm -g "$FFI_ARCHIVE" 2>/dev/null || true)"
for symbol in kirin_hypha_poll_attack_navigation_v2 kirin_hypha_poll_meter_session_v2 \
              kirin_hypha_poll_time_snapshot_v2 kirin_hypha_poll_attack_band_summary_v2 \
              kirin_hypha_request_attack_single_v2 kirin_hypha_poll_attack_single_v2 \
              kirin_hypha_cancel_attack_single_v2 kirin_hypha_snapshot_abi_contract kirin_hypha_restore_pair_candidate_v2 kirin_hypha_get_paired_pre_locator \
              kirin_hypha_poll_record_display \
              kirin_hypha_publish_local_blind_pre_capture \
              kirin_hypha_read_local_blind_pre_capture \
              kirin_hypha_ack_local_blind_pre_capture \
              kirin_hypha_local_blind_pre_capture_was_consumed \
              kirin_hypha_retire_local_blind_pre_capture; do
  if ! grep -Eq "[[:space:]]T[[:space:]]_?${symbol}$" <<<"$FFI_SYMBOLS"; then
    echo "release gate missing defined C ABI symbol: $symbol" >&2
    exit 1
  fi
done

# Static contracts for the common JUCE AU/VST3 shell, audio-thread safety, packaging, and CI.
run cargo test -p xtask --locked

# These real-time filesystem suites are deliberately ignored by the normal cargo test command.
# Pin the inventory before running it so a renamed/deleted blocker test cannot disappear silently.
assert_ignored_count parity 20
assert_ignored_count pairing_candidates 6
assert_ignored_count record_auxiliary_cuts 1
run cargo test -p kirin_hypha_ffi --test parity --locked -- --ignored --test-threads=1
run cargo test -p kirin_hypha_ffi --test pairing_candidates --locked -- --ignored --test-threads=1
run cargo test -p kirin_hypha_ffi --test record_auxiliary_cuts --locked -- --ignored --test-threads=1

# Release-owned Rust code must remain warning-free. Upstream vendor crates are outside this gate.
run cargo clippy -p kirin_measure -p kirin_hypha_ffi -p xtask --all-targets --locked -- -D warnings
run cargo clippy -p hypha_pre -p hypha_post --all-targets --locked -- -D warnings

echo "release source contract: PASS"
