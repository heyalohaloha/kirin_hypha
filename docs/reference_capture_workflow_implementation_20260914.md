# Capture A and workflow implementation evidence

Date: 2026-09-14
Status: In progress. Implements the approved B-879 plan; this is not a release or host-validation claim.
Plan: [Integrated plan](hypha_capture_and_workflow_integrated_plan_20260914.md), [evidence and discovery contract](reference_evidence_and_discovery_contract_20260914.md).

## B-880: Capture ownership, historical evidence and input observations

- Separate serialized ownership from asynchronous decoding and active capture. Pending restore is immediately saveable. Capture/restore generations reject older results; malformed restore retains the last valid document. Explicit empty state clears it.
- Preserve partial capture state and its notice on restore. Keep fit-to-capture separate from manual range selection, including continued growth of one capture ID.
- Store historical position and gain receipts per Capture ID and immutable B file/PCM identity. A current live map, Work name, gain change or reselected B never replaces an old receipt.
- Derive capture-specific receipts from the existing bounded four-second input and independently aligned B probe, using the existing 400 ms / 100 ms paired-block gain analysis. Receipt creation has no audio-output command. Later observations start at capture-index boundaries; four exact complete units and non-repeated evidence are required. No additional whole-song PCM is retained.
- Index original input in fixed 64-byte one-second units: canonical float32 SHA-256, four band energies, RMS and sample peak. Compute only on the worker. Notification distinguishes exact digest, material difference, other raw difference and unknown timing. A live difference never attributes the change to a particular plugin or asserts an entire song is unchanged.
- Carry callback clock/PDC signatures and runtime-only time epochs. Restore needs fresh exact sequence evidence for a current cursor or material-change interpretation. Per-range pass/time observations remain separate from immutable A and historical B receipts. Concealed Reference suspends new checks; previous difference is labelled LAST CHECK.
- Write bounded v2 capture payloads; continue accepting bounded v1 data without inventing missing evidence. Captured B display explicitly shows its fixed dB offset or ORIGINAL LEVELS.

## Focused evidence recorded so far

These are native harness and algorithm results, not DAW-host results.

| Check | Result |
| --- | --- |
| Streaming index tests | 3 pass: canonical zero/tail/nonfinite/format boundaries, processing versus dither, worker timing |
| Processing fixtures | Gain and RMS-normalized EQ/dynamics detected; 16/24-bit dither produces no material notification; polarity-only raw difference is not called exact |
| Index worker, 48 kHz stereo | 100 ms audio: p95 0.283 ms; target at most 1 ms |
| Index worker, 8–768 kHz | Tested rates remain below 10% realtime; 768 kHz stereo p95 3.327 ms per 100 ms audio |
| Capture runtime | Pass: exact frames, immutable original A, zero callback allocations, seek, cancel, missing/changed clock, PDC notification, format change, nonfinite input, queue overflow, restore/save generations and partial restore |
| Historical projection | Pass: live one-second anchor shift and gain change leave historical sample windows and gain unchanged; v1/missing receipts do not inherit Work alignment; missing B retains A |
| End-to-end Capture/B evidence | Pass: B-free capture → later measured Version → exact replay → fixed −6.021 dB receipt, 0-sample position error → persisted receipt → current A edit with historical A/B unchanged |
| Position evidence boundaries | Pass: one-sample and one-second moved observations, wrong rate and repeated four-second sequence cannot create capture identity |
| Two-hour capture | 57,600,000 actual input frames at 8 kHz mono; 1,126 display bins; 734,770 serialized bytes; callback p95 0.004093 ms and p99 0.019051 ms; RT allocations zero |
| Maximum serialization | Pass: 7 rates × mono/stereo, 7,200 units + 2,048 bins + 16 receipts; full XML remains below 1 MiB and round-trips without truncation |
| Reference UI | Pass at all five sizes: growing fit range, preserved manual range, source/view separation, complete Blind concealment. 300/900 renders inspected |
| Source/format checks | Pass: new source ≤500 lines, existing ratchet unchanged; Rust formatting and diff whitespace checks |

Logs and rendered images are local under `target/b880-*`. The JUCE helper build initially used an unset deployment target and failed against the macOS 15 SDK. Setting the existing helper build cache to macOS 12.0 resolved this; vendor source was not changed.

