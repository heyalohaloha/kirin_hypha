#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASELINE="$ROOT/scripts/source_line_budget.tsv"
LIMIT=500

if [[ ! -f "$BASELINE" ]]; then
  echo "source line budget baseline is missing: $BASELINE" >&2
  exit 1
fi

line_count() {
  awk 'END { print NR }' "$1"
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
  lines="$(line_count "$ROOT/$file")"
  (( lines > LIMIT )) || continue
  debt=$((debt + 1))
  allowed="$(awk -F '\t' -v key="$file" '$1 == key { print $2; found=1 } END { if (!found) exit 1 }' "$BASELINE" || true)"
  if [[ -z "$allowed" ]]; then
    echo "source line budget: NEW violation $file ($lines > $LIMIT)" >&2
    failures=$((failures + 1))
  elif (( lines > allowed )); then
    echo "source line budget: REGRESSION $file ($lines > baseline $allowed)" >&2
    failures=$((failures + 1))
  fi
done < <(cd "$ROOT" && git ls-files -z)

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
  fi
done < "$BASELINE"

if (( failures > 0 )); then
  exit 1
fi

echo "source line budget: PASS ($debt grandfathered files, no increase; target <= $LIMIT)"
