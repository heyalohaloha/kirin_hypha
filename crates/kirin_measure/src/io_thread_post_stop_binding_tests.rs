//! Exercise the actual Watch tick, not only the display projection.
use super::*;
use crate::{ComparisonReason, ComparisonState, LatchedPreReadiness};
use std::sync::atomic::Ordering;

struct Fixture {
    directory: tempfile::TempDir,
    pre: std::path::PathBuf,
    latched: Mutex<Option<LatchedPre>>,
    delta: Arc<Mutex<DeltaResult>>,
    signal: Arc<AtomicU8>,
    name: String,
}

impl Fixture {
    fn new(name: &str) -> Self {
        let directory = tempfile::tempdir().unwrap();
        let project = directory.path().join("project");
        let pre = project.join("pre").join("pre.json");
        fs::create_dir_all(pre.parent().unwrap()).unwrap();
        let fixture = Self {
            latched: Mutex::new(Some(LatchedPre {
                name: name.into(),
                instance_id: "pre".into(),
                project_dir: project,
                pre_json: pre.clone(),
                daw_session_id: None,
                host_process_id: Some(crate::post_candidates::current_host_process_id()),
                readiness: LatchedPreReadiness::Confirmed,
            })),
            directory,
            pre,
            delta: Arc::new(Mutex::new(DeltaResult::default())),
            signal: Arc::new(AtomicU8::new(SignalState::Active as u8)),
            name: name.into(),
        };
        fixture.write_pre("active");
        fixture
    }

    fn write_pre(&self, state: &str) {
        let layout = crate::plugin_data::MeasurementLayout::new(
            crate::channel_layout::ChannelLayout::stereo(),
        );
        let json = serde_json::json!({
            "v": 2, "role": "PRE", "instance_id": "pre", "name": self.name,
            "host_process_id": crate::post_candidates::current_host_process_id(),
            "signal_state": state, "t": chrono::Utc::now().to_rfc3339(),
            "lufs_m": -14.0, "true_peak": -1.0, "crest": 12.0, "layout": layout
        });
        fs::write(&self.pre, serde_json::to_vec(&json).unwrap()).unwrap();
    }

    fn tick(&self) -> DeltaResult {
        let root = self.directory.path();
        let post_dir = root.join("project").join("post");
        let exact = crate::paired_pre_instance_id(&self.latched);
        run_tick(
            &root.join("project"),
            root,
            &mut PostDiscoveryState::new(),
            &post_dir,
            &post_dir.join("post.json"),
            "post",
            "owner",
            &Arc::new(Mutex::new(MeasureResult {
                lufs_m: Some(-10.0),
                true_peak: Some(-1.0),
                ..Default::default()
            })),
            &crate::plugin_data::MeasurementLayout::new(
                crate::channel_layout::ChannelLayout::stereo(),
            ),
            &self.delta,
            &self.signal,
            &self.name,
            12.5,
            1,
            exact.as_deref(),
            "project",
            "daw",
            false,
            &self.latched,
            false,
        )
        .unwrap();
        self.delta.lock().unwrap().clone()
    }
}

#[test]
fn unnamed_exact_pair_stops_without_no_pre_and_resumes_without_reselection() {
    let f = Fixture::new("");
    assert_eq!(f.tick().comparison.state, ComparisonState::Active);
    f.write_pre("inactive");
    f.signal
        .store(SignalState::Inactive as u8, Ordering::Release);
    for _ in 0..90 {
        let delta = f.tick();
        assert_ne!(delta.mode, DeltaMode::NoPre);
        assert_eq!(delta.comparison.reason, ComparisonReason::LocalInactive);
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
    assert_eq!(
        crate::paired_pre_instance_id(&f.latched).as_deref(),
        Some("pre")
    );
    f.signal.store(SignalState::Active as u8, Ordering::Release);
    f.write_pre("active"); // The real PRE publishes a new snapshot when playback resumes.
    assert_eq!(f.tick().comparison.state, ComparisonState::Active);
}

#[test]
fn pre_stop_retains_exact_pair_but_a_real_unbind_is_no_pair() {
    let f = Fixture::new("");
    assert_eq!(f.tick().comparison.state, ComparisonState::Active);
    f.write_pre("inactive");
    assert_eq!(f.tick().comparison.reason, ComparisonReason::PreInactive);
    f.write_pre("active");
    assert_eq!(f.tick().comparison.state, ComparisonState::Active);
    f.latched.lock().unwrap().take();
    fs::remove_file(&f.pre).unwrap();
    for state in [SignalState::Active, SignalState::Inactive] {
        f.signal.store(state as u8, Ordering::Release);
        assert_eq!(f.tick().comparison.reason, ComparisonReason::NoPair);
    }
}

#[test]
fn unresolved_name_is_not_an_exact_pair_during_stop() {
    let f = Fixture::new("missing");
    f.latched.lock().unwrap().take();
    fs::remove_file(&f.pre).unwrap();
    f.signal
        .store(SignalState::Inactive as u8, Ordering::Release);
    assert_eq!(f.tick().comparison.reason, ComparisonReason::NoPair);
}
