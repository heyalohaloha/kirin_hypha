#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

run() {
  echo "==> $*"
  "$@"
}

# This is the source-only contract shared by local verification, every CI event, and the
# full release-source gate. Keep it independent of JUCE builds, signing material, and hardware.
run cargo fmt --all -- --check
run node --test scripts/structural_repair_detection.test.mjs
run node --test scripts/native_storage_guard.test.mjs scripts/native_path_budget.test.mjs
run node --test scripts/check_typography_source.test.mjs
run node scripts/check_typography_source.mjs
run node --test scripts/check_screen_text.test.mjs
run node scripts/check_screen_text.mjs
run node --test scripts/check_native_test_inventory.test.mjs
run node --test scripts/ci_build_structure.test.mjs scripts/check_native_shards.test.mjs
run node --test scripts/native_test_libraries.test.mjs
run node --test scripts/analyze_chain_trace.test.mjs
run node --test scripts/listen_text_inventory.test.mjs
run node --test scripts/hypha_workflow_discovery.test.mjs
run node --test scripts/build_hypha.test.mjs
run node --test scripts/release_hypha.test.mjs scripts/ls_release/hypha_release_hp.test.mjs
run node --test scripts/provenance/*.test.mjs
run bash scripts/test_source_line_budget.sh
run bash scripts/check_source_line_budget.sh
# Tests wait for notifications or follow audio time; short sleeps only shrink (CI stretches them).
run node --test scripts/check_test_sleep_budget.test.mjs
run node scripts/check_test_sleep_budget.mjs
# Public text: what a change adds and its commit messages carry no internal records.
run node --test scripts/check_public_text.test.mjs
run node scripts/check_public_text.mjs
# The shell scripts also run under macOS's /bin/bash 3.2, which stops on an empty array under set -u.
if [[ -x /bin/bash ]]; then
  run /bin/bash scripts/test_source_line_budget.sh
  run /bin/bash scripts/check_source_line_budget.sh
fi
run bash scripts/test_juce_patch_state.sh

echo "lightweight source contract: PASS"
