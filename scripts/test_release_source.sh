#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

UI_CONTRACT_BIN="${TMPDIR:-/tmp}/kirin-hypha-ui-contract-$$"
PRE_DISPLAY_BUILD="$(mktemp -d "${TMPDIR:-/tmp}/kirin-pre-display-test.XXXXXX")"
cleanup() {
  cmake -E rm -f "$UI_CONTRACT_BIN"
  cmake -E remove_directory "$PRE_DISPLAY_BUILD"
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
run cargo fmt --all -- --check
run node --test scripts/ls_release/release_metadata.test.mjs

# Pure C++ contract used by the common AU/VST3 editor. This deliberately runs before any JUCE
# bundle build and blocks mismatched dimensions, bounds, fonts, colours, metric ordering, or MAX
# inventory while remaining independent of host/plugin-format wrappers.
run "${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  juce_shell/tests/ui_contract_test.cpp -o "$UI_CONTRACT_BIN"
run "$UI_CONTRACT_BIN"

# The pinned JUCE submodule is intentionally pristine in a clean checkout. Both the runtime
# build below and xtask's wrapper parity checks consume the tracked build-time patch stack, so
# materialize that exact state here instead of relying on a developer's existing submodule tree.
run bash scripts/apply_juce_patches.sh

# Execute the file-backed PRE consumer itself: bounded parser, SHA-verified pointer recovery,
# explicit clear authority, multiple-instance fan-out, time boundaries, and clock retention.
PRE_DISPLAY_CMAKE_ARGS=(
  -S juce_shell
  -B "$PRE_DISPLAY_BUILD"
  -DKIRIN_HYPHA_BUILD_PRE_DISPLAY_TESTS=ON
  -DKIRIN_HYPHA_BUILD_UI_RENDER_TESTS=ON
  -DCMAKE_BUILD_TYPE=Debug
)
if [[ "$(uname -s)" == "Darwin" ]]; then
  PRE_DISPLAY_CMAKE_ARGS+=("-DCMAKE_OSX_ARCHITECTURES=$(uname -m)")
fi
run cmake "${PRE_DISPLAY_CMAKE_ARGS[@]}"
run cmake --build "$PRE_DISPLAY_BUILD" \
  --target KirinPreDisplayRuntimeTests KirinUiRenderContractTests --config Debug
run ctest --test-dir "$PRE_DISPLAY_BUILD" --build-config Debug \
  --output-on-failure -R '^(kirin_pre_display_runtime|kirin_ui_render_contract)$'

run cargo test -p kirin_measure --locked
run cargo test -p kirin_hypha_ffi --locked

# The C++ shell consumes the static C ABI, not Rust's rlib symbols. Build that exact archive and
# require the restart locator entry points to be exported definitions before any plugin bundle.
run cargo build -p kirin_hypha_ffi --locked
FFI_ARCHIVE="${CARGO_TARGET_DIR:-target}/debug/libkirin_hypha_ffi.a"
# Apple nm from the installed Xcode can be older than rustc's LLVM and report unsupported debug
# attributes for unrelated dependency objects. It still emits the public symbol table for this
# crate; discard those diagnostic-only failures and require our entries to be defined (`T`).
FFI_SYMBOLS="$(nm -g "$FFI_ARCHIVE" 2>/dev/null || true)"
for symbol in kirin_hypha_restore_pair_candidate kirin_hypha_get_paired_pre_locator \
              kirin_hypha_poll_record_display; do
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
assert_ignored_count pairing_candidates 5
run cargo test -p kirin_hypha_ffi --test parity --locked -- --ignored --test-threads=1
run cargo test -p kirin_hypha_ffi --test pairing_candidates --locked -- --ignored --test-threads=1

# Release-owned Rust code must remain warning-free. Upstream vendor crates are outside this gate.
run cargo clippy -p kirin_measure -p kirin_hypha_ffi -p xtask --all-targets --locked -- -D warnings

echo "release source contract: PASS"
