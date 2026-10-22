//! Non-RT exchange clock. Fixtures can advance lease time independently of worker scheduling.
use super::*;

impl SpectrumCoordinator {
    pub(super) fn clock_now(&self) -> Instant {
        #[cfg(test)]
        if let Some(clock) = self.test_clock.lock().unwrap().as_ref() {
            return clock.now();
        }
        Instant::now()
    }
    pub(super) fn clock_unix_ms(&self) -> i64 {
        #[cfg(test)]
        if let Some(clock) = self.test_clock.lock().unwrap().as_ref() {
            return clock.unix_ms();
        }
        unix_ms_now()
    }
    #[cfg(test)]
    pub(super) fn use_test_clock(&self, clock: &Arc<TestClock>) {
        *self.test_clock.lock().unwrap() = Some(Arc::clone(clock));
    }
}

#[cfg(test)]
pub(super) struct TestClock {
    origin: Instant,
    unix_origin: i64,
    millis: AtomicU64,
}
#[cfg(test)]
impl TestClock {
    pub(super) fn new() -> Arc<Self> {
        Arc::new(Self {
            origin: Instant::now(),
            unix_origin: unix_ms_now(),
            millis: AtomicU64::new(0),
        })
    }
    pub(super) fn advance(&self, duration: Duration) {
        self.millis
            .fetch_add(duration.as_millis().try_into().unwrap(), Ordering::AcqRel);
    }
    fn now(&self) -> Instant {
        self.origin + Duration::from_millis(self.millis.load(Ordering::Acquire))
    }
    fn unix_ms(&self) -> i64 {
        self.unix_origin + self.millis.load(Ordering::Acquire) as i64
    }
}
