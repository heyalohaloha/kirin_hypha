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

# B-091: apply juce_shell/patches/0001 (macOS 15 SDK CGWindowListCreateImage bypass) before cmake.
# Idempotent: on a tree where it is already applied (dev), the reverse-check passes and we skip;
# on a fresh checkout (no patch) we apply it. The submodule is never committed (PATCHES.md
# discipline); the patch is applied at build time only. Subshell keeps the outer cwd unchanged.
echo "==> apply juce_shell/patches/0001 (idempotent)"
(
  cd juce_shell/JUCE
  if git apply --reverse --check ../patches/0001-macos15-sdk-cgwindowlistcreateimage-bypass.patch 2>/dev/null; then
    echo "patches/0001 already applied — skipping"
  else
    git apply ../patches/0001-macos15-sdk-cgwindowlistcreateimage-bypass.patch
    echo "patches/0001 applied"
  fi
)

echo "==> cmake configure (universal: x86_64;arm64 + universal staticlib)"
cmake -S juce_shell -B juce_shell/build-universal \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
  -DKIRIN_FFI_LIB="$ROOT/target/universal/libkirin_hypha_ffi.a"

echo "==> cmake build (Release, universal)"
cmake --build juce_shell/build-universal --config Release

echo "==> universal bundles under juce_shell/build-universal/KirinHypha{PRE,POST}_artefacts/Release/"
