//! Private authority retirement owns only the current request publication.
use super::*;

impl SpectrumCoordinator {
    pub(super) fn remove_local_spectrum_publication(
        &self,
        request_id: Uuid,
        instance_dir: &Path,
    ) -> bool {
        let had_owned_publication = {
            let Some(mut slot) = self.try_pre_session() else {
                return false;
            };
            let Some(current) = slot.as_mut().filter(|current| {
                current.request_id == request_id && current.instance_dir == instance_dir
            }) else {
                return false;
            };
            current.last_write_attempt_end = None;
            current.last_write_attempt_at = None;
            current.last_written_end.take().is_some()
        };
        // Once per authority edge, outside the session lock. A different request's file is
        // retained, and ordinary snapshot mutex contention never removes a publication.
        if had_owned_publication
            && read_snapshot(instance_dir).is_some_and(|snapshot| snapshot.request_id == request_id)
        {
            remove_snapshot(instance_dir);
        }
        true
    }

    pub(super) fn remove_retired_spectrum_result(&self, request_id: Uuid, instance_dir: &Path) {
        if read_snapshot(instance_dir).is_some_and(|snapshot| snapshot.request_id == request_id) {
            remove_snapshot(instance_dir);
        }
    }
}
