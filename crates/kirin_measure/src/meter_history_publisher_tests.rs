use super::*;
use crate::channel_layout::ChannelLayout;
use crate::MeterClockStart;

fn fixture() -> (
    tempfile::TempDir,
    Arc<Mutex<MeterSession>>,
    Arc<MeterDeltaHistoryExchange>,
) {
    let dir = tempfile::tempdir().unwrap();
    let session = Arc::new(Mutex::new(
        MeterSession::new(48_000, ChannelLayout::stereo()).unwrap(),
    ));
    let exchange = MeterDeltaHistoryExchange::new(48_000, Arc::clone(&session));
    (dir, session, exchange)
}

fn publish(exchange: &MeterDeltaHistoryExchange, dir: &Path) -> Result<(), String> {
    exchange.service_pre_endpoint("pre", "song", "owner", dir)
}

fn counts(exchange: &MeterDeltaHistoryExchange) -> (usize, usize) {
    let publisher = exchange.publisher.lock().unwrap();
    (publisher.snapshots_built, publisher.snapshots_serialized)
}

fn advance(session: &Arc<Mutex<MeterSession>>, start: i64) {
    let audio: Vec<_> = (0..4_800)
        .flat_map(|i| {
            let value = 0.2 * (i as f64 * std::f64::consts::TAU * 1_000.0 / 48_000.0).sin();
            [value, value]
        })
        .collect();
    assert!(session.lock().unwrap().push_active_at(
        &audio,
        MeterClockStart {
            position_samples: Some(start),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
        }
    ));
}

#[test]
fn unchanged_history_skips_clone_serialization_and_replacement_for_600_polls() {
    let (dir, session, exchange) = fixture();
    advance(&session, 0);
    publish(&exchange, dir.path()).unwrap();
    let path = dir.path().join(METER_HISTORY_EXCHANGE_FILE);
    let initial = FileStamp::read(&path).unwrap();
    let bytes = fs::read(&path).unwrap();
    session.lock().unwrap().pause();
    for _ in 0..600 {
        publish(&exchange, dir.path()).unwrap();
    }
    assert_eq!(counts(&exchange), (1, 1));
    assert_eq!(FileStamp::read(&path).unwrap(), initial);
    assert_eq!(fs::read(path).unwrap(), bytes);
}

#[test]
fn initial_empty_snapshot_and_each_explicit_reset_are_published() {
    let (dir, session, exchange) = fixture();
    publish(&exchange, dir.path()).unwrap();
    for expected in 2..=3 {
        session.lock().unwrap().reset();
        publish(&exchange, dir.path()).unwrap();
        assert_eq!(counts(&exchange), (expected, expected));
        assert!(read_publication(dir.path()).unwrap().points.is_empty());
    }
    advance(&session, 0);
    publish(&exchange, dir.path()).unwrap();
    assert!(!read_publication(dir.path()).unwrap().points.is_empty());
    session.lock().unwrap().reset();
    publish(&exchange, dir.path()).unwrap();
    assert!(read_publication(dir.path()).unwrap().points.is_empty());
    assert_eq!(counts(&exchange), (5, 5));
}

#[test]
fn new_history_and_short_loop_same_endpoint_are_not_suppressed() {
    let (dir, session, exchange) = fixture();
    advance(&session, 0);
    publish(&exchange, dir.path()).unwrap();
    let first = read_publication(dir.path()).unwrap();
    advance(&session, 0);
    publish(&exchange, dir.path()).unwrap();
    let second = read_publication(dir.path()).unwrap();
    assert_eq!(counts(&exchange), (2, 2));
    assert_eq!(second.points.len(), 2);
    assert_eq!(
        first.points[0].endpoint_samples,
        second.points[1].endpoint_samples
    );
    assert_ne!(
        first.points[0].observed_frames,
        second.points[1].observed_frames
    );
}

#[test]
fn owner_song_instance_and_destination_changes_publish_without_new_audio() {
    let (dir, _, exchange) = fixture();
    publish(&exchange, dir.path()).unwrap();
    for (pre, song, owner) in [
        ("pre", "song", "new-owner"),
        ("pre", "new-song", "new-owner"),
        ("new-pre", "new-song", "new-owner"),
    ] {
        exchange
            .service_pre_endpoint(pre, song, owner, dir.path())
            .unwrap();
        let wire = read_publication(dir.path()).unwrap();
        assert_eq!(
            (
                wire.pre_instance_id.as_str(),
                wire.daw_session_id.as_str(),
                wire.watch_owner_id.as_str()
            ),
            (pre, song, owner)
        );
    }
    let other = dir.path().join("other");
    publish(&exchange, &other).unwrap();
    assert_eq!(counts(&exchange), (5, 5));
    assert!(other.join(METER_HISTORY_EXCHANGE_FILE).is_file());
}

#[test]
fn a_removed_file_or_parent_is_recreated_without_new_audio() {
    let (dir, _, exchange) = fixture();
    let endpoint = dir.path().join("endpoint");
    publish(&exchange, &endpoint).unwrap();
    let file = endpoint.join(METER_HISTORY_EXCHANGE_FILE);
    fs::remove_file(&file).unwrap();
    fs::remove_dir(&endpoint).unwrap();
    publish(&exchange, &endpoint).unwrap();
    assert!(file.is_file());
    assert_eq!(counts(&exchange), (2, 2));
}