## B-881 / B-882: Blind workflow, Reference choices and measurement help

B-881 extracts candidate access and Blind presentation without changing behavior. B-882 then changes the owned workflow modules.

- Normal POST at 600/900 has one direct PRE/POST Blind entry; smaller entries enlarge to 300%. Reset Meter Session remains in MENU. Keep start remains in MENU: the fixed Stop slot cannot also contain the longer finalization/failure actions without taking width from the other controls. The optional direct Keep expansion is not adopted and no 3-to-2 operation reduction is claimed.
- Keep/NOTE transitions preserve Blind and MENU positions. Reference, PRE, VU and unsupported wrappers do not receive a duplicate direct Blind control.
- Blind keeps its captured range in a fixed place. Complete-pass facts drive each Source's DONE state; actual output acknowledgement drives selection feedback. Answers appear only when both passes are complete, with Stop/End in a separate fixed slot.
- END and RETURN TO LIVE remain separate explicit gestures. Only the matching scope/capture/command audio receipt can close the Editor's recovery view automatically. Empty, bypassed and offline callbacks cannot acknowledge return. Reopened Editors do not inherit the old Editor's close intent. Applied attenuation, rather than approval alone, supplies the possible Live gain increase.
- Admission now exposes known causes. Missing PRE and Keep return actions are available in preflight; no recording is stopped automatically. Unknown lower-level refusal is not diagnosed as an analysis-slot shortage. Eligible capture failures reuse the existing safe recapture transaction and release/generation checks.
- Blind presentation skips unchanged states and uses the existing UI timer at no more than 10 Hz. Explicit gestures refresh immediately. No new RT processing or file polling is introduced for these UI changes.
- TIME retains one-click stepping and adds a direct range menu at 600/900. Reference keeps B/Version and C/Check positions stable; known singleton choices become readable labels. Missing IDs still require explicit selection. Blind conceals both labels and dropdowns.
- The user's subsequent NOW/SESSION feedback supersedes permanent range captions. LEVEL metrics explain their observation source and time scope on hover. Painted bounds use a fixed eight-entry metadata array; text is constructed only for the requested tooltip.
- The top-right TP header now stacks L/R numbers across the strip width, independently of the bar widths. Existing numeric font sizes are retained. Signed values from −14.5 through −897.1 and +770.6 fit their measured bounds at 600/900.

Focused native results (not DAW host evidence):

| Check | Result |
| --- | --- |
| Local Blind trial/preparation | 2 targets pass: explicit return, exact receipts, duplicate request, stale/reopened Editor, no-callback/bypass/offline cases |
| Local Blind product | Stereo and track/stem paths pass for VST3- and AAX-designated native processors. The track UI test initially raced its 10 Hz presentation update; waiting for the actual context control fixed the test, and the affected case passed |
| Product entry / UI | Pass: all five sizes, fixed action/range placement, repair affordance, readonly/missing Reference choices, Blind privacy, metric hover and signed TP width |
| Typography / composites | Pass; fresh 600/900 LEVEL frames with −14.5/−114.5 and direct Blind inspected. ABC Reference frame inspected. Preview writers now truncate existing PNG files instead of appending another PNG stream |
| Reference rendering | Focused run: 900×600 panel p95 1.831 ms; cached comparison p95 1.001 ms; rebuild p95 1.532 ms |

Logs: `target/b882-*`. Verified new layout frames: `target/b882-verified-ui/`; ABC/Blind fixtures: `target/b882-ui/` (those writers truncate). No native plugin installation or release packaging was performed.

## B-883: Bounded PRE discovery and footer cleanup

