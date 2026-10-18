use super::post_tick::unpaired_post_owner_label;
use super::*;

// 2026-10-18: a POST without a pair holds an analysis slot under the name its own name field shows
// when no name is given (the first 8 characters of its instance id), so the waiting third view can
// say who uses the two slots. A pair's PRE name still names the slot when there is one.
fn coordinators(count: usize, temp: &tempfile::TempDir) -> Vec<Arc<SpectrumCoordinator>> {
    let paths = [
        temp.path().join("analysis.0.lease"),
        temp.path().join("analysis.1.lease"),
    ];
    (0..count)
        .map(|_| {
            let runtime =
                SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
            let coordinator = SpectrumCoordinator::new_with_lease(
                48_000,
                runtime,
                crate::analysis_lease::AnalysisLease::at_paths(paths.clone()),
            );
            coordinator.set_post_visible(true);
            coordinator
        })
        .collect()
}

#[test]
fn unpaired_posts_are_named_by_their_instance_for_the_waiting_third() {
    let temp = tempfile::tempdir().unwrap();
    let posts = coordinators(3, &temp);
    assert!(posts[0].post_tick_for_owner("3fa9c1d2-0000-4000-8000-000000000001", None, ""));
    assert!(posts[1].post_tick_for_owner("77ab01ff-0000-4000-8000-000000000002", None, "Vocal"));
    assert!(!posts[2].post_tick_for_owner("c0ffee00-0000-4000-8000-000000000003", None, "  "));
    let view = posts[2].try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::InUse);
    assert_eq!(
        view.analysis_owner_names,
        ["POST 3fa9c1d2".to_string(), "Vocal".to_string()]
    );
}

#[test]
fn an_unpaired_owner_label_is_bounded_and_never_empty() {
    assert_eq!(unpaired_post_owner_label("3fa9c1d2-0000"), "POST 3fa9c1d2");
    assert_eq!(unpaired_post_owner_label("ab"), "POST ab");
    assert_eq!(unpaired_post_owner_label(""), "POST");
    assert!(crate::sanitize_name(&unpaired_post_owner_label("3fa9c1d2-0000")) == "POST 3fa9c1d2");
}
