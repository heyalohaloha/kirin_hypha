#!/usr/bin/env bash
# Validate the macOS ship-set VST3 bundles with Tracktion pluginval.
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

if [[ -n "${PLUGINVAL_BIN:-}" ]]; then
  PLUGINVAL_EXE="$PLUGINVAL_BIN"
else
  PLUGINVAL_EXE="$CACHE_DIR/pluginval.app/Contents/MacOS/pluginval"
  if [[ ! -x "$PLUGINVAL_EXE" ]]; then
    ZIP="$CACHE_DIR/pluginval_macOS.zip"
    if [[ "$PLUGINVAL_VERSION" == "latest" ]]; then
      URL="https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip"
    else
      URL="https://github.com/Tracktion/pluginval/releases/download/${PLUGINVAL_VERSION}/pluginval_macOS.zip"
    fi
    echo "==> download pluginval: $URL"
    rm -rf "$CACHE_DIR/pluginval.app" "$CACHE_DIR/unpacked"
    mkdir -p "$CACHE_DIR/unpacked"
    curl -L "$URL" -o "$ZIP"
    unzip -q "$ZIP" -d "$CACHE_DIR/unpacked"
    APP_PATH="$(find "$CACHE_DIR/unpacked" -maxdepth 3 -type d -name pluginval.app | head -n 1)"
    if [[ -z "$APP_PATH" ]]; then
      echo "ERROR: pluginval.app not found in downloaded archive." >&2
      exit 1
    fi
    mv "$APP_PATH" "$CACHE_DIR/pluginval.app"
  fi
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

BUNDLES=(
  "$BUILD_DIR/KirinHyphaPRE_artefacts/Release/VST3/Kirin Hypha PRE.vst3"
  "$BUILD_DIR/KirinHyphaPOST_artefacts/Release/VST3/Kirin Hypha POST.vst3"
)
PHYSICAL_NAMES=(
  "Kirin Hypha PRE"
  "Kirin Hypha POST"
)
DISPLAY_NAMES=(
  "PRE Kirin Hypha"
  "POST Kirin Hypha"
)
COMPONENT_CIDS=(
  "4B6972696E4879706861505245763031"
  "4B6972696E4879706861504F53547631"
)

for idx in "${!BUNDLES[@]}"; do
  BUNDLE="${BUNDLES[$idx]}"
  DISPLAY_NAME="${DISPLAY_NAMES[$idx]}"
  PHYSICAL_NAME="${PHYSICAL_NAMES[$idx]}"
  if [[ ! -d "$BUNDLE" ]]; then
    echo "ERROR: missing ship-set VST3 bundle: $BUNDLE" >&2
    exit 1
  fi
  PLIST="$BUNDLE/Contents/Info.plist"
  if [[ ! -f "$PLIST" ]]; then
    echo "ERROR: missing Info.plist in VST3 bundle: $BUNDLE" >&2
    exit 1
  fi
  for key in CFBundleDisplayName CFBundleName; do
    ACTUAL="$(/usr/libexec/PlistBuddy -c "Print :$key" "$PLIST" 2>/dev/null || true)"
    if [[ "$ACTUAL" != "$PHYSICAL_NAME" ]]; then
      echo "ERROR: $BUNDLE $key=$ACTUAL, expected $PHYSICAL_NAME." >&2
      echo "Run: scripts/build_juce_universal.sh" >&2
      exit 1
    fi
  done
  MODULEINFO="$BUNDLE/Contents/Resources/moduleinfo.json"
  if [[ ! -f "$MODULEINFO" ]] || ! grep -q "\"CID\": \"${COMPONENT_CIDS[$idx]}\"" "$MODULEINFO"; then
    echo "ERROR: $BUNDLE does not preserve the shipped component CID ${COMPONENT_CIDS[$idx]}." >&2
    exit 1
  fi
  if ! grep -q "\"Name\": \"$DISPLAY_NAME\"" "$MODULEINFO"; then
    echo "ERROR: $BUNDLE does not expose role-first display name $DISPLAY_NAME." >&2
    exit 1
  fi
done

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
  env HOME="$RUN_HOME" TMPDIR="$RUN_TMP" "$PLUGINVAL_EXE" "${PLUGINVAL_ARGS[@]}" "$BUNDLE"
done < <(printf '%s\n' "${BUNDLES[@]}")

echo "==> pluginval OK: ${#BUNDLES[@]} macOS ship-set VST3 bundles"
