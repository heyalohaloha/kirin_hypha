# Native storage guard

The guard compares the whole Kirin OS storage tree before and after native tests, except
for the explicit app-owned paths below. Unknown paths are protected without source analysis
or a writer registration. Documents, macOS Application Support and Windows Roaming/Local
AppData roots use the real OS account independently of a redirected HOME. The guard never
creates, writes, restores or connects to a socket in these roots.

## Narrow app-owned exclusions

`scripts/native_storage_exclusions.mjs` is the single exclusion list. Names are exact paths
relative to each protected root, including complete subtrees for directories. The config rules
apply only to regular files, with numeric PID/counter fields for atomic siblings. Matching folds
case to cover case-insensitive volumes; it never matches a basename elsewhere or a prefix
such as `GPUCache-backup`. No `plugin_data` branch is excluded.

Each row has two ownership checks: it is a Kirin OS/Electron artifact, and its path component
is absent from Hypha's owned native source. Setup and cleanup recheck the latter, including
new source files. The observed changes motivating the list were Electron cache data, database
journals, endpoint discovery, browser/session preferences, single-instance links and folder
scan objects. DIPS covers the database accompanying its observed WAL. Kirin OS `getConfigPath()`
resolves userData/config.json and `writeConfig()` saves it through `atomicWriteFileSync`; that
writer creates `.config.json.tmp-<pid>-<counter>` in the same directory before rename.

| Excluded root-relative path | App ownership; Hypha source evidence checked on every inspection |
| --- | --- |
| `Code Cache/` | Electron compiled script cache; no `Code Cache` path reference |
| `GPUCache/` | Electron GPU cache; no `GPUCache` path reference |
| `DawnGraphiteCache/` | Electron Dawn/Graphite cache; no `DawnGraphiteCache` path reference |
| `DawnWebGPUCache/` | Electron Dawn/WebGPU cache; no `DawnWebGPUCache` path reference |
| `GraphiteDawnCache/` | Electron Graphite/Dawn cache; no `GraphiteDawnCache` path reference |
| `DevToolsActivePort` | Electron debugging endpoint; no `DevToolsActivePort` path reference |
| `DIPS` | Electron browsing-state database; no `DIPS` path reference |
| `DIPS-wal` | Electron browsing-state journal; no `DIPS-wal` path reference |
| `Local Storage/leveldb/` | Electron local storage; no `Local Storage` component reference |
| `Preferences` | Electron browser preferences; no `Preferences` path reference |
| `Session Storage/` | Electron session database; no `Session Storage` path reference |
| `SingletonCookie` | Electron single-instance cookie; no `SingletonCookie` path reference |
| `SingletonLock` | Electron single-instance lock; no `SingletonLock` path reference |
| `SingletonSocket` | Electron single-instance socket; no `SingletonSocket` path reference |
| `config.json` | Kirin OS userData settings; no `config.json` path reference |
| `.config.json.tmp-<pid>-<counter>` | Kirin OS atomic config sibling; no `.config.json.tmp-` prefix reference |
| `folder-scan/` | Kirin OS scan object store; no `folder-scan` path reference |

The source contract scans all owned C/C++/Objective-C/Rust files, including tests and files in
new directories. Vendored JUCE, vendor dependencies and generated/build/local-state trees are
excluded from this source scan. It rejects literal path component references even in readers;
introducing one requires reviewing/removing the exclusion. This avoids inferring destinations
from writer APIs, branches, aliases or reassignments. Adjacent/plus-joined string literals and
two-digit hex and Unicode escapes are checked too. Comment-separated adjacent literals, nonzero or multi-digit octal
escapes and variable-length hex escapes are refused as unsupported spellings. Source directory
links, native source file links and dangling links are refused, including a linked source root;
they cannot silently remove a writer from discovery. There are no source hashes or saved source
inventories.
This conservative literal check is not a compiler or a proof against arbitrary computed names;
computed paths and new languages still require ownership review. Full snapshot coverage of
non-excluded paths never depends on the outcome of path extraction.

