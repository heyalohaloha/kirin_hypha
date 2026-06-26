use serde::Serialize;
use std::path::PathBuf;

#[derive(Debug, Default, Serialize)]
pub(super) struct Snapshot {
    pub kirin_root: PathBuf,
    pub plugin_data_dir: Option<PathBuf>,
    pub summary: Summary,
    pub watch_rows: Vec<WatchRow>,
    pub signal_rows: Vec<SignalRow>,
    pub record_rows: Vec<RecordRow>,
    pub finding_rows: Vec<FindingRow>,
    pub warnings: Vec<String>,
}

#[derive(Debug, Default, Clone, PartialEq, Eq, Serialize)]
pub(super) struct Summary {
    pub live_pre: usize,
    pub live_post: usize,
    pub eligible_pre: usize,
    pub paired_post: usize,
    pub pending_signals: usize,
    pub active_records: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub(super) struct WatchRow {
    pub role: String,
    pub project: String,
    pub instance: String,
    pub name: String,
    pub signal_state: String,
    pub peer_state: String,
    pub pair_pre_name: String,
    pub age_secs: Option<u64>,
    pub age_s: String,
    pub path: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub(super) struct FindingRow {
    pub level: String,
    pub code: String,
    pub subject: String,
    pub detail: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub(super) struct SignalRow {
    pub kind: String,
    pub project: String,
    pub file: String,
    pub status: String,
    pub requested_by: String,
    pub target_pre: String,
    pub pair_name: String,
    pub daw_session: String,
    pub t: String,
    pub age_secs: Option<u64>,
    pub age_s: String,
    pub path: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub(super) struct RecordRow {
    pub project: String,
    pub instance: String,
    pub pre_files: usize,
    pub post_files: usize,
    pub active_files: usize,
    pub closed_files: usize,
    pub latest_status: String,
    pub latest_age_secs: Option<u64>,
    pub latest_path: String,
}
