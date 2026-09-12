#!/usr/bin/env bash
# Build the opt-in macOS Universal AAX targets against an external licensed SDK.
# The SDK and all PACE credentials remain outside this GPL repository.
set -euo pipefail

cd "$(dirname "$0")/.."
KIRIN_ROOT="$PWD"
AAX_SDK_PATH=""
LICENSE_CONFIRMED=0
SIGN_OUTPUT=0
DIAGNOSTIC_SIGN_OUTPUT=0
DIAGNOSTIC_OUTPUT=0
DRY_RUN=0
KIMERA_FONT_FILE=""
KIMERA_LICENSE_CONFIRMED=0
DEFAULT_WRAPTOOL="/Applications/PACEAntiPiracy/Eden/Fusion/Versions/6/bin/wraptool"
FALLBACK_WRAPTOOL="/Applications/PACEAntiPiracy/Eden/Fusion/Current/bin/wraptool"
NOTARY_PROFILE="${KIRIN_NOTARY_PROFILE:-kirin-notarize}"

usage() {
  cat <<'EOF'
Usage: scripts/build_aax_universal.sh --sdk PATH --license-confirmed [--diagnostic | --diagnostic-sign | --sign] [--dry-run]

Options:
  --sdk PATH             External AAX SDK root containing Interfaces/ACF
  --license-confirmed    Confirm that the external SDK may be used for this build
  --sign                 PACE + Developer ID sign, notarize, and attest PRE and POST
  --diagnostic           Build an unsigned, explicitly non-distributable diagnostic artifact
  --diagnostic-sign      PACE + Developer ID sign a Kimera-free, unnotarized diagnostic artifact
  --dry-run              Validate and print the build/sign command plan without executing it
  --kimera-font PATH     Licensed KMR Waldenburg Book OTF kept outside the repository
  --kimera-license-confirmed
                         Confirm the font is covered by the Kirin Hypha App License

Signing environment (required with --sign or --diagnostic-sign):
  KIRIN_AAX_PACE_ACCOUNT       Optional account ID; passed to wraptool when set
  KIRIN_AAX_PACE_CUSTOMER_NUMBER
  KIRIN_AAX_PACE_CUSTOMER_NAME
  KIRIN_AAX_APPLE_SIGN_IDENTITY
  KIRIN_AAX_WRAPTOOL             Optional wraptool executable override
  KIRIN_NOTARY_PROFILE           notarytool keychain profile. Default: kirin-notarize
EOF
}

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

run() {
  if [[ "$DRY_RUN" == 1 ]]; then
    local redact_next=0
    local arg
    printf '+'
    for arg in "$@"; do
      if [[ "$redact_next" == 1 ]]; then
        printf ' %q' '<redacted>'
        redact_next=0
      elif [[ "$arg" == "--account" || "$arg" == "--customernumber" || "$arg" == "--customername" || "$arg" == "--signid" ]]; then
        printf ' %q' "$arg"
        redact_next=1
      else
        printf ' %q' "$arg"
      fi
    done
    printf '\n'
    return 0
  fi
  "$@"
}

run_sensitive() {
  if [[ "$DRY_RUN" == 1 ]]; then
    run "$@"
    return
  fi
  local output
  local status
  if output="$("$@" 2>&1)"; then
    status=0
  else
    status=$?
  fi
  local safe="$output"
  local secret
  for secret in \
    "${KIRIN_AAX_PACE_ACCOUNT:-}" \
    "${KIRIN_AAX_PACE_CUSTOMER_NUMBER:-}" \
    "${KIRIN_AAX_PACE_CUSTOMER_NAME:-}" \
    "${KIRIN_AAX_APPLE_SIGN_IDENTITY:-}"; do
    if [[ -n "$secret" ]]; then
      safe="${safe//"$secret"/<redacted>}"
    fi
  done
  if [[ -n "$safe" ]]; then
    printf '%s\n' "$safe"
  fi
  return "$status"
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
    --diagnostic-sign)
      DIAGNOSTIC_SIGN_OUTPUT=1
      shift
      ;;
    --diagnostic)
      DIAGNOSTIC_OUTPUT=1
      shift
      ;;
    --dry-run)
      DRY_RUN=1
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

