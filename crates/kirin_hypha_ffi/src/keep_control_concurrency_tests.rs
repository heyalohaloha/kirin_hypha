use super::*;
use std::ffi::OsString;
use std::fs::OpenOptions;
use std::path::PathBuf;

struct StorageRoot(Option<OsString>);

impl Drop for StorageRoot {
    fn drop(&mut self) {
        match &self.0 {
            Some(value) => std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, value),
            None => std::env::remove_var(kirin_measure::TEST_STORAGE_ROOT_ENV),
        }
    }
}

fn wait_for_preparation_worker(base: PathBuf, engine: &KirinHyphaEngine, all: bool, label: &str) {
    let diagnostic_base = base.clone();
    // The worker owns this installation-local lock until feedback and rollback have finished.
    // Block on that notification instead of sleeping or polling the worker's phase.
    TimedJoin::spawn(move || {
        let lock = OpenOptions::new()
            .read(true)
            .write(true)
            .open(base.join("capture_generation/.publish.lock"))
            .unwrap();
        lock.lock().unwrap();
    })
    .join_diagnosed(label, || control_diagnostic(engine, &diagnostic_base, all));
}

// Failure-only observation. Never acquire the Keep control/publication lock.
// These independent atomics/files may straddle a transition; they are not authority.
fn control_diagnostic(engine: &KirinHyphaEngine, base: &std::path::Path, all: bool) {
    eprintln!(
        "keep timeout: all={all}, phase={}, phase_generation={}, record_generation={}, keep_record_generation={}, heartbeat={}, worker_generation={}",
        engine.keep_phase.load(Ordering::Acquire),
        engine
            .keep_phase_generation_started_at_ms
            .load(Ordering::Acquire),
        engine.record_sm.generation(),
        engine.keep_record_generation.load(Ordering::Acquire),
        engine.heartbeat.load(Ordering::Acquire),
        engine.__measure_worker_generation_for_test(),
    );
    for name in ["current.json", "preparing.json"] {
        let result = (|| -> Result<serde_json::Value, String> {
            use std::io::Read;
            let file = std::fs::File::open(base.join("capture_generation").join(name))
                .map_err(|error| format!("open: {error}"))?;
            let mut bytes = Vec::new();
            file.take(65537)
                .read_to_end(&mut bytes)
                .map_err(|error| format!("read: {error}"))?;
            if bytes.len() > 65536 {
                return Err("diagnostic size bound".into());
            }
            let value: serde_json::Value =
                serde_json::from_slice(&bytes).map_err(|error| format!("parse: {error}"))?;
            Ok(serde_json::json!({
                "capture_generation_id": value.get("capture_generation_id"),
                "started_at_ms": value.get("started_at_ms"),
                "state": value.get("state"),
            }))
        })();
        eprintln!("keep publication: file={name}, generation={result:?}");
    }
}

fn paired_engine(paths: &PlatformPaths) -> Arc<KirinHyphaEngine> {
    let engine = Arc::new(KirinHyphaEngine::new(48_000, ChannelLayout::stereo()));
    engine.set_license(LICENSE_OS);
    engine.set_identity(
        "post".into(),
        "project".into(),
        "session".into(),
        String::new(),
    );
    engine.identity.lock().unwrap().project_hash = "project".into();
    *engine.project_hash_cell.write().unwrap() = "project".into();
    *engine.daw_session_id_cell.write().unwrap() = "session".into();
    let pre_dir = paths.kirin_tmp_root.join("project/pre");
    let post_dir = paths.kirin_tmp_root.join("project/post");
    std::fs::create_dir_all(&pre_dir).unwrap();
    std::fs::create_dir_all(&post_dir).unwrap();
    std::fs::write(
        pre_dir.join("pre.json"),
        serde_json::to_vec(&serde_json::json!({
            "instance_id": "pre", "name": "mix", "signal_state": "inactive",
            "host_process_id": current_host_process_id(), "daw_session_id": "session"
        }))
        .unwrap(),
    )
    .unwrap();
    std::fs::write(
        post_dir.join("post.json"),
        serde_json::to_vec(&serde_json::json!({
            "instance_id": "post", "signal_state": "inactive", "t": "",
            "host_process_id": current_host_process_id(), "daw_session_id": "session",
            "pair_pre_name": "mix", "paired_pre_instance_id": "pre"
        }))
        .unwrap(),
    )
    .unwrap();
    assert!(engine.set_pair_candidate("pre"));
    assert_eq!(
        enumerate_ready_post_pair_candidates_for_operation_group(
            &paths.kirin_tmp_root,
            "project",
            "session",
            current_host_process_id()
        )
        .len(),
        1
    );
    engine
}

