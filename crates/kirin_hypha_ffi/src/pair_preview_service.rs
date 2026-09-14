//! One lazy worker and one coalesced pending demand per loaded module. Not an engine worker.
use kirin_measure::pre_candidates::pair_preview::{self, Scope, Snapshot};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Condvar, Mutex, Weak};
#[cfg(any(not(windows), test))]
use std::thread;
#[cfg(not(windows))]
use std::thread::JoinHandle;
use std::time::{Duration, Instant};
#[cfg(windows)]
#[path = "pair_preview_windows.rs"]
mod windows;

pub struct Published {
    pub generation: u64,
    pub at: Instant,
    pub snapshot: Snapshot,
}
pub struct Ticket {
    pub scope: Scope,
    generation: AtomicU64,
    enabled: AtomicBool,
    result: Mutex<Option<Arc<Published>>>,
    last_request: Mutex<Option<Instant>>,
}
impl Ticket {
    pub fn new(scope: Scope) -> Arc<Self> {
        Arc::new(Self {
            scope,
            generation: AtomicU64::new(1),
            enabled: AtomicBool::new(true),
            result: Mutex::new(None),
            last_request: Mutex::new(None),
        })
    }
    pub fn cancel(&self) {
        self.enabled.store(false, Ordering::Release);
        self.generation.fetch_add(1, Ordering::AcqRel);
        if let Ok(mut result) = self.result.try_lock() {
            *result = None;
        }
    }
    pub fn poll(&self) -> Option<Arc<Published>> {
        if !self.enabled.load(Ordering::Acquire) {
            return None;
        }
        let result = self.result.try_lock().ok()?.clone()?;
        (result.generation == self.generation.load(Ordering::Acquire)
            && result.at.elapsed() <= Duration::from_secs(2))
        .then_some(result)
    }
    fn current(&self, generation: u64) -> bool {
        self.enabled.load(Ordering::Acquire)
            && self.generation.load(Ordering::Acquire) == generation
    }
}
struct Demand {
    ticket: Weak<Ticket>,
    generation: u64,
}
struct State {
    pending: Option<Demand>, // latest demand replaces the pending bit; no growing instance queue
    slots: [Option<Arc<Published>>; 8],
    #[cfg(windows)]
    running: bool,
}
pub struct Service {
    state: Mutex<State>,
    wake: Condvar,
    quit: AtomicBool,
    #[cfg(not(windows))]
    thread: Mutex<Option<JoinHandle<()>>>,
}
static SERVICE: Mutex<Option<Arc<Service>>> = Mutex::new(None);
pub fn shared() -> Option<Arc<Service>> {
    let mut service = SERVICE.try_lock().ok()?;
    Some(Arc::clone(service.get_or_insert_with(Service::start)))
}
pub fn shutdown() {
    // A Windows DLL detach must never wait on another thread or a poisoned process-exit lock.
    // Outstanding Windows callbacks own a module reference, so normal unload is already idle.
    let service = SERVICE
        .try_lock()
        .ok()
        .and_then(|mut service| service.take());
    if let Some(service) = service {
        service.shutdown();
    }
}
impl Service {
    fn start() -> Arc<Self> {
        let service = Arc::new(Self {
            state: Mutex::new(State {
                pending: None,
                slots: std::array::from_fn(|_| None),
                #[cfg(windows)]
                running: false,
            }),
            wake: Condvar::new(),
            quit: AtomicBool::new(false),
            #[cfg(not(windows))]
            thread: Mutex::new(None),
        });
        #[cfg(not(windows))]
        {
            let owned = Arc::clone(&service);
            match thread::Builder::new()
                .name("hypha-pair-preview".into())
                .spawn(move || owned.run())
            {
                Ok(thread) => {
                    *service.thread.lock().unwrap_or_else(|e| e.into_inner()) = Some(thread)
                }
                Err(_) => service.quit.store(true, Ordering::Release),
            }
        }
        service
    }
    pub fn request(self: &Arc<Self>, ticket: &Arc<Ticket>) -> bool {
        if self.quit.load(Ordering::Acquire) {
            return false;
        }
        let Ok(mut last) = ticket.last_request.try_lock() else {
            return false;
        };
        if last.is_some_and(|at| at.elapsed() < Duration::from_secs(1)) {
            return false;
        }
        let Ok(mut state) = self.state.try_lock() else {
            return false;
        };
        if self.quit.load(Ordering::Acquire) {
            return false;
        }
        ticket.enabled.store(true, Ordering::Release);
        let generation = ticket
            .generation
            .fetch_add(1, Ordering::AcqRel)
            .wrapping_add(1);
        *last = Some(Instant::now());
        state.pending = Some(Demand {
            ticket: Arc::downgrade(ticket),
            generation,
        });
        #[cfg(windows)]
        {
            let start = !state.running;
            state.running = true;
            drop(state);
            drop(last);
            // Acquiring a module reference must happen outside our locks (loader-lock order).
            if start && !windows::submit(Arc::clone(self)) {
                if let Ok(mut state) = self.state.lock() {
                    state.pending = None;
                    state.running = false;
                }
                return false;
            }
        }
        self.wake.notify_one();
        true
    }
    fn run(&self) {
        loop {
            let (demand, slot) = {
                let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
                #[cfg(not(windows))]
                while state.pending.is_none() && !self.quit.load(Ordering::Acquire) {
                    state = self.wake.wait(state).unwrap_or_else(|e| e.into_inner());
                }
                #[cfg(windows)]
                if state.pending.is_none() || self.quit.load(Ordering::Acquire) {
                    state.running = false;
                    return;
                }
                if self.quit.load(Ordering::Acquire) {
                    return;
                }
                let demand = state.pending.take().unwrap();
                let slot = state.slots.iter().position(|slot| {
                    slot.as_ref()
                        .is_none_or(|value| Arc::strong_count(value) == 1)
                });
                let Some(slot) = slot else {
                    continue;
                }; // all eight pinned: no ninth result allocation
                state.slots[slot] = None;
                (demand, slot)
            };
            let Some(ticket) = demand.ticket.upgrade() else {
                continue;
            };
            let cancelled =
                || self.quit.load(Ordering::Acquire) || !ticket.current(demand.generation);
            if cancelled() {
                continue;
            }
            let snapshot = pair_preview::scan(ticket.scope.clone(), &cancelled);
            if cancelled() || retained_bytes(&snapshot) > 120 * 1024 {
                continue;
            }
            let published = Arc::new(Published {
                generation: demand.generation,
                at: Instant::now(),
                snapshot,
            });
            {
                let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
                state.slots[slot] = Some(Arc::clone(&published));
            }
            if let Ok(mut result) = ticket.result.lock() {
                if ticket.current(demand.generation) {
                    *result = Some(published);
                }
            };
        }
    }
    fn shutdown(&self) {
        #[cfg(windows)]
        self.quit.store(true, Ordering::Release);
        #[cfg(not(windows))]
        {
            {
                let mut state = self.state.lock().unwrap_or_else(|e| e.into_inner());
                self.quit.store(true, Ordering::Release);
                state.pending = None;
                self.wake.notify_all();
            }
            let thread = self.thread.lock().unwrap_or_else(|e| e.into_inner()).take();
            // Invoked only by the loaded module's non-RT unload guard, never Ticket/engine Drop.
            if let Some(thread) = thread {
                let _ = thread.join();
            }
        }
    }
}
fn retained_bytes(snapshot: &Snapshot) -> usize {
    let scope = &snapshot.scope;
    std::mem::size_of::<Published>()
        + scope.root.capacity() * 4
        + scope.project.capacity()
        + scope.session.capacity()
        + scope.post.capacity()
        + snapshot.candidates.capacity() * std::mem::size_of::<pair_preview::Candidate>()
        + snapshot
            .candidates
            .iter()
            .map(|c| {
                c.id.capacity()
                    + c.name.as_ref().map_or(0, String::capacity)
                    + c.owner.as_ref().map_or(0, String::capacity)
            })
            .sum::<usize>()
}

#[cfg(test)]
#[path = "pair_preview_service_tests.rs"]
mod tests;