[[ $((SIGN_OUTPUT + DIAGNOSTIC_SIGN_OUTPUT + DIAGNOSTIC_OUTPUT)) -le 1 ]] \
  || fail "--sign, --diagnostic-sign, and --diagnostic are mutually exclusive"

[[ -n "$AAX_SDK_PATH" ]] || fail "--sdk is required"
[[ "$LICENSE_CONFIRMED" == 1 ]] || fail "--license-confirmed is required"
[[ -d "$AAX_SDK_PATH/Interfaces/ACF" ]] \
  || fail "AAX SDK path is invalid; expected an external SDK root containing Interfaces/ACF"
AAX_SDK_PATH="$(cd "$AAX_SDK_PATH" && pwd -P)"
case "$AAX_SDK_PATH/" in
  "$KIRIN_ROOT/"*) fail "AAX SDK must remain outside the repository" ;;
esac
if [[ -n "$KIMERA_FONT_FILE" ]]; then
  [[ "$KIMERA_LICENSE_CONFIRMED" == 1 ]] \
    || fail "--kimera-font requires --kimera-license-confirmed"
  [[ "$DRY_RUN" == 1 || -f "$KIMERA_FONT_FILE" ]] || fail "Kimera font file does not exist"
  KIMERA_FONT_FILE="$(cd "$(dirname "$KIMERA_FONT_FILE")" && pwd -P)/$(basename "$KIMERA_FONT_FILE")"
  case "$KIMERA_FONT_FILE/" in
    "$KIRIN_ROOT/"*) fail "licensed Kimera font must remain outside the repository" ;;
  esac
elif [[ "$KIMERA_LICENSE_CONFIRMED" == 1 ]]; then
  fail "--kimera-license-confirmed requires --kimera-font"
fi
if [[ "$DIAGNOSTIC_OUTPUT" == 1 || "$DIAGNOSTIC_SIGN_OUTPUT" == 1 ]] \
  && [[ -n "$KIMERA_FONT_FILE" || "$KIMERA_LICENSE_CONFIRMED" == 1 ]]; then
  fail "diagnostic modes cannot include Kimera font or license confirmation"
fi

RELEASE_SOURCE_ID=""
WRAPTOOL=""
WRAPTOOL_SIGN_ARGS=(sign)
if [[ "$SIGN_OUTPUT" == 1 || "$DIAGNOSTIC_SIGN_OUTPUT" == 1 ]]; then
  if [[ "$SIGN_OUTPUT" == 1 ]]; then
    [[ -n "$KIMERA_FONT_FILE" ]] \
      || fail "distribution signing requires --kimera-font and --kimera-license-confirmed"
  fi
  : "${KIRIN_AAX_PACE_CUSTOMER_NUMBER:?required with signing mode}"
  : "${KIRIN_AAX_PACE_CUSTOMER_NAME:?required with signing mode}"
  : "${KIRIN_AAX_APPLE_SIGN_IDENTITY:?required with signing mode}"
  if [[ -n "${KIRIN_AAX_WRAPTOOL:-}" ]]; then
    WRAPTOOL="$KIRIN_AAX_WRAPTOOL"
  elif [[ -x "$DEFAULT_WRAPTOOL" ]]; then
    WRAPTOOL="$DEFAULT_WRAPTOOL"
  else
    WRAPTOOL="$FALLBACK_WRAPTOOL"
  fi
  if [[ -n "${KIRIN_AAX_PACE_ACCOUNT:-}" ]]; then
    WRAPTOOL_SIGN_ARGS+=(--account "$KIRIN_AAX_PACE_ACCOUNT")
  fi
  if [[ "$DRY_RUN" == 0 ]]; then
    [[ -x "$WRAPTOOL" ]] || fail "wraptool is not executable; set KIRIN_AAX_WRAPTOOL"
  fi
fi

if [[ "$DRY_RUN" == 0 ]]; then
  [[ "$(uname -s)" == "Darwin" ]] || fail "this script builds macOS AAX and requires macOS"
fi

run bash scripts/apply_juce_patches.sh
run bash scripts/verify_juce_patch_state.sh

