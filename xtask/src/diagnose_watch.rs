use anyhow::{bail, Context, Result};
use kirin_measure::PlatformPaths;
use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

mod render;
#[cfg(test)]
mod tests;

use render::render_snapshot;

const RECORD_SIGNAL_DIR: &str = "record_signal";
const ALL_KEEP_SIGNAL_DIR: &str = "all_keep_signal";
const ALL_STOP_SIGNAL_DIR: &str = "all_stop_signal";
const DEFAULT_HISTORY_AGE_SECS: u64 = 24 * 60 * 60;

#[derive(Debug, Default)]
struct Snapshot {
    kirin_root: PathBuf,
    plugin_data_dir: Option<PathBuf>,
    watch_rows: Vec<WatchRow>,
    signal_rows: Vec<SignalRow>,
    record_rows: Vec<RecordRow>,
    warnings: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct WatchRow {
    role: String,
    project: String,
    instance: String,
    name: String,
    signal_state: String,
    peer_state: String,
    pair_pre_name: String,
    age_s: String,
    path: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct SignalRow {
    kind: String,
    project: String,
    file: String,
    status: String,
    requested_by: String,
    target_pre: String,
    pair_name: String,
    daw_session: String,
    t: String,
    age_secs: Option<u64>,
    age_s: String,
    path: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct RecordRow {
    project: String,
    instance: String,
    pre_files: usize,
    post_files: usize,
    active_files: usize,
    closed_files: usize,
    latest_status: String,
    latest_age_secs: Option<u64>,
    latest_path: String,
}

pub fn run(args: Vec<String>) -> Result<()> {
    let mut kirin_root: Option<PathBuf> = None;
    let mut plugin_data_dir: Option<PathBuf> = None;
    let mut all_history = false;
    let mut max_age_secs = DEFAULT_HISTORY_AGE_SECS;

    let mut iter = args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--kirin-root" => {
                kirin_root = Some(PathBuf::from(
                    iter.next().context("--kirin-root requires a value")?,
                ));
            }
            "--plugin-data-dir" => {
                plugin_data_dir = Some(PathBuf::from(
                    iter.next().context("--plugin-data-dir requires a value")?,
                ));
            }
            "--all-history" => all_history = true,
            "--max-age-secs" => {
                max_age_secs = iter
                    .next()
                    .context("--max-age-secs requires a value")?
                    .parse()
                    .context("--max-age-secs value must be u64")?;
            }
            "-h" | "--help" => {
                print_help();
                return Ok(());
            }
            other => bail!("unknown argument: {other}"),
        }
    }

    let platform = PlatformPaths::default_current().ok();
    let kirin_root = kirin_root
        .or_else(|| platform.as_ref().map(|p| p.kirin_tmp_root.clone()))
        .unwrap_or_else(PlatformPaths::current_kirin_tmp_root);
    let plugin_data_dir =
        plugin_data_dir.or_else(|| platform.as_ref().map(|p| p.storage.plugin_data_dir()));

    let mut snapshot = collect_snapshot(kirin_root, plugin_data_dir);
    if !all_history {
        apply_history_filter(&mut snapshot, max_age_secs);
    }
    print!("{}", render_snapshot(&snapshot));
    Ok(())
}

fn print_help() {
    eprintln!(
        "Usage: cargo run -p xtask -- diagnose-watch [--kirin-root PATH] [--plugin-data-dir PATH] [--all-history] [--max-age-secs N]\n\n\
         Prints a read-only PRE/POST Watch snapshot from the platform temp root and plugin_data.\n\
         Default history filter keeps pending signals, active records, and rows newer than 24h.\n\
         Use --kirin-root and --plugin-data-dir for isolated test captures."
    );
}

fn collect_snapshot(kirin_root: PathBuf, plugin_data_dir: Option<PathBuf>) -> Snapshot {
    let mut snapshot = Snapshot {
        kirin_root,
        plugin_data_dir,
        ..Snapshot::default()
    };

    scan_watch_root(&mut snapshot);
    scan_plugin_data(&mut snapshot);
    snapshot.watch_rows.sort_by(|a, b| {
        a.project
            .cmp(&b.project)
            .then(a.role.cmp(&b.role))
            .then(a.instance.cmp(&b.instance))
    });
    snapshot.signal_rows.sort_by(|a, b| {
        a.project
            .cmp(&b.project)
            .then(a.kind.cmp(&b.kind))
            .then(a.file.cmp(&b.file))
    });
    snapshot
        .record_rows
        .sort_by(|a, b| a.project.cmp(&b.project).then(a.instance.cmp(&b.instance)));
    snapshot
}

