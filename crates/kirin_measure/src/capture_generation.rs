//! Capture-generation identity shared by one Keep / All Keep operation.
//!
//! A generation is the producer-owned transaction key.  Kirin OS must never
//! reconstruct a Drop set from names, wall-clock windows, or all open sessions.
//! Every project shelf participating in one All Keep receives the same immutable
//! identity before any Record signal is armed.

use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use uuid::Uuid;

pub const CAPTURE_GENERATION_SUBDIR: &str = "capture_generation";
pub const CAPTURE_GENERATION_CURRENT: &str = "current.json";
pub const CAPTURE_GENERATION_SCHEMA: &str = "capture_generation.v1";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
pub struct CaptureGenerationMember {
    pub project_hash: String,
    pub post_instance_id: String,
    #[serde(default)]
    pub pre_instance_id: String,
    /// The exact Record session path for this member. Assigned before the roster is published;
    /// Kirin OS follows it directly instead of scanning historical claim files.
    #[serde(default)]
    pub record_session_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CaptureGeneration {
    pub schema_version: String,
    pub capture_generation_id: String,
    pub originator_post_instance_id: String,
    pub daw_session_id: String,
    pub host_process_id: u32,
    pub started_at_ms: i64,
    /// Immutable All Keep roster. Drop is publishable only when every member
    /// produced exactly one closed pair session in this generation.
    pub members: Vec<CaptureGenerationMember>,
}

impl CaptureGeneration {
    pub fn new_single(
        project_hash: String,
        originator_post_instance_id: String,
        pre_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
    ) -> Self {
        Self::new_for_members(
            originator_post_instance_id.clone(),
            daw_session_id,
            host_process_id,
            vec![CaptureGenerationMember {
                project_hash,
                post_instance_id: originator_post_instance_id,
                pre_instance_id,
                record_session_id: String::new(),
            }],
        )
    }

    pub fn new_for_members(
        originator_post_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
        mut members: Vec<CaptureGenerationMember>,
    ) -> Self {
        members.sort();
        members.dedup();
        for member in &mut members {
            if member.record_session_id.trim().is_empty() {
                member.record_session_id = Uuid::new_v4().to_string();
            }
        }
        Self {
            schema_version: CAPTURE_GENERATION_SCHEMA.to_string(),
            capture_generation_id: Uuid::new_v4().to_string(),
            originator_post_instance_id,
            daw_session_id,
            host_process_id,
            started_at_ms: chrono::Utc::now().timestamp_millis(),
            members,
        }
    }

    pub fn is_valid(&self) -> bool {
        let unique_pre_members = self
            .members
            .iter()
            .filter(|member| !member.pre_instance_id.trim().is_empty())
            .map(|member| (&member.project_hash, &member.pre_instance_id))
            .collect::<std::collections::BTreeSet<_>>();
        self.schema_version == CAPTURE_GENERATION_SCHEMA
            && Uuid::parse_str(self.capture_generation_id.trim()).is_ok()
            && !self.originator_post_instance_id.trim().is_empty()
            && self.started_at_ms > 0
            && !self.members.is_empty()
            && self.members.iter().all(|member| {
                !member.project_hash.trim().is_empty()
                    && !member.post_instance_id.trim().is_empty()
                    && Uuid::parse_str(member.record_session_id.trim()).is_ok()
            })
            && self.members.windows(2).all(|pair| {
                pair[0].project_hash != pair[1].project_hash
                    || pair[0].post_instance_id != pair[1].post_instance_id
            })
            && unique_pre_members.len()
                == self
                    .members
                    .iter()
                    .filter(|member| !member.pre_instance_id.trim().is_empty())
                    .count()
            && self
                .members
                .iter()
                .any(|member| member.post_instance_id == self.originator_post_instance_id)
    }

    pub fn member(
        &self,
        project_hash: &str,
        post_instance_id: &str,
    ) -> Option<&CaptureGenerationMember> {
        self.members.iter().find(|member| {
            member.project_hash == project_hash && member.post_instance_id == post_instance_id
        })
    }
}

pub fn generation_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    let project_hash =
        crate::path_identity::guard_path_component(project_hash, "capture_generation.project_hash");
    base_dir
        .join(&*project_hash)
        .join(CAPTURE_GENERATION_SUBDIR)
}

pub fn current_generation_path(base_dir: &Path, project_hash: &str) -> PathBuf {
    generation_dir(base_dir, project_hash).join(CAPTURE_GENERATION_CURRENT)
}

/// Installation-wide pointer to the one generation currently owned by Keep/All Keep.
/// Kirin OS reads this exact file on Drop, then follows the immutable member roster. It
/// never needs to discover projects by walking `/tmp/kirin` or plugin-data history.
pub fn active_generation_path(base_dir: &Path) -> PathBuf {
    base_dir
        .join(CAPTURE_GENERATION_SUBDIR)
        .join(CAPTURE_GENERATION_CURRENT)
}

pub fn publish_current_generation(
    base_dir: &Path,
    project_hash: &str,
    generation: &CaptureGeneration,
) -> Result<(), CaptureGenerationError> {
    if !generation.is_valid() {
        return Err(CaptureGenerationError::Invalid);
    }
    let json = serde_json::to_vec(generation)?;
    crate::atomic_file::write_bytes_atomic(
        &current_generation_path(base_dir, project_hash),
        &json,
    )?;
    Ok(())
}