if [[ "$SIGN_OUTPUT" == 1 ]]; then
  if [[ "$DRY_RUN" == 1 ]]; then
    RELEASE_SOURCE_ID="0000000000000000000000000000000000000000"
  else
    RELEASE_SOURCE_ID="$(node scripts/ls_release/release_source_identity.mjs \
      --require-clean --field commit)"
  fi
fi

echo "==> build kirin_hypha_ffi for both Apple architectures"
run cargo build --release -p kirin_hypha_ffi --target x86_64-apple-darwin --locked
run cargo build --release -p kirin_hypha_ffi --target aarch64-apple-darwin --locked

run mkdir -p target/universal
run lipo -create \
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
run cmake -S juce_shell -B build-aax-universal "${cmake_args[@]}"

# A prior signed bundle leaves PACE compatibility links and Apple signature resources in the
# product directory. Remove only the two generated AAX products so a diagnostic build cannot inherit
# stale signing material and every signing pass starts from fresh output.
for role in PRE POST; do
  run cmake -E remove_directory \
    "build-aax-universal/KirinHypha${role}_artefacts/Release/AAX/Kirin Hypha ${role}.aaxplugin"
done
run cmake -E rm -f build-aax-universal/kirin-hypha-macos-aax-notarization.json
run cmake --build build-aax-universal --config Release \
  --target KirinHyphaPRE_AAX KirinHyphaPOST_AAX --parallel 2

for role in PRE POST; do
  bundle="build-aax-universal/KirinHypha${role}_artefacts/Release/AAX/Kirin Hypha ${role}.aaxplugin"
  binary="$bundle/Contents/MacOS/Kirin Hypha ${role}"
  if [[ "$DRY_RUN" == 1 ]]; then
    echo "+ verify universal: $binary"
  else
    archs="$(lipo -archs "$binary")"
    [[ " $archs " == *" x86_64 "* && " $archs " == *" arm64 "* ]] \
      || fail "${role} is not Universal: $archs"
  fi
done

if [[ "$SIGN_OUTPUT" == 1 || "$DIAGNOSTIC_SIGN_OUTPUT" == 1 ]]; then
  for role in PRE POST; do
    lower="$(printf '%s' "$role" | tr '[:upper:]' '[:lower:]')"
    bundle="build-aax-universal/KirinHypha${role}_artefacts/Release/AAX/Kirin Hypha ${role}.aaxplugin"
    run_sensitive "$WRAPTOOL" "${WRAPTOOL_SIGN_ARGS[@]}" \
      --in "$bundle" \
      --customernumber "$KIRIN_AAX_PACE_CUSTOMER_NUMBER" \
      --customername "$KIRIN_AAX_PACE_CUSTOMER_NAME" \
      --signid "$KIRIN_AAX_APPLE_SIGN_IDENTITY" \
      --dsig1-compat on
    if [[ "$SIGN_OUTPUT" == 1 ]]; then
      run node scripts/ls_release/aax_bundle_verify.mjs \
        --bundle "$bundle" \
        --executable "Kirin Hypha ${role}" \
        --identifier "com.kirinmastering.hypha.${lower}" \
        --version "$(sed -n 's/^version = "\([^"]*\)"/\1/p' crates/hypha_pre/Cargo.toml | head -1)" \
        --source-id "$RELEASE_SOURCE_ID" \
        --source-state "clean source" \
        --require-kimera \
        --require-native-only
    fi
  done
  if [[ "$SIGN_OUTPUT" == 1 ]]; then
    run node scripts/ls_release/aax_notarization_receipt.mjs submit \
      --artifact-dir build-aax-universal \
      --keychain-profile "$NOTARY_PROFILE"
    echo "==> Universal AAX PRE/POST signed, notarized, and attested"
  else
    run node scripts/ls_release/aax_diagnostic_receipt.mjs \
      --artifact-dir build-aax-universal \
      --signed
    echo "==> Universal AAX PRE/POST signed for diagnostics; unnotarized and never use for distribution"
  fi
elif [[ "$DIAGNOSTIC_OUTPUT" == 1 ]]; then
  run node scripts/ls_release/aax_diagnostic_receipt.mjs --artifact-dir build-aax-universal
  echo "==> Universal AAX PRE/POST diagnostic artifact written; never use for distribution"
else
  echo "==> Universal AAX PRE/POST built but not distribution-ready; rerun with --sign"
fi
