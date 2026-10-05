# Hypha: shared lighting and recessed observation windows

## Scope

Complete the lighting implementation on the common JUCE
editor. The visual intent is depth and a restrained instrument finish: bronze bevels around
one main observation window per page, recessed dark glass, and a consistent overhead light.
Diagonal decorative reflections remain removed. This is a GUI change; measurement values,
audio processing, Record, PRESENCE settings and the VU chassis bitmap are unchanged.

## Lighting contract

- One stationary light sits at `(0.42 × logical editor width, −0.6 × logical editor height)`.
  Each component receives it in its own coordinates. Root magnification is outside those
  logical coordinates; transforms inside the root are accounted for.
- The main window has a bronze bevel, a brighter upper lip nearest the light, subdued sides,
  a small warm lower bounce and an inner upper shadow. Distance attenuates the frame light.
- Secondary cards, lanes, guides, menus and Blind surfaces remain quiet. The glass centre
  has no decorative reflection, diagonal band, central glow or autonomous animation.
- Raised controls catch a thin reflection on their upper edge in the same light direction.
  Existing selection and measurement colours keep their meanings.

PRE/POST named listening, Exact 4 S, Live Blind and every Reference phase share the quiet
recessed body and raised control material. Blind preparation, approval, anonymous listening,
reveal, interruption and return change their facts and available actions; their material does
not change with the hidden source assignment. Disabled buttons and selectors never brighten
on hover or press. Reference's tonal-band cards use the same quiet recessed material as its
Guide, access and connection-help, metric and workflow surfaces. Selector menus and live-comparison approval menus share
the quiet popup background while keeping their existing item, selection and text colours.

## Main observation area

| Page | Main window |
| --- | --- |
| LEVEL | History; the grouped readings when no history is shown |
| TIME HISTORY | History plot and RUN observation |
| TIME SHARP / LIVE | Time plot |
| TIME DRUM | HISTORY area, including the band summary or envelope occupying that area |
| FREQ | Spectrum, delta or PSB plot |
| SPACE | M/S field |
| REF | Visible Comparison, Tonal or configured comparison-chart group |

Reference owns its outer frame; child charts paint their glass without adding another frame.
Guide-only and Blind views have no main observation frame. Existing Reference selectors and
ABCV controls retain their current layout.

Reference waveform and detail-chart layout share one geometry contract. The layout reserves
the A/B waveform, tabs and readout before allocating enough plot height for the actual font's
axis labels. Smaller panes keep fewer quarter-grid labels when all three cannot fit. The
renderer, cache invalidation and waveform interaction use those same assigned bounds.

DRUM shares the frame dimensions with its pure layout contract. Its horizontal allocation
includes the cast shadow; its vertical allocation reserves the bevel and clips the shadow to
HISTORY. This keeps header controls clear while retaining the 150% summary and selected-hit
time and the 200%/300% HEAD/TAIL panes. History, axes, lanes and hit testing use one time column.

## Capture and cache

Capture uses its output's logical editor size and body origin while a live external pane is
being snapshotted. This override applies to that pane's subtree and restores the live light
afterward without reparenting. DRUM's chrome cache includes both light position and editor
diagonal, so equal positions at different editor sizes cannot reuse a different bevel.

Static frame composition clips away the unchanged glass centre. The flat centre of a glass
well is filled directly; only its edge material is cached. Existing lifetime, device-scale
budget and direct-versus-cached pixel tolerances apply. Native and software rendering costs
are checked by `MaterialCacheContract.h`.

## Verification and acceptance

`MaterialLightContract.h` checks light direction and quiet secondary surfaces;
`KeyLightCoordinateContract.h` checks transformed roots, Capture restoration and DRUM cache
invalidation. The Reference component contract checks all supported sizes and its visible
chart variants. Opt-in page review images are generated from native component fixtures.
PSB contracts check bar/frame/axis separation and exact Bark-band hit mapping. Surface layer
contracts check that glass fills precede the frame interior and that text and data follow it.
Comparison-state contracts cover named PRE/POST, Blind and Reference state transitions at all
five sizes, with opt-in native review images for preparation, approval, listening, results,
failure and restoration (`BlindLightContract.h` and `ReferenceStateLightContract.h`).
`ReferenceComparisonLayoutContract.h` checks real waveform, chart, readout and label bounds,
including the reserved-frame geometry and detail-admission boundaries.

The local source gate includes native GUI contracts, paint budgets, normal Rust suites,
ignored CPU and Record/pairing suites, static source contracts and clippy. A successful local
fixture run does not establish DAW visual acceptance, Windows host acceptance, signing,
installation or public release readiness.