/// Publish the complete immutable roster before arming any member. Project pointers are
/// written first; the installation-wide pointer is the final commit barrier. A partial IO
/// failure therefore cannot promote an incomplete roster to Kirin OS.
pub fn publish_generation_roster(
    base_dir: &Path,
    generation: &CaptureGeneration,
) -> Result<(), CaptureGenerationError> {
    if !generation.is_valid() {
        return Err(CaptureGenerationError::Invalid);
    }
    let project_hashes = generation
        .members
        .iter()
        .map(|member| member.project_hash.as_str())
        .collect::<std::collections::BTreeSet<_>>();
    for project_hash in project_hashes {
        publish_current_generation(base_dir, project_hash, generation)?;
    }
    let json = serde_json::to_vec(generation)?;
    crate::atomic_file::write_bytes_atomic(&active_generation_path(base_dir), &json)?;
    Ok(())
}

pub fn read_current_generation(
    base_dir: &Path,
    project_hash: &str,
) -> Result<Option<CaptureGeneration>, CaptureGenerationError> {
    let bytes = match fs::read(current_generation_path(base_dir, project_hash)) {
        Ok(bytes) => bytes,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error.into()),
    };
    let generation: CaptureGeneration = serde_json::from_slice(&bytes)?;
    if !generation.is_valid() {
        return Err(CaptureGenerationError::Invalid);
    }
    Ok(Some(generation))
}

pub fn read_active_generation(
    base_dir: &Path,
) -> Result<Option<CaptureGeneration>, CaptureGenerationError> {
    let bytes = match fs::read(active_generation_path(base_dir)) {
        Ok(bytes) => bytes,
        Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(None),
        Err(error) => return Err(error.into()),
    };
    let generation: CaptureGeneration = serde_json::from_slice(&bytes)?;
    if !generation.is_valid() {
        return Err(CaptureGenerationError::Invalid);
    }
    Ok(Some(generation))
}

#[derive(Debug)]
pub enum CaptureGenerationError {
    Io(io::Error),
    Serde(serde_json::Error),
    Invalid,
}

impl std::fmt::Display for CaptureGenerationError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(error) => write!(formatter, "IO error: {error}"),
            Self::Serde(error) => write!(formatter, "JSON error: {error}"),
            Self::Invalid => write!(formatter, "invalid capture generation"),
        }
    }
}

impl std::error::Error for CaptureGenerationError {}

impl From<io::Error> for CaptureGenerationError {
    fn from(error: io::Error) -> Self {
        Self::Io(error)
    }
}

impl From<serde_json::Error> for CaptureGenerationError {
    fn from(error: serde_json::Error) -> Self {
        Self::Serde(error)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn one_generation_is_published_identically_to_every_project() {
        let temp = tempfile::tempdir().unwrap();
        let generation = CaptureGeneration::new_for_members(
            "post-a".into(),
            "daw-a".into(),
            42,
            vec![
                CaptureGenerationMember {
                    project_hash: "project-a".into(),
                    post_instance_id: "post-a".into(),
                    pre_instance_id: "pre-a".into(),
                    record_session_id: String::new(),
                },
                CaptureGenerationMember {
                    project_hash: "project-b".into(),
                    post_instance_id: "post-b".into(),
                    pre_instance_id: "pre-b".into(),
                    record_session_id: String::new(),
                },
            ],
        );

        publish_generation_roster(temp.path(), &generation).unwrap();

        assert_eq!(
            read_current_generation(temp.path(), "project-a")
                .unwrap()
                .unwrap(),
            generation
        );
        assert_eq!(
            read_current_generation(temp.path(), "project-b")
                .unwrap()
                .unwrap(),
            generation
        );
        assert_eq!(
            read_active_generation(temp.path()).unwrap().unwrap(),
            generation
        );
    }

    #[test]
    fn generation_roster_is_sorted_and_member_addressable() {
        let generation = CaptureGeneration::new_for_members(
            "post-b".into(),
            "daw-a".into(),
            42,
            vec![
                CaptureGenerationMember {
                    project_hash: "project-b".into(),
                    post_instance_id: "post-b".into(),
                    pre_instance_id: "pre-b".into(),
                    record_session_id: String::new(),
                },
                CaptureGenerationMember {
                    project_hash: "project-a".into(),
                    post_instance_id: "post-a".into(),
                    pre_instance_id: "pre-a".into(),
                    record_session_id: String::new(),
                },
            ],
        );
        assert!(generation.is_valid());
        assert!(generation
            .members
            .iter()
            .all(|member| Uuid::parse_str(&member.record_session_id).is_ok()));
        assert_eq!(generation.members[0].post_instance_id, "post-a");
        assert_eq!(
            generation
                .member("project-b", "post-b")
                .map(|member| member.pre_instance_id.as_str()),
            Some("pre-b")
        );
    }

    #[test]
    fn generation_rejects_two_posts_targeting_the_same_pre() {
        let generation = CaptureGeneration::new_for_members(
            "post-a".into(),
            "daw-a".into(),
            42,
            vec![
                CaptureGenerationMember {
                    project_hash: "project-a".into(),
                    post_instance_id: "post-a".into(),
                    pre_instance_id: "pre-shared".into(),
                    record_session_id: String::new(),
                },
                CaptureGenerationMember {
                    project_hash: "project-a".into(),
                    post_instance_id: "post-b".into(),
                    pre_instance_id: "pre-shared".into(),
                    record_session_id: String::new(),
                },
            ],
        );

        assert!(
            !generation.is_valid(),
            "one PRE inbox cannot belong to two POST members in one generation"
        );
    }

    #[test]
    fn invalid_pointer_is_not_promoted_to_a_generation() {
        let temp = tempfile::tempdir().unwrap();
        let path = current_generation_path(temp.path(), "project-a");
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, br#"{"schema_version":"capture_generation.v1"}"#).unwrap();

        assert!(matches!(
            read_current_generation(temp.path(), "project-a"),
            Err(CaptureGenerationError::Serde(_))
        ));
    }
}
