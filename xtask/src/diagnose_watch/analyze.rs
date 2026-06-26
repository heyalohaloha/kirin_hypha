use std::collections::HashMap;

use super::{FindingRow, Snapshot, WatchRow};

pub(super) fn refresh_findings(snapshot: &mut Snapshot) {
    snapshot.finding_rows = analyze(snapshot);
}

fn analyze(snapshot: &Snapshot) -> Vec<FindingRow> {
    let mut rows = Vec::new();
    let pre_rows: Vec<&WatchRow> = snapshot
        .watch_rows
        .iter()
        .filter(|row| row.role == "PRE")
        .collect();
    let post_rows: Vec<&WatchRow> = snapshot
        .watch_rows
        .iter()
        .filter(|row| row.role == "POST")
        .collect();
    let eligible_pre_rows: Vec<&WatchRow> = pre_rows
        .iter()
        .copied()
        .filter(|row| is_pair_candidate(row))
        .collect();

    for pre in &pre_rows {
        add_pre_exclusion_finding(&mut rows, pre);
    }
    add_duplicate_name_findings(&mut rows, &eligible_pre_rows);
    add_post_target_findings(&mut rows, &post_rows, &pre_rows, &eligible_pre_rows);
    add_pending_signal_findings(&mut rows, snapshot);

    rows.sort_by(|a, b| {
        level_rank(&a.level)
            .cmp(&level_rank(&b.level))
            .then(a.code.cmp(&b.code))
            .then(a.subject.cmp(&b.subject))
    });
    rows
}

fn add_pre_exclusion_finding(rows: &mut Vec<FindingRow>, pre: &WatchRow) {
    if pre.signal_state == "bypassed" {
        rows.push(FindingRow {
            level: "warn".to_string(),
            code: "BYPASSED_PRE".to_string(),
            subject: watch_subject(pre),
            detail: "PRE is bypassed; pair candidate scan excludes bypassed PRE rows".to_string(),
        });
    }
    if pre
        .age_secs
        .is_some_and(|age| age > kirin_measure::DISCOVERY_STALE_SECS)
    {
        rows.push(FindingRow {
            level: "warn".to_string(),
            code: "STALE_PRE".to_string(),
            subject: watch_subject(pre),
            detail: format!(
                "pre.json age {} exceeds discovery freshness window {}s; POST candidate menus ignore it",
                pre.age_s,
                kirin_measure::DISCOVERY_STALE_SECS
            ),
        });
    }
}

fn add_duplicate_name_findings(rows: &mut Vec<FindingRow>, eligible_pre_rows: &[&WatchRow]) {
    let mut by_name: HashMap<&str, Vec<&WatchRow>> = HashMap::new();
    for pre in eligible_pre_rows {
        if let Some(name) = normalized_name(&pre.name) {
            by_name.entry(name).or_default().push(pre);
        }
    }

    for (name, matches) in by_name {
        if matches.len() > 1 {
            rows.push(FindingRow {
                level: "warn".to_string(),
                code: "DUPLICATE_PRE_NAME".to_string(),
                subject: format!("PRE name \"{name}\""),
                detail: format!(
                    "{} fresh non-bypassed PRE rows share this name; strict POST selection becomes ambiguous: {}",
                    matches.len(),
                    watch_subjects(&matches)
                ),
            });
        }
    }
}

fn add_post_target_findings(
    rows: &mut Vec<FindingRow>,
    post_rows: &[&WatchRow],
    pre_rows: &[&WatchRow],
    eligible_pre_rows: &[&WatchRow],
) {
    for post in post_rows {
        let Some(target) = normalized_name(&post.pair_pre_name) else {
            if eligible_pre_rows.is_empty() {
                rows.push(FindingRow {
                    level: "warn".to_string(),
                    code: "POST_NO_ELIGIBLE_PRE".to_string(),
                    subject: watch_subject(post),
                    detail: "POST has no selected pair name and no fresh non-bypassed PRE candidates are visible".to_string(),
                });
            }
            continue;
        };

        let eligible_matches: Vec<&WatchRow> = eligible_pre_rows
            .iter()
            .copied()
            .filter(|pre| normalized_name(&pre.name) == Some(target))
            .collect();
        if eligible_matches.len() == 1 {
            continue;
        }
        if eligible_matches.len() > 1 {
            rows.push(FindingRow {
                level: "warn".to_string(),
                code: "POST_TARGET_AMBIGUOUS".to_string(),
                subject: watch_subject(post),
                detail: format!(
                    "pair_pre_name \"{target}\" matches {} fresh non-bypassed PRE rows: {}",
                    eligible_matches.len(),
                    watch_subjects(&eligible_matches)
                ),
            });
            continue;
        }

        let any_name_matches: Vec<&WatchRow> = pre_rows
            .iter()
            .copied()
            .filter(|pre| normalized_name(&pre.name) == Some(target))
            .collect();
        let detail = if any_name_matches.is_empty() {
            format!("pair_pre_name \"{target}\" has no PRE row in the watch root")
        } else {
            format!(
                "pair_pre_name \"{target}\" exists only as stale or bypassed PRE: {}",
                watch_subjects(&any_name_matches)
            )
        };
        rows.push(FindingRow {
            level: "warn".to_string(),
            code: "POST_TARGET_UNAVAILABLE".to_string(),
            subject: watch_subject(post),
            detail,
        });
    }
}

fn add_pending_signal_findings(rows: &mut Vec<FindingRow>, snapshot: &Snapshot) {
    let timeout_secs = kirin_measure::ACK_TIMEOUT_SECONDS as u64;
    for signal in &snapshot.signal_rows {
        if signal.status != "pending" {
            continue;
        }
        if signal.age_secs.is_some_and(|age| age > timeout_secs) {
            rows.push(FindingRow {
                level: "warn".to_string(),
                code: "PENDING_SIGNAL_STALE".to_string(),
                subject: format!("{}:{}/{}", signal.kind, signal.project, signal.file),
                detail: format!(
                    "pending signal age {} exceeds POST ack timeout {}s; PRE likely did not acknowledge",
                    signal.age_s, timeout_secs
                ),
            });
        }
    }
}

pub(super) fn is_pair_candidate(row: &WatchRow) -> bool {
    row.role == "PRE"
        && row.signal_state != "bypassed"
        && row
            .age_secs
            .is_none_or(|age| age <= kirin_measure::DISCOVERY_STALE_SECS)
}

fn normalized_name(value: &str) -> Option<&str> {
    let trimmed = value.trim();
    if trimmed.is_empty() || trimmed == "-" {
        None
    } else {
        Some(trimmed)
    }
}

fn watch_subject(row: &WatchRow) -> String {
    format!("{}:{}/{}", row.role, row.project, row.instance)
}

fn watch_subjects(rows: &[&WatchRow]) -> String {
    rows.iter()
        .map(|row| watch_subject(row))
        .collect::<Vec<_>>()
        .join(", ")
}

fn level_rank(level: &str) -> u8 {
    match level {
        "error" => 0,
        "warn" => 1,
        "info" => 2,
        _ => 3,
    }
}
