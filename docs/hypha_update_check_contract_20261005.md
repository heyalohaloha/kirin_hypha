# Optional official update information — implementation and acceptance

Date: 2026-10-05. Source baseline: `cd045837b4f4c28c08ade2cdb3ea707560b39ff7` (B-1229).
This is source implementation, not a public release or real-DAW acceptance record.

## Scope and independence

The same editor implementation serves PRE/POST and TRACK/STEM/2MIX. The updater adds only
information-menu actions and a brief passive notice. A processor owns the non-RT service only;
its processing, audio output, measurement, pairing, Reference, license projection, Record and
`plugin_data` behaviour are unchanged.

Update state is `state.json` plus an OS exclusion file `owner.lock` inside:

- macOS: the OS account's actual home / `Library/Application Support/Kirin Hypha/UpdateCheck/v1`.
- Windows: JUCE's per-user application-data folder / `Kirin Hypha/UpdateCheck/v1`.

The macOS path is resolved from the account rather than a DAW container. If that location cannot
be accessed, no HTTP is performed; there is no alternate container or temporary-root fallback.
Different OS users have separate state. Real sandbox hosts still need permission/path acceptance.
`identity.json`, `plugin_data/reference/v2` and all Reference lease/state files remain untouched.

## Communication and lifetime

Automatic checking starts OFF. An explicit manual request and automatic checking share a persisted
24-hour attempt budget. The worker reserves it durably before HTTP. Success, HTTP error, invalid
signature, cancellation, failure or restart cannot refund it. Simultaneous modules/hosts use a
nonblocking OS file lock (`flock` / `LockFileEx`), including separate modules in one process.
Damaged, aliased, inaccessible or rolled-back state fails closed instead of causing repeated HTTP.
The budget assumes an intact dedicated state and a normally advancing OS clock; deliberate state
deletion or clock manipulation is not an entitlement/security control.

No audio callback, UI timer or repeated menu opening performs HTTP. Enabled services schedule at
most one daily worker wake, not repeated polling. UI timers copy memory at most once a second.
Closing an editor does not wait for HTTP: a live processor lazily owns its non-RT service across
editor closes. The module registry holds only weak references. Destroying the last processor
cancels and drains its worker before the host unloads the module; CRT/DllMain static destruction
never owns or joins an update worker. The ownership field is not accessed by any audio callback.
Automatic checks can run daily with the editor closed only while a processor still owns the
service. A service is not created just by constructing a processor. Native lifetime fixtures and
real Windows DLL-unload acceptance are separate evidence.

There is one fixed GET endpoint:
`https://kirinmastering.com/updates/hypha-stable.v1.json`.
No redirects or application retries. The worker requests cancellation at a three-second total
communication deadline, including a stalled body. The response must be HTTP 200, JSON, valid
UTF-8 without raw NUL bytes and at most 16 KiB. No version, installation ID, license, Work,
media path or audio is sent in query/body. IP addresses and normal OS networking behaviour
are not anonymous telemetry.

macOS uses one raw Network.framework TLS connection and one explicit HTTP/1.1 GET send. It does
not use NSURLSession, JUCE's accumulating WebInputStream or an HTTP-layer replay/retry mechanism.
Fast-open/replay and keep-alive are disabled; a waiting connection is cancelled, not restarted.
Default certificate verification, fixed server name, required peer authentication and minimum
TLS 1.2 remain enabled. Redirects, HTTP authentication and compressed responses are rejected.
The bounded parser handles fixed, chunked and close-delimited responses, retaining at most
8 KiB of headers and 16 KiB of body; one bounded receive is outstanding. Network-stack buffers
and TCP packet retransmissions are not an unconditional total-wire-byte guarantee. Cancellation
is requested at the three-second deadline. The final cancelled callback, zero outstanding
send/receive callbacks and a private serial-queue barrier precede handle release and return.
This drain prioritises host safety and is not an unconditional three-second return guarantee.
Loopback disconnect tests assert exactly one received GET, including an immediate dropped socket.

Windows uses its own asynchronous WinHTTP transport, not JUCE's internally retrying Windows
transport. It submits one GET, disables automatic authentication/cookies/redirects/keep-alive,
allows one connection attempt, and disables failed-connection retries and shared connection
pooling. It does not discover or use a proxy; unsupported options or a proxy-only network fail
closed, leaving the explicit browser link available. Closing cancels the request, then waits for
WinHTTP's unload notification before freeing callback state or unloading module code. This
callback drain prioritises host safety and is not an unconditional three-second function-return
guarantee. Windows native compilation, timeout and unload acceptance remain unperformed.

## Trust and notification

The fixed pinned RSA-2048 key verifies PKCS#1 v1.5 / SHA-256 signatures over exact payload bytes.
Strict schema, numeric stable version, full source commit, platform/format, expiry (maximum 30
days) and monotonic publication sequence are required. An existing sequence cannot change bytes.
Cached information is reverified; a failed later request is not relabelled as a successful check
after restart. Equal version numbers with a different/unverified source do not attest an official
build. A greater loaded version is not automatically downgraded. Snapshots carry the verified
publication/expiry timestamps. A memory-only time check invalidates expired facts and delayed
notice tickets, without HTTP, filesystem reads or changing the daily communication budget.

A notice ticket is reserved in the dedicated state before presentation, prioritising at-most-once
cross-host suppression. A hidden/closed owner may therefore miss the brief notice; available
information remains in the menu. Blind, invisible editors and existing action/Record feedback
defer presentation. Explicit manual requests always receive a result, even for an already announced
release. Automatic failures are silent. Browser opening, language and update installation are
explicit user choices through the existing fixed official links.

