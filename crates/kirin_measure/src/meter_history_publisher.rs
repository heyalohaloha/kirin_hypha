//! Change-driven history snapshots. Watch owns the lease; history is not a heartbeat.
//! This state belongs to the exchange (not its replaceable IO worker) and never touches RT.

use super::*;
use std::time::SystemTime;

#[derive(Debug, PartialEq, Eq)]
struct FileStamp {
    len: u64,
    modified: SystemTime,
    #[cfg(unix)]
    identity: (u64, u64),
    #[cfg(not(unix))]
    identity: SystemTime,
}

impl FileStamp {
    fn from_metadata(metadata: &fs::Metadata) -> Option<Self> {
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

    fn read(path: &Path) -> Option<Self> {
        // A symlink or missing/replaced file must not inherit the previous publication cache.
        Self::from_metadata(&fs::symlink_metadata(path).ok()?)
    }
}

struct Published {
    revision: (u64, u64, u64),
    pre_instance_id: String,
    daw_session_id: String,
    watch_owner_id: String,
    path: PathBuf,
    stamp: FileStamp,
    publication: Publication,
}

#[derive(Default)]
pub(super) struct HistoryPublisher {
    published: Option<Published>,
    #[cfg(test)]
    snapshots_built: usize,
    #[cfg(test)]
    snapshots_serialized: usize,
}

impl HistoryPublisher {
    pub(super) fn publish(
        &mut self,
        exchange: &MeterDeltaHistoryExchange,
        pre_instance_id: &str,
        daw_session_id: &str,
        watch_owner_id: &str,
        instance_dir: &Path,
    ) -> Result<(), String> {
        // Serialize competing IO publishers before taking the session lock. A delayed older
        // snapshot cannot overtake a newer one; the audio thread never acquires this mutex.
        let session = exchange
            .meter_session
            .try_lock()
            .map_err(|_| "meter session busy".to_string())?;
        let revision = session.history_publication_revision();
        let unchanged = self.published.as_ref().filter(|last| {
            last.revision == revision
                && last.pre_instance_id == pre_instance_id
                && last.daw_session_id == daw_session_id
                && last.watch_owner_id == watch_owner_id
                && last.path.parent() == Some(instance_dir)
        });
        if let Some(last) = unchanged {
            // Do not hold the measurement mutex across any filesystem operation.
            drop(session);
            if FileStamp::read(&last.path).as_ref() == Some(&last.stamp) {
                return Ok(());
            }
            // Reacquire and capture the current revision after disappearance/replacement.
            self.published = None;
            return self.publish(
                exchange,
                pre_instance_id,
                daw_session_id,
                watch_owner_id,
                instance_dir,
            );
        }
        let points = session
            .recent_history(MeterHistoryResolution::Hz10, METER_HISTORY_EXCHANGE_POINTS)
            .into_iter()
            .filter_map(WirePoint::from_history)
            .collect();
        drop(session);
        #[cfg(test)]
        {
            self.snapshots_built += 1;
        }
        let publication = Publication {
            schema: METER_HISTORY_EXCHANGE_SCHEMA,
            pre_instance_id: pre_instance_id.into(),
            watch_owner_id: watch_owner_id.into(),
            daw_session_id: daw_session_id.into(),
            sample_rate: exchange.sample_rate,
            layout: exchange.layout.clone(),
            points,
        };
        let path = instance_dir.join(METER_HISTORY_EXCHANGE_FILE);
        // Unsupported/missing presentation clocks may advance local history without changing
        // the exact wire tail. Do not serialize those again; RESET/new session must still publish.
        if let Some(last) = self.published.as_mut() {
            if last.path == path
                && (last.revision.0, last.revision.1) == (revision.0, revision.1)
                && last.publication == publication
                && FileStamp::read(&path).as_ref() == Some(&last.stamp)
            {
                last.revision = revision;
                return Ok(());
            }
        }
        #[cfg(test)]
        {
            self.snapshots_serialized += 1;
        }
        let bytes = serde_json::to_vec(&publication).map_err(|error| error.to_string())?;
        let written = crate::atomic_file::write_bytes_atomic_metadata(&path, &bytes)
            .map_err(|error| error.to_string())?;
        // Commit only after success, and only for our own file. On Windows last-write time
        // may settle on close, so capture the final stamp after confirming file identity.
        self.published = FileStamp::from_metadata(&written)
            .zip(FileStamp::read(&path))
            .filter(|(written, current)| {
                #[cfg(unix)]
                {
                    written == current
                }
                #[cfg(not(unix))]
                {
                    written.identity == current.identity && written.len == current.len
                }
            })
            .map(|(_, stamp)| Published {
                revision,
                pre_instance_id: pre_instance_id.into(),
                daw_session_id: daw_session_id.into(),
                watch_owner_id: watch_owner_id.into(),
                path,
                stamp,
                publication,
            });
        Ok(())
    }
}

#[cfg(test)]
#[path = "meter_history_publisher_tests.rs"]
mod tests;
