//! Expected WAV metadata bridge for PairRecordSession.
//!
//! Kirin OS owns the dropped/exported WAV header truth. Hypha reads that truth
//! from `plugin_data/{project_hash}/record_expected/current.json` before arming
//! Record and copies it into `record_signal` and final plugin_data artifacts.

use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub const EXPECTED_SUBDIR: &str = "record_expected";
pub const EXPECTED_FILENAME: &str = "current.json";
pub const EXPECTED_METADATA_MAX_AGE_MS: i64 = 10 * 60 * 1_000;
const EXPECTED_METADATA_FUTURE_SKEW_MS: i64 = 60 * 1_000;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ExpectedWavMetadata {
    pub expected_duration_samples: u64,
    pub expected_sample_rate: u32,
    pub wav_path: String,
    #[serde(default)]
    pub bounce_id: String,
    #[serde(default)]
    pub created_at_ms: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub wav_file_size: Option<u64>,
    #[serde(default)]
    pub wav_mtime_ms: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub wav_hash: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub consumed_at_ms: Option<i64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub consumed_by_session_id: Option<String>,
}

impl ExpectedWavMetadata {
    pub fn is_usable(&self) -> bool {
        self.is_complete() && self.consumed_at_ms.is_none() && self.consumed_by_session_id.is_none()
    }

    pub fn is_complete(&self) -> bool {
        self.expected_duration_samples > 0
            && self.expected_sample_rate > 0
            && !self.wav_path.trim().is_empty()
            && !self.bounce_id.trim().is_empty()
            && self.created_at_ms > 0
            && self.wav_file_size.is_some_and(|size| size > 0)
            && self.wav_mtime_ms > 0
            && self
                .wav_hash
                .as_ref()
                .is_some_and(|hash| !hash.trim().is_empty())
    }

    pub fn is_fresh_for_arm(&self, now_ms: i64) -> bool {
        if !self.is_usable() {
            return false;
        }
        if self.created_at_ms > now_ms.saturating_add(EXPECTED_METADATA_FUTURE_SKEW_MS) {
            return false;
        }
        now_ms.saturating_sub(self.created_at_ms) <= EXPECTED_METADATA_MAX_AGE_MS
    }
}

#[derive(Debug)]
pub enum ExpectedMetadataError {
    Io(io::Error),
    Serde(serde_json::Error),
    Invalid,
    Stale,
    Consumed,
}

impl std::fmt::Display for ExpectedMetadataError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "IO error: {e}"),
            Self::Serde(e) => write!(f, "JSON error: {e}"),
            Self::Invalid => write!(f, "expected WAV metadata is incomplete"),
            Self::Stale => write!(f, "expected WAV metadata is stale"),
            Self::Consumed => write!(f, "expected WAV metadata has already been consumed"),
        }
    }
}

impl std::error::Error for ExpectedMetadataError {}

impl From<io::Error> for ExpectedMetadataError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<serde_json::Error> for ExpectedMetadataError {
    fn from(e: serde_json::Error) -> Self {
        Self::Serde(e)
    }
}

pub fn expected_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    let ph = crate::path_identity::guard_path_component(
        project_hash,
        "record_expected.expected_dir.project_hash",
    );
    base_dir.join(&*ph).join(EXPECTED_SUBDIR)
}

pub fn expected_path(base_dir: &Path, project_hash: &str) -> PathBuf {
    expected_dir(base_dir, project_hash).join(EXPECTED_FILENAME)
}

pub fn read_expected_metadata(
    base_dir: &Path,
    project_hash: &str,
) -> Result<ExpectedWavMetadata, ExpectedMetadataError> {
    let path = expected_path(base_dir, project_hash);
    let bytes = fs::read(path)?;
    let metadata: ExpectedWavMetadata = serde_json::from_slice(&bytes)?;
    if !metadata.is_complete() {
        Err(ExpectedMetadataError::Invalid)
    } else if metadata.consumed_at_ms.is_some() || metadata.consumed_by_session_id.is_some() {
        Err(ExpectedMetadataError::Consumed)
    } else if !metadata.is_fresh_for_arm(now_epoch_ms()) {
        Err(ExpectedMetadataError::Stale)
    } else {
        Ok(metadata)
    }
}

