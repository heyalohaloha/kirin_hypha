#!/usr/bin/env bash
# Build the opt-in macOS Universal AAX targets against an external licensed SDK.
# The SDK and all PACE credentials remain outside this GPL repository.
set -euo pipefail

cd "$(dirname "$0")/.."
KIRIN_ROOT="$PWD"
AAX_SDK_PATH=""
LICENSE_CONFIRMED=0
SIGN_OUTPUT=0

usage() {
  cat <<'EOF'
Usage: scripts/build_aax_universal.sh --sdk PATH --license-confirmed [--sign]

Options:
  --sdk PATH             External AAX SDK root containing Interfaces/ACF
  --license-confirmed    Confirm that the external SDK may be used for this build
  --sign                 PACE + Developer ID sign PRE and POST after building

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
    -h|--help)
      usage
      exit 0
      ;;
    *) fail "unknown option: $1" ;;
  esac
done

[[ -n "$AAX_SDK_PATH" ]] || fail "--sdk is required"
[[ "$LICENSE_CONFIRMED" == 1 ]] || fail "--license-confirmed is required"
[[ -d "$AAX_SDK_PATH/Interfaces/ACF" ]] || fail "SDK must contain Interfaces/ACF"
AAX_SDK_PATH="$(cd "$AAX_SDK_PATH" && pwd -P)"
case "$AAX_SDK_PATH/" in
  "$KIRIN_ROOT/"*) fail "AAX SDK must remain outside the repository" ;;
esac

echo "==> build kirin_hypha_ffi for both Apple architectures"
cargo build --release -p kirin_hypha_ffi --target x86_64-apple-darwin --locked
cargo build --release -p kirin_hypha_ffi --target aarch64-apple-darwin --locked

mkdir -p target/universal
lipo -create \
  target/x86_64-apple-darwin/release/libkirin_hypha_ffi.a \
  target/aarch64-apple-darwin/release/libkirin_hypha_ffi.a \
  -output target/universal/libkirin_hypha_ffi.a

bash scripts/apply_juce_patches.sh
bash scripts/verify_juce_patch_state.sh

echo "==> configure external-SDK Universal AAX build"
cmake -S juce_shell -B build-aax-universal \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
  -DKIRIN_FFI_LIB="$KIRIN_ROOT/target/universal/libkirin_hypha_ffi.a" \
  -DKIRIN_HYPHA_AAX_SDK_PATH="$AAX_SDK_PATH" \
  -DKIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=ON \
  -DKIRIN_HYPHA_REQUIRE_AAX=ON
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
  : "${KIRIN_AAX_PACE_CUSTOMER_NUMBER:?required with --sign}"
  : "${KIRIN_AAX_PACE_CUSTOMER_NAME:?required with --sign}"
  : "${KIRIN_AAX_APPLE_SIGN_IDENTITY:?required with --sign}"
  WRAPTOOL="${KIRIN_AAX_WRAPTOOL:-/Applications/PACEAntiPiracy/Eden/Fusion/Current/bin/wraptool}"
  [[ -x "$WRAPTOOL" ]] || fail "wraptool is not executable; set KIRIN_AAX_WRAPTOOL"
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
      --version "$(sed -n 's/^version = "\([^"]*\)"/\1/p' crates/hypha_pre/Cargo.toml | head -1)"
  done
  echo "==> Universal AAX PRE/POST signed and verified"
else
  echo "==> Universal AAX PRE/POST built but not distribution-ready; rerun with --sign"
fi
