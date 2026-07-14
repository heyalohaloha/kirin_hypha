# JUCE submodule patches (vendored, tracked)

The `juce_shell/JUCE` submodule is pinned to upstream **JUCE 7.0.12**
(tag `7.0.12`, commit `4f43011b96eb0636104cb3e433894cda98243626`).

Local source patches are applied on top of that pin and tracked here, following
the same "upstream + tracked patch" discipline already used for
`vendor/baseview` and `vendor/egui-baseview`. The submodule itself is **not**
committed with local edits; `juce_shell/patches/*.patch` are the source of
truth and can be re-applied after a submodule update.

To re-apply after a fresh submodule checkout, use the idempotent project script so
the exact per-patch flags and ordering stay in one place:

```sh
bash scripts/apply_juce_patches.sh
bash scripts/verify_juce_patch_state.sh
```

`scripts/build_juce_universal.sh` runs both commands before every universal build.

To verify that a dirty `juce_shell/JUCE` checkout is **only** this tracked patch
stack and nothing else:

```sh
bash scripts/verify_juce_patch_state.sh
```

`cargo run --package xtask -- release-package` also runs this verifier before it
allows an uploadable zip. A dirty submodule is acceptable only when it matches
the pinned JUCE commit plus these six patch files byte-for-byte; unexpected
JUCE edits, staged files, untracked files, or a moved submodule HEAD fail the
release gate.

---

## 0001 — macOS 15 SDK: bypass obsoleted `CGWindowListCreateImage`

- **File:** `modules/juce_gui_basics/native/juce_Windowing_mac.mm`
  (function `createNSWindowSnapshot`, around line 519)
- **Patch:** `patches/0001-macos15-sdk-cgwindowlistcreateimage-bypass.patch`
- **Why:** `CGWindowListCreateImage` is *obsoleted* (marked `unavailable` → hard
  compile error) in the macOS 15.0 SDK. Xcode 16.4 ships only the macOS 15.5
  SDK, so JUCE 7.0.12 fails to compile `juce_gui_basics` (and the `juceaide`
  build helper) on this machine. JUCE's own guard only suppressed the older
  *deprecation warning*; it does not handle the 15.0 *unavailable* upgrade.
  The fix landed upstream only in JUCE 8.0.x and was not backported to 7.x.
- **Change:** When built against a 15.0+ SDK
  (`__MAC_OS_X_VERSION_MAX_ALLOWED >= 150000`), `createNSWindowSnapshot` now
  returns an empty `juce::Image{}` and never references `CGWindowListCreateImage`.
  Older SDKs keep the original code path unchanged (`#else` branch).
- **Scope / impact:** This is the native-window screenshot utility only
  (`Component::createComponentSnapshot` of native windows). The Kirin Hypha PRE
  plugin is a Watch-only meter and never snapshots native windows, so behaviour
  regression is zero. **No audio, measurement, FFI, plugin-format, or Watch-UI
  path is touched.** `kirin_measure`, the existing nih-plug PRE
  (`crates/hypha_pre`), and `crates/kirin_hypha_ffi` are unchanged.
- **Upstream ref:** JUCE 7.0.12 (`4f43011b96`); resolved in JUCE 8.0.x via
  ScreenCaptureKit. ScreenCaptureKit replacement intentionally NOT done here
  (out of scope for Phase 2 後段; the plugin does not need window capture).

---

## 0002 — drop WebKit OSXFramework from juce_gui_extra (link-surface optics)

- **File:** `modules/juce_gui_extra/juce_gui_extra.h` (module declaration,
  `OSXFrameworks:` line)
- **Patch:** `patches/0002-drop-webkit-osxframework-from-juce-gui-extra.patch`
- **Why:** The AU/VST3 shells link `juce_audio_processors`, which hard-depends on
  `juce_gui_extra`, which unconditionally declares `OSXFrameworks: WebKit`. JUCE's
  CMake links every `OSXFrameworks` entry as an INTERFACE usage requirement with no
  `JUCE_WEB_BROWSER` guard on macOS, so `WebKit.framework` is linked into the bundle
  even though `JUCE_WEB_BROWSER=0` (set in `juce_shell/CMakeLists.txt`) already
  disables the `WebBrowserComponent` class. The plugin never uses a web browser, so
  the WebKit link is dead surface (the B-130 note in CMakeLists already documented
  that config alone cannot drop it).
- **Change:** Remove the single `OSXFrameworks: WebKit` line from the `juce_gui_extra`
  module declaration. `iOSFrameworks: WebKit` is left untouched (no iOS target). All
  WebKit/WKWebView references in `juce_gui_extra` are behind `#if JUCE_WEB_BROWSER`
  (= 0 here), so no WebKit symbol is referenced and the link succeeds without the
  framework.
