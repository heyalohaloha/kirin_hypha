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
  # A nested temporary tree must not inherit this disposable repository's path prefix.
  count="$(TMPDIR="$tmp/JUCE" KIRIN_JUCE_DIR="$tmp/JUCE" "$shell" scripts/verify_juce_patch_state.sh --applied-count)"
  [[ "$count" == "0" ]] || { echo "error: $shell counted $count patches with nested TMPDIR" >&2; exit 1; }
  count="$("$shell" scripts/verify_juce_patch_state.sh --applied-count)"
  [[ "$count" == "0" || "$count" == "$PATCHES" ]] \
    || { echo "error: $shell counted $count of $PATCHES patches on the submodule" >&2; exit 1; }
  # Sparse expected-tree reconstruction must retain every rejection of the full-tree guard.
  target="modules/juce_audio_basics/audio_play_head/juce_AudioPlayHead.h"
  printf '\nfixture tamper\n' >> "$tmp/JUCE/$target"
  if KIRIN_JUCE_DIR="$tmp/JUCE" "$shell" scripts/verify_juce_patch_state.sh --applied-count >/dev/null 2>&1; then
    echo "error: $shell accepted altered patch target" >&2; exit 1
  fi
  git -C "$tmp/JUCE" add "$target"
  if KIRIN_JUCE_DIR="$tmp/JUCE" "$shell" scripts/verify_juce_patch_state.sh --applied-count >/dev/null 2>&1; then
    echo "error: $shell accepted staged changes" >&2; exit 1
  fi
  git -C "$tmp/JUCE" restore --staged --worktree -- "$target"
  printf 'fixture\n' > "$tmp/JUCE/fixture-untracked.txt"
  if KIRIN_JUCE_DIR="$tmp/JUCE" "$shell" scripts/verify_juce_patch_state.sh --applied-count >/dev/null 2>&1; then
    echo "error: $shell accepted untracked file" >&2; exit 1
  fi
  rm "$tmp/JUCE/fixture-untracked.txt"
  printf '\nfixture tamper\n' >> "$tmp/JUCE/LICENSE.md"
  if KIRIN_JUCE_DIR="$tmp/JUCE" "$shell" scripts/verify_juce_patch_state.sh --applied-count >/dev/null 2>&1; then
    echo "error: $shell accepted unrelated tracked changes" >&2; exit 1
  fi
  git -C "$tmp/JUCE" restore --worktree -- LICENSE.md
done
echo "JUCE patch state test: PASS (${SHELLS[*]})"
