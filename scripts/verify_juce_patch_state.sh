#!/usr/bin/env bash
# Verify that the JUCE submodule working tree contains exactly Kirin Hypha's
# tracked local patch stack and nothing else.
#
# --applied-count prints how many patches from the start of the stack the tree holds exactly
# (0 for an unpatched tree) and fails when it matches no prefix. apply_juce_patches.sh uses it
# to bring a checkout built before a patch was appended up to the whole stack.
set -euo pipefail
MODE="verify"
case "${1:-}" in
  "") ;;
  --applied-count) MODE="count" ;;
  *) echo "usage: $0 [--applied-count]" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/.."
ROOT="$PWD"
JUCE_DIR="juce_shell/JUCE"
EXPECTED_HEAD="4f43011b96eb0636104cb3e433894cda98243626"

EXPECTED_FILES=(
  "modules/juce_audio_basics/audio_play_head/juce_AudioPlayHead.h"
  "modules/juce_audio_plugin_client/juce_audio_plugin_client_AAX.cpp"
  "modules/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm"
  "modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp"
  "modules/juce_audio_processors/processors/juce_AudioProcessor.h"
  "modules/juce_gui_basics/native/juce_Windowing_mac.mm"
  "modules/juce_gui_extra/juce_gui_extra.h"
)

PATCHES=(
  "0001-macos15-sdk-cgwindowlistcreateimage-bypass.patch::"
  "0002-drop-webkit-osxframework-from-juce-gui-extra.patch::"
  "0003-au-preferred-channel-layout-tags-for-logic-mono.patch::--unidiff-zero --ignore-whitespace"
  "0004-au-clock-provenance.patch::--unidiff-zero --ignore-whitespace"
  "0005-host-presentation-clock.patch::--unidiff-zero --ignore-whitespace"
  "0006-vst3-component-id-continuity.patch::--unidiff-zero --ignore-whitespace"
  "0007-vst3-host-component-activation.patch::--unidiff-zero --ignore-whitespace"
  "0008-raw-auxiliary-sample-clock.patch::--unidiff-zero --ignore-whitespace"
  "0009-aax-delay-compensation-state.patch::--unidiff-zero --ignore-whitespace"
  "0010-aax-instance-group.patch::--unidiff-zero --ignore-whitespace"
  "0011-aax-engine-clock.patch::--unidiff-zero --ignore-whitespace"
  "0012-au-studio-one-window-relayout.patch::--unidiff-zero --ignore-whitespace"
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

apply_to_expected() {
  local patch="${1%%::*}"
  local flags="${1#*::}"
  if [[ -n "$flags" ]]; then
    # shellcheck disable=SC2086
    (cd "$expected_tree" && git apply $flags "$ROOT/juce_shell/patches/$patch")
  else
    (cd "$expected_tree" && git apply "$ROOT/juce_shell/patches/$patch")
  fi
}

# The pinned JUCE checkout is CRLF while git-apply additions are LF. Compare logical content
# after removing CR so a harmless line-ending rewrite cannot masquerade as an untracked patch.
first_mismatch() {
  local expected
  for expected in "${EXPECTED_FILES[@]}"; do
    if ! cmp -s <(tr -d '\r' < "$expected_tree/$expected") <(tr -d '\r' < "$JUCE_DIR/$expected"); then
      echo "$expected"
      return
    fi
  done
}

if [[ "$MODE" == "count" ]]; then
  applied=-1
  if [[ -z "$(first_mismatch)" ]]; then applied=0; fi
  index=0
  for patch_spec in "${PATCHES[@]}"; do
    index=$((index + 1))
    apply_to_expected "$patch_spec"
    if [[ -z "$(first_mismatch)" ]]; then applied=$index; fi
  done
  (( applied >= 0 )) || die "JUCE files match no prefix of the tracked patch stack"
  echo "$applied"
  exit 0
fi

for patch_spec in "${PATCHES[@]}"; do
  apply_to_expected "$patch_spec"
done

mismatch="$(first_mismatch)"
[[ -z "$mismatch" ]] || die "JUCE file does not match tracked patch stack: ${mismatch}"

echo "JUCE patch state OK: upstream ${EXPECTED_HEAD} + ${#PATCHES[@]} tracked patches"
