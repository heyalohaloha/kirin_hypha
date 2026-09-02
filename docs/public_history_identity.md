# Public history identity contract

Kirin Hypha keeps public Git history additive. Published commits and tags are not rewritten to make
an auxiliary label unique.

## Identifiers

- The primary source identifier is the full 40-character Git commit SHA.
- A published release is identified by its immutable SemVer tag.
- A review is identified by its GitHub pull request number.
- A `B-NNN` value in a commit subject is a supplemental work label. It is not a unique commit,
  release, or review identifier.
- A history-convergence merge uses its full SHA and PR number. It does not allocate a new B number
  unless Daisuke supplies one.

## Duplicate B labels

`scripts/check_public_history.mjs` scans every commit reachable from the candidate public-main tip.
Only the canonical leading subject form `[B-NNN]` is counted. Historical composite text such as
`[B-447/B-448]` remains untouched but is not parsed as a canonical single label.

The checker compares every duplicated canonical B label with
`scripts/public_history_b_allowlist.tsv`. The allowlist contains the exact full-SHA set for each
known duplicate and is generated from the integrated history by:

```bash
node scripts/check_public_history.mjs --write-allowlist --tip HEAD
```

An unlisted duplicate, an added or missing SHA, a stale group, a short SHA, or a hand-reordered
allowlist fails the release-source contract. Past subjects and tags remain unchanged.

## Public tags

Every strict public tag matching `vMAJOR.MINOR.PATCH` must be reachable from the candidate main tip.
The pull request gate checks the complete would-be-main history. After merge, the same command is run
against `origin/main`, making reachability a property of the actual public default branch.

## Main enforcement

- Every change to `main` goes through a pull request, including changes made by repository admins.
- `public history identity`, `release source contract (macos)`, `auval arm64 (AU validation)`, and
  `windows VST3 preflight` are required checks against the current `main` tip.
- The lightweight public-history job has no event condition, so it also runs after an ordinary
  `main` push. Expensive build jobs retain their workflow-dispatch, pull-request, or `[ci full]` gate.
- Merge commits are the only enabled pull-request merge method. Squash and rebase merges are disabled
  because they discard the source-branch ancestry this contract preserves.
- Force pushes and branch deletion are disabled for `main`.
