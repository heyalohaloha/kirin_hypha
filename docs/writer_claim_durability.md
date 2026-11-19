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

Entry acquisition reserves its canonical-parent, lowercase-filename key in a
binary-local set before disk ownership checks or replacement. The global mutex
protects only insertion and removal, never disk I/O. This remains a secondary guard:
a sibling `.json.lock` file supplies non-waiting OS exclusion across independently
linked plugin binaries and processes. Acquisition holds it from before publication
through the stale-entry reread, age check, removal and link. Contention returns
AlreadyActive; it never waits for an entry mutation. The existing 15-minute deadline
and durable link/removal operations are unchanged.

Progress publication takes the same sibling lock before reading, verifies the exact
owner token returned to its IO caller at claim time, then writes the plain atomic
checkpoint under the lock. A busy lock skips that checkpoint; a later chunk may
retry. A stale owner cannot adopt a replacement token from disk. Tokens use UUIDs
rather than PID plus a binary-local sequence, since separate plugin binaries in the
same process can otherwise generate equal tokens. The JSON fields are unchanged.

The IO caller retains one token bound to its session and Record generation. An
unrelated session/generation cannot use it; the next successful claim replaces it.
Audio, Measure and Stop do not access or clear this private slot. Access by IO is
also non-waiting. This slot is neither a durable registry nor a new writer-readiness
fact. A restarted state without its token does not recover authority by reading an
entry file.

The sibling inode is not unlinked on release: unlinking a held lock could allow a
second opener to lock a different inode at the same name. It is an empty, retained
session artifact. This serializes cooperating entry mutations, not arbitrary file
edits or older binaries which do not take the lock. The separate writer-claim
recovery limitation above remains unchanged. No Audio Thread work, schema or ABI
is added.
