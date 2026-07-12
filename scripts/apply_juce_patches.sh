#!/usr/bin/env bash
# Apply the local JUCE patches used by Kirin Hypha builds.
#
# The JUCE submodule stays pinned and uncommitted; these patches are applied
# at build time in local builds and CI.
set -euo pipefail
cd "$(dirname "$0")/.."

# A later patch may intentionally modify lines introduced by an earlier patch, making the
# earlier patch's reverse-check inconclusive. Accept the complete tracked stack up front.
if scripts/verify_juce_patch_state.sh >/dev/null 2>&1; then
  echo "==> JUCE tracked patch stack already applied — skipping"
  exit 0
fi

apply_patch_idempotent() {
  local label="$1"
  local patch="$2"
  shift 2
  local -a flags=("$@")

  echo "==> apply juce_shell/patches/${label} (idempotent)"
  (
    cd juce_shell/JUCE
    if (( ${#flags[@]} )); then
      local -a reverse_check=(git apply "${flags[@]}" --reverse --check "../patches/${patch}")
      local -a apply=(git apply "${flags[@]}" "../patches/${patch}")
    else
      local -a reverse_check=(git apply --reverse --check "../patches/${patch}")
      local -a apply=(git apply "../patches/${patch}")
    fi

    if "${reverse_check[@]}" 2>/dev/null; then
      echo "patches/${label} already applied — skipping"
    else
      "${apply[@]}"
      echo "patches/${label} applied"
    fi
  )
}

apply_patch_idempotent \
  "0001" \
  "0001-macos15-sdk-cgwindowlistcreateimage-bypass.patch"

apply_patch_idempotent \
  "0002" \
  "0002-drop-webkit-osxframework-from-juce-gui-extra.patch"

# B-139: 0003 is intentionally whitespace-tolerant because the JUCE AU wrapper
# source in the pinned submodule carries mixed line endings around this hunk.
apply_patch_idempotent \
  "0003" \
  "0003-au-preferred-channel-layout-tags-for-logic-mono.patch" \
  --unidiff-zero \
  --ignore-whitespace

apply_patch_idempotent \
  "0004" \
  "0004-au-clock-provenance.patch" \
  --unidiff-zero \
  --ignore-whitespace

apply_patch_idempotent \
  "0005" \
  "0005-host-presentation-clock.patch" \
  --unidiff-zero \
  --ignore-whitespace
