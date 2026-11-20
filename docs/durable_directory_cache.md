# Durable directory cache boundaries

Low-frequency durable directory creation examines every ancestor separately. On macOS,
only a filesystem identified as APFS retains the existing path/device/inode cache shortcut.
Non-APFS, unknown filesystems and other Unix platforms do not skip directory syncing on
that identity alone. Failed filesystem discovery is conservative, not permission to cache.
The existing best-effort rule remains: sync errors are logged and do not stop saving.
Windows does not use this Unix directory cache; its existing file-sync policy is unchanged.

This avoids falsely reusing a cached directory sync when another writer recreated a path
with the same inode on an affected filesystem. It does not establish that APFS can never
reuse an inode, nor add a crash, network-filesystem or TOCTOU guarantee. Modification or
birth timestamps are not treated as unique generation identifiers. No audio-thread,
heartbeat, JSON schema or ABI change is involved.

On affected filesystems, directory-sync work increases with ancestor depth. Latency must
be measured on the actual filesystem. The unit fixtures inject cache eligibility and equal
identities; that is not an actual same-inode recreation acceptance test. The separate
unmodified-platform disposable-save fixture checks normal operation without that override.
Real non-APFS inode reuse, performance and Windows acceptance remain unverified until run
on those systems. Existing cache-algorithm assertions remain under an explicit cache-eligible
fixture rather than being removed to accommodate the conservative policy.
