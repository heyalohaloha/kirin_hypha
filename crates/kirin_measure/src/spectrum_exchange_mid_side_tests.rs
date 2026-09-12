use super::*;
use crate::SpectrumRuntime;
use std::{fs, sync::Arc};

#[test]
fn post_mid_side_is_local_even_when_a_pre_target_exists() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("pre");
    fs::create_dir_all(&pre_dir).unwrap();
    let runtime = SpectrumRuntime::new(48_000, 2);
    let coordinator = SpectrumCoordinator::new_with_lease(
        48_000,
        Arc::clone(&runtime),
        crate::analysis_lease::AnalysisLease::at_paths([
            temp.path().join("analysis.0.lease"),
            temp.path().join("analysis.1.lease"),
        ]),
    );
    assert!(coordinator.set_post_mid_side_enabled(true));
    coordinator.set_post_visible(true);
    let target = SpectrumTarget {
        pre_instance_id: "pre".to_string(),
        instance_dir: pre_dir.clone(),
    };
    assert!(coordinator.post_tick("post", Some(target)));
    assert!(runtime.is_enabled());
    assert!(runtime.mid_side_enabled());
    assert!(!pre_dir.join("spectrum").join("request.json").exists());
    let view = coordinator.try_mid_side_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::WarmingUp);
    assert!(view.frame.is_none());
    coordinator.shutdown();
    runtime.shutdown_and_join();
}
