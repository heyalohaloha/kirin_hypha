# PR #90: CI fixture timing repair

2026-10-09. Base: `3c68902559eab631c9374b3b0b273d1d3fa1c094` (B-1340).
Scope: test fixtures only; shipping processors, clock policy, gap thresholds, lease duration,
PCM oracle, deadlines, native test inventory, CI matrix and installed candidates are unchanged.

## Observed failures

CI run [37855871221](https://github.com/heyalohaloha/kirin_hypha/actions/runs/37855871221):

- Windows `kirin_live_blind_loop_product` failed twice after 145.14 s. Stage 3,
  reason callbackGap/loopClockUnavailable (3/31), longest callback intervals 1322.11/1162.5 ms.
  The synthetic host supplied only plugin-frame clocks. A scheduler stall correctly retired
  the unprovable LOOP occurrence; the nominal test kept waiting for its old MATCH.
- macOS `kirin_live_compare_offset_product` waited at stage 8 after a 236.881 ms callback
  interval. Its plugin-frame clock correctly sealed PRE; the test expected compensation
  recovery to suffice even though a second, unrelated callback gap had occurred.
- Seven Rust spectrum clock tests failed in `Fixture::paired()` at the initial PRE publication.
  Sequential worker feeds/waits could outlast the 1500 ms request lease. The fixture had no
  POST IO heartbeat during that interval.

## Repair and preserved failure coverage

The nominal 200-lap LOOP and offset fixtures supply the same qualified synthetic VST3 continuous
clock policy already used by `live_compare_timing_product_test.cpp`. Project clocks still fold,
continuous clocks do not. Clock numbers alone do not override product policy or PCM proof.
The nominal LOOP deliberately stalls 1.1 s while Blind is active, and requires the same trial,
fixed MATCH and independent all-frame delayed-PCM oracle to remain valid. The offset fixture
stalls 600 ms while compensation is restored and requires PRE recovery without another click.

`--loop-stall` retains the no-auxiliary-clock host: a deliberate 1.1 s gap must hold exact POST,
never resume PRE, expose the cause and permit END. Existing gap/content/stop faults, stale MATCH,
held gain, offline END and ordinary bit-identical output checks remain selected.
No fixture retries, threshold increases or deadline extensions were added.

The Rust fixture renews POST's request after worker completion before asking PRE to serve it,
as the real IO path does. Both pair setup and subsequent PRE authority/recovery publications use
the same helper. Paused-write tests renew before the write begins, then leave the request untouched
while paused. The explicitly expired write still exceeds the original 1500 ms limit and must fail.
The pair regression and recovery path intentionally wait past that limit. Each reproduced its
PRE publication failure before the corresponding repair and passed after it.

## Follow-up from B-1341 CI

Run [37861733664](https://github.com/heyalohaloha/kirin_hypha/actions/runs/37861733664),
head `a3454ec80027f2d08552aa34cfabcca7a0fe552b`, passed Windows preflight (including LOOP,
pluginval and unsigned installer lifecycle), AU arm64 and public history. Its first Mac source
attempt failed only the unchanged UI TIME scaling benchmark. The preceding candidate and the
local exact-source benchmark passed. One Mac-only rerun tested the timing-variation hypothesis;
Windows/AU jobs were reused. The rerun passed that UI benchmark, all 90 native product tests and
7 update integration tests, but found a missing request heartbeat in the Rust clock-cutover
recovery path. The initial B-1341 fix covered pair setup only. No further same-source rerun was
started. This follow-up fixes every ordinary PRE service path and adds a deterministic expired
lease before recovery; safety deadlines and paused-write invalidation assertions remain intact.

## Local validation

The follow-up request helper passed the clock suite (10 tests) and workspace (2386 passed,
0 failed, 43 ignored including doc tests). Its forced recovery delay reproduced the exact
PRE assertion before the helper was applied. Clippy, fmt and lightweight contracts passed.
The 1413 production/tooling paths compared with B-1340 have no changes after excluding this
test-only module. Native fixture code is identical to B-1341, whose Mac 90-test suite and
Windows preflight passed; the native local evidence below is reused within that exact scope.

- Rust clock suite: 10 passed, including the new delayed-worker regression.
- Clippy workspace/all-targets, Cargo fmt, lightweight source contract, source line budget and diff check: passed.
- Native nominal LOOP: passed in 109.76 s, including the forced 1.1 s stall, 200 laps,
  exact delayed PCM, both audible receipts, reveal, END, held attenuation and offline resume.
- Native offset: passed in 15.94 s, including the forced 600 ms scheduling stall after
  compensation was restored.
- Rust workspace: 2386 passed, 0 failed, 43 ignored (including doc tests).
- Native safety: all 8 passed in 94.04 s, including unclocked LOOP stall/POST hold/END,
  callback-gap/content/stop faults, direct and reused MATCH, and explicit attenuation approval.
- Total affected native inventory: 10 tests; all passed without reruns.
- Exact-commit CI is required after publishing each repair; the old failed run is not reused as PASS.

## Session record

Current DEV: isolated `hypha-pr90-ci-fix` worktree; PR #90 CI timing failures investigated and
fixture corrections prepared. Daily log: 2026-10-09; old failing run inspected, no blind rerun.
Handoff: merge/release and G3 host/performance acceptance remain with their existing owners.
Notion SECTION:DEV, daily database and INBOX writes were not performed because this repository
explicitly prohibits all Notion writes. This local record preserves the unsent content.