Reference sharing (including library publications and recovery acknowledgements), Record and
reservation data, identity/licence, appearance and unknown DB/settings files remain protected.
Only the exact browser `Preferences` entry and confirmed app config files are excluded;
Hypha preference files, config backups, malformed temporary names and nested config paths stay
guarded. A directory or symlink named config.json or like a numeric temporary is also guarded.

## Snapshot boundaries and existing limitation

Snapshots record size, mtime, ctime, mode, inode and device, symlink text and target metadata.
Files are statted rather than copied. Sockets are observed without connecting, and symlink
cycles are bounded. An unreadable/disappearing monitored entry fails closed. Excluded entries
are skipped before stat, so app cache rotation during either snapshot cannot invalidate it.

Only ancestor directories of exclusions (the root and `Local Storage`) omit size/mtime/ctime,
since excluded activity changes those values. A non-root ordinary ancestor containing only
excluded entries, or no entries yet, is omitted altogether: its first creation and later removal
are expected app scaffolding. An unknown child makes the ancestor monitored again, including
mode/inode/device identity. Symlink ancestors always remain monitored. Protected roots themselves
retain presence and identity checks; all other directories retain their mutation timestamps.

A before/after snapshot cannot guarantee detecting a lock created and deleted entirely between
snapshots. This was also a limitation of the original whole-tree guard; restoring wider coverage
does not make it a write barrier. Existing parent timestamps catch many transient operations,
but an absent/recreated ancestry, omitted ancestor timestamps or filesystem timing can leave
no distinguishing final observation. In particular, a successful comparison is not proof that
an Appearance lock was never opened. The next version should reject real storage roots at
native-test write entry points, before lock creation or any other mutation. That write-entry
barrier is outside this change. The guard never attributes changes to a particular process:
an app update outside the exclusions still fails, even if Hypha did not cause it.

Baselines use schema `hypha-storage-guard-v4` and include exact names, file-only constraints and
the atomic sibling pattern. Original whole-tree, narrowed-scope and v3 baselines, changed rules
and mismatched roots are rejected.
Baselines must stay outside every protected root, including excluded branches and symlink
aliases. They are private build state; diagnostic output omits private filenames.

## Disposable regression tests

```sh
node --test scripts/native_storage_guard.test.mjs
```

Tests use only disposable checkout/build trees. Independently specified one-byte creation and
overwrite controls cover Reference, plugin_data, licence, appearance, unknown paths, misleading
prefixes, nested exclusion-name collisions and case/prototype-like names. Both platform root
layouts, missing/new roots, rename/removal, symlinks, sockets, invalid/legacy baselines and the
CTest setup/cleanup lifecycle remain covered. Source fixtures add excluded writes in branches,
reassignments, constants and new directories and require rejection; an ordinary new writer
passes without changing any list. A child process continuously updates the listed app paths
while snapshots pass; an additional Hypha byte makes cleanup fail. No real app is operated.

Child response waits use IPC notifications, errors and process exit, without individual response
timers. One 45-second parent test bounds the complete contract; cancellation kills only owned
fixture children and awaits their exit before deleting fixtures. CTest retains its 60-second
outer limit, leaving cleanup headroom. A nested disposable contract with a deliberately silent
child verifies cancellation and shutdown. Its short deadline tests the whole-contract watchdog,
not response latency. Config atomic replacement, in-progress temporary files, excluded-only
ancestor creation/removal, unsupported literal spellings and linked source folders have explicit
regression controls.

## Phase 1: Appearance standalone test write-entry guard

The first phase introduced a target-local guard for `KirinAppearanceContractTests`.
Its Appearance preference parent creation, primary/backup atomic-write entry and
lock creation/removal (including stale cleanup and destruction) reject mutations
outside an existing absolute `KIRIN_HYPHA_TEST_STORAGE_ROOT`. Shipping targets,
class layouts and ABI are unchanged. This is not a guard for all native writers;
shared runtime, activation/startup and plugin writers still need their own phase.

