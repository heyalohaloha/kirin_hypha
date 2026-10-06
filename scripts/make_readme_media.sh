#!/usr/bin/env bash
# Redraw the README pictures (docs/media/readme) with the shipping editor and invented data.
# Usage: scripts/make_readme_media.sh <KirinUiRenderContractTests binary>
# Needs ffmpeg. After a change, update the digests in scripts/ls_release/release_metadata.test.mjs.
set -euo pipefail

binary="${1:?usage: scripts/make_readme_media.sh <KirinUiRenderContractTests binary>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/docs/media/readme"
renders="$(mktemp -d)"
trap 'rm -rf "$renders"' EXIT

KIRIN_HYPHA_README_MEDIA_DIR="$renders" KIRIN_HYPHA_REVIEW_ONLY=1 "$binary" >/dev/null

mkdir -p "$out"
for name in vu level listen time drum sharp live freq space ref_b ref_c ref_v blind blind_result; do
  ffmpeg -loglevel error -y -i "$renders/$name.png" -q:v 3 -pix_fmt yuvj444p "$out/$name.jpg"
done

# The tour: each screen for about two seconds at the editor's own 900 x 600.
list="$renders/tour.txt"
: > "$list"
for item in vu:2.4 level:2.0 time:1.8 drum:2.0 freq:1.8 space:1.6 ref_b:1.8 ref_c:2.0 ref_v:2.0 \
            listen:1.8 blind:2.0 blind_result:2.4; do
  printf "file '%s'\nduration %s\n" "$renders/${item%%:*}.png" "${item##*:}" >> "$list"
done
printf "file '%s'\n" "$renders/blind_result.png" >> "$list"
ffmpeg -loglevel error -y -f concat -safe 0 -i "$list" \
  -vf "scale=900:600:flags=lanczos,split[a][b];[a]palettegen=max_colors=256:stats_mode=full[p];[b][p]paletteuse=dither=sierra2_4a" \
  -fps_mode vfr "$out/tour.gif"
