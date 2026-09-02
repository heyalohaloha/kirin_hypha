//! Renewable POST-owned reservation lease for one exact Record pair.

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::storage::StoragePaths;

pub(super) struct ReservationLeaseRefresh {
    next_refresh: Instant,
}

impl ReservationLeaseRefresh {
    pub(super) fn new(now: Instant) -> Self {
        Self { next_refresh: now }
    }

    fn service_with<F>(&mut self, now: Instant, is_recording: bool, refresh: F)
    where
        F: FnOnce(),
    {
        if !is_recording {
            self.next_refresh = now;
            return;
        }
        if now < self.next_refresh {
            return;
        }
        refresh();
        self.next_refresh =
            now + Duration::from_secs(crate::reservation::RESERVATION_LEASE_REFRESH_SECS);
    }

    pub(super) fn service(
        &mut self,
        now: Instant,
        is_recording: bool,
        paired_pre_target: &Arc<Mutex<Option<String>>>,
        project_hash: &str,
        post_instance_id: &str,
    ) {
        self.service_with(now, is_recording, || {
            let pre_instance_id = paired_pre_target
                .lock()
                .ok()
                .and_then(|guard| guard.clone());
            if let (Some(pre_instance_id), Ok(paths)) =
                (pre_instance_id, StoragePaths::default_platform())
            {
                let _ = crate::reservation::refresh_pairing(
                    &paths.plugin_data_dir(),
                    project_hash,
                    &pre_instance_id,
                    post_instance_id,
                );
            }
        });
    }
}

#[cfg(test)]
#[path = "io_thread_post_reservation_tests.rs"]
mod tests;
