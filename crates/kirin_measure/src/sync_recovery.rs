//! Poison recovery for non-real-time worker state.
//!
//! These locks are never taken by the Audio Thread. A Measure Thread panic may poison a shared
//! result mutex; recovering only the guard is insufficient because the poison flag would keep the
//! restarted producer from publishing forever. Worker-side recovery therefore clears the flag at
//! the same boundary where the last complete value is recovered.

use std::sync::{Mutex, MutexGuard};

pub(crate) fn lock_recover<'a, T>(mutex: &'a Mutex<T>, context: &str) -> MutexGuard<'a, T> {
    match mutex.lock() {
        Ok(guard) => guard,
        Err(poisoned) => {
            log::warn!("[{context}] recovered poisoned worker-state mutex");
            mutex.clear_poison();
            poisoned.into_inner()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    #[test]
    fn recovery_clears_poison_for_the_restarted_producer() {
        let value = Arc::new(Mutex::new(7_u32));
        let poison_target = Arc::clone(&value);
        let _ = std::thread::spawn(move || {
            let _guard = poison_target.lock().unwrap();
            panic!("poison fixture");
        })
        .join();
        assert!(value.is_poisoned());

        *lock_recover(&value, "test") = 8;

        assert!(!value.is_poisoned());
        assert_eq!(*value.lock().unwrap(), 8);
    }
}
