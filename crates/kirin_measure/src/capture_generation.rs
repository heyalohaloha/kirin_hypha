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
pub const CAPTURE_GENERATION_PREPARING: &str = "preparing.json";
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

/// Human-facing and semantic metadata for one immutable member.
///
/// `pair_key` is not a second identity: it is the consumer-facing name of the exact
/// `record_session_id`. Keeping the equality explicit prevents two independent IDs from drifting.
/// `channel_key` is the persisted PRE instance id, not a display-name classification. This makes
/// one exact channel unique inside a generation even when names change or collide. `channel_role`
/// is optional semantic metadata; an arbitrary display name is always valid without a role.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
pub struct CaptureGenerationMemberIdentity {
    pub pair_key: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub display_name: Option<String>,
    pub channel_key: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub channel_role: Option<String>,
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
    /// Additive v1 metadata. Empty is accepted only for captures produced before this contract.
    /// New captures contain exactly one identity for every member.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub member_identities: Vec<CaptureGenerationMemberIdentity>,
}

impl CaptureGeneration {
    pub fn new_single(
        project_hash: String,
        originator_post_instance_id: String,
        pre_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
    ) -> Self {
        Self::new_single_named(
            project_hash,
            originator_post_instance_id,
            pre_instance_id,
            daw_session_id,
            host_process_id,
            None,
        )
    }

    pub fn new_single_named(
        project_hash: String,
        originator_post_instance_id: String,
        pre_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
        display_name: Option<String>,
    ) -> Self {
        Self::new_for_named_members(
            originator_post_instance_id.clone(),
            daw_session_id,
            host_process_id,
            vec![(
                CaptureGenerationMember {
                    project_hash,
                    post_instance_id: originator_post_instance_id,
                    pre_instance_id,
                    record_session_id: String::new(),
                },
                display_name,
            )],
        )
    }

    pub fn new_for_members(
        originator_post_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
        members: Vec<CaptureGenerationMember>,
    ) -> Self {
        Self::new_for_named_members(
            originator_post_instance_id,
            daw_session_id,
            host_process_id,
            members.into_iter().map(|member| (member, None)).collect(),
        )
    }

    pub fn new_for_named_members(
        originator_post_instance_id: String,
        daw_session_id: String,
        host_process_id: u32,
        mut named_members: Vec<(CaptureGenerationMember, Option<String>)>,
    ) -> Self {
        named_members.sort_by(|a, b| a.0.cmp(&b.0));
        named_members.dedup_by(|a, b| a.0 == b.0);
        let mut members = Vec::with_capacity(named_members.len());
        let mut member_identities = Vec::with_capacity(named_members.len());
        for (mut member, display_name) in named_members {
            if member.record_session_id.trim().is_empty() {
                member.record_session_id = Uuid::new_v4().to_string();
            }
            let display_name = crate::channel_identity::display_name_snapshot(
                display_name.as_deref().unwrap_or_default(),
            );
            let channel_role = display_name
                .as_deref()
                .and_then(crate::channel_identity::canonical_channel_role)
                .map(str::to_string);
            member_identities.push(CaptureGenerationMemberIdentity {
                pair_key: member.record_session_id.clone(),
                display_name,
                channel_key: member.pre_instance_id.clone(),
                channel_role,
            });
            members.push(member);
        }
        Self {
            schema_version: CAPTURE_GENERATION_SCHEMA.to_string(),
            capture_generation_id: Uuid::new_v4().to_string(),
            originator_post_instance_id,
            daw_session_id,
            host_process_id,
            started_at_ms: chrono::Utc::now().timestamp_millis(),
            members,
            member_identities,
        }
    }

    pub fn is_valid(&self) -> bool {
        let unique_pre_members = self
            .members
            .iter()
            .filter(|member| !member.pre_instance_id.trim().is_empty())
            .map(|member| (&member.project_hash, &member.pre_instance_id))
            .collect::<std::collections::BTreeSet<_>>();
        let unique_identity_channels = self
            .member_identities
            .iter()
            .map(|identity| identity.channel_key.as_str())
            .collect::<std::collections::BTreeSet<_>>();
        let identities_valid =
            self.member_identities.is_empty()
                || (self.member_identities.len() == self.members.len()
                    && unique_identity_channels.len() == self.member_identities.len()
                    && self.member_identities.iter().zip(&self.members).all(
                        |(identity, member)| {
                            identity.pair_key == member.record_session_id
                                && identity.channel_key == member.pre_instance_id
                                && identity
                                    .display_name
                                    .as_deref()
                                    .is_none_or(|name| !name.trim().is_empty())
                                && identity.channel_role.as_deref()
                                    == identity
                                        .display_name
                                        .as_deref()
                                        .and_then(crate::channel_identity::canonical_channel_role)
                        },
                    ));
        self.schema_version == CAPTURE_GENERATION_SCHEMA
            && Uuid::parse_str(self.capture_generation_id.trim()).is_ok()
            && !self.originator_post_instance_id.trim().is_empty()
            && self.started_at_ms > 0
            && !self.members.is_empty()
            && self.members.iter().all(|member| {
                !member.project_hash.trim().is_empty()
                    && !member.post_instance_id.trim().is_empty()
                    && !member.pre_instance_id.trim().is_empty()
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
            && identities_valid
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

    pub fn member_identity(
        &self,
        record_session_id: &str,
    ) -> Option<&CaptureGenerationMemberIdentity> {
        self.member_identities
            .iter()
            .find(|identity| identity.pair_key == record_session_id)
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

/// Producer-only preparation pointer. PRE/POST workers may arm an exact member while this
/// pointer matches, but Kirin OS must never treat it as a Drop/TRACE commit barrier. The
/// installation-wide `current.json` remains the only consumer-authoritative generation.
pub fn preparing_generation_path(base_dir: &Path) -> PathBuf {
    base_dir
        .join(CAPTURE_GENERATION_SUBDIR)
        .join(CAPTURE_GENERATION_PREPARING)
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
    read_generation_file(&active_generation_path(base_dir))
}

pub fn read_preparing_generation(
    base_dir: &Path,
) -> Result<Option<CaptureGeneration>, CaptureGenerationError> {
    read_generation_file(&preparing_generation_path(base_dir))
}

/// Resolves one project member against the producer preparation/commit barrier. This is the only
/// authorization path used by PRE/POST workers: project-local roster data alone is never enough.
/// Consumers continue to use [`read_active_generation`] and therefore cannot observe an
/// incompletely armed generation.
pub fn read_producer_authorized_generation(
    base_dir: &Path,
    project_hash: &str,
    capture_generation_id: &str,
    started_at_ms: i64,
) -> Result<Option<CaptureGeneration>, CaptureGenerationError> {
    let Some(project_generation) = read_current_generation(base_dir, project_hash)? else {
        return Ok(None);
    };
    if project_generation.capture_generation_id != capture_generation_id
        || project_generation.started_at_ms != started_at_ms
    {
        return Ok(None);
    }

    let global_matches = |generation: &CaptureGeneration| {
        generation.capture_generation_id == project_generation.capture_generation_id
            && generation.started_at_ms == project_generation.started_at_ms
    };
    if read_active_generation(base_dir)?
        .as_ref()
        .is_some_and(global_matches)
        || read_preparing_generation(base_dir)?
            .as_ref()
            .is_some_and(global_matches)
    {
        return Ok(Some(project_generation));
    }
    Ok(None)
}

fn read_generation_file(path: &Path) -> Result<Option<CaptureGeneration>, CaptureGenerationError> {
    let bytes = match fs::read(path) {
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
#[path = "capture_generation_tests.rs"]
mod tests;
