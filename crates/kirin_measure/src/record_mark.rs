//! Record MARK transport.
//!
//! GUI Thread enqueues a fixed user tag together with the latest exact producer
//! sample boundary published by [`RecordTakeTracker`]. POST IO Thread is the
//! only consumer and the only owner that mutates `PluginDataWriter`.

use crate::plugin_data::{Annotation, AnnotationMark};
use crate::record::RecordStateMachine;
use crate::record_take::RecordTakeTracker;
use chrono::Utc;
use std::collections::VecDeque;
use std::sync::{Arc, Mutex};
use uuid::Uuid;

pub const RECORD_MARK_BASIS: &str = "producer_content_samples_v1";
const RECORD_MARK_QUEUE_CAPACITY: usize = 256;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PendingRecordMark {
    pub generation: u64,
    pub annotation: Annotation,
}

pub type RecordMarkQueue = Arc<Mutex<VecDeque<PendingRecordMark>>>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecordMarkError {
    InvalidTag,
    NotRecording,
    ClockUnavailable,
    QueueFull,
    QueueUnavailable,
}

impl RecordMarkError {
    pub fn ui_message(self) -> &'static str {
        match self {
            Self::InvalidTag => "Mark unavailable",
            Self::NotRecording => "Mark requires Record",
            Self::ClockUnavailable => "Mark waiting for audio",
            Self::QueueFull | Self::QueueUnavailable => "Mark unavailable",
        }
    }
}

pub fn new_record_mark_queue() -> RecordMarkQueue {
    Arc::new(Mutex::new(VecDeque::with_capacity(
        RECORD_MARK_QUEUE_CAPACITY,
    )))
}

pub fn is_record_mark_tag(tag: &str) -> bool {
    matches!(tag, "Good" | "Fix" | "Hold")
}

/// Capture a MARK on the GUI Thread. The queue lock orders this operation with
/// POST close: a mark accepted before close is visible to its final drain; an
/// attempt after close observes Watch and is rejected.
pub fn enqueue_record_mark(
    record_sm: &RecordStateMachine,
    tracker: &RecordTakeTracker,
    queue: &RecordMarkQueue,
    tag: &str,
) -> Result<PendingRecordMark, RecordMarkError> {
    if !is_record_mark_tag(tag) {
        return Err(RecordMarkError::InvalidTag);
    }
    if !record_sm.is_recording() {
        return Err(RecordMarkError::NotRecording);
    }
    let generation = record_sm.generation();
    let mut pending = queue
        .lock()
        .map_err(|_| RecordMarkError::QueueUnavailable)?;
    if !record_sm.is_recording() || record_sm.generation() != generation {
        return Err(RecordMarkError::NotRecording);
    }
    // A new Record generation is a terminal boundary for any failed previous close. Those marks
    // can no longer be attached to the new session and must not consume its bounded queue budget.
    pending.retain(|mark| mark.generation == generation);
    if pending.len() >= RECORD_MARK_QUEUE_CAPACITY {
        return Err(RecordMarkError::QueueFull);
    }
    let point = tracker
        .record_mark_point(generation)
        .ok_or(RecordMarkError::ClockUnavailable)?;
    if !record_sm.is_recording() || record_sm.generation() != generation {
        return Err(RecordMarkError::NotRecording);
    }
    let mark = PendingRecordMark {
        generation,
        annotation: Annotation {
            t: Utc::now().to_rfc3339(),
            memo: tag.to_string(),
            mark: Some(AnnotationMark {
                schema_version: "record_mark.v1".to_string(),
                id: Uuid::new_v4().to_string(),
                basis: RECORD_MARK_BASIS.to_string(),
                producer_position_samples: point.position_samples,
                clock_source: point.source.as_str().unwrap_or("unknown").to_string(),
                wav_position_samples: None,
            }),
        },
    };
    pending.push_back(mark.clone());
    Ok(mark)
}

pub(crate) fn pending_record_marks_for_generation(
    queue: &RecordMarkQueue,
    generation: u64,
) -> Vec<PendingRecordMark> {
    queue
        .lock()
        .map(|pending| {
            pending
                .iter()
                .filter(|mark| mark.generation == generation)
                .cloned()
                .collect()
        })
        .unwrap_or_default()
}

