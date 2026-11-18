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

Entry preparation progress remains asynchronous. Completion is the existing writer
ready fact; failure/cancellation is the existing AllStop/DropCommitted/Aborted
lifecycle fact. After those successful endpoints, the process rewrites only its
own registered entry's last samples once with the same schema using the durable
atomic writer. Readiness/lifecycle facts, not progress, remain terminal authority.
No-progress entries are not populated with invented samples, and abandoned owners
are not written on their behalf.

Entry acquisition registers the exact owner token in process memory. Progress and
final samples publication share an IO/control-only mutex; no Audio Thread work,
JSON field or ABI is added. Each write rechecks owner, PID, session, role, instance,
and preparation generation. A terminal observation stops further plain publication
before attempting final synchronization. Ordinary write failure leaves that entry
stopped and retryable through a repeated endpoint or progress observation; it does
not invalidate an already committed terminal fact. Such failures are logged.
Best-effort synchronization failure retains the durable helper's existing contract,
so one attempted synchronization is not proof against power loss.

The bookkeeping is per entry path in process memory, replaced on a newly acquired
owner, and is not persisted across process death. It serializes this process's
cooperating writers, not arbitrary external modifications or stale cross-process
compare/unlink races. It records only ownership and the last published progress,
not PCM. A later process cannot adopt that memory as permission to finalize an old
owner. Successful endpoints and delayed progress use the same serialization.
