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
execution/reclaim deadlines. A damaged immutable ownership claim remains an error:
no owner, process identity or timestamp is inferred, and no mtime-based deadline or
immediate takeover is introduced.

Entry preparation progress remains asynchronous. Its samples-only schema has no
four-way terminal state; completion/abort evidence in existing separate lifecycle
records must not be represented as a newly implemented preparation terminal. That
remaining mapping requires explicit resolution before claiming complete coverage.
