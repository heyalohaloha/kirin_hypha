//! Read-only observation of one closed pair; never participates in Drop or Record commits.
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use crate::plugin_data::{verify_checksum, PluginDataFile, Role, Status};
use crate::record::RecordStateMachine;
use crate::RecordDisplayStatus;

#[derive(Clone, PartialEq, Eq)]
struct FileStamp {
    len: u64,
    modified: SystemTime,
    #[cfg(unix)]
    identity: (u64, u64),
    #[cfg(not(unix))]
    identity: SystemTime,
}

impl FileStamp {
    fn read(path: &Path) -> Option<Self> {
        let metadata = std::fs::symlink_metadata(path).ok()?;
        if !metadata.is_file() {
            return None;
        }
        #[cfg(unix)]
        use std::os::unix::fs::MetadataExt;
        Some(Self {
            len: metadata.len(),
            modified: metadata.modified().ok()?,
            #[cfg(unix)]
            identity: (metadata.dev(), metadata.ino()),
            #[cfg(not(unix))]
            identity: metadata.created().ok()?,
        })
    }
}

struct CachedPair {
    project: String,
    post: String,
    session: String,
    pre: String,
    post_path: PathBuf,
    post_stamp: FileStamp,
    pre_path: PathBuf,
    pre_stamp: FileStamp,
    waiting: bool,
}

#[derive(Default)]
pub(super) struct WavBindingObserver {
    cached: Option<CachedPair>,
}

fn pending_path(base: &Path, project: &str, instance: &str, role: &str, session: &str) -> PathBuf {
    let project = crate::path_identity::observed_path_component(project);
    let instance = crate::path_identity::observed_path_component(instance);
    let session = crate::path_identity::observed_path_component(session);
    base.join(project.as_ref())
        .join(instance.as_ref())
        .join(role)
        .join(".pair_pending")
        .join(format!("{session}.json"))
}

fn read_member(path: &Path) -> Option<(PluginDataFile, Option<FileStamp>)> {
    if !std::fs::symlink_metadata(path).ok()?.is_file() {
        return None;
    }
    let before = FileStamp::read(path);
    let data: PluginDataFile = serde_json::from_slice(&std::fs::read(path).ok()?).ok()?;
    if before.is_some() && before != FileStamp::read(path) {
        return None;
    }
    verify_checksum(&data).then_some((data, before))
}

fn complete_pending(
    data: &PluginDataFile,
    project: &str,
    instance: &str,
    session: &str,
    role: Role,
) -> bool {
    data.project_hash == project
        && data.instance_id == instance
        && data.role == role
        && data.record_session_id.as_deref() == Some(session)
        && data.status == Status::Closed
        && data.commit_status.as_deref() == Some("pair_pending")
        && !data.integrity_degraded
        && data
            .record_quality
            .as_ref()
            .is_some_and(|quality| quality.complete && quality.usable)
}

impl WavBindingObserver {
    fn observe(
        &mut self,
        base: &Path,
        project: &str,
        post: &str,
        session: &str,
        expected_pre: Option<&str>,
    ) -> bool {
        if project.trim().is_empty() || post.trim().is_empty() || session.trim().is_empty() {
            self.cached = None;
            return false;
        }
        let project_path = crate::path_identity::observed_path_component(project);
        let session_path = crate::path_identity::observed_path_component(session);
        if base
            .join(project_path.as_ref())
            .join("record_sessions")
            .join(format!("{session_path}.json"))
            .is_file()
        {
            self.cached = None;
            return false;
        }
        let post_path = pending_path(base, project, post, "post", session);
        if let Some(cached) = &self.cached {
            if cached.project == project
                && cached.post == post
                && cached.session == session
                && cached.post_path == post_path
                && FileStamp::read(&post_path).as_ref() == Some(&cached.post_stamp)
                && FileStamp::read(&cached.pre_path).as_ref() == Some(&cached.pre_stamp)
            {
                return cached.waiting && expected_pre.is_none_or(|pre| pre == cached.pre);
            }
        }
        self.cached = None;
        let Some((post_data, post_stamp)) = read_member(&post_path) else {
            return false;
        };
        let Some(pre) = post_data
            .paired_pre_instance_id
            .as_deref()
            .filter(|id| !id.trim().is_empty())
        else {
            return false;
        };
        if expected_pre.is_some_and(|expected| expected != pre) {
            return false;
        }
        let pre_path = pending_path(base, project, pre, "pre", session);
        let Some((pre_data, pre_stamp)) = read_member(&pre_path) else {
            return false;
        };
        let waiting = complete_pending(&post_data, project, post, session, Role::Post)
            && complete_pending(&pre_data, project, pre, session, Role::Pre)
            && pre_data.paired_post_instance_id.as_deref() == Some(post)
            && (!post_data
                .record_quality
                .as_ref()
                .unwrap()
                .expected_wav_ready
                || !pre_data.record_quality.as_ref().unwrap().expected_wav_ready);
        if let (Some(post_stamp), Some(pre_stamp)) = (post_stamp, pre_stamp) {
            self.cached = Some(CachedPair {
                project: project.to_string(),
                post: post.to_string(),
                session: session.to_string(),
                pre: pre.to_string(),
                post_path,
                post_stamp,
                pre_path,
                pre_stamp,
                waiting,
            });
        }
        waiting
    }

    pub(super) fn poll(
        &mut self,
        base: Option<&Path>,
        project: &str,
        post: &str,
        state: &RecordStateMachine,
    ) {
        let generation = state.generation();
        let Some(display) = state.try_record_display_snapshot() else {
            return;
        };
        if generation == 0
            && display.generation == 0
            && display.status == RecordDisplayStatus::Empty
            && !state.is_recording()
        {
            let waiting = base
                .and_then(|base| {
                    released_target(base, project, post).map(|(session, pre)| {
                        self.observe(base, project, post, &session, Some(&pre))
                    })
                })
                .unwrap_or(false);
            state.set_record_wav_binding_pending(0, waiting);
            return;
        }
        if state.is_recording()
            || display.generation != generation
            || !matches!(
                display.status,
                RecordDisplayStatus::Finalized | RecordDisplayStatus::Dismissed
            )
        {
            self.cached = None;
            return;
        }
        let session = state.last_closed_session_id();
        let waiting = base
            .zip(session.as_deref())
            .is_some_and(|(base, session)| self.observe(base, project, post, session, None));
        state.set_record_wav_binding_pending(generation, waiting);
    }
}

fn released_target(base: &Path, project: &str, post: &str) -> Option<(String, String)> {
    if project.trim().is_empty() || post.trim().is_empty() {
        return None;
    }
    let project = crate::path_identity::observed_path_component(project);
    let post_path = crate::path_identity::observed_path_component(post);
    let path = base
        .join(project.as_ref())
        .join(crate::record_signal::SIGNALS_SUBDIR)
        .join(format!("{post_path}.json"));
    if !std::fs::symlink_metadata(&path).ok()?.is_file() {
        return None;
    }
    let signal: crate::record_signal::RecordSignal =
        serde_json::from_slice(&std::fs::read(path).ok()?).ok()?;
    (signal.status == crate::record_signal::SignalStatus::Released
        && signal.requested_by == post
        && !signal.session_id.trim().is_empty()
        && !signal.target_pre_instance_id.trim().is_empty())
    .then_some((signal.session_id, signal.target_pre_instance_id))
}

#[cfg(test)]
pub(crate) fn saved_pair_waits_for_audio(
    base: &Path,
    project: &str,
    post: &str,
    session: &str,
) -> bool {
    WavBindingObserver::default().observe(base, project, post, session, None)
}
