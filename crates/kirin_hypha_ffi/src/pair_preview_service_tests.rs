use super::*;
fn scope(path: &std::path::Path, post: &str) -> Scope {
    Scope {
        root: path.to_owned(),
        host: std::process::id(),
        lifetime: 1,
        project: "project".into(),
        session: "session".into(),
        post: post.into(),
    }
}
fn wait_for(mut ready: impl FnMut() -> bool) {
    let until = Instant::now() + Duration::from_secs(2);
    while !ready() {
        assert!(Instant::now() < until, "discovery did not settle");
        thread::sleep(Duration::from_millis(2));
    }
}
#[test]
fn demand_is_debounced_cancelled_and_never_transfers_between_instances() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::start();
    let ticket = Ticket::new(scope(root.path(), "post-a"));
    wait_for(|| service.request(&ticket));
    assert!(!service.request(&ticket));
    wait_for(|| ticket.poll().is_some());
    let held = ticket.poll().unwrap();
    assert!(held.snapshot.stopped.is_none());
    assert!(held.snapshot.single_available().is_none());
    ticket.cancel();
    assert!(ticket.poll().is_none());
    let other = Ticket::new(scope(root.path(), "post-b"));
    assert!(other.poll().is_none());
    wait_for(|| service.request(&other));
    wait_for(|| other.poll().is_some());
    assert_eq!(other.poll().unwrap().snapshot.scope.post, "post-b");
    assert_eq!(held.snapshot.scope.post, "post-a");
    service.shutdown();
    assert!(!service.request(&other));
}
#[test]
fn eight_pinned_slots_cannot_allocate_a_ninth_result() {
    let root = tempfile::tempdir().unwrap();
    let service = Service::start();
    let pins: Vec<_> = (0..8)
        .map(|i| {
            Arc::new(Published {
                generation: 1,
                at: Instant::now(),
                snapshot: pair_preview::scan(scope(root.path(), &format!("post-{i}")), &|| false),
            })
        })
        .collect();
    {
        let mut state = service.state.lock().unwrap();
        for (slot, pin) in state.slots.iter_mut().zip(&pins) {
            *slot = Some(Arc::clone(pin));
        }
    }
    let ticket = Ticket::new(scope(root.path(), "new-post"));
    wait_for(|| service.request(&ticket));
    wait_for(|| service.state.lock().unwrap().pending.is_none());
    assert!(ticket.poll().is_none());
    let state = service.state.lock().unwrap();
    assert_eq!(state.slots.iter().filter(|s| s.is_some()).count(), 8);
    assert!(
        state
            .slots
            .iter()
            .flatten()
            .map(|p| retained_bytes(&p.snapshot))
            .sum::<usize>()
            < 1048576
    );
    drop(state);
    drop(pins);
    service.shutdown();
}
#[test]
fn pending_demand_is_one_latest_slot_and_ticket_drop_does_not_join() {
    let root = tempfile::tempdir().unwrap();
    // The same scheduler, held before its lazy worker starts, makes coalescing deterministic.
    let service = Arc::new(Service {
        state: Mutex::new(State {
            pending: None,
            slots: std::array::from_fn(|_| None),
            #[cfg(windows)]
            running: true, // deliberately held before a callback starts
        }),
        wake: Condvar::new(),
        quit: AtomicBool::new(false),
        #[cfg(not(windows))]
        thread: Mutex::new(None),
    });
    let mut latest = None;
    for i in 0..1000 {
        let ticket = Ticket::new(scope(root.path(), &format!("post-{i}")));
        assert!(service.request(&ticket));
        latest = Some(ticket);
    }
    let demand = service.state.lock().unwrap().pending.take().unwrap();
    assert_eq!(demand.ticket.upgrade().unwrap().scope.post, "post-999");
    latest.take().unwrap().cancel();
    assert!(demand.ticket.upgrade().is_none());
    service.shutdown();
}