fn apply_history_filter(snapshot: &mut Snapshot, max_age_secs: u64) {
    snapshot
        .signal_rows
        .retain(|r| r.status == "pending" || r.age_secs.is_some_and(|age| age <= max_age_secs));
    snapshot
        .record_rows
        .retain(|r| r.active_files > 0 || r.latest_age_secs.is_some_and(|age| age <= max_age_secs));
    snapshot.warnings.push(format!(
        "history filter applied: signals/records newer than {max_age_secs}s plus pending/active; pass --all-history to include old plugin_data"
    ));
}

fn scan_watch_root(snapshot: &mut Snapshot) {
    let root = snapshot.kirin_root.clone();
    if !root.exists() {
        snapshot
            .warnings
            .push(format!("watch root missing: {}", root.display()));
        return;
    }

    let Ok(projects) = fs::read_dir(&root) else {
        snapshot
            .warnings
            .push(format!("watch root unreadable: {}", root.display()));
        return;
    };

    for project_entry in projects.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        let project = file_name(&project_dir);
        let Ok(instances) = fs::read_dir(&project_dir) else {
            snapshot
                .warnings
                .push(format!("project unreadable: {}", project_dir.display()));
            continue;
        };
        for instance_entry in instances.flatten() {
            let instance_dir = instance_entry.path();
            if !instance_dir.is_dir() {
                continue;
            }
            scan_watch_file(snapshot, &project, &instance_dir, "PRE", "pre.json");
            scan_watch_file(snapshot, &project, &instance_dir, "POST", "post.json");
        }
    }
}

fn scan_watch_file(
    snapshot: &mut Snapshot,
    project: &str,
    instance_dir: &Path,
    role: &str,
    filename: &str,
) {
    let path = instance_dir.join(filename);
    if !path.is_file() {
        return;
    }
    let Some(json) = read_json(&path, &mut snapshot.warnings) else {
        return;
    };
    let fallback_instance = file_name(instance_dir);
    snapshot.watch_rows.push(WatchRow {
        role: role.to_string(),
        project: project.to_string(),
        instance: field(&json, "instance_id").unwrap_or(fallback_instance),
        name: field(&json, "name").unwrap_or_else(|| "-".to_string()),
        signal_state: field(&json, "signal_state").unwrap_or_else(|| "-".to_string()),
        peer_state: field(&json, "pre_signal_state").unwrap_or_else(|| "-".to_string()),
        pair_pre_name: field(&json, "pair_pre_name").unwrap_or_else(|| "-".to_string()),
        age_s: age_s(&path),
        path,
    });
}

fn scan_plugin_data(snapshot: &mut Snapshot) {
    let Some(root) = snapshot.plugin_data_dir.clone() else {
        snapshot
            .warnings
            .push("plugin_data unavailable: platform storage path could not be resolved".into());
        return;
    };
    if !root.exists() {
        snapshot
            .warnings
            .push(format!("plugin_data missing: {}", root.display()));
        return;
    }

    let Ok(projects) = fs::read_dir(&root) else {
        snapshot
            .warnings
            .push(format!("plugin_data unreadable: {}", root.display()));
        return;
    };

    for project_entry in projects.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        let project = file_name(&project_dir);
        scan_signal_dir(snapshot, &project, &project_dir, RECORD_SIGNAL_DIR);
        scan_signal_dir(snapshot, &project, &project_dir, ALL_KEEP_SIGNAL_DIR);
        scan_signal_dir(snapshot, &project, &project_dir, ALL_STOP_SIGNAL_DIR);
        scan_record_dirs(snapshot, &project, &project_dir);
    }
}

fn scan_signal_dir(snapshot: &mut Snapshot, project: &str, project_dir: &Path, kind: &str) {
    let dir = project_dir.join(kind);
    if !dir.is_dir() {
        return;
    }
    let Ok(entries) = fs::read_dir(&dir) else {
        snapshot
            .warnings
            .push(format!("signal dir unreadable: {}", dir.display()));
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !is_json_file(&path) {
            continue;
        }
        let Some(json) = read_json(&path, &mut snapshot.warnings) else {
            continue;
        };
        snapshot.signal_rows.push(SignalRow {
            kind: kind.to_string(),
            project: project.to_string(),
            file: file_name(&path),
            status: field(&json, "status").unwrap_or_else(|| "-".to_string()),
            requested_by: field(&json, "requested_by")
                .or_else(|| field(&json, "originator_post_instance_id"))
                .unwrap_or_else(|| "-".to_string()),
            target_pre: field(&json, "target_pre_instance_id").unwrap_or_else(|| "-".to_string()),
            pair_name: field(&json, "paired_pre_name").unwrap_or_else(|| "-".to_string()),
            daw_session: field(&json, "daw_session_id").unwrap_or_else(|| "-".to_string()),
            t: field(&json, "t")
                .or_else(|| field(&json, "started_at"))
                .unwrap_or_else(|| "-".to_string()),
            age_secs: age_secs(&path),
            age_s: age_s(&path),
            path,
        });
    }
}

