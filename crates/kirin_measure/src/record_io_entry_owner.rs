//! Claim identity retained by the IO caller only. No Audio/Measure/Stop access.
use super::RecordStateMachine;
impl RecordStateMachine {
    pub(crate) fn remember_io_entry_owner(&self, session: &str, generation: u64, owner: String) {
        if let Ok(mut slot) = self.io_entry_owner.try_lock() {
            *slot = Some((session.to_owned(), generation, owner));
        }
    }
    pub(crate) fn io_entry_owner(&self, session: &str, generation: u64) -> Option<String> {
        let slot = self.io_entry_owner.try_lock().ok()?;
        let (stored_session, stored_generation, owner) = slot.as_ref()?;
        (stored_session == session && *stored_generation == generation).then(|| owner.clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn retained_io_owner_is_exact_and_replaced_not_recovered_from_disk() {
        let sm = RecordStateMachine::new();
        assert_eq!(sm.io_entry_owner("one", 1), None);
        sm.remember_io_entry_owner("one", 1, "old".into());
        assert_eq!(sm.io_entry_owner("one", 1).as_deref(), Some("old"));
        assert_eq!(sm.io_entry_owner("other", 1), None);
        assert_eq!(sm.io_entry_owner("one", 2), None);
        sm.remember_io_entry_owner("two", 2, "new".into());
        assert_eq!(sm.io_entry_owner("one", 1), None);
        assert_eq!(sm.io_entry_owner("two", 2).as_deref(), Some("new"));
        // A fresh IO state never adopts a current file's owner implicitly.
        assert_eq!(RecordStateMachine::new().io_entry_owner("two", 2), None);
    }
    #[test]
    fn io_owner_contention_skips_without_waiting() {
        let sm = RecordStateMachine::new();
        let held = sm.io_entry_owner.try_lock().unwrap();
        sm.remember_io_entry_owner("one", 1, "owner".into());
        assert_eq!(sm.io_entry_owner("one", 1), None);
        drop(held);
        assert_eq!(sm.io_entry_owner("one", 1), None);
    }
}
