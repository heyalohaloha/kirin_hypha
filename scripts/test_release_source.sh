#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

UI_CONTRACT_BIN="${TMPDIR:-/tmp}/kirin-hypha-ui-contract-$$"
OBSERVATORY_CONTRACT_BIN="${TMPDIR:-/tmp}/kirin-hypha-observatory-contract-$$"
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

assert_ctest_inventory() {
  local build_dir="$1"
  local build_config="$2"
  local test_regex="$3"
  local expected="$4"
  local actual
  actual="$(ctest --test-dir "$build_dir" --build-config "$build_config" -N -R "$test_regex" \
    | awk '/Total Tests:/ { print $3 }')"
  if [[ "$actual" != "$expected" ]]; then
    echo "release gate CTest inventory mismatch: selected=$actual expected=$expected regex=$test_regex" >&2
    exit 1
  fi
}

# Shipping producer/consumer contract. This includes measurement, Record writer, generation,
# pairing, TRACE publication, and error-path integration tests without treating the retired
# nih-plug editors as the AU/VST3 release shell.
run cargo fmt --all -- --check
run node --test scripts/public_history.test.mjs
run node scripts/check_public_history.mjs --tip HEAD
run node --test scripts/check_aax_sdk_absence.test.mjs
run node scripts/check_aax_sdk_absence.mjs
run node --test scripts/check_typography_source.test.mjs
run node scripts/check_typography_source.mjs
run node --test scripts/structural_repair_detection.test.mjs
run node --test scripts/research/review/review.test.mjs
run node --test scripts/research/review/evaluate_review_answers.test.mjs
run bash scripts/test_source_line_budget.sh
run bash scripts/check_source_line_budget.sh
run node --test scripts/ls_release/release_metadata.test.mjs
run node --test scripts/windows/windows_installer.test.mjs

# Pure C++ contract used by the common AU/VST3 editor. This deliberately runs before any JUCE
# bundle build and blocks mismatched dimensions, bounds, fonts, colours, metric ordering, or MAX
# inventory while remaining independent of host/plugin-format wrappers.
run "${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
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
  -DCMAKE_BUILD_TYPE=Release
)
if [[ "$(uname -s)" == "Darwin" ]]; then
  PRE_DISPLAY_CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=$(uname -m)")
fi
run cmake "${PRE_DISPLAY_CMAKE_ARGS[@]}"
JUCE_TEST_TARGETS=(
  KirinPreDisplayRuntimeTests
  KirinCaptureWorkAttachmentTests
  KirinUiRenderContractTests
  KirinAttackUiContractTests
  KirinReferenceAuditionRuntimeTests
  KirinReferenceAudioPagesTests
  KirinLocalBlindCaptureTests
  KirinLocalBlindTrialTests
  KirinLocalBlindHostContextTests
  KirinLocalBlindPreparationTests
  KirinLocalBlindCaptureServiceTests
  KirinLocalBlindCapturePairComparisonTests
  KirinLocalBlindPdcValidationDelayTests
)
JUCE_TEST_REGEX='^(kirin_pre_display_runtime|kirin_capture_work_attachment|kirin_ui_render_contract|kirin_time_history_contract|kirin_analysis_demand_contract|kirin_attack_ui_contract|kirin_reference_audition_runtime|kirin_reference_audio_pages|kirin_local_blind_capture|kirin_local_blind_trial|kirin_local_blind_host_context|kirin_local_blind_preparation|kirin_local_blind_capture_service|kirin_local_blind_capture_pair_comparison|kirin_local_blind_pdc_validation_delay)$'
JUCE_TEST_COUNT=15
run cmake --build "$PRE_DISPLAY_BUILD" --target "${JUCE_TEST_TARGETS[@]}" --config Release
assert_ctest_inventory "$PRE_DISPLAY_BUILD" Release "$JUCE_TEST_REGEX" "$JUCE_TEST_COUNT"
run ctest --test-dir "$PRE_DISPLAY_BUILD" --build-config Release \
  --output-on-failure --no-tests=error -R "$JUCE_TEST_REGEX"

run cargo test -p kirin_measure --locked
run cargo test -p kirin_hypha_ffi --locked
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
for symbol in kirin_hypha_restore_pair_candidate_v2 kirin_hypha_get_paired_pre_locator \
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
run cargo test -p kirin_hypha_ffi --test parity --locked -- --ignored --test-threads=1
run cargo test -p kirin_hypha_ffi --test pairing_candidates --locked -- --ignored --test-threads=1

# Release-owned Rust code must remain warning-free. Upstream vendor crates are outside this gate.
run cargo clippy -p kirin_measure -p kirin_hypha_ffi -p xtask --all-targets --locked -- -D warnings

echo "release source contract: PASS"
