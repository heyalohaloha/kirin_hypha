#!/usr/bin/env bash
# Verify that the JUCE submodule working tree contains exactly Kirin Hypha's
# tracked local patch stack and nothing else.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
JUCE_DIR="juce_shell/JUCE"
EXPECTED_HEAD="4f43011b96eb0636104cb3e433894cda98243626"

EXPECTED_FILES=(
  "modules/juce_audio_basics/audio_play_head/juce_AudioPlayHead.h"
  "modules/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm"
  "modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp"
  "modules/juce_gui_basics/native/juce_Windowing_mac.mm"
  "modules/juce_gui_extra/juce_gui_extra.h"
)

PATCHES=(
  "0001-macos15-sdk-cgwindowlistcreateimage-bypass.patch::"
  "0002-drop-webkit-osxframework-from-juce-gui-extra.patch::"
  "0003-au-preferred-channel-layout-tags-for-logic-mono.patch::--unidiff-zero --ignore-whitespace"
  "0004-au-clock-provenance.patch::--unidiff-zero --ignore-whitespace"
  "0005-host-presentation-clock.patch::--unidiff-zero --ignore-whitespace"
)

die() {
  echo "error: $*" >&2
  exit 1
}

require_file_in_expected_set() {
  local needle="$1"
  local expected
  for expected in "${EXPECTED_FILES[@]}"; do
    [[ "$needle" == "$expected" ]] && return 0
  done
  die "unexpected JUCE submodule change: ${needle}"
}

[[ -d "$JUCE_DIR" ]] || die "missing ${JUCE_DIR}; initialize submodules first"
[[ -d "$JUCE_DIR/.git" || -f "$JUCE_DIR/.git" ]] || die "${JUCE_DIR} is not a git checkout"

head_sha="$(git -C "$JUCE_DIR" rev-parse HEAD)"
[[ "$head_sha" == "$EXPECTED_HEAD" ]] \
  || die "JUCE HEAD mismatch: expected ${EXPECTED_HEAD}, got ${head_sha}"

staged_files=()
while IFS= read -r path; do
  [[ -n "$path" ]] && staged_files+=("$path")
done < <(git -C "$JUCE_DIR" diff --cached --name-only)
if (( ${#staged_files[@]} )); then
  printf 'error: staged changes inside JUCE submodule are not allowed:\n' >&2
  printf '  %s\n' "${staged_files[@]}" >&2
  exit 1
fi

untracked_files=()
while IFS= read -r path; do
  [[ -n "$path" ]] && untracked_files+=("$path")
done < <(git -C "$JUCE_DIR" ls-files --others --exclude-standard)
if (( ${#untracked_files[@]} )); then
  printf 'error: untracked files inside JUCE submodule are not allowed:\n' >&2
  printf '  %s\n' "${untracked_files[@]}" >&2
  exit 1
fi

dirty_files=()
while IFS= read -r path; do
  [[ -n "$path" ]] && dirty_files+=("$path")
done < <(git -C "$JUCE_DIR" diff --name-only)
for dirty in "${dirty_files[@]}"; do
  require_file_in_expected_set "$dirty"
done

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/kirin-juce-patch-state.XXXXXX")"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

expected_tree="$tmp_dir/expected"
mkdir -p "$expected_tree"
git -C "$JUCE_DIR" checkout-index -a --prefix="$expected_tree/"

for patch_spec in "${PATCHES[@]}"; do
  patch="${patch_spec%%::*}"
  flags="${patch_spec#*::}"
  if [[ -n "$flags" ]]; then
    # shellcheck disable=SC2086
    (cd "$expected_tree" && git apply $flags "$ROOT/juce_shell/patches/$patch")
  else
    (cd "$expected_tree" && git apply "$ROOT/juce_shell/patches/$patch")
  fi
done

for expected in "${EXPECTED_FILES[@]}"; do
  # The pinned JUCE checkout is CRLF while git-apply additions are LF. Compare logical content
  # after removing CR so a harmless line-ending rewrite cannot masquerade as an untracked patch.
  if ! cmp -s <(tr -d '\r' < "$expected_tree/$expected") <(tr -d '\r' < "$JUCE_DIR/$expected"); then
    die "JUCE file does not match tracked patch stack: ${expected}"
  fi
done

echo "JUCE patch state OK: upstream ${EXPECTED_HEAD} + 5 tracked patches"
