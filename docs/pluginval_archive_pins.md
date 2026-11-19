# pluginval archive integrity

`scripts/pluginval_archives.json` pins the exact official Tracktion v1.0.4 macOS and Windows
release assets. The upstream release does not publish an independent checksum for these
archives; these SHA-256 values fix the first official bytes retrieved on 2026-11-19, not an
independent signature. Asset ID and byte count are provenance metadata, not substitutes for a hash.

Both validation paths verify the whole archive before extraction. Unknown versions (including
`latest`) fail closed, and an integrity mismatch is not retried or hidden by another download.
Every invocation extracts into a fresh private directory, so an old cached executable or
leftover file is not reused. macOS may cache the ZIP but must verify it every time.

`PLUGINVAL_BIN` is an explicit local diagnostic override only. It is rejected in CI; a local
run writes `pluginval-tool.json` with `qualification: diagnostic-only` and exits 2 after successful
diagnostic validation rather than reporting formal qualification. It cannot replace the archive
pin using an environment SHA. Existing binary provenance checks remain separate and unchanged.

Run the synthetic archive and source-wiring checks with `node --test scripts/pluginval_archive.test.mjs`.
These fixtures do not execute a downloaded vendor binary. Workflow execution and actual pluginval
host/plugin validation remain separate release checks.
