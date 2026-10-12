# Live comparison publication lifetime

PRE owns the live transfer ring from identity/write enable until its prepared format is
retired or the processor is destroyed. POST owns a reader and renderer lease only for its
explicit comparison. POST's inactive-session cleanup must not run on PRE: PRE has no
POST session, so its inactive/fade flags cannot decide its publication lifetime.

The regression retired PRE's ring in `serviceLiveCompare()`, immediately after
`enableWritesNow()` published it in the same processor timer callback. Closing the owner
unlinked the macOS object or closed the Windows section. Consequently POST's open returned
`preUnavailable` even while measurement pairing and deltas remained valid. The fix confines
session retirement to POST while preserving PRE's restore service acknowledgement. Key hashing, ring generation/version, AAX clock/reset
handling, state serialization, and the renderer's fade/reader collection fence are unchanged.

## Native regression tests

`LivePublicationTests.cmake` registers tests against both shipping role libraries. Each
test constructs actual PRE and POST processors and covers mono/stereo with VST3 and AAX,
plus AU on macOS. AAX mono uses distinct, known single-member host groups.

- `kirin_live_publication_{PRE,POST}_state-only` publishes an unmapped lifetime control
  into the processor's actual RT publication slot. It executes the actual processor timer,
  tests PRE retention across reset notifications and state restore without idle polling, normal bit-identical audio and zero
  latency, actual PRE discovery/pairing, missing-PRE open refusal, POST fade retention,
  renderer entry refusal before open, completed-fade collection, and pending END retention.
  An unmapped control carries no PCM and never counts as a successful comparison.
- `kirin_live_publication_{PRE,POST}_shared-ring` requires the actual PRE
  prepare/identity/write-enable path to create the OS mapping. It verifies key/rate/version,
  owner lifetime across timer service, successful LISTEN and direct BLIND starts through
  POST open, demand release without destroying PRE, wrong-rate refusal, and owner retirement.

Configure native tests with `KIRIN_HYPHA_BUILD_UI_RENDER_TESTS=ON` or
`KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS=ON`, then build only the test executables:

```sh
cmake --build target/hypha-release-native-tests --target \
  KirinLivePublicationPRETests KirinLivePublicationPOSTTests
ctest --test-dir target/hypha-release-native-tests \
  -R '^kirin_live_publication_' --output-on-failure
```

The shared-ring tests fail if native mapping is unavailable; they do not silently skip or
substitute in-memory PCM. In environments denying `shm_open`, run the state-only tests and
retain the shared-ring tests as an explicit unfinished gate for a permitted native environment.

The earlier portable renderer lease test exercised a slot and renderer directly, without
the processor's PRE enable-to-timer sequence. It could prove safe renderer re-entry while
missing PRE retirement. Full native transport tests require OS shared memory; leaving them
unexecuted cannot establish product transport acceptance. The independent state-only tests
now catch this lifetime error even when OS mappings cannot be created. Synthetic wrapper
tests still require separate real-host acceptance on supported platforms and formats.
