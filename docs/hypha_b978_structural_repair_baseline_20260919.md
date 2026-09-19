# B-978 structural repair baseline

This record fixes the product paths and reproducible failures used by the structural repair.
It does not expand the public channel count or role-view surface.

## Shipping presentation paths

| Surface | Producer and transport | Shipping consumer | Retained state | Product-path check |
|---|---|---|---|---|
| LEVEL live absolute / Delta | `io_thread_post_tick.rs` and `io_thread_post_delta.rs` publish the POST observation; `kirin_hypha_poll_observatory_frame` copies one `KirinObservatoryFrame` | `PluginProcessorMeter.cpp::pollObservatoryFrame` → `PluginEditorObservatory.cpp::refreshObservatory` → `HyphaObservatoryView::setObservatoryFrame` | Observatory frame and Watch display owned by the editor | Observatory composite and editor-surface product contracts |
| TIME history | Meter session history and Delta history in `kirin_measure`; the two decimated FFI polling functions keep absolute and Delta distinct | `PluginProcessorMeter.cpp::pollMeterHistory` / `pollMeterDeltaHistory` → `refreshObservatory` → `setHistory` | Fixed-capacity histories in the engine and the current projected history in the view | Meter history ABI tests and time-field contract |
| FREQ | `SpectrumRuntime` → spectrum history → `kirin_hypha_poll_spectrum_batch` | `PluginProcessorAnalysis.cpp::pollSpectrumBatch` → `PluginEditorAnalysis.cpp::refreshAnalysisViews` → `SpectrumComponent::setBatch` | Engine spectrum history and the component batch | Analysis-demand contract and spectrum product tests |
| TIME / SHARP | Perceptual Delta or POST absolute history selected by the typed analysis demand | `pollPerceptualBatch` or `pollAbsoluteBatch` → `refreshAnalysisViews` | Perceptual or absolute batch in the corresponding component | Analysis-demand contract and perceptual presentation tests |
| TIME / LIVE | Absolute analyzer history → `kirin_hypha_poll_absolute_batch` | `pollAbsoluteBatch` → `refreshAnalysisViews` → absolute component | Absolute batch in the component | Analysis-demand contract and absolute presentation tests |
| Keep / completed Record | Record state and `KirinRecordDisplay`; POST control state is refreshed by `updatePost` | `PluginEditorMeter.cpp::updatePost` updates Observatory Keep state and completed-record presentation | `cachedRecordDisplay`, Keep phase, and completed session summary | Record display ABI, editor-surface, and Keep workflow tests |
| Capture image | The explicit menu action calls the editor's one synchronous read boundary | `beginObservatoryCapture` → `freezeObservatoryCapture`; LEVEL re-polls the selected history and analysis surfaces are snapshotted before the asynchronous chooser | Owned immutable `capture::Snapshot` image | Capture product and storage/error contracts |

`PluginEditorMeter.cpp::updatePost` still updates hidden compatibility cells. Those cells are not
evidence for the visible Observatory. The visible live path is the Observatory frame path above.

## Confirmed baseline failures

At B-978 (`af0f32d6`):

- `cargo fmt --all -- --check` reports formatting changes in 15 Rust source files.
- `node --test scripts/structural_repair_detection.test.mjs` fails before testing a mutation because
  it searches `PluginProcessor.cpp` for `prepareToPlay`; B-961 moved that responsibility to
  `PluginProcessorFormat.cpp`.
- `scripts/check_source_line_budget.sh` passes with 31 exact legacy entries. After formatting,
  `crates/kirin_hypha_ffi/src/lib.rs` falls from 5,258 to 5,256 lines, so its ratchet must fall too.
- The checked-in diagnostic log contains repeated PRE atomic-rename `ENOENT` warnings. It is not
  treated as a current product failure without a new product-path reproduction.

## Deterministic selector interleaving

The B-978 selector is split across `SpectrumRuntime::view`, `channel_mode`, and `generation`.
The reachable order is:

1. The worker accepts ingress block generation `G`.
2. `set_view` stores the new view code.
3. Before it stores the derived channel mode or increments the generation, the worker reads the old
   channel mode and the new view-derived positions.
4. The worker can therefore construct a frame carrying generation `G`, the new view label, and the
   old channel-mode calculation.
5. Before the control thread increments the generation and clears history, the current-state check
   can observe that same intermediate combination.

This proves the internal mixed-selection state is reachable. It does not prove that a JUCE paint
observes it: the later generation change clears the engine history, and the UI polling window has
not been demonstrated. The repair detector must interrupt the real selection publication boundary,
not rewrite fields in an already completed frame.

## Repair boundaries

- Publish one encoded analysis selection and one monotonic selection generation.
- Derive channel mode, analysis channel count, and role positions from one decoded selection.
- Attach the captured selection to ingress; the worker never relabels a completed frame from current
  control state.
- Keep mode-specific clocks and histories separate; a shared selection identity does not merge their
  measurement algorithms.
- Carry comparison refusal as a reason-bearing snapshot through FFI and the actual Observatory path.
  Hidden compatibility cells do not own refusal presentation.