fn scan_record_dirs(snapshot: &mut Snapshot, project: &str, project_dir: &Path) {
    let Ok(entries) = fs::read_dir(project_dir) else {
        return;
    };
    for entry in entries.flatten() {
        let instance_dir = entry.path();
        if !instance_dir.is_dir() {
            continue;
        }
        let name = file_name(&instance_dir);
        if matches!(
            name.as_str(),
            RECORD_SIGNAL_DIR | ALL_KEEP_SIGNAL_DIR | ALL_STOP_SIGNAL_DIR
        ) {
            continue;
        }
        let pre = count_role_records(&instance_dir.join("pre"), &mut snapshot.warnings);
        let post = count_role_records(&instance_dir.join("post"), &mut snapshot.warnings);
        if pre.files == 0 && post.files == 0 {
            continue;
        }
        let latest = latest_record(pre.latest, post.latest);
        snapshot.record_rows.push(RecordRow {
            project: project.to_string(),
            instance: name,
            pre_files: pre.files,
            post_files: post.files,
            active_files: pre.active + post.active,
            closed_files: pre.closed + post.closed,
            latest_status: latest
                .as_ref()
                .map(|r| r.status.clone())
                .unwrap_or_else(|| "-".to_string()),
            latest_age_secs: latest.as_ref().and_then(|r| age_secs(&r.path)),
            latest_path: latest
                .as_ref()
                .map(|r| r.path.display().to_string())
                .unwrap_or_else(|| "-".to_string()),
        });
    }
}

#[derive(Debug, Default)]
struct RoleRecordStats {
    files: usize,
    active: usize,
    closed: usize,
    latest: Option<LatestRecord>,
}

#[derive(Debug, Clone)]
struct LatestRecord {
    mtime: SystemTime,
    status: String,
    path: PathBuf,
}

fn count_role_records(role_dir: &Path, warnings: &mut Vec<String>) -> RoleRecordStats {
    let mut stats = RoleRecordStats::default();
    let Ok(entries) = fs::read_dir(role_dir) else {
        return stats;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if !is_json_file(&path) {
            continue;
        }
        stats.files += 1;
        let status = read_json(&path, warnings)
            .and_then(|json| field(&json, "status"))
            .unwrap_or_else(|| "-".to_string());
        match status.as_str() {
            "active" => stats.active += 1,
            "closed" => stats.closed += 1,
            _ => {}
        }
        let mtime = fs::metadata(&path)
            .and_then(|m| m.modified())
            .unwrap_or(UNIX_EPOCH);
        let candidate = LatestRecord {
            mtime,
            status,
            path,
        };
        stats.latest = latest_record(stats.latest, Some(candidate));
    }
    stats
}

fn latest_record(a: Option<LatestRecord>, b: Option<LatestRecord>) -> Option<LatestRecord> {
    match (a, b) {
        (None, None) => None,
        (Some(x), None) | (None, Some(x)) => Some(x),
        (Some(x), Some(y)) if x.mtime >= y.mtime => Some(x),
        (Some(_), Some(y)) => Some(y),
    }
}

fn read_json(path: &Path, warnings: &mut Vec<String>) -> Option<Value> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(e) => {
            warnings.push(format!("read failed {}: {e}", path.display()));
            return None;
        }
    };
    match serde_json::from_slice(&bytes) {
        Ok(json) => Some(json),
        Err(e) => {
            warnings.push(format!("json parse failed {}: {e}", path.display()));
            None
        }
    }
}

fn field(json: &Value, key: &str) -> Option<String> {
    let value = json.get(key)?;
    if let Some(s) = value.as_str() {
        if s.is_empty() {
            None
        } else {
            Some(s.to_string())
        }
    } else if value.is_null() {
        None
    } else {
        Some(value.to_string())
    }
}

fn age_s(path: &Path) -> String {
    match age_secs(path) {
        Some(age) => format!("{age}s"),
        None => "-".to_string(),
    }
}

fn age_secs(path: &Path) -> Option<u64> {
    let Ok(meta) = fs::metadata(path) else {
        return None;
    };
    let Ok(modified) = meta.modified() else {
        return None;
    };
    match SystemTime::now().duration_since(modified) {
        Ok(age) => Some(age.as_secs()),
        Err(_) => Some(0),
    }
}

fn file_name(path: &Path) -> String {
    path.file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("-")
        .to_string()
}

fn is_json_file(path: &Path) -> bool {
    path.is_file() && path.extension().and_then(|e| e.to_str()) == Some("json")
}
