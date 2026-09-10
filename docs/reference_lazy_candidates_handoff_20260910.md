# Reference Candidate lazy preparation handoff — 2026-09-10

## Completed boundary

- Kirin OS publishes every enabled Check and Candidate in order, but initially prepares only the first Candidate in each Check.
- Hypha keeps pending Candidates visible in the existing B Source selector with a `PREPARE` suffix. Selecting one writes an immutable request for the exact Preset revision, Check, and Candidate.
- Preparation never selects B. Hypha revokes any prior B publication, keeps A live, disables the stale Cue selector, and restores the requested Candidate only after an exact acknowledgement and refreshed projection.
- A Check is rejected unless it has at least one prepared Candidate. Prepared Candidates have a real source artifact; pending Candidates must have `null`.
- Normal Kirin OS refresh still revalidates the active source and Profile. Reuse of unrelated prepared artifacts is limited to an explicit Candidate request.

## Commits

- Kirin OS: `2a8da9109` (`W-3026`)
- Hypha: recorded in the session completion report.

## Automated verification completed

- Hypha POST Debug compile: passed.
- Hypha focused runtime test: pending Candidate visibility, exact request, A safety, acknowledgement, selection restoration, and no duplicate Candidate row passed.
- Kirin OS focused service tests: exact request/ack binding, live capability and Manifest validation, crash-recovered acknowledgement, one-Candidate preparation, unchanged dependency retention, malformed target rejection, publication rollback safety, and IPC routing passed.
- Kirin OS changed-file ESLint and JSON Schema compilation passed.
- Hypha source line budget passed. Full suites were intentionally not repeated.

## Manual verification remaining

Run after returning to the DAW machine:

1. Open POST Reference with a Check that has at least two Candidates.
2. Confirm the first Candidate is immediately usable and the others remain visible with `PREPARE`.
3. While A is playing, choose a pending Candidate. Confirm audio remains the live DAW mix, B cannot start, and the old Cue selector is disabled during preparation.
4. Confirm the status progresses from preparation to ready, the chosen Candidate remains selected, and B starts only after an explicit B action.
5. Remove or replace the selected source and confirm the existing safe recovery message appears without changing A.
6. Repeat once after restarting Kirin OS to confirm an already-prepared Candidate does not require a second analysis.

No release, installation, notarization, or Windows validation was performed in this session.