pub(crate) fn acknowledge_record_marks(queue: &RecordMarkQueue, ids: &[String]) {
    if ids.is_empty() {
        return;
    }
    if let Ok(mut pending) = queue.lock() {
        pending.retain(|mark| {
            let id = mark.annotation.mark.as_ref().map(|mark| mark.id.as_str());
            !id.is_some_and(|id| ids.iter().any(|acked| acked == id))
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::identity::License;
    use crate::record_take::{CaptureClockSource, RecordTakeBlock};

    fn publish_record_point(sm: &RecordStateMachine, tracker: &RecordTakeTracker) {
        tracker.note_block(RecordTakeBlock {
            generation: sm.generation(),
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: 48_000,
            num_frames: 256,
            clock_start_samples: 48_000,
            clock_end_samples: None,
        });
        tracker.note_capture_window_with_source(
            true,
            48_000,
            256,
            CaptureClockSource::ProjectTimeline,
        );
    }

    #[test]
    fn mark_requires_current_record_clock_and_fixed_tag() {
        let sm = RecordStateMachine::new();
        let tracker = RecordTakeTracker::new();
        let queue = new_record_mark_queue();
        assert_eq!(
            enqueue_record_mark(&sm, &tracker, &queue, "Good"),
            Err(RecordMarkError::NotRecording)
        );
        sm.try_enter_record(License::Os).unwrap();
        assert_eq!(
            enqueue_record_mark(&sm, &tracker, &queue, "Custom"),
            Err(RecordMarkError::InvalidTag)
        );
        assert_eq!(
            enqueue_record_mark(&sm, &tracker, &queue, "Good"),
            Err(RecordMarkError::ClockUnavailable)
        );
    }

    #[test]
    fn mark_uses_exact_published_block_end_and_generation() {
        let sm = RecordStateMachine::new();
        let tracker = RecordTakeTracker::new();
        let queue = new_record_mark_queue();
        sm.try_enter_record(License::Os).unwrap();
        publish_record_point(&sm, &tracker);
        let queued = enqueue_record_mark(&sm, &tracker, &queue, "Fix").unwrap();
        assert_eq!(queued.generation, sm.generation());
        let mark = queued.annotation.mark.unwrap();
        assert_eq!(mark.producer_position_samples, 48_256);
        assert_eq!(mark.clock_source, "project_timeline");
        assert!(mark.wav_position_samples.is_none());
    }

    #[test]
    fn acknowledged_ids_do_not_remove_later_marks() {
        let sm = RecordStateMachine::new();
        let tracker = RecordTakeTracker::new();
        let queue = new_record_mark_queue();
        sm.try_enter_record(License::Os).unwrap();
        publish_record_point(&sm, &tracker);
        let first = enqueue_record_mark(&sm, &tracker, &queue, "Good").unwrap();
        let second = enqueue_record_mark(&sm, &tracker, &queue, "Hold").unwrap();
        let first_id = first.annotation.mark.unwrap().id;
        acknowledge_record_marks(&queue, &[first_id]);
        let remaining = pending_record_marks_for_generation(&queue, sm.generation());
        assert_eq!(remaining, vec![second]);
    }

    #[test]
    fn failed_previous_generation_cannot_exhaust_new_record_queue() {
        let sm = RecordStateMachine::new();
        let tracker = RecordTakeTracker::new();
        let queue = new_record_mark_queue();
        sm.try_enter_record(License::Os).unwrap();
        let first_generation = sm.generation();
        publish_record_point(&sm, &tracker);
        let old = enqueue_record_mark(&sm, &tracker, &queue, "Fix").unwrap();
        queue
            .lock()
            .unwrap()
            .resize(RECORD_MARK_QUEUE_CAPACITY, old);

        sm.exit_record();
        sm.try_enter_record(License::Os).unwrap();
        assert_ne!(sm.generation(), first_generation);
        publish_record_point(&sm, &tracker);
        let current = enqueue_record_mark(&sm, &tracker, &queue, "Good").unwrap();

        let pending = queue.lock().unwrap();
        assert_eq!(pending.len(), 1);
        assert_eq!(pending.front().unwrap(), &current);
    }
}
