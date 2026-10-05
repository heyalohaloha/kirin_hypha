#!/usr/bin/env bash
# The JUCE patch state check under macOS's /bin/bash 3.2 (set -u stops on an empty array expansion):
# an untouched JUCE at the pinned commit counts 0 patches, and the patched submodule counts the stack.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
SHELLS=(bash)
[[ -x /bin/bash ]] && SHELLS+=(/bin/bash)
if [[ ! -e juce_shell/JUCE/.git ]]; then
  echo "JUCE patch state test: SKIPPED (the JUCE submodule is not checked out)"
  exit 0
fi
EXPECTED_HEAD="$(sed -n 's/^EXPECTED_HEAD="\([0-9a-f]*\)"$/\1/p' scripts/verify_juce_patch_state.sh)"
PATCHES="$(grep -c '\.patch::' scripts/verify_juce_patch_state.sh)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/kirin-juce-pristine.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
git clone --quiet --shared --no-checkout "$ROOT/juce_shell/JUCE" "$tmp/JUCE"
git -C "$tmp/JUCE" checkout --quiet "$EXPECTED_HEAD"
for shell in "${SHELLS[@]}"; do
  count="$(KIRIN_JUCE_DIR="$tmp/JUCE" "$shell" scripts/verify_juce_patch_state.sh --applied-count)"
  [[ "$count" == "0" ]] || { echo "error: $shell counted $count patches on an untouched JUCE" >&2; exit 1; }
  count="$("$shell" scripts/verify_juce_patch_state.sh --applied-count)"
  [[ "$count" == "0" || "$count" == "$PATCHES" ]] \
    || { echo "error: $shell counted $count of $PATCHES patches on the submodule" >&2; exit 1; }
done
echo "JUCE patch state test: PASS (${SHELLS[*]})"
