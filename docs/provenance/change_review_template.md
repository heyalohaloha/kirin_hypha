# Provenance review for changes after the baseline

Use this in a PR description or retained review record. Record the actual base and candidate SHA.
This reviews new differences; it does not require another audit of the 1,357 historical commits.
New evidence can reopen an affected baseline item. Security/source gates still run as defined.

| Field | Evidence to provide |
|---|---|
| Base and candidate | Actual commit SHAs and changed files |
| Source classification | Original work, specification/reference, OSS adaptation/dependency, or Unknown |
| Origin | Source URL/version/commit and relevant license, where applicable |
| Changes | Adaptation extent, retained attribution and modification notices |
| Assets/data/AI | Input origins, generation references, applicable terms and intended distribution use |
| Distribution | Newly included components/assets, NOTICE/licenses and Corresponding Source delivery |
| Verification | Relevant test commands/results and candidate CI |
| Unresolved | Missing evidence, affected materials/use and review/hold decision |

Do not accept leaked/unauthorized private materials or third-party proprietary decompiled code.
Do not treat product comparisons or a common feature concept as implementation-source evidence.
Unknown is neither a finding of infringement nor automatic permission to distribute.

The final baseline SHA is recorded separately once all accepted PRs are merged. Until then,
the 2026-10-05 record is a baseline candidate. Do not use an earlier CI run as a new candidate PASS.
