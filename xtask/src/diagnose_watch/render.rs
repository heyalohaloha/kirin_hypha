use super::{analyze::is_pair_candidate, Snapshot};

pub(super) fn render_snapshot(snapshot: &Snapshot, max_rows: Option<usize>) -> String {
    let mut out = String::new();
    out.push_str("Kirin Hypha Watch Snapshot\n");
    out.push_str(&format!("kirin_root: {}\n", snapshot.kirin_root.display()));
    out.push_str(&format!(
        "plugin_data: {}\n\n",
        snapshot
            .plugin_data_dir
            .as_ref()
            .map(|p| p.display().to_string())
            .unwrap_or_else(|| "-".to_string())
    ));

    out.push_str("Summary\n");
    out.push_str(&render_summary(snapshot));

    out.push_str("Findings\n");
    out.push_str(&render_table(
        &["level", "code", "subject", "detail"],
        snapshot
            .finding_rows
            .iter()
            .map(|r| {
                vec![
                    r.level.clone(),
                    r.code.clone(),
                    r.subject.clone(),
                    r.detail.clone(),
                ]
            })
            .collect(),
        max_rows,
    ));

    out.push('\n');
    out.push_str("Watch PRE/POST\n");
    out.push_str(&render_table(
        &[
            "role",
            "project",
            "instance",
            "name",
            "state",
            "pre_state",
            "pair_pre",
            "age",
            "path",
        ],
        snapshot
            .watch_rows
            .iter()
            .map(|r| {
                vec![
                    r.role.clone(),
                    r.project.clone(),
                    r.instance.clone(),
                    r.name.clone(),
                    r.signal_state.clone(),
                    r.peer_state.clone(),
                    r.pair_pre_name.clone(),
                    r.age_s.clone(),
                    r.path.display().to_string(),
                ]
            })
            .collect(),
        max_rows,
    ));

    out.push_str("\nSignals\n");
    out.push_str(&render_table(
        &[
            "kind",
            "project",
            "file",
            "status",
            "requested_by",
            "target_pre",
            "pair_name",
            "daw_session",
            "t",
            "age",
            "path",
        ],
        snapshot
            .signal_rows
            .iter()
            .map(|r| {
                vec![
                    r.kind.clone(),
                    r.project.clone(),
                    r.file.clone(),
                    r.status.clone(),
                    r.requested_by.clone(),
                    r.target_pre.clone(),
                    r.pair_name.clone(),
                    r.daw_session.clone(),
                    r.t.clone(),
                    r.age_s.clone(),
                    r.path.display().to_string(),
                ]
            })
            .collect(),
        max_rows,
    ));

    out.push_str("\nplugin_data records\n");
    out.push_str(&render_table(
        &[
            "project",
            "instance",
            "pre",
            "post",
            "active",
            "closed",
            "latest_status",
            "latest_path",
        ],
        snapshot
            .record_rows
            .iter()
            .map(|r| {
                vec![
                    r.project.clone(),
                    r.instance.clone(),
                    r.pre_files.to_string(),
                    r.post_files.to_string(),
                    r.active_files.to_string(),
                    r.closed_files.to_string(),
                    r.latest_status.clone(),
                    r.latest_path.clone(),
                ]
            })
            .collect(),
        max_rows,
    ));

    if !snapshot.warnings.is_empty() {
        out.push_str("\nWarnings\n");
        for warning in &snapshot.warnings {
            out.push_str("- ");
            out.push_str(warning);
            out.push('\n');
        }
    }
    out
}

fn render_summary(snapshot: &Snapshot) -> String {
    let pre_live = snapshot
        .watch_rows
        .iter()
        .filter(|r| r.role == "PRE")
        .count();
    let post_live = snapshot
        .watch_rows
        .iter()
        .filter(|r| r.role == "POST")
        .count();
    let paired_post = snapshot
        .watch_rows
        .iter()
        .filter(|r| r.role == "POST" && r.pair_pre_name != "-")
        .count();
    let eligible_pre = snapshot
        .watch_rows
        .iter()
        .filter(|r| is_pair_candidate(r))
        .count();
    let pending_signals = snapshot
        .signal_rows
        .iter()
        .filter(|r| r.status == "pending")
        .count();
    let active_records: usize = snapshot.record_rows.iter().map(|r| r.active_files).sum();

    let mut out = String::new();
    out.push_str(&format!("  live_pre:        {pre_live}\n"));
    out.push_str(&format!("  live_post:       {post_live}\n"));
    out.push_str(&format!("  eligible_pre:    {eligible_pre}\n"));
    out.push_str(&format!("  paired_post:     {paired_post}\n"));
    out.push_str(&format!("  pending_signals: {pending_signals}\n"));
    out.push_str(&format!("  active_records:  {active_records}\n\n"));
    out
}

pub(super) fn render_table(
    headers: &[&str],
    rows: Vec<Vec<String>>,
    max_rows: Option<usize>,
) -> String {
    if rows.is_empty() {
        return "  (none)\n".to_string();
    }

    let mut widths: Vec<usize> = headers.iter().map(|h| h.len()).collect();
    for row in &rows {
        for (i, cell) in row.iter().enumerate() {
            widths[i] = widths[i].max(cell.len());
        }
    }

    let mut out = String::new();
    push_table_row(&mut out, headers.iter().map(|s| s.to_string()), &widths);
    push_table_rule(&mut out, &widths);
    let visible_len = max_rows.map_or(rows.len(), |limit| rows.len().min(limit));
    let omitted_len = rows.len().saturating_sub(visible_len);
    for row in rows.into_iter().take(visible_len) {
        push_table_row(&mut out, row, &widths);
    }
    if omitted_len > 0 {
        out.push_str(&format!(
            "  ... {omitted_len} more rows hidden; pass --max-rows 0 to show all\n"
        ));
    }
    out
}

fn push_table_row(out: &mut String, row: impl IntoIterator<Item = String>, widths: &[usize]) {
    out.push_str("  ");
    for (i, cell) in row.into_iter().enumerate() {
        if i > 0 {
            out.push_str("  ");
        }
        out.push_str(&format!("{cell:<width$}", width = widths[i]));
    }
    out.push('\n');
}

fn push_table_rule(out: &mut String, widths: &[usize]) {
    out.push_str("  ");
    for (i, width) in widths.iter().enumerate() {
        if i > 0 {
            out.push_str("  ");
        }
        out.push_str(&"-".repeat(*width));
    }
    out.push('\n');
}
