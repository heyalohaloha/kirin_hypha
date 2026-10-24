//! Synchronously publish a finite Record fixture without polling the spool worker.
use super::*;

impl RecordIngress {
    pub(crate) fn finish_fixture_capture(&self, generation: u64) {
        assert_eq!(self.armed_generation.load(Ordering::Acquire), generation);
        assert_eq!(self.audio_pushes_in_flight.load(Ordering::Acquire), 0);
        self.accepting_audio.store(false, Ordering::Release);
        let mut control = self.control.lock().unwrap();
        control.spool.as_mut().unwrap().finish_fixture();
    }
}