pub fn write_expected_metadata(
    base_dir: &Path,
    project_hash: &str,
    metadata: &ExpectedWavMetadata,
) -> Result<(), ExpectedMetadataError> {
    if !metadata.is_usable() {
        return Err(ExpectedMetadataError::Invalid);
    }
    let path = expected_path(base_dir, project_hash);
    let json = serde_json::to_vec(metadata)?;
    crate::atomic_file::write_bytes_atomic(&path, &json)?;
    Ok(())
}

pub fn mark_expected_metadata_consumed(
    base_dir: &Path,
    project_hash: &str,
    bounce_id: &str,
    session_id: &str,
) -> Result<bool, ExpectedMetadataError> {
    if bounce_id.trim().is_empty() || session_id.trim().is_empty() {
        return Ok(false);
    }
    let path = expected_path(base_dir, project_hash);
    let bytes = fs::read(&path)?;
    let mut metadata: ExpectedWavMetadata = serde_json::from_slice(&bytes)?;
    if metadata.bounce_id != bounce_id {
        return Ok(false);
    }
    metadata.consumed_at_ms = Some(now_epoch_ms());
    metadata.consumed_by_session_id = Some(session_id.to_string());
    let json = serde_json::to_vec(&metadata)?;
    crate::atomic_file::write_bytes_atomic(&path, &json)?;
    Ok(true)
}

fn now_epoch_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!(
            "kirin_record_expected_test_{}_{}",
            std::process::id(),
            n
        ));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn expected_metadata_roundtrips_under_project_dir() {
        let base = isolated_dir();
        let metadata = ExpectedWavMetadata {
            expected_duration_samples: 1_440_000,
            expected_sample_rate: 96_000,
            wav_path: "/Volumes/ALOHA/Peach19(10).wav".to_string(),
            bounce_id: "bounce-1".to_string(),
            created_at_ms: now_epoch_ms(),
            wav_file_size: Some(11_520_044),
            wav_mtime_ms: now_epoch_ms(),
            wav_hash: Some("hash-1".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };

        write_expected_metadata(&base, "ph", &metadata).unwrap();

        assert_eq!(read_expected_metadata(&base, "ph").unwrap(), metadata);
        assert!(expected_path(&base, "ph").exists());
    }

    #[test]
    fn empty_wav_path_is_invalid() {
        let base = isolated_dir();
        let metadata = ExpectedWavMetadata {
            expected_duration_samples: 1,
            expected_sample_rate: 48_000,
            wav_path: String::new(),
            bounce_id: "bounce-1".to_string(),
            created_at_ms: now_epoch_ms(),
            wav_file_size: Some(1),
            wav_mtime_ms: now_epoch_ms(),
            wav_hash: Some("hash-1".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };

        assert!(matches!(
            write_expected_metadata(&base, "ph", &metadata),
            Err(ExpectedMetadataError::Invalid)
        ));
    }

    #[test]
    fn consumed_metadata_is_not_armable_again() {
        let base = isolated_dir();
        let metadata = ExpectedWavMetadata {
            expected_duration_samples: 48_000,
            expected_sample_rate: 48_000,
            wav_path: "/tmp/kirin.wav".to_string(),
            bounce_id: "bounce-consume".to_string(),
            created_at_ms: now_epoch_ms(),
            wav_file_size: Some(1_000),
            wav_mtime_ms: now_epoch_ms(),
            wav_hash: Some("hash-consume".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };
        write_expected_metadata(&base, "ph", &metadata).unwrap();
        assert!(
            mark_expected_metadata_consumed(&base, "ph", "bounce-consume", "session-1").unwrap()
        );
        assert!(matches!(
            read_expected_metadata(&base, "ph"),
            Err(ExpectedMetadataError::Consumed)
        ));
    }
}
