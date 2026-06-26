use std::path::PathBuf;

#[derive(Debug, Default)]
pub(super) struct Snapshot {
    pub kirin_root: PathBuf,
    pub plugin_data_dir: Option<PathBuf>,
    pub watch_rows: Vec<WatchRow>,
    pub signal_rows: Vec<SignalRow>,
    pub record_rows: Vec<RecordRow>,
    pub warnings: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(super) struct WatchRow {
    pub role: String,
    pub project: String,
    pub instance: String,
    pub name: String,
    pub signal_state: String,
    pub peer_state: String,
    pub pair_pre_name: String,
    pub age_s: String,
    pub path: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq)]
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

#[derive(Debug, Clone, PartialEq, Eq)]
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
