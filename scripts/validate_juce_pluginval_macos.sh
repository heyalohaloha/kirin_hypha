#!/usr/bin/env bash
# Validate built JUCE VST3 bundles with Tracktion pluginval on macOS.
#
# Usage:
#   scripts/validate_juce_pluginval_macos.sh [juce_build_dir]
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

BUILD_DIR="${1:-${KIRIN_JUCE_BUILD_DIR:-juce_shell/build-universal}}"
STRICTNESS="${PLUGINVAL_STRICTNESS_LEVEL:-5}"
TIMEOUT_MS="${PLUGINVAL_TIMEOUT_MS:-120000}"
PLUGINVAL_VERSION="${PLUGINVAL_VERSION:-v1.0.4}"
OUTPUT_DIR="${PLUGINVAL_OUTPUT_DIR:-target/pluginval/logs/macos}"
CACHE_DIR="${PLUGINVAL_CACHE_DIR:-target/pluginval/bin/macos/${PLUGINVAL_VERSION}}"

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

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "ERROR: JUCE build dir not found: $BUILD_DIR" >&2
  echo "Build first, for example: scripts/build_juce_universal.sh" >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR" "$CACHE_DIR"

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
  xattr -dr com.apple.quarantine "$(dirname "$(dirname "$(dirname "$PLUGINVAL_EXE")")")" 2>/dev/null || true
fi

BUNDLE_LIST="$(mktemp "${TMPDIR:-/tmp}/kirin-pluginval-bundles.XXXXXX")"
trap 'rm -f "$BUNDLE_LIST"' EXIT

find "$BUILD_DIR" -path "*/Release/VST3/*.vst3" -type d | sort > "$BUNDLE_LIST"
BUNDLE_COUNT="$(wc -l < "$BUNDLE_LIST" | tr -d '[:space:]')"
if [[ "$BUNDLE_COUNT" != "2" ]]; then
  echo "ERROR: expected exactly 2 JUCE VST3 bundles under $BUILD_DIR, found $BUNDLE_COUNT." >&2
  cat "$BUNDLE_LIST" >&2
  exit 1
fi

echo "==> pluginval strictness level: $STRICTNESS"
echo "==> pluginval timeout ms: $TIMEOUT_MS"
echo "==> pluginval output dir: $OUTPUT_DIR"
if [[ -n "${VST3_VALIDATOR_BIN:-}" ]]; then
  if [[ ! -x "$VST3_VALIDATOR_BIN" ]]; then
    echo "ERROR: VST3_VALIDATOR_BIN is not executable: $VST3_VALIDATOR_BIN" >&2
    exit 1
  fi
  echo "==> Steinberg VST3 validator: $VST3_VALIDATOR_BIN"
fi

while IFS= read -r BUNDLE; do
  echo "==> pluginval: $BUNDLE"
  if [[ -n "${VST3_VALIDATOR_BIN:-}" ]]; then
    "$PLUGINVAL_EXE" \
      --validate-in-process \
      --strictness-level "$STRICTNESS" \
      --timeout-ms "$TIMEOUT_MS" \
      --output-dir "$OUTPUT_DIR" \
      --vst3validator "$VST3_VALIDATOR_BIN" \
      "$BUNDLE"
  else
    "$PLUGINVAL_EXE" \
      --validate-in-process \
      --strictness-level "$STRICTNESS" \
      --timeout-ms "$TIMEOUT_MS" \
      --output-dir "$OUTPUT_DIR" \
      "$BUNDLE"
  fi
done < "$BUNDLE_LIST"

echo "==> pluginval OK: $BUNDLE_COUNT JUCE VST3 bundles"