The protected root list is intended as a superset of the Node snapshot guard:
OS-account Documents/Kirin OS and platform application-data roots remain protected.
Windows additionally includes redirected Known Folder Documents, LocalAppData and
RoamingAppData, the conventional Profile/AppData/Local and Roaming paths, and any
absolute APPDATA/LOCALAPPDATA environment paths. Unix also protects .config/Kirin OS.
Invalid root discovery fails closed. The Node snapshot implementation is unchanged.
Mac and Windows path components compare case-insensitively, deliberately rejecting
case-only alternate names even on case-sensitive Mac volumes. Existing descendant
symlinks/reparse points are rejected. This is preflight validation, not a TOCTOU
barrier or a guarantee against all filesystem aliases (such as Windows short names
or alternate volume identities). Atomic staging/cleanup uses the checked parent;
it is not independently intercepted at each syscall.

Disposable fixtures exercise denied writes/locks, stale cleanup, policy revocation,
case aliases and normal isolated persistence. They do not attempt writes to real
storage. Windows Known Folder redirection, reparse points and Windows execution
remain unverified on real hardware; a Mac pass must not be reported as Windows
validation. Mutation-entry counts are permitted guard entries, not syscall counts.

## Phase 2a: limited C++ runtime activation

`StorageWriteGuard.h` now supplies identical inline runtime definitions to Appearance
and AtomicFile callers. Shipping starts disabled and saves normally; environment
variables alone do not activate it. No class layout or C ABI changes are introduced.
The Appearance and AtomicFile native mains explicitly enable it before fixture
creation (including the Appearance lock child). Missing, relative, nonexistent or
protected roots exit 78. Failed initialization stays enabled-and-denied permanently.
The initial allowed root is fixed: later fixture/environment roots cannot expand it.
Fixture policies can narrow it and add protected exclusions, never remove real roots.

Appearance guards preference directories, primary/backup publication and lock
creation/removal. AtomicFile guards the target, staging entry and final publication.
TemporaryFile cleanup retains its existing checked same-parent behavior; this is
not a syscall interception layer or a TOCTOU guarantee. The two mains use
`--guard-startup-only` for no-write preflight. AtomicFile also has isolated
`--guard-failed-enable` and `--guard-shipping-disabled` modes. The latter deliberately
leaves the runtime disabled but validates its disposable fixture root before saving.
`kirin_storage_write_guard_startup` tests direct startup separately from CTest setup.
`kirin_atomic_file_boundaries` continues to provide its own existing child root.

This phase does NOT protect other mains, upper-level directory creation outside
Appearance, Rust, or separately loaded modules. Inline state is not a cross-module
policy transport. It does not claim protection if a test runs writers before its
explicit initialization. The normal POST build must still be checked independently.
Windows execution and alias/redirect behavior remain unverified on actual Windows.

## Phase 2b: PreDisplay presence writer and one additional main

PreDisplay presence/capability/acknowledgement publication now checks permission
before parent creation and delegates identical LF JSON publication to AtomicFile.
Presence, acknowledgement and capability removal each check permission before
unlink. Existing identity checks, lease durations and worker/RT behavior are unchanged.
The standalone PreDisplay runtime main enables the same fixed-root guard before
fixtures. `--guard-startup-only` has the same exit-78 failure contract; its isolated
`--guard-shipping-disabled` mode exercises normal publication/update/removal without
activating test policy. `kirin_pre_display_write_guard_startup` includes this third
main as well as the two preceding ones. Synthetic revoked-policy fixtures check
missing parent creation and byte-preserving denial of all three cleanup operations.

This additional main does not activate the guard for other executables, Rust or
separately loaded plugins. Pre-main writers and module propagation are still not
covered. In particular, Windows environment-redirecting sandboxes require separate
policy work before broader bootstrap adoption; this phase does not relax real-root
protection to accommodate them. Windows runtime execution remains unverified.
