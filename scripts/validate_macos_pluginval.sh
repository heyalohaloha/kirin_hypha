#!/usr/bin/env bash
# Recreate the macOS role-first installed VST3 layout and validate it with Tracktion pluginval.
#
# Usage:
#   scripts/validate_macos_pluginval.sh [juce_build_dir]
#
# Defaults:
#   juce_build_dir = juce_shell/build-universal
#   PLUGINVAL_VERSION = v1.0.4
#   PLUGINVAL_STRICTNESS_LEVEL = 5
#
# Set PLUGINVAL_BIN=/path/to/pluginval to use an already-installed binary.
# Set VST3_VALIDATOR_BIN=/path/to/validator to include Steinberg VST3 validation.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "ERROR: macOS pluginval validation must run on Darwin." >&2
  exit 1
fi

BUILD_DIR="${1:-${KIRIN_MACOS_VST3_DIR:-juce_shell/build-universal}}"
STRICTNESS="${PLUGINVAL_STRICTNESS_LEVEL:-5}"
TIMEOUT_MS="${PLUGINVAL_TIMEOUT_MS:-120000}"
SKIP_GUI_TESTS="${PLUGINVAL_SKIP_GUI_TESTS:-0}"
PLUGINVAL_VERSION="${PLUGINVAL_VERSION:-v1.0.4}"
OUTPUT_DIR="${PLUGINVAL_OUTPUT_DIR:-target/pluginval/logs/macos}"
CACHE_DIR="${PLUGINVAL_CACHE_DIR:-target/pluginval/bin/macos/${PLUGINVAL_VERSION}}"
RUNTIME_DIR="${PLUGINVAL_RUNTIME_DIR:-target/pluginval/runtime/macos/$(date +%Y%m%d%H%M%S)-$$}"
if [[ "$RUNTIME_DIR" != /* ]]; then
  RUNTIME_DIR="$ROOT/$RUNTIME_DIR"
fi
RUN_HOME="$RUNTIME_DIR/home"
RUN_TMP="$RUNTIME_DIR/tmp"

case "$STRICTNESS" in
  ''|*[!0-9]*)
    echo "ERROR: PLUGINVAL_STRICTNESS_LEVEL must be an integer from 1 to 10." >&2
    exit 1
    ;;
esac
if [[ "$STRICTNESS" -lt 1 || "$STRICTNESS" -gt 10 ]]; then
  echo "ERROR: PLUGINVAL_STRICTNESS_LEVEL must be between 1 and 10." >&2
  exit 1
fi
case "$TIMEOUT_MS" in
  ''|*[!0-9]*)
    echo "ERROR: PLUGINVAL_TIMEOUT_MS must be a positive integer." >&2
    exit 1
    ;;
esac
if [[ "$TIMEOUT_MS" -lt 1 ]]; then
  echo "ERROR: PLUGINVAL_TIMEOUT_MS must be a positive integer." >&2
  exit 1
fi
case "$SKIP_GUI_TESTS" in
  1|true|TRUE|yes|YES)
    SKIP_GUI_TESTS=1
    ;;
  0|false|FALSE|no|NO)
    SKIP_GUI_TESTS=0
    ;;
  *)
    echo "ERROR: PLUGINVAL_SKIP_GUI_TESTS must be 0/1 or true/false." >&2
    exit 1
    ;;
esac

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "ERROR: JUCE universal build dir not found: $BUILD_DIR" >&2
  echo "Build first: scripts/build_juce_universal.sh" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR" "$CACHE_DIR" "$RUN_HOME" "$RUN_TMP"

# Each invocation owns its archive bytes through verification and extraction.
# The shared cache is only an acquisition hint, never the verified execution input.
ARCHIVE_WORK=""
CACHE_PART=""
cleanup_archive() {
  if [[ -n "$CACHE_PART" ]]; then rm -f "$CACHE_PART"; fi
  if [[ -n "$ARCHIVE_WORK" ]]; then rm -rf "$ARCHIVE_WORK"; fi
}
trap cleanup_archive EXIT

PINNED_TOOL=1
if [[ -n "${PLUGINVAL_BIN:-}" ]]; then
  if [[ "${CI:-}" == "true" ]]; then
    echo "ERROR: CI requires a pinned archive, not PLUGINVAL_BIN." >&2
    exit 1
  fi
  PINNED_TOOL=0
  PLUGINVAL_EXE="$PLUGINVAL_BIN"
  printf '%s\n' '{"qualification":"diagnostic-only","tool":"explicit-unpinned-override"}' > "$OUTPUT_DIR/pluginval-tool.json"
  echo "WARNING: explicit unpinned tool; diagnostic only, not a formal qualification." >&2
else
  URL="$(node scripts/pluginval_archive.mjs url "$PLUGINVAL_VERSION" macOS)"
  CACHE_ZIP="$CACHE_DIR/pluginval_macOS.zip"
  ARCHIVE_WORK="$(mktemp -d "$CACHE_DIR/archive.XXXXXX")"
  ZIP="$ARCHIVE_WORK/pluginval_macOS.zip"
  DOWNLOADED=0
  if [[ -f "$CACHE_ZIP" ]]; then
    cp "$CACHE_ZIP" "$ZIP"
  else
    # Retry transport failures only. Completed corrupt bytes fail below, without retry.
    for attempt in 1 2 3 4 5 6; do
      if curl -fL --connect-timeout 30 --retry 3 --retry-delay 5 "$URL" -o "$ZIP"; then break; fi
      if [[ "$attempt" == 6 ]]; then echo "ERROR: pluginval download failed" >&2; exit 1; fi
      sleep $((attempt * 10))
    done
    DOWNLOADED=1
  fi
  node scripts/pluginval_archive.mjs verify "$PLUGINVAL_VERSION" macOS "$ZIP" > "$OUTPUT_DIR/pluginval-tool.json"
  if [[ "$DOWNLOADED" == 1 ]]; then
    # Publish verified bytes atomically, without making the shared name our input again.
    CACHE_PART="$(mktemp "$CACHE_DIR/publish.XXXXXX")"
    cp "$ZIP" "$CACHE_PART"
    mv "$CACHE_PART" "$CACHE_ZIP"
    CACHE_PART=""
  fi
  # Never trust an old expanded app, nor overlay a new archive onto one.
  VERIFIED_STAGE="$ARCHIVE_WORK/expanded"
  mkdir "$VERIFIED_STAGE"
  unzip -q "$ZIP" -d "$VERIFIED_STAGE"
  PLUGINVAL_EXE="$VERIFIED_STAGE/pluginval.app/Contents/MacOS/pluginval"
fi

if [[ ! -x "$PLUGINVAL_EXE" ]]; then
  echo "ERROR: pluginval executable not found or not executable: $PLUGINVAL_EXE" >&2
  exit 1
fi

if command -v xattr >/dev/null 2>&1; then
  XATTR_TARGET="$PLUGINVAL_EXE"
  if [[ "$PLUGINVAL_EXE" == *.app/Contents/MacOS/* ]]; then
    XATTR_TARGET="${PLUGINVAL_EXE%%.app/Contents/MacOS/*}.app"
  fi
  xattr -dr com.apple.quarantine "$XATTR_TARGET" 2>/dev/null || true
fi

STAGE_ROOT="$RUNTIME_DIR/ship-set"
mkdir -p "$STAGE_ROOT"
BUNDLES=()
while IFS=$'\t' read -r SOURCE_BUNDLE INSTALL_RELATIVE LABEL; do
  if [[ ! -d "$SOURCE_BUNDLE" ]]; then
    echo "ERROR: missing ship-set VST3 bundle: $SOURCE_BUNDLE" >&2
    exit 1
  fi
  STAGED_BUNDLE="$STAGE_ROOT/$INSTALL_RELATIVE"
  mkdir -p "$(dirname "$STAGED_BUNDLE")"
  ditto "$SOURCE_BUNDLE" "$STAGED_BUNDLE"
  BUNDLES+=("$STAGED_BUNDLE")
  echo "==> staged $LABEL: $STAGED_BUNDLE"
done < <(
  node scripts/ls_release/kirin_hypha_ship_bundles.mjs \
    --build-root "$BUILD_DIR" \
    --kind vst3
)

if [[ "${#BUNDLES[@]}" -ne 2 ]]; then
  echo "ERROR: ship bundle manifest did not resolve exactly two VST3 bundles." >&2
  exit 1
fi

cargo run --quiet --package xtask -- \
  ship-bundle-verify \
  --build-root "$BUILD_DIR" \
  --installed-root "$STAGE_ROOT" \
  --format vst3

echo "==> pluginval strictness level: $STRICTNESS"
echo "==> pluginval timeout ms: $TIMEOUT_MS"
echo "==> pluginval skip GUI tests: $SKIP_GUI_TESTS"
echo "==> pluginval output dir: $OUTPUT_DIR"
echo "==> pluginval isolated runtime dir: $RUNTIME_DIR"
if [[ -n "${VST3_VALIDATOR_BIN:-}" ]]; then
  if [[ ! -x "$VST3_VALIDATOR_BIN" ]]; then
    echo "ERROR: VST3_VALIDATOR_BIN is not executable: $VST3_VALIDATOR_BIN" >&2
    exit 1
  fi
  echo "==> Steinberg VST3 validator: $VST3_VALIDATOR_BIN"
fi

while IFS= read -r BUNDLE; do
  echo "==> pluginval: $BUNDLE"
  PLUGINVAL_ARGS=(
    --validate-in-process
    --strictness-level "$STRICTNESS"
    --timeout-ms "$TIMEOUT_MS"
    --output-dir "$OUTPUT_DIR"
  )
  if [[ "$SKIP_GUI_TESTS" == "1" ]]; then
    PLUGINVAL_ARGS+=(--skip-gui-tests)
  fi
  if [[ -n "${VST3_VALIDATOR_BIN:-}" ]]; then
    PLUGINVAL_ARGS+=(--vst3validator "$VST3_VALIDATOR_BIN")
  fi
  env HOME="$RUN_HOME" TMPDIR="$RUN_TMP" KIRIN_HYPHA_TEST_STORAGE_ROOT="$RUNTIME_DIR" \
    "$PLUGINVAL_EXE" "${PLUGINVAL_ARGS[@]}" "$BUNDLE"
done < <(printf '%s\n' "${BUNDLES[@]}")

if [[ "$PINNED_TOOL" != 1 ]]; then
  echo "==> pluginval diagnostic finished; unpinned tool is NOT a formal qualification" >&2
  exit 2
fi
echo "==> pluginval OK: ${#BUNDLES[@]} role-first installed-layout VST3 bundles"
