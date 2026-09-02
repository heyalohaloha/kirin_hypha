use super::*;

pub(super) struct PreparedPostSession {
    pub(super) session: PostSession,
    pub(super) renewal: bool,
    pub(super) retired: Option<(Option<SpectrumTarget>, Uuid)>,
    pub(super) reset_runtime: bool,
}

#[cfg(test)]
impl SpectrumCoordinator {
    /// Test convenience wrapper. Production supplies the pair name to the lease metadata.
    pub(crate) fn post_tick(&self, post_instance_id: &str, target: Option<SpectrumTarget>) -> bool {
        self.post_tick_for_owner(post_instance_id, target, "")
    }
}
