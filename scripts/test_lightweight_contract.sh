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
run node --test scripts/hypha_workflow_discovery.test.mjs
run node --test scripts/build_hypha.test.mjs
run node --test scripts/release_hypha.test.mjs scripts/ls_release/hypha_release_hp.test.mjs
run bash scripts/test_source_line_budget.sh
run bash scripts/check_source_line_budget.sh
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
