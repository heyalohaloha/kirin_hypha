#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHECKER="$SCRIPT_DIR/check_source_line_budget.sh"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/kirin-source-line-budget.XXXXXX")"

cleanup() {
  rm -rf "$FIXTURE_ROOT"
}
trap cleanup EXIT

fail() {
  echo "source line budget self-test: FAIL: $*" >&2
  exit 1
}

write_lines() {
  local path="$1"
  local count="$2"
  mkdir -p "$(dirname "$path")"
  awk -v count="$count" 'BEGIN { for (i = 1; i <= count; i++) print "line" }' > "$path"
}

write_baseline() {
  local allowance="${1:-}"
  if [[ -z "$allowance" ]]; then
    : > "$FIXTURE_ROOT/scripts/source_line_budget.tsv"
  else
    printf 'crates/demo/src/legacy.rs\t%s\n' "$allowance" \
      > "$FIXTURE_ROOT/scripts/source_line_budget.tsv"
  fi
}

run_checker() {
  SOURCE_LINE_BUDGET_ROOT="$FIXTURE_ROOT" \
  SOURCE_LINE_BUDGET_BASELINE="$FIXTURE_ROOT/scripts/source_line_budget.tsv" \
  SOURCE_LINE_BUDGET_BASE_REF="${COMPARISON_REF:-}" \
    bash "$CHECKER"
}

expect_failure() {
  local expected="$1"
  local output
  if output="$(run_checker 2>&1)"; then
    fail "expected failure containing '$expected'"
  fi
  [[ "$output" == *"$expected"* ]] \
    || fail "missing '$expected' in: $output"
}

mkdir -p "$FIXTURE_ROOT/scripts" "$FIXTURE_ROOT/crates/demo/src"
git -C "$FIXTURE_ROOT" init -q

# Boundary: a new untracked owned source is accepted at exactly 500 lines.
write_baseline
write_lines "$FIXTURE_ROOT/crates/demo/src/new.rs" 500
run_checker >/dev/null

# Error path: the same untracked source must be rejected at 501 lines.
write_lines "$FIXTURE_ROOT/crates/demo/src/new.rs" 501
expect_failure "NEW violation"
rm -f "$FIXTURE_ROOT/crates/demo/src/new.rs"

# Existing debt is accepted only when its baseline equals the exact current count.
write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 501
git -C "$FIXTURE_ROOT" add crates/demo/src/legacy.rs
write_baseline 501
run_checker >/dev/null

# A one-line increase is a regression.
write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 502
expect_failure "REGRESSION"

# A reduction must lower the baseline in the same change, preventing later regrowth.
write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 501
write_baseline 502
expect_failure "RATCHET REQUIRED"

# Reaching the target removes the legacy allowance instead of preserving it.
write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 500
write_baseline 501
expect_failure "remove resolved baseline entry"

# A staged or working-tree deletion is not an input file and must not make the checker call awk
# on a path that no longer exists. Any stale allowance is checked separately below the inventory.
rm -f "$FIXTURE_ROOT/crates/demo/src/legacy.rs"
write_baseline
run_checker >/dev/null

# The committed source and committed allowance are the immutable comparison facts. Raising both
# in one working tree must not turn a regression into a new baseline.
write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 501
write_baseline 501
git -C "$FIXTURE_ROOT" add .
git -C "$FIXTURE_ROOT" -c user.name='Line Budget Test' -c user.email=test@example.invalid \
  commit -qm 'comparison baseline'
COMPARISON_REF=HEAD

write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 900
write_baseline 900
expect_failure "COMPARISON REGRESSION"

# An untracked oversized source cannot bring its own newly-added allowance.
write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 501
write_lines "$FIXTURE_ROOT/crates/demo/src/new.rs" 501
printf 'crates/demo/src/legacy.rs\t501\ncrates/demo/src/new.rs\t501\n' \
  > "$FIXTURE_ROOT/scripts/source_line_budget.tsv"
expect_failure "NEW allowance"

# Deleting an allowance while retaining its oversized source is still a new violation.
rm -f "$FIXTURE_ROOT/crates/demo/src/new.rs"
write_baseline
expect_failure "NEW violation"

# Renaming both an oversized source and its allowance cannot erase the comparison identity.
rm -f "$FIXTURE_ROOT/crates/demo/src/legacy.rs"
write_lines "$FIXTURE_ROOT/crates/demo/src/renamed.rs" 501
printf 'crates/demo/src/renamed.rs\t501\n' > "$FIXTURE_ROOT/scripts/source_line_budget.tsv"
expect_failure "NEW allowance"

# A real reduction and exact lower allowance remain valid.
rm -f "$FIXTURE_ROOT/crates/demo/src/renamed.rs"
write_lines "$FIXTURE_ROOT/crates/demo/src/legacy.rs" 500
write_baseline
run_checker >/dev/null

echo "source line budget self-test: PASS"
