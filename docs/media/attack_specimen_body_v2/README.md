# ATTACK specimen body v2 asset provenance

Generated: 2026-09-09

Generator: OpenAI built-in `imagegen`

Mode: reference-guided generation followed by a single-shape proportion edit and black-background production conversion

## References

1. Daisuke-provided ATTACK four-panel reference: Base / Strength / Texture / Sharpness.
2. Daisuke-provided 300% ATTACK product mockup.
3. `docs/hypha_attack_visual_completion_proposal_20260909.md`.

The references define material, colour, shape, and information hierarchy. UI text, charts, borders, and layout from the reference images are not part of this raster asset.

## Output

| File | Size | Use |
|---|---:|---|
| `crates/hypha_gui/assets/attack_specimen_body_v2.png` | 768×590 | Production source for the native ATTACK specimen painter |

The asset uses a near-black RGB background because the native painter separates the body, warm fibres, and cool perimeter into ARGB layers at startup. The source rectangle itself is never painted into the product UI.

## Final generation prompt

```text
Use case: precise-object-edit
Asset type: production raster source for a DAW plugin
Primary request: Change only the outer proportions and internal flow so the visible mycelial specimen becomes substantially taller and more compact. The exact visible silhouette bounding box must be approximately 1.30:1 width-to-height. Keep it rounded, softly lobed, mildly asymmetric, with rounded left and right ends. Reflow the internal fibers naturally to fit the compact body.
Preserve: the premium detail level, dark translucent blue-black/cyan cellular membrane, warm copper-gold branching mycelial fibers, restrained cyan rim light, straight-on orthographic view, single centered specimen, and pure black RGB background.
Composition: the object should occupy about 75% of the canvas width and 75% of the canvas height, centered with black padding.
Constraints: featureless solid #000000 background to every edge; no checkerboard; no tail; no pointed ends; no leaf or spindle silhouette; no text; no UI; no border; no watermark; no extra object.
```

The built-in generator produced a 1430×1100 original. The production copy was downsampled without creative alteration to 768×590 for first-paint and package-size budgets.
