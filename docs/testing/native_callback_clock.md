# Native product fixture clocks

`CallbackWallClock.h` returns `steady_clock` in shipping AU/VST3/AAX builds, on both platforms.
Only `KirinNativeTestProcessor` and its fixture executables define
`KIRIN_HYPHA_TEST_WALL_CLOCK`. They compile the same processor sources separately: changing
only an executable definition would leave its linked processor on the real clock and violate
the inline-function definition contract. Shipping libraries never link the test library.

`ProductTestClock` has one owner per synthetic host. PRE, POST and the non-RT chain timing
reader see the same lock-free atomic nanoseconds. Each completed host cycle adds its audio
duration; deliberate host pauses and inter-side jitter add explicit durations. Waiting for a
worker, painting the GUI, and OS scheduling do not advance callback time. Destruction restores
the real clock, after callbacks have stopped. No product threshold or clock proof changes.

## Inventory

| Native fixture / CTest family | Clock and handoff |
| --- | --- |
| `ContentAnalysisProductFixture.h`: `kirin_{reaper,au,content}_analysis_*_product` | Audio clock; both analysis queues drained between callbacks; jitter adds 12 ms every third callback |
| Same fixture: `kirin_reaper_listen_{mono,stereo}_product` | Same clock for LISTEN, MATCH and BLIND; unchanged physical delay and PCM oracle |
| `AaxAnalysisProductFixture.h`: `kirin_aax_analysis_*_product` | Audio clock and analysis queue receipt; input-count wait budget plus whole-item real watchdog |
| `kirin_live_timing_product`, `kirin_live_initial_loop_product` | Audio clock; explicit missing-clock callback remains a missing host-clock fact |
| `kirin_live_blind*_product` | Audio clock; gap = 600 ms, loop stall = 1100 ms on that clock; audible waits use audio time |
| `kirin_live_compare_pin_product` | Audio clock, including the four-second PIN window |
| `kirin_live_compare_offset_product` | Audio clock; deliberate 600 ms stall; interval diagnostics use the same clock |
| `kirin_live_compare_aax_group_product` | One audio clock for all synthetic instances in the group |
| `kirin_live_lifecycle_*`, `kirin_live_reentry_*_product` | Audio clock; handoff pauses freeze it, while stop/seek/clock loss remain explicit host events |
| `kirin_local_blind_product*` | Audio callback clock; exact capture/transport protocol still uses real expiry time |
| `kirin_live_recovery_product` | Explicit callback clock for its processor callback sequence |
| `kirin_product_wall_clock` | Production gap/analysis policy under 0/40/80/175 ms real delays, explicit gap/recovery and threshold boundary; no shared memory |
| `kirin_processor_wall_clock` | Actual PRE/POST processor, mono/stereo, 0/40/175 ms real delays, gap invalidation, bit identity and zero latency; no shared memory |
| `kirin_shipping_wall_clock` | No test definition; brackets the result with real steady-clock reads |

The other low-level clock, correspondence, REAPER fault, analysis clock, capture owner and
Blind session tests already pass explicit timestamps or `BlockClock` values. They never used
the runner's callback wall clock. Publication/storage, content-notification, editor-surface,
reference-audition and transparency fixtures do not assert callback-gap continuity.

## Real time retained intentionally

`chain_timing_product_test.cpp` measures actual elapsed work between two callbacks against an
independent host timer; `live_compare_processor_benchmark.cpp` measures actual callback cost.
Their real-time clocks are the quantity under test. GUI paint/performance probes likewise keep
their real timers. Low-level chain timing tests supply explicit timestamps for policy checks.
Blind's suspended-callback END check waits across real message-thread ticks: audio time is
frozen there, and no output receipt may complete END until callbacks resume.

The source audit covered `callbackWallNanos`, `steady_clock`, JUCE millisecond counters and
Unix timestamps in `juce_shell/src`. Every gap detector, chain freshness check and chain trace
uses the callback clock. Other reads serve separate purposes: UI presentation/toasts,
Reference tracking cadence and bounded lock-contention polls, update/HTTP timeout and metadata,
file-lock waits, appearance refresh, trace filenames, capture/pairing protocol expiry and
identity generations. Unix timestamps must agree with Rust/file-transport peers; substituting
callback nanoseconds there would change the protocol under test. These real timers, worker/IO
polling and fixture failure watchdogs remain real. The test clock does not claim that an
infinitely stalled process, expired protocol request or unavailable OS transport must pass.

## Analysis receipt

The separate test Rust static library enables `native-test-support`, which exposes a read-only
queue receipt. The shipping library has no such symbol. The sole producer calls it outside
`processBlock`; both descriptor and sample queues must be empty. A descriptor popped before
the final sample is not receipt. A stuck worker fails the real watchdog. Receipt neither
certifies timing nor proves an analysis result: all existing delta, warmup, drop, recovery,
PCM and ownership assertions remain in place. Rust tests cover both queue boundaries and
the running worker with no dropped input.

The optional `KirinProcessorWallClockTests --real-clock-control` restores the old real-clock
route under the same 175 ms assertion and is expected to fail. It is a regression control,
not a passing CTest case. The normal tests belong to the release-source inventory.