- Image CAPTURE is now MENU → Save measurement image. Reference Capture A stays on its primary surface. This follows the user's subsequent footer feedback.
- A demand-driven, value-only preview scans at most 512 entries / 64 JSON attempts / 64 KiB per file / 1 MiB read bytes. Batch and elapsed/active time ceilings stop incomplete scans. Canonical per-PRE transactional claims are read in the same scan; unavailable legacy ownership proof keeps the ordinary menu.
- One scheduler per loaded module coalesces one pending demand; eight published/retired slots stay below 1 MiB. UI polling is at most 10 Hz, requests are debounced for one second, and preview freshness is two seconds. No engine pointer or audio-thread work enters discovery.
- H02 displays the full fitting identity label and requires its painted generation before direct connection. Resize invalidates that receipt. Existing exact pairing rechecks live identity, ownership and playback; stale/incomplete/unfitting results retain the menu.
- Windows callbacks hold a counted DLL reference until callback return; DLL detach never joins a worker. The implementation uses Microsoft's documented GetModuleHandleExW / FreeLibraryWhenCallbackReturns / TrySubmitThreadpoolCallback contracts. Non-Windows module retirement drains the dedicated worker outside engine destruction.
- Focused results so far: five bounded scanner tests, three scheduler tests, native live PRE discovery → exact pairing → full Local Blind cycle, and all-size product-entry UI pass. Windows Rust cross-target check passes. Module lifetime and maximum RAM probes are being completed before the consolidated baseline.

The macOS lifetime probe initially expected physical unmapping. The test binary has MH_HAS_TLV_DESCRIPTORS; dyld retains these images. The revised probe records explicit non-RT retirement and close/reopen separately from actual unmapping. Windows keeps its physical-unload assertion. This is a platform distinction, not a claim that macOS unmapping was tested.

API references: [Microsoft DLL restrictions](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices), [counted module reference](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw), [callback retirement](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-freelibrarywhencallbackreturns), [callback submission](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-trysubmitthreadpoolcallback), [Apple dyld TLV lifetime](https://github.com/apple-oss-distributions/dyld/blob/main/dyld/Loader.h).

## B-884: Allocation reuse and maximum-memory/lifetime probes

- Reuse the offline paired-block EBU meter's allocated storage, resetting its filter/history before every A or B block. Six focused gain tests pass, including exact comparison against fresh meters after alternating audio and silence. The paired-block selection and frozen gain policy are unchanged; the audio callback gains no work.
- Reserve bounded Capture codec buffers before serialization/decoding, avoiding growth copies while old, pending, draft and published documents coexist.
- The maximum-memory harness uses fresh processes for seven rates × mono/stereo. Two POST lanes concurrently retain 7,200 units, 2,048 bins, 16 receipts, pending restore, UI copies, XML and the complete 2 MiB queue allowance per lane. Existing four-second A/B PCM is allocated before the baseline and receipts retain zero PCM.
- On macOS, 50 µs sampling of live malloc bytes observed a largest combined increase of 24,066,832 bytes against the two-POST 33,554,432-byte target. This is sampled live heap, not an absolute peak or process RSS claim. The allocator's touched-memory high-water mark also includes retired arenas and is logged separately. Non-macOS runs explicitly skip allocator measurement while retaining capacity/restore/serialization checks.
- As specified by the original Capture A plan, the existing EBU three-second history/filter is reported separately: approximately 2.39 MB at 48 kHz stereo and 37.00 MB at 768 kHz stereo. The existing gain-analysis frame cap rejects a four-second 768 kHz probe; these changes neither relax that limit nor claim gain calibration at that rate.
- The macOS loader probe passed 36 guard-drain and close/reopen cycles across three independently loaded modules, with the engine destroyed before each request. Actual image unmapping remains untested on macOS because dyld retains Rust TLV-bearing images.
- An isolated Windows worktree built the Rust static library and native lifetime probe successfully. Its DLL loading was rejected with Win32 error 4551; actual Windows unload verification remains blocked. No OS security setting, installed plugin or active DAW was changed. The remote worktree is `C:\Users\hello\Dev\kirin_hypha_b883_validation` at B-883 plus the loader diagnostic/UTF-8 test-build changes in B-884.

Focused logs: `target/b884-gain-test.log`, `target/b884-memory-detail.log`, `target/b883-lifetime-test.log`, `target/b883-windows-validation.log`.

## Remaining work in the same approved task

- Maximum summary memory and macOS retirement probes are recorded above; current results do not establish DAW wall-clock latency or all-wrapper peak resident memory.
- Windows actual module unload remains an external verification blocker (error 4551). H02 and H01/H03–H08 UI work is implemented; final baseline and native cross-feature checks still apply.
- Consolidate the full Rust/FFI ignored/native baseline once the final implementation is ready. Run new focused tests only for changed or unresolved paths.
- Record actual Studio One / Pro Tools and Windows verification separately. No new release build, installation, signing, notarization or public distribution has been performed for these changes.
