#!/usr/bin/env bash
set -euo pipefail

DEFAULT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="${SOURCE_LINE_BUDGET_ROOT:-$DEFAULT_ROOT}"
BASELINE="${SOURCE_LINE_BUDGET_BASELINE:-$ROOT/scripts/source_line_budget.tsv}"
BASELINE_PATH="${SOURCE_LINE_BUDGET_BASELINE_PATH:-scripts/source_line_budget.tsv}"
BASE_REF="${SOURCE_LINE_BUDGET_BASE_REF:-}"
LIMIT=500

if [[ ! -f "$BASELINE" ]]; then
  echo "source line budget baseline is missing: $BASELINE" >&2
  exit 1
fi

line_count() {
  awk 'END { print NR }' "$1"
}

baseline_value() {
  local baseline="$1"
  local file="$2"
  awk -F '\t' -v key="$file" \
    '$1 == key { print $2; found=1 } END { if (!found) exit 1 }' "$baseline" || true
}

is_owned_source() {
  case "$1" in
    crates/*.rs|crates/*.h|crates/*/*.rs|crates/*/*.h|crates/*/*/*.rs|crates/*/*/*.h|\
    crates/*/*/*/*.rs|crates/*/*/*/*.h|crates/*/*/*/*/*.rs|crates/*/*/*/*/*.h|\
    xtask/*.rs|xtask/*/*.rs|xtask/*/*/*.rs|\
    juce_shell/src/*.cpp|juce_shell/src/*.h|juce_shell/src/*/*.cpp|juce_shell/src/*/*.h|\
    juce_shell/tests/*.cpp|juce_shell/tests/*.h|juce_shell/tests/*/*.cpp|juce_shell/tests/*/*.h)
      return 0
      ;;
  esac
  return 1
}

failures=0
debt=0
while IFS= read -r -d '' file; do
  is_owned_source "$file" || continue
  [[ -f "$ROOT/$file" ]] || continue
  lines="$(line_count "$ROOT/$file")"
  (( lines > LIMIT )) || continue
  debt=$((debt + 1))
  allowed="$(baseline_value "$BASELINE" "$file")"
  if [[ -z "$allowed" ]]; then
    echo "source line budget: NEW violation $file ($lines > $LIMIT)" >&2
    failures=$((failures + 1))
  elif (( lines > allowed )); then
    echo "source line budget: REGRESSION $file ($lines > baseline $allowed)" >&2
    failures=$((failures + 1))
  fi
done < <(cd "$ROOT" && git ls-files -z --cached --others --exclude-standard)

while IFS=$'\t' read -r file allowed; do
  [[ -n "$file" && "$file" != \#* ]] || continue
  if [[ ! -f "$ROOT/$file" ]]; then
    echo "source line budget: stale baseline path $file" >&2
    failures=$((failures + 1))
    continue
  fi
  lines="$(line_count "$ROOT/$file")"
  if (( lines <= LIMIT )); then
    echo "source line budget: remove resolved baseline entry $file ($lines <= $LIMIT)" >&2
    failures=$((failures + 1))
  elif (( allowed <= LIMIT )); then
    echo "source line budget: invalid allowance for $file ($allowed <= $LIMIT)" >&2
    failures=$((failures + 1))
  elif (( lines < allowed )); then
    echo "source line budget: RATCHET REQUIRED $file (baseline $allowed -> current $lines)" >&2
    failures=$((failures + 1))
  fi
done < "$BASELINE"

if [[ -n "$BASE_REF" ]]; then
  if ! git -C "$ROOT" rev-parse --verify --quiet "$BASE_REF^{commit}" >/dev/null; then
    echo "source line budget: comparison commit is invalid: $BASE_REF" >&2
    exit 1
  fi
  comparison_baseline="$(mktemp "${TMPDIR:-/tmp}/kirin-source-line-baseline.XXXXXX")"
  trap 'rm -f "$comparison_baseline"' EXIT
  if ! git -C "$ROOT" show "$BASE_REF:$BASELINE_PATH" > "$comparison_baseline"; then
    echo "source line budget: comparison baseline is missing: $BASE_REF:$BASELINE_PATH" >&2
    exit 1
  fi

  while IFS=$'\t' read -r file allowed; do
    [[ -n "$file" && "$file" != \#* ]] || continue
    previous="$(baseline_value "$comparison_baseline" "$file")"
    if [[ -z "$previous" ]]; then
      echo "source line budget: NEW allowance $file ($allowed) absent from $BASE_REF" >&2
      failures=$((failures + 1))
    elif (( allowed > previous )); then
      echo "source line budget: ALLOWANCE REGRESSION $file ($allowed > $BASE_REF $previous)" >&2
      failures=$((failures + 1))
    fi
  done < "$BASELINE"

  while IFS=$'\t' read -r file _allowed; do
    [[ -n "$file" && "$file" != \#* ]] || continue
    [[ -f "$ROOT/$file" ]] || continue
    if ! git -C "$ROOT" cat-file -e "$BASE_REF:$file" 2>/dev/null; then
      echo "source line budget: comparison source is missing: $BASE_REF:$file" >&2
      failures=$((failures + 1))
      continue
    fi
    previous_lines="$(git -C "$ROOT" show "$BASE_REF:$file" | awk 'END { print NR }')"
    current_lines="$(line_count "$ROOT/$file")"
    if (( current_lines > previous_lines )); then
      echo "source line budget: COMPARISON REGRESSION $file ($current_lines > $BASE_REF $previous_lines)" >&2
      failures=$((failures + 1))
    fi
  done < "$comparison_baseline"
fi

if (( failures > 0 )); then
  exit 1
fi

comparison_note=""
if [[ -n "$BASE_REF" ]]; then
  comparison_note="; no debt growth from $BASE_REF"
fi
echo "source line budget: PASS ($debt legacy oversized files, exact ratchet; new source <= $LIMIT$comparison_note)"
