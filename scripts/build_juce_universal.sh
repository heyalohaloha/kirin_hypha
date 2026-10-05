#!/usr/bin/env bash
# B-082: universal (x86_64 + arm64) build of the Kirin Hypha JUCE AU+VST3 shells.
#
# Builds kirin_hypha_ffi for both Apple targets, lipos them into a universal staticlib,
# then configures + builds the JUCE PRE/POST shells universal into juce_shell/build-universal.
# Produces 4 universal bundles: KirinHypha{PRE,POST}.{component,vst3} (Release/AU, Release/VST3).
#
# engine(kirin_measure) calculation is unchanged -- this is a build/distribution-layer step.
# The dev loop (juce_shell/build, x86_64, host staticlib) is untouched.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
# Each invocation supplies an explicit input, including empty; cached keys never enable checking.
export KIRIN_HYPHA_UPDATE_PUBLIC_KEY_INPUT="${KIRIN_HYPHA_UPDATE_PUBLIC_KEY:-}"
node scripts/updates/update_key_binding.mjs key >/dev/null

echo "==> cargo build kirin_hypha_ffi (x86_64-apple-darwin)"
cargo build --release -p kirin_hypha_ffi --target x86_64-apple-darwin
echo "==> cargo build kirin_hypha_ffi (aarch64-apple-darwin)"
cargo build --release -p kirin_hypha_ffi --target aarch64-apple-darwin

echo "==> lipo -> universal staticlib"
mkdir -p target/universal
lipo -create \
  "target/x86_64-apple-darwin/release/libkirin_hypha_ffi.a" \
  "target/aarch64-apple-darwin/release/libkirin_hypha_ffi.a" \
  -output "target/universal/libkirin_hypha_ffi.a"
lipo -info "target/universal/libkirin_hypha_ffi.a"

# B-091/B-135/B-139: apply local JUCE patches with the exact same flags used by CI.
bash scripts/apply_juce_patches.sh
bash scripts/verify_juce_patch_state.sh

echo "==> cmake configure (universal: x86_64;arm64 + universal staticlib)"
cmake -S juce_shell -B juce_shell/build-universal \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
  -DKIRIN_FFI_LIB="$ROOT/target/universal/libkirin_hypha_ffi.a"

echo "==> cmake build (Release, universal)"
cmake --build juce_shell/build-universal --config Release --clean-first

for role in PRE POST; do
  plist="juce_shell/build-universal/KirinHypha${role}_artefacts/Release/AU/Kirin Hypha ${role}.component/Contents/Info.plist"
  # Dotted plist keys cannot be extracted with a dotted keypath; use the existing
  # PlistBuddy dictionary path, then verify the value instead of mere key presence.
  files_access="$(/usr/libexec/PlistBuddy -c 'Print :AudioComponents:0:resourceUsage:temporary-exception.files.all.read-write' "$plist")"
  if [[ "$files_access" != "true" ]]; then
    echo "ERROR: ${role} AU missing true files.all permission" >&2
    exit 1
  fi
done
node scripts/updates/update_key_binding.mjs mac-tree juce_shell/build-universal 4 >/dev/null

echo "==> universal bundles under juce_shell/build-universal/KirinHypha{PRE,POST}_artefacts/Release/"
