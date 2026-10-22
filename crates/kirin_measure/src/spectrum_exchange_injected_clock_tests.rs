//! Lease boundaries use fixture time while filesystem transactions use real synchronization.
use super::*;

#[test]
#[cfg(not(windows))]
fn an_expired_publication_repairs_the_current_owner_with_a_fresh_lease() {
    let fixture = Fixture::new();
    fixture.clock.advance(REQUEST_RENEW_INTERVAL);
    let pause =
        crate::atomic_file::AtomicWritePause::install(request_path(&fixture.target.instance_dir));
    let post = Arc::clone(&fixture.post);
    let target = fixture.target.clone();
    let pending = std::thread::spawn(move || post.post_tick("post", Some(target)));
    pause.wait_until_entered();
    fixture
        .clock
        .advance(Duration::from_millis((REQUEST_LEASE_MS + 1) as u64));
    pause.release();
    assert!(
        !pending.join().unwrap(),
        "the expired transaction is rejected"
    );
    let renewed = validated_request(
        &fixture.target.instance_dir,
        "pre",
        48_000,
        fixture.post.clock_unix_ms(),
    )
    .expect("repair publishes the same current owner with a new lease");
    let current = fixture.post.post_session.lock().unwrap();
    assert_eq!(renewed.request_id, current.as_ref().unwrap().request_id);
}
