# Hypha signed update manifest — offline producer

`update_manifest.mjs` prepares a signed static file only. It does not communicate,
publish HP changes, deploy, upload, generate a production key or alter release gates.
The normal Hypha release pipeline must first reach `RELEASE_COMPLETE`: exact source,
signed packages, all three delivery channels, EN/JA HP and A7 evidence. The producer
rechecks the existing report schemas, candidate bindings and retained artifact hashes.

The wire is one JSON envelope with `payload` and `signature`. Payload is base64 of
the compact UTF-8 JSON bytes; signature is base64 of the 256-byte RSA-2048
PKCS#1 v1.5 SHA-256 signature. The response including both is at most 16 KiB.
There is no URL or remote key in the payload. The product uses its fixed HP landing
and a pinned public key (`10001,<512 lowercase modulus hex digits>`). Empty keys
must keep checking disabled. Payload publication time is manifest publication time,
not the historical product release date; maximum lifetime is 30 days. Publish with
a shorter operational expiry if regular refresh is available. Expired manifests are
not update notifications, even when their signatures remain valid.

```sh
node scripts/updates/update_manifest.mjs --help
node --test scripts/updates/update_manifest.test.mjs
```

An authorized operator supplies the existing private release-state path, an external
RSA-2048/65537 private key, a strictly increasing safe-integer sequence, expiry and
a new output path. Use `--previous` with the last signed envelope (expired is permitted
only for sequence/version continuity). `--initial` is explicit first publication;
it is not a routine substitute for missing previous evidence. Output is exclusive
and never overwrites an existing file. Production keys must not be inside either
the producer checkout or the retained product repository. This command prepares
output; a separate authorized HP publication and actual route readback remain required.

`generate_test_fixture.mjs` creates fresh disposable cryptographic vectors on stdout,
retaining only the test public key and signatures. Its private key remains in memory
and is discarded. `juce_shell/tests/fixtures/update_manifest_fixture.json` is such a
test-only fixture, not a trusted product key. The native test accepts a fixture path
so CI can also exercise fresh vectors without credentials or external state.

The existing HP OS/Sense `kirin-products.json` producer permits one verified delivery
channel and has no Hypha signature. It is not a substitute for this three-channel
gate. The actual Hypha manifest route, approved production key provisioning and HP
allowlist/deployment/readback integration must be established before enabling checking.

Build/release inputs default explicitly to empty. The normal entry accepts
`--update-public-key KEY`; formal shell producers and packagers use
`KIRIN_HYPHA_UPDATE_PUBLIC_KEY`, Windows AAX uses `-UpdatePublicKey`, and the Windows
installer uses `--update-public-key`. CMake takes only the per-invocation
`KIRIN_HYPHA_UPDATE_PUBLIC_KEY_INPUT` and resets its cache on every configure.
All inputs reject a zero, even or shorter-than-2048-bit modulus and noncanonical text.
Each binary carries `KirinHyphaUpdateKeySha256=<digest or disabled>;` and every Mac
bundle plist carries the digest. The expanded PKG, extracted ZIP, signed Windows
payload and AAX provenance record the digest together with exact binary hashes in
`updateCheck`. The release freeze binds that same input. `--key-file` must resolve
to that exact public key before a manifest is prepared; a disabled or mismatched
distributed candidate cannot produce an enabled update manifest.
Mac Universal inspection validates the fat architecture table and both thin headers,
requires the key marker inside each ARM64/Intel slice, and records each slice hash
beside the full binary hash. A single aggregate marker cannot qualify an old slice.

An incident can withdraw an already completed public release without certifying a
new release: use `--withdrawn --previous LAST_SIGNED_FILE --completed-state FILE`
with the current `RELEASE_INCIDENT` state. The completed-state snapshot must retain
all original successful gates, reports and bytes; its source/candidate/freeze/package
set must equal the incident's. The previous signed payload must name that same
version/source and both platforms/all formats. Missing or changed evidence fails
closed. Preserve the completed snapshot and its immutable reports before recording
incident results. The sequence still advances and publication remains a separate
authorized operation. This route does not accept incomplete new-release evidence.

References: [Node crypto signing](https://nodejs.org/api/crypto.html#cryptosignalgorithm-data-key-callback),
[RFC 8017 RSASSA-PKCS1-v1_5 verification](https://www.rfc-editor.org/rfc/rfc8017.html#section-8.2.2).

## Windows no-resend transport

`UpdateTransportWindows.cpp` uses a separate asynchronous WinHTTP session rather
than JUCE's WinINet transport (which can resend after `ERROR_INTERNET_FORCE_RETRY`).
It submits one fixed HTTPS GET, refuses redirects/authentication/cookies, disables
connection reuse and automatic failed-connection retries, and accepts only a 200
JSON/UTF-8 response of at most 16 KiB. No proxy/PAC discovery is performed; proxy-only
networks fail closed and the manual HP landing remains available. TLS is restricted
to TLS 1.2 with normal certificate validation, without an insecure protocol fallback.

The entire network operation has a 3-second deadline and cancellation is checked
at most 50 ms apart while waiting. Closing the request cancels pending work. Callback
state and read buffers are not destroyed until `WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT`
signals the last session callback has returned. This cleanup/drain is separate from
the network deadline and must not be timed out by freeing callback state or unloading
the module prematurely. Actual cancellation/unload timing needs Windows-native tests.
If the OS does not support the required no-retry/no-pooling options, no GET is sent.

`node --test scripts/updates/winhttp_transport.test.mjs` checks the source boundaries;
it is not a Windows SDK compile, actual network test or DLL-unload acceptance.
Required native coverage includes success, 30x/401/407, slow headers/body, cancellation,
connection/TLS failures, body bounds, unsupported options and session/module unload.

Official references: [WinHttpSendRequest](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsendrequest),
[WinHttpCloseHandle and callback lifetime](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle),
[WinHTTP options](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags),
[Microsoft Windows SDK declarations](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/winhttp.h).