Lock contention observes one bounded, securely opened atomic state snapshot without owning a
lease, writing state or sending HTTP. This observation is not authoritative if the owner is
still committing a preference. A worker-only flag therefore schedules a state refresh no sooner
than 24 hours later, even when the observed setting is OFF or the read failed. Only a successful
read under the exclusion lock clears that flag. Refresh never replays a failed preference action
or acknowledges a new one, and an authoritative OFF then sleeps indefinitely with zero HTTP.
This exceptional daily state refresh is distinct from communication or UI polling.

## Producer and public activation

`scripts/updates/update_manifest.mjs` is offline-only. It verifies retained exact-candidate release
evidence, all three distribution channels and public HP/GitHub receipts, then signs a bounded
manifest with an externally supplied private key. Existing OS/Sense unsigned manifests are not
Hypha release evidence. Private keys and production evidence are never placed in the repository.

No production key, signed endpoint, HP publication or signing credential was provisioned in this
implementation. An empty/invalid key means zero HTTP; existing official links still work. Before
activation, approve key ownership/rotation, supply the public key for the exact release,
publish the signed endpoint with JSON MIME/no redirect, and
verify it through the actual public route. A manifest expiry also requires an authorised refresh
with a higher sequence; expired content must not silently be called current.

The three-channel release gate, signing/notarisation, Windows installer acceptance and release
permission are unchanged. The normal entry accepts an explicit approved public key and defaults
to empty. Every configure resets the cache from its per-invocation input, including empty; a
stale cached key cannot enable checking. Canonical RSA-2048/65537 validation rejects zero,
even and shorter-than-2048-bit moduli. The key digest is compiled into every binary, stamped
into every Mac plist, and bound into freeze and the PKG/ZIP/EXE/AAX provenance beside the exact
distributed payload hashes. Package gates inspect expanded/extracted Mac bytes and signed
Windows payloads. Mac Universal evidence validates the fat table and both thin architecture
headers, requires the marker inside each ARM64/Intel slice and retains per-slice SHA-256 beside
the full binary hash. One aggregate marker cannot qualify an unbound old architecture.
The offline producer requires the external signer to resolve to that key.
AU `network.client` additionally requires protocol integer `1` and the approved nonempty digest;
protocol `1` alone cannot authorize networking.

Withdrawal after `RELEASE_INCIDENT` requires a retained prior completed-state snapshot and a
previous signed manifest for the same version/source and all platforms/formats. The snapshot's
original gates/reports/bytes must still verify, and its freeze/package set must equal the incident.
Only a higher-sequence withdrawn manifest is prepared. This never qualifies an incomplete new
release or performs publication. Preserve completed receipts before recording incident results.

## Regression and remaining gates

Local fixtures cover default OFF, daily boundary, repeated requests, signature errors, success and
failure across restart, cancellation, state corruption, rollback, thread/process exclusion and
crashed owners. Producer fixtures cover required channel receipts and tampered retained bytes.
UI/source contracts cover no timer I/O, explicit fixed links, language, deferred notices and no
updater dependencies in the existing OS/audio subsystems.
Preference/manual responses use their own completion tokens, not a general status revision.
An older in-flight request cannot acknowledge a queued OFF action. The preference acknowledgement
also records its own persisted result/value; a superseding window action is not called a save failure.
Successful preference persistence is published independently BEFORE optional HTTP. A later transport
exception cannot report a saved ON as OFF/unsaved. Only the communication outcome then fails.

Existing Reference runtime, PRE display, editor language/render and Rust Record/pairing/license
suites must be run in isolated fixtures. Existing successes are not a new exact-binary host pass.
After a provisioned build, real macOS and Windows acceptance must still cover:

1. Old/new/same version information and explicit English/Japanese download routing.
2. Multiple PRE/POST, formats and DAWs resolving the same root, with at most one HTTP attempt.
3. Offline, slow/truncated responses, timeout, OFF cancellation, restart and DLL unload.
4. Reference receiving/reopening, manual license recheck, Record finalisation and `plugin_data`
   output both with update checking ON and during update failure/cancellation.
5. Normal measurement/audio transparency and zero callback allocations, locks or HTTP.

The public and real-device gates that remain are listed under Status below.
No published claim of completed regression is implied by this implementation document.

## Status

This document describes the source implementation and its offline tests. Before it can be
published as a deployed feature, these remain: approved production key selection and the
exact-candidate signing factory review, a signed public endpoint and its readback, real macOS
and Windows host regression with checking enabled, Windows native transport, SDK and
module-unload acceptance, and the complete three-channel release gates.

## Primary API references

- [JUCE WebInputStream](https://docs.juce.com/master/classjuce_1_1WebInputStream.html)
- [Apple connection cancellation](https://developer.apple.com/documentation/network/nw_connection_cancel(_:))
- [Apple bounded connection receive](https://developer.apple.com/documentation/network/nw_connection_receive(_:_:_:_:))
- [Apple explicit connection send](https://developer.apple.com/documentation/network/nw_connection_send(_:_:_:_:_:))
- [Apple TLS server name](https://developer.apple.com/documentation/security/sec_protocol_options_set_tls_server_name(_:_:))
- [Apple required peer authentication](https://developer.apple.com/documentation/security/sec_protocol_options_set_peer_authentication_required(_:_:))
- [HTTP message framing, RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html#section-6.3)
- [Apple getpwuid_r](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/getpwuid_r.3.html)
- [Apple flock](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/flock.2.html)
- [Microsoft LockFileEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)
- [Microsoft WinHTTP options](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags)
- [Microsoft WinHttpCloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle)
