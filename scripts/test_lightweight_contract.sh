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
run bash scripts/test_source_line_budget.sh
run bash scripts/check_source_line_budget.sh

echo "lightweight source contract: PASS"
