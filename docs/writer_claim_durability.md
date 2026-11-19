# Writer claim transition durability

The immutable ownership claim and its mutable `.state` sidecar keep their existing
schema. Readiness and closure publication use the existing durable atomic writer:
content synchronization, rename, and parent synchronization on supported platforms.
Synchronization remains best-effort under the existing durability contract; ordinary
write/rename failure is still an error. In-memory ready/closed flags are committed
only after successful publication, so a later plain heartbeat cannot publish a
transition whose write failed. A synchronization failure alone is logged and
suppressed by the existing helper: successful return is not proof of persistence
across power loss on that filesystem. The sync-failure fixture verifies this
best-effort behavior, not an unconditional durability guarantee. Frequent heartbeat-only publication remains
plain atomic, without synchronization, because its measured cost exceeded budget.

A damaged or unreadable sidecar supplies no fresh readiness or closure. Readers use
only the immutable claim's original heartbeat and ownership under the existing
execution deadlines. Acquisition handles a damaged immutable body separately from
ownership/readiness readers: it waits until more than the existing 15-minute
reclaim deadline after the latest body mtime, sidecar mtime, or readable sidecar
heartbeat. None of these recovery timestamps creates an owner, readiness, closure,
or execution authority. Missing/unusable timestamp evidence denies recovery;
future timestamps extend the wait. One diagnostic is logged per recovery attempt.
A repaired owner or changed timestamp evidence on the final recheck denies recovery.
The existing remove/link primitive is retained; the recheck is not a filesystem
compare-and-unlink transaction and does not eliminate its pre-existing race.
The initial immutable body already uses successful content `sync_all` before
hard-link publication; its synchronization failure remains an error.

The isolated tests exercise the exact 15-minute boundary, empty/truncated bodies,
new/old/missing sidecars, body-read error injection, repair/refresh during recovery,
and actual claim replacement without fabricated readiness. Permission-error
injection is deliberate so privileged test runners cannot silently bypass chmod;
it is not a real-device permission or power-loss test.

Entry preparation progress is a best-effort, plain atomic checkpoint. Neither
intermediate nor final samples are synchronized. Samples do not authorize readiness
or completion: the existing writer ready and AllStop/DropCommitted/Aborted lifecycle
facts remain the durable terminal authority under the contract above. No terminal
samples rewrite or process-lifetime owner registry is retained. Existing on-disk
entry schemas and samples are still readable; no data migration or deletion occurs.

Entry acquisition reserves its normalized entry path in a process-local set before
disk ownership checks or replacement. The global mutex protects only insertion and
removal, never disk I/O. A concurrent acquisition for that path is rejected; the
reservation is released on every return path after disk publication or failure.
After success, the fresh disk claim provides ownership. This serializes cooperating
same-process acquisitions, not arbitrary external modifications or cross-process
compare/unlink races. No Audio Thread work, schema or ABI is added.