#[test]
fn replacement_and_truncation_are_repaired_without_new_audio() {
    let (dir, _, exchange) = fixture();
    publish(&exchange, dir.path()).unwrap();
    let path = dir.path().join(METER_HISTORY_EXCHANGE_FILE);
    crate::atomic_file::write_bytes_atomic(&path, b"foreign").unwrap();
    publish(&exchange, dir.path()).unwrap();
    assert_eq!(
        read_publication(dir.path()).unwrap().watch_owner_id,
        "owner"
    );
    fs::write(&path, b"").unwrap();
    publish(&exchange, dir.path()).unwrap();
    assert!(read_publication(dir.path()).is_ok());
    assert_eq!(counts(&exchange), (3, 3));
}

#[test]
fn failed_write_does_not_commit_revision_and_recovers() {
    let (dir, _, exchange) = fixture();
    let path = dir.path().join(METER_HISTORY_EXCHANGE_FILE);
    fs::create_dir(&path).unwrap();
    assert!(publish(&exchange, dir.path()).is_err());
    assert!(exchange.publisher.lock().unwrap().published.is_none());
    assert_eq!(
        fs::read_dir(dir.path()).unwrap().count(),
        1,
        "temporary cleaned"
    );
    fs::remove_dir(&path).unwrap();
    publish(&exchange, dir.path()).unwrap();
    publish(&exchange, dir.path()).unwrap();
    assert_eq!(counts(&exchange), (2, 2));
    assert!(path.is_file());
}

#[test]
fn failed_changed_revision_retries_and_does_not_cache_success() {
    let (dir, session, exchange) = fixture();
    publish(&exchange, dir.path()).unwrap();
    let path = dir.path().join(METER_HISTORY_EXCHANGE_FILE);
    fs::remove_file(&path).unwrap();
    fs::create_dir(&path).unwrap();
    advance(&session, 0);
    assert!(publish(&exchange, dir.path()).is_err());
    fs::remove_dir(&path).unwrap();
    publish(&exchange, dir.path()).unwrap();
    assert_eq!(read_publication(dir.path()).unwrap().points.len(), 1);
    assert_eq!(counts(&exchange), (3, 3));
}

#[test]
fn busy_session_and_busy_publisher_skip_without_marking_published() {
    let (dir, session, exchange) = fixture();
    let session_guard = session.lock().unwrap();
    assert!(publish(&exchange, dir.path()).is_err());
    drop(session_guard);
    let publisher_guard = exchange.publisher.lock().unwrap();
    assert!(publish(&exchange, dir.path()).is_err());
    drop(publisher_guard);
    assert_eq!(counts(&exchange), (0, 0));
    publish(&exchange, dir.path()).unwrap();
    assert_eq!(counts(&exchange), (1, 1));
}

#[test]
fn restart_and_replacement_session_do_not_reuse_an_old_cache() {
    let (dir, session, exchange) = fixture();
    publish(&exchange, dir.path()).unwrap();
    *session.lock().unwrap() = MeterSession::new(48_000, ChannelLayout::stereo()).unwrap();
    publish(&exchange, dir.path()).unwrap();
    assert_eq!(counts(&exchange), (2, 2));
    let restarted = MeterDeltaHistoryExchange::new(48_000, session);
    publish(&restarted, dir.path()).unwrap();
    assert_eq!(counts(&restarted), (1, 1));
}

#[test]
fn history_with_no_exact_wire_points_does_not_repeat_empty_publication() {
    let (dir, session, exchange) = fixture();
    publish(&exchange, dir.path()).unwrap();
    session.lock().unwrap().push_active(&vec![0.2; 9_600]);
    publish(&exchange, dir.path()).unwrap();
    assert_eq!(counts(&exchange).1, 1);
    assert!(read_publication(dir.path()).unwrap().points.is_empty());
}

#[test]
fn no_snapshot_until_a_complete_history_observation_advances() {
    let (dir, session, exchange) = fixture();
    publish(&exchange, dir.path()).unwrap();
    session.lock().unwrap().push_active_at(
        &[0.2; 64],
        MeterClockStart {
            position_samples: Some(0),
            epoch: Some(1),
            source: CaptureClockSource::ProjectTimeline,
        },
    );
    publish(&exchange, dir.path()).unwrap();
    assert_eq!(counts(&exchange), (1, 1));
}

#[cfg(not(windows))]
#[test]
fn delayed_io_releases_measurement_lock_and_cannot_skip_a_later_reset() {
    let (dir, session, exchange) = fixture();
    advance(&session, 0);
    let pause =
        crate::atomic_file::AtomicWritePause::install(dir.path().join(METER_HISTORY_EXCHANGE_FILE));
    let writer = Arc::clone(&exchange);
    let destination = dir.path().to_path_buf();
    let worker = std::thread::spawn(move || publish(&writer, &destination));
    pause.wait_until_entered();
    session
        .try_lock()
        .expect("IO must release the measurement mutex")
        .reset();
    assert!(
        publish(&exchange, dir.path()).is_err(),
        "concurrent publisher skips"
    );
    pause.release();
    worker.join().unwrap().unwrap();
    publish(&exchange, dir.path()).unwrap();
    assert!(read_publication(dir.path()).unwrap().points.is_empty());
    assert_eq!(counts(&exchange), (2, 2));
}
