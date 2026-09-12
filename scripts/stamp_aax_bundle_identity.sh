#!/usr/bin/env bash
# Stamp build provenance into the macOS AAX Info.plist before distribution signing.
set -euo pipefail

[[ $# -eq 3 ]] || {
  echo "usage: stamp_aax_bundle_identity.sh BUNDLE IDENTITY_HEADER KIMERA_EMBEDDED" >&2
  exit 2
}

bundle="$1"
identity_header="$2"
kimera_embedded="$3"
plist="$bundle/Contents/Info.plist"

[[ -f "$plist" ]] || { echo "AAX Info.plist missing: $plist" >&2; exit 1; }
[[ -f "$identity_header" ]] || { echo "build identity header missing: $identity_header" >&2; exit 1; }
[[ "$kimera_embedded" == 0 || "$kimera_embedded" == 1 ]] \
  || { echo "invalid Kimera embedded value: $kimera_embedded" >&2; exit 1; }

source_id="$(sed -n 's/^#define HYPHA_SOURCE_COMMIT "\([0-9a-f]*\)"$/\1/p' "$identity_header")"
source_state="$(sed -n 's/^#define HYPHA_SOURCE_STATE "\([^"]*\)"$/\1/p' "$identity_header")"
[[ "$source_id" =~ ^[0-9a-f]{40}$ ]] || { echo "invalid AAX source commit: $source_id" >&2; exit 1; }
case "$source_state" in
  "clean source"|"modified source"|"unverified source") ;;
  *) echo "invalid AAX source state: $source_state" >&2; exit 1 ;;
esac

for key in \
  KirinHyphaSourceID \
  KirinHyphaSourceState \
  KirinHyphaKimeraEmbedded \
  KirinHyphaAudioSuiteEnabled \
  KirinHyphaAaxBuildMode; do
  /usr/libexec/PlistBuddy -c "Delete :$key" "$plist" >/dev/null 2>&1 || true
done
/usr/libexec/PlistBuddy -c "Add :KirinHyphaSourceID string $source_id" "$plist"
/usr/libexec/PlistBuddy -c "Add :KirinHyphaSourceState string $source_state" "$plist"
/usr/libexec/PlistBuddy -c "Add :KirinHyphaKimeraEmbedded bool $([[ "$kimera_embedded" == 1 ]] && echo true || echo false)" "$plist"
/usr/libexec/PlistBuddy -c "Add :KirinHyphaAudioSuiteEnabled bool false" "$plist"
/usr/libexec/PlistBuddy -c "Add :KirinHyphaAaxBuildMode string $([[ "$kimera_embedded" == 1 ]] && echo release || echo diagnostic)" "$plist"
