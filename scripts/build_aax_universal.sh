#!/usr/bin/env bash
# Build the opt-in macOS Universal AAX targets against an external licensed SDK.
# The SDK and all PACE credentials remain outside this GPL repository.
set -euo pipefail

cd "$(dirname "$0")/.."
KIRIN_ROOT="$PWD"
AAX_SDK_PATH=""
LICENSE_CONFIRMED=0
SIGN_OUTPUT=0
DIAGNOSTIC_OUTPUT=0
KIMERA_FONT_FILE=""
KIMERA_LICENSE_CONFIRMED=0

usage() {
  cat <<'EOF'
Usage: scripts/build_aax_universal.sh --sdk PATH --license-confirmed [--diagnostic | --sign]

Options:
  --sdk PATH             External AAX SDK root containing Interfaces/ACF
  --license-confirmed    Confirm that the external SDK may be used for this build
  --sign                 PACE + Developer ID sign PRE and POST after building
  --diagnostic           Build an unsigned, explicitly non-distributable diagnostic artifact
  --kimera-font PATH     Licensed KMR Waldenburg Book OTF kept outside the repository
  --kimera-license-confirmed
                         Confirm the font is covered by the Kirin Hypha App License

Signing environment (required with --sign):
  KIRIN_AAX_PACE_CUSTOMER_NUMBER
  KIRIN_AAX_PACE_CUSTOMER_NAME
  KIRIN_AAX_APPLE_SIGN_IDENTITY
  KIRIN_AAX_WRAPTOOL             Optional wraptool executable override
EOF
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --sdk)
      [[ $# -ge 2 ]] || fail "--sdk requires a path"
      AAX_SDK_PATH="$2"
      shift 2
      ;;
    --license-confirmed)
      LICENSE_CONFIRMED=1
      shift
      ;;
    --sign)
      SIGN_OUTPUT=1
      shift
      ;;
    --diagnostic)
      DIAGNOSTIC_OUTPUT=1
      shift
      ;;
    --kimera-font)
      [[ $# -ge 2 ]] || fail "--kimera-font requires a path"
      KIMERA_FONT_FILE="$2"
      shift 2
      ;;
    --kimera-license-confirmed)
      KIMERA_LICENSE_CONFIRMED=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *) fail "unknown option: $1" ;;
  esac
done

[[ "$SIGN_OUTPUT" == 0 || "$DIAGNOSTIC_OUTPUT" == 0 ]] \
  || fail "--sign and --diagnostic are mutually exclusive"

[[ -n "$AAX_SDK_PATH" ]] || fail "--sdk is required"
[[ "$LICENSE_CONFIRMED" == 1 ]] || fail "--license-confirmed is required"
[[ -d "$AAX_SDK_PATH/Interfaces/ACF" ]] || fail "SDK must contain Interfaces/ACF"
AAX_SDK_PATH="$(cd "$AAX_SDK_PATH" && pwd -P)"
case "$AAX_SDK_PATH/" in
  "$KIRIN_ROOT/"*) fail "AAX SDK must remain outside the repository" ;;
esac
if [[ -n "$KIMERA_FONT_FILE" ]]; then
  [[ "$KIMERA_LICENSE_CONFIRMED" == 1 ]] \
    || fail "--kimera-font requires --kimera-license-confirmed"
  [[ -f "$KIMERA_FONT_FILE" ]] || fail "Kimera font file does not exist"
  KIMERA_FONT_FILE="$(cd "$(dirname "$KIMERA_FONT_FILE")" && pwd -P)/$(basename "$KIMERA_FONT_FILE")"
  case "$KIMERA_FONT_FILE/" in
    "$KIRIN_ROOT/"*) fail "licensed Kimera font must remain outside the repository" ;;
  esac
elif [[ "$KIMERA_LICENSE_CONFIRMED" == 1 ]]; then
  fail "--kimera-license-confirmed requires --kimera-font"
fi
if [[ "$DIAGNOSTIC_OUTPUT" == 1 && ( -n "$KIMERA_FONT_FILE" || "$KIMERA_LICENSE_CONFIRMED" == 1 ) ]]; then
  fail "--diagnostic cannot include Kimera font or license confirmation"
fi

RELEASE_SOURCE_ID=""
WRAPTOOL=""
if [[ "$SIGN_OUTPUT" == 1 ]]; then
  [[ -n "$KIMERA_FONT_FILE" ]] \
    || fail "distribution signing requires --kimera-font and --kimera-license-confirmed"
  : "${KIRIN_AAX_PACE_CUSTOMER_NUMBER:?required with --sign}"
  : "${KIRIN_AAX_PACE_CUSTOMER_NAME:?required with --sign}"
  : "${KIRIN_AAX_APPLE_SIGN_IDENTITY:?required with --sign}"
  WRAPTOOL="${KIRIN_AAX_WRAPTOOL:-/Applications/PACEAntiPiracy/Eden/Fusion/Current/bin/wraptool}"
  [[ -x "$WRAPTOOL" ]] || fail "wraptool is not executable; set KIRIN_AAX_WRAPTOOL"