#[test]
fn accepted_keep_and_all_keep_with_repeated_controls_and_generation_changes_finish() {
    for all in [false, true] {
        let temporary = tempfile::tempdir().unwrap();
        let _storage = StorageRoot(std::env::var_os(kirin_measure::TEST_STORAGE_ROOT_ENV));
        std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, temporary.path());
        let paths = PlatformPaths::default_current().unwrap();
        let engine = paired_engine(&paths);
        let starting_engine = Arc::clone(&engine);
        assert!(
            TimedJoin::spawn(move || {
                if all {
                    starting_engine.keep_all()
                } else {
                    starting_engine.keep()
                }
            })
            .join_diagnosed("initial keep", || {
                control_diagnostic(&engine, &paths.storage.plugin_data_dir(), all)
            }),
            "the fixture must enter the actual preparation path"
        );
        assert_eq!(engine.keep_phase(), KIRIN_KEEP_PHASE_PREPARING);
        let first_generation = engine
            .keep_phase_generation_started_at_ms
            .load(Ordering::Acquire);
        assert!(first_generation > 0);

        let start = Arc::new(Barrier::new(3));
        let keep_engine = Arc::clone(&engine);
        let keep_start = Arc::clone(&start);
        let keeps = TimedJoin::spawn(move || {
            keep_start.wait();
            for _ in 0..8 {
                keep_engine.keep();
                if all {
                    keep_engine.keep_all();
                } else {
                    keep_engine.keep();
                }
            }
        });
        let stop_engine = Arc::clone(&engine);
        let stop_start = Arc::clone(&start);
        let stops = TimedJoin::spawn(move || {
            stop_start.wait();
            for _ in 0..8 {
                stop_engine.stop();
                stop_engine.stop_all();
            }
        });
        start.wait();
        keeps.join_diagnosed("repeated keeps", || {
            control_diagnostic(&engine, &paths.storage.plugin_data_dir(), all)
        });
        stops.join_diagnosed("repeated stops", || {
            control_diagnostic(&engine, &paths.storage.plugin_data_dir(), all)
        });
        engine.stop();
        wait_for_preparation_worker(
            paths.storage.plugin_data_dir(),
            &engine,
            all,
            "publication lock after controls",
        );
        assert_eq!(engine.keep_phase(), KIRIN_KEEP_PHASE_IDLE);
        assert!(engine.drain_keep_action_notice().is_none());

        // Follow an All Keep by a single Keep and a single Keep by an All Keep.
        let restarting_engine = Arc::clone(&engine);
        assert!(TimedJoin::spawn(move || {
            if all {
                restarting_engine.keep()
            } else {
                restarting_engine.keep_all()
            }
        })
        .join_diagnosed("opposite keep restart", || {
            control_diagnostic(&engine, &paths.storage.plugin_data_dir(), all)
        }));
        assert_ne!(
            engine
                .keep_phase_generation_started_at_ms
                .load(Ordering::Acquire),
            first_generation
        );
        engine.stop();
        wait_for_preparation_worker(
            paths.storage.plugin_data_dir(),
            &engine,
            all,
            "publication lock after restart",
        );
        assert_eq!(engine.keep_phase(), KIRIN_KEEP_PHASE_IDLE);
        assert!(engine.drain_keep_action_notice().is_none());
    }
}
