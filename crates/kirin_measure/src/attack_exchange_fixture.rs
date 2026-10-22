//! Owns workers even when a fixture assertion unwinds.
use super::*;
pub(super) struct Fixture {
    _temp: tempfile::TempDir,
    pub(super) pre_dir: std::path::PathBuf,
    pub(super) pre: Arc<SpectrumCoordinator>,
    pub(super) post: Arc<SpectrumCoordinator>,
    pub(super) pre_attack: Arc<AttackRuntime>,
    pub(super) post_attack: Arc<AttackRuntime>,
    pub(super) pre_spectrum: Arc<SpectrumRuntime>,
    pub(super) post_spectrum: Arc<SpectrumRuntime>,
    pub(super) clock: Arc<super::super::clock::TestClock>,
    pub(super) target: SpectrumTarget,
}
impl Fixture {
    pub(super) fn new() -> Self {
        let temp = tempfile::tempdir().unwrap();
        let pre_dir = temp.path().join("project").join("pre");
        let pre_json = pre_dir.join("pre.json");
        crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();

        let pre_spectrum =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let post_spectrum =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let pre_attack = AttackRuntime::new(48_000, 2).unwrap();
        let post_attack = AttackRuntime::new(48_000, 2).unwrap();
        pre_attack.set_audio_end_for_test(false);
        post_attack.set_audio_end_for_test(false);
        let pre = SpectrumCoordinator::new_with_lease_and_attack(
            48_000,
            Arc::clone(&pre_spectrum),
            Some(Arc::clone(&pre_attack)),
            AnalysisLease::at_path(temp.path().join("pre.lease")),
        );
        let post = SpectrumCoordinator::new_with_lease_and_attack(
            48_000,
            Arc::clone(&post_spectrum),
            Some(Arc::clone(&post_attack)),
            AnalysisLease::at_path(temp.path().join("post.lease")),
        );
        let clock = super::clock::TestClock::new();
        pre.use_test_clock(&clock);
        post.use_test_clock(&clock);
        let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();

        Self {
            _temp: temp,
            pre_dir,
            pre,
            post,
            pre_attack,
            post_attack,
            pre_spectrum,
            post_spectrum,
            clock,
            target,
        }
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        self.pre.shutdown();
        self.post.shutdown();
        self.pre_attack.shutdown_and_join();
        self.post_attack.shutdown_and_join();
        self.pre_spectrum.shutdown_and_join();
        self.post_spectrum.shutdown_and_join();
    }
}
#[cfg(test)]
#[path = "attack_exchange_fixture_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "attack_exchange_fixture_lease_tests.rs"]
mod lease_tests;
