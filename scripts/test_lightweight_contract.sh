#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PLAN=false
if [[ "${1:-}" == --plan && $# == 1 ]]; then PLAN=true
elif [[ $# != 0 ]]; then echo "unknown lightweight argument" >&2; exit 2; fi

run() {
  echo "==> $*"
  if [[ "$PLAN" == false ]]; then "$@"; fi
}

# This is the source-only contract shared by local verification, every CI event, and the
# full release-source gate. Keep it independent of JUCE builds, signing material, and hardware.
run cargo fmt --all -- --check
run node --test scripts/structural_repair_detection.test.mjs scripts/watch_snapshot_runtime.test.mjs
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
run node --test scripts/build_hypha.test.mjs scripts/macos_build_parallel.test.mjs
run node --test scripts/ls_release/windows_promotion.test.mjs scripts/ls_release/windows_delivery_bind.test.mjs
run node --test scripts/ls_release/reconcile_recovery.test.mjs scripts/ls_release/reconcile_override.test.mjs scripts/ls_release/reconcile_ls.test.mjs scripts/ls_release/reconcile_cli.test.mjs
run node --test scripts/ls_release/pkg_staple.test.mjs scripts/ls_release/pkg_context.test.mjs scripts/ls_release/pkg_release_recovery.test.mjs
run node --test scripts/acceptance/acceptance_tools.test.mjs scripts/acceptance/reaper_run.test.mjs scripts/acceptance/reaper_operation_log.test.mjs scripts/acceptance/reaper_record_quality.test.mjs scripts/acceptance/reaper_capture_binding.test.mjs scripts/acceptance/reaper_policy.test.mjs scripts/acceptance/reaper_private_acl.test.mjs scripts/acceptance/reaper_audit_regression.test.mjs
run node --test scripts/ls_release/ls_api_readback.test.mjs scripts/ls_release/hypha_release_ls.test.mjs scripts/ls_release/release_preflight.test.mjs
run node --test scripts/ls_release/hypha_release_criteria.test.mjs
run node --test scripts/release_hypha.test.mjs scripts/ls_release/release_stage_order.test.mjs scripts/ls_release/publication_input.test.mjs scripts/release_juce_init.test.mjs scripts/ls_release/hypha_release_hp.test.mjs scripts/ls_release/hypha_release_github.test.mjs
run node --test scripts/provenance/*.test.mjs
run node --test scripts/ls_release/release_source_identity.test.mjs
run bash scripts/test_source_line_budget.sh
run bash scripts/check_source_line_budget.sh
# Tests wait for notifications or follow audio time; short sleeps only shrink (CI stretches them).
run node --test scripts/check_test_sleep_budget.test.mjs
run node scripts/check_test_sleep_budget.mjs
run node --test scripts/check_perf_budget.test.mjs
run node scripts/check_perf_budget.mjs
# Public text: what a change adds and its commit messages carry no internal records.
run node --test scripts/check_public_text.test.mjs
run node scripts/check_public_text.mjs
# The shell scripts also run under macOS's /bin/bash 3.2, which stops on an empty array under set -u.
if [[ -x /bin/bash ]]; then
  run /bin/bash scripts/test_source_line_budget.sh
  run /bin/bash scripts/check_source_line_budget.sh
fi
run bash scripts/test_juce_patch_state.sh

run node --test scripts/check_public_environment.test.mjs
run node scripts/check_public_environment.mjs

if [[ "$PLAN" == false ]]; then echo "lightweight source contract: PASS"; fi
