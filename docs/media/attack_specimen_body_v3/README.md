# ATTACK specimen body v3 asset provenance

Generated: 2026-09-09

Generator: OpenAI built-in `imagegen`

Mode: reference-guided edit followed by production downsampling

## References

1. The v2 production raster.
2. Daisuke-provided ATTACK four-panel reference: Base / Strength / Texture / Sharpness.
3. Daisuke-provided 300% ATTACK product mockup.
4. Daisuke's direction: a translucent underwater organism evoking a jellyfish or squid, with motion and life.

The references define material, movement, colour, shape, and information hierarchy. UI text, charts, borders, and layout from the reference images are not part of this raster asset.

## Output

| File | Size | Use |
|---|---:|---|
| `crates/hypha_gui/assets/attack_specimen_body_v3.png` | 768×590 | Production source for the native ATTACK specimen painter |

The asset uses a black or near-black RGB background because the native painter thresholds that canvas away, then separates the membrane, warm bioluminescent fibres, and cool perimeter into ARGB layers at startup. The source rectangle itself is never painted into the product UI. The painter intentionally renders the cropped organism at a fixed compact aspect so changes in generator canvas padding cannot change the product silhouette.

## Final generation prompt

```text
Edit the first image into the production raster for the Kirin Hypha ATTACK meter. The second and third images are UI and visual-direction references only. Create one isolated, horizontally oriented underwater organism that evokes the translucent mantle of a jellyfish and the streamlined motion of a squid. It should feel alive, buoyant, and in motion: a broad compact semi-transparent cyan/teal membrane, subtle layered tissue and refractive depth, warm amber-gold bioluminescent vascular filaments flowing from a bright anchored nucleus at the LEFT into the body, and a few elegant internal currents that imply forward movement. Keep the main silhouette compact, approximately 1.35:1 width to height, with softly asymmetric organic lobes. A very short translucent trailing fringe may appear on the RIGHT, but no long tentacles, no literal animal anatomy, no eyes, no face, no fins, no typography, no UI, no border, and no cyan divider lines. Preserve an ample pure-black margin on all sides, with corners exactly black; the app composites this raster onto a black panel. Avoid a solid opaque blob: the membrane and overlapping inner layers must visibly transmit light and create an underwater glasslike transparency. Keep the organism centered and large enough for a 768 x 590 source raster. The visual must remain readable when displayed around 500 x 260 pixels. High-end scientific instrument aesthetic, restrained cyan glow, warm living interior, crisp enough for a professional audio plugin.
```

The built-in generator produced a 1430×1100 original. The production copy was downsampled without creative alteration to 768×590 for first-paint and package-size budgets.