fi

bash scripts/apply_juce_patches.sh
bash scripts/verify_juce_patch_state.sh

if [[ "$SIGN_OUTPUT" == 1 ]]; then
  RELEASE_SOURCE_ID="$(node scripts/ls_release/release_source_identity.mjs \
    --require-clean --field commit)"
fi

echo "==> build kirin_hypha_ffi for both Apple architectures"
cargo build --release -p kirin_hypha_ffi --target x86_64-apple-darwin --locked
cargo build --release -p kirin_hypha_ffi --target aarch64-apple-darwin --locked

mkdir -p target/universal
lipo -create \
  target/x86_64-apple-darwin/release/libkirin_hypha_ffi.a \
  target/aarch64-apple-darwin/release/libkirin_hypha_ffi.a \
  -output target/universal/libkirin_hypha_ffi.a

echo "==> configure external-SDK Universal AAX build"
cmake_args=(
  -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64' \
  "-DKIRIN_FFI_LIB=$KIRIN_ROOT/target/universal/libkirin_hypha_ffi.a" \
  "-DKIRIN_HYPHA_AAX_SDK_PATH=$AAX_SDK_PATH" \
  -DKIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=ON \
  -DKIRIN_HYPHA_REQUIRE_AAX=ON)
if [[ -n "$KIMERA_FONT_FILE" ]]; then
  cmake_args+=(
    "-DKIRIN_HYPHA_KIMERA_FONT_FILE=$KIMERA_FONT_FILE"
    -DKIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED=ON
    -DKIRIN_HYPHA_REQUIRE_KIMERA_FONT=ON)
else
  cmake_args+=(
    -DKIRIN_HYPHA_KIMERA_FONT_FILE=
    -DKIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED=OFF
    -DKIRIN_HYPHA_REQUIRE_KIMERA_FONT=OFF)
fi
cmake -S juce_shell -B build-aax-universal "${cmake_args[@]}"

# A prior signed bundle leaves PACE compatibility links and Apple signature resources in the
# product directory. Remove only the two generated AAX products so an unsigned diagnostic build
# cannot inherit stale signing material and a release signing pass always starts from fresh output.
for role in PRE POST; do
  cmake -E remove_directory \
    "build-aax-universal/KirinHypha${role}_artefacts/Release/AAX/Kirin Hypha ${role}.aaxplugin"
done
cmake --build build-aax-universal --config Release \
  --target KirinHyphaPRE_AAX KirinHyphaPOST_AAX --parallel 2

for role in PRE POST; do
  bundle="build-aax-universal/KirinHypha${role}_artefacts/Release/AAX/Kirin Hypha ${role}.aaxplugin"
  binary="$bundle/Contents/MacOS/Kirin Hypha ${role}"
  archs="$(lipo -archs "$binary")"
  [[ " $archs " == *" x86_64 "* && " $archs " == *" arm64 "* ]] \
    || fail "${role} is not Universal: $archs"
done

if [[ "$SIGN_OUTPUT" == 1 ]]; then
  for role in PRE POST; do
    lower="$(printf '%s' "$role" | tr '[:upper:]' '[:lower:]')"
    bundle="build-aax-universal/KirinHypha${role}_artefacts/Release/AAX/Kirin Hypha ${role}.aaxplugin"
    "$WRAPTOOL" sign \
      --in "$bundle" \
      --customernumber "$KIRIN_AAX_PACE_CUSTOMER_NUMBER" \
      --customername "$KIRIN_AAX_PACE_CUSTOMER_NAME" \
      --signid "$KIRIN_AAX_APPLE_SIGN_IDENTITY" \
      --dsig1-compat on
    node scripts/ls_release/aax_bundle_verify.mjs \
      --bundle "$bundle" \
      --executable "Kirin Hypha ${role}" \
      --identifier "com.kirinmastering.hypha.${lower}" \
      --version "$(sed -n 's/^version = "\([^"]*\)"/\1/p' crates/hypha_pre/Cargo.toml | head -1)" \
      --source-id "$RELEASE_SOURCE_ID" \
      --source-state "clean source" \
      --require-kimera \
      --require-native-only
  done
  echo "==> Universal AAX PRE/POST signed and verified"
elif [[ "$DIAGNOSTIC_OUTPUT" == 1 ]]; then
  node scripts/ls_release/aax_diagnostic_receipt.mjs --artifact-dir build-aax-universal
  echo "==> Universal AAX PRE/POST diagnostic artifact written; never use for distribution"
else
  echo "==> Universal AAX PRE/POST built but not distribution-ready; rerun with --sign"
fi