- **Scope / impact:** Link-surface (optics) only. Verified by universal build
  (G-115-392): `otool -L` shows `WebKit.framework` removed from all four bundles
  (KirinHypha{PRE,POST}.{component,vst3}) — before present, after absent — while the
  link succeeds with `JUCE_WEB_BROWSER=0` (no WebKit symbol referenced) and
  `DiscRecording.framework` (dropped by B-130) stays absent. **No audio, measurement,
  FFI, plugin-format, or Watch-UI path is touched.**
- **Upstream ref:** JUCE 7.0.12 (`4f43011b96`); `juce_gui_extra` declares WebKit as a
  non-weak OSXFramework and no upstream config gates this on macOS in 7.x.

---

## 0003 — AU preferred channel layout tags for Logic mono menu

- **File:** `modules/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm`
  (`GetChannelLayoutTags`)
- **Patch:** `patches/0003-au-preferred-channel-layout-tags-for-logic-mono.patch`
- **Why:** Kirin Hypha AU declares the preferred map `{1,1}, {2,2}` so the same
  PRE/POST components can run as mono or stereo. JUCE 7.0.12's AU wrapper reports
  those channel counts through `SupportedNumChannels`, but `GetChannelLayoutTags`
  falls through the bus-layout path and may omit explicit `Mono`/`Stereo` layout
  tags when preferred channel configurations are used. Apple's `auval` can still
  pass the count map, but Logic uses the layout-tag metadata for its AU menu and
  can cache the component as stereo-only.
- **Change:** When `JucePlugin_PreferredChannelConfigurations` is defined, derive
  `kAudioChannelLayoutTag_Mono` and `kAudioChannelLayoutTag_Stereo` from the
  existing preferred channel map after checking `AudioUnitHelpers::isLayoutSupported`.
  Non-1/2 channel configurations fall back to discrete-in-order tags. The existing
  bus-layout code remains for builds without preferred channel configurations.
- **Scope / impact:** AU metadata only. Audio processing, bypass, latency, FFI,
  and measurement code are untouched. Verified on Logic Pro with Kirin Hypha PRE
  and POST: `ChannelConfigurations = (1,1), (2,2)`, layout tags `6553601` (Mono)
  and `6619138` (Stereo), `auval -v aufx Khpr/Khpo Kirn` pass.
- **Upstream ref:** JUCE 7.0.12 (`4f43011b96`). This patch is intentionally small
  and local to AU v2 metadata because replacing the JUCE wrapper or changing AU
  identifiers would break existing project recall.

---

## 0004 — AU host/render clock provenance

- **Files:** `juce_AudioPlayHead.h`, `juce_audio_plugin_client_AU_1.mm`
- **Patch:** `patches/0004-au-clock-provenance.patch`
- **Why:** AU v2 may obtain the timeline from the host transport callback or fall
  back to the render timestamp. Hypha must preserve which source supplied a valid
  sample position instead of labeling both as the same clock.
- **Change:** Validate finite/representable AU sample positions, expose the chosen
  provenance through `PositionInfo`, and leave the position absent when neither
  source is valid.
- **Scope / impact:** AU clock provenance only. It does not shift, infer, or align
  PRE/POST data and does not alter audio.

---

## 0005 — VST3/AU host presentation-latency diagnostics

- **Files:** `juce_AudioPlayHead.h`, `juce_audio_plugin_client_VST3.cpp`,
  `juce_audio_plugin_client_AU_1.mm`
- **Patch:** `patches/0005-host-presentation-clock.patch`
- **Why:** VST3 `IAudioPresentationLatency` and AU
  `kAudioUnitProperty_PresentationLatency` are the format-level host facts needed
  to determine how a DAW presents a plug-in's input/output timeline. JUCE 7.0.12
  does not expose those callbacks to `AudioProcessor::processBlock`.
- **Change:** Implement the optional VST3 interface and AU write-only property for
  main bus 0, retain host-provided zero distinctly from an absent callback, and
  surface the values in samples through `PositionInfo`. Wrapper storage is a
  compile-time verified lock-free atomic; the Audio Thread only loads it.
- **Scope / impact:** Observation only. This patch never changes the audio buffer,
  callback position, frame values, PRE/POST offset, or TRACE axis. Production
  alignment may consume the facts only after a host/format conformance capture has
  proven the correct sample-domain mapping.

---

## 0006 — VST3 component identity continuity

- **File:** `juce_audio_plugin_client_VST3.cpp` (`JuceVST3Component::iid`).
- **Why:** macOS previously shipped the nih-plug VST3 while AU and Windows used the
  JUCE shell. Unifying macOS AU/VST3 on one editor is required for identical type,
  colour, metric labels and Keep geometry, but changing the VST3 component CID would
  make existing DAW projects report a missing plug-in.
- **Change:** Let a target provide four explicit VST3 UID words. PRE keeps
  `KirinHyphaPREv01`; POST keeps `KirinHyphaPOSTv1`, including Steinberg's Windows
  byte-order convention. Targets without these definitions retain JUCE defaults.
- **Scope / impact:** Factory identity only. DSP, audio buffers, parameters and clocks
  are untouched. Old nih-plug state bytes are migrated separately at the FFI boundary.
