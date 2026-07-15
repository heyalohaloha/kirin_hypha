#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

UI_CONTRACT_BIN="${TMPDIR:-/tmp}/kirin-hypha-ui-contract-$$"
trap 'rm -f "$UI_CONTRACT_BIN"' EXIT

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

# Pure C++ contract used by the common AU/VST3 editor. This deliberately runs before any JUCE
# bundle build and blocks mismatched dimensions, bounds, fonts, colours, metric ordering, or MAX
# inventory while remaining independent of host/plugin-format wrappers.
run "${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  juce_shell/tests/ui_contract_test.cpp -o "$UI_CONTRACT_BIN"
run "$UI_CONTRACT_BIN"

run cargo test -p kirin_measure --locked
run cargo test -p kirin_hypha_ffi --locked

# The C++ shell consumes the static C ABI, not Rust's rlib symbols. Build that exact archive and
# require the restart locator entry points to be exported definitions before any plugin bundle.
run cargo build -p kirin_hypha_ffi --locked
FFI_ARCHIVE="target/debug/libkirin_hypha_ffi.a"
# Apple nm from the installed Xcode can be older than rustc's LLVM and report unsupported debug
# attributes for unrelated dependency objects. It still emits the public symbol table for this
# crate; discard those diagnostic-only failures and require our entries to be defined (`T`).
FFI_SYMBOLS="$(nm -g "$FFI_ARCHIVE" 2>/dev/null || true)"
for symbol in kirin_hypha_restore_pair_candidate kirin_hypha_get_paired_pre_locator; do
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
