use super::*;
use kirin_measure::SignalError;
use std::io::{Error, ErrorKind};

fn feedback(all: bool) -> KeepPreparationFeedback {
    KeepPreparationFeedback {
        control: Arc::new(Mutex::new(())),
        notice: Arc::new(RwLock::new(None)),
        phase: Arc::new(AtomicU8::new(KIRIN_KEEP_PHASE_PREPARING)),
        phase_generation: Arc::new(AtomicI64::new(7)),
        generation_started_at_ms: 7,
        all,
        after_generation_check: None,
    }
}

#[test]
fn keep_and_all_keep_report_readiness_timeout_with_a_retry() {
    let notices = [false, true].map(|all| {
        let feedback = feedback(all);
        feedback.complete(Err(CaptureGenerationError::Io(Error::new(
            ErrorKind::TimedOut,
            "private producer details must stay in the debug log",
        ))));
        assert_eq!(
            feedback.phase.load(Ordering::Acquire),
            KIRIN_KEEP_PHASE_IDLE
        );
        let notice = feedback.notice.read().unwrap().clone();
        notice
    });
    assert_eq!(
        notices.each_ref().map(Option::as_deref),
        [
            Some("PRE and POST timed out. Try Keep again."),
            Some("PRE and POST timed out. Try Keep again."),
        ]
    );
}

#[test]
fn keep_and_all_keep_report_storage_access_failure() {
    let notices = [false, true].map(|all| {
        let feedback = feedback(all);
        feedback.complete(Err(CaptureGenerationError::Io(Error::new(
            ErrorKind::PermissionDenied,
            "private save location and diagnostic details",
        ))));
        let notice = feedback.notice.read().unwrap().clone();
        notice
    });
    assert_eq!(
        notices.each_ref().map(Option::as_deref),
        [
            Some("Cannot write Keep. Check folder access."),
            Some("Cannot write Keep. Check folder access."),
        ]
    );
}

#[test]
fn successful_preparation_arms_without_a_failure_notice() {
    for all in [false, true] {
        let feedback = feedback(all);
        feedback.complete(Ok(()));
        assert_eq!(
            feedback.phase.load(Ordering::Acquire),
            KIRIN_KEEP_PHASE_ARMED
        );
        assert!(feedback.notice.read().unwrap().is_none());
    }
}

#[test]
fn an_old_preparation_cannot_replace_the_current_notice_or_phase() {
    for all in [false, true] {
        let feedback = feedback(all);
        feedback.phase_generation.store(8, Ordering::Release);
        *feedback.notice.write().unwrap() = Some("current action".into());
        feedback.complete(Err(CaptureGenerationError::Invalid));
        feedback.dispatch_failed(
            &CaptureGenerationError::Invalid,
            &feedback.control.lock().unwrap(),
        );
        feedback.complete(Ok(()));
        assert_eq!(
            feedback.phase.load(Ordering::Acquire),
            KIRIN_KEEP_PHASE_PREPARING
        );
        assert_eq!(
            feedback.notice.read().unwrap().as_deref(),
            Some("current action")
        );
    }
}

#[test]
fn preparation_error_kinds_have_fixed_private_detail_free_summaries() {
    for (kind, expected) in [
        (
            ErrorKind::TimedOut,
            "PRE and POST timed out. Try Keep again.",
        ),
        (
            ErrorKind::PermissionDenied,
            "Cannot write Keep. Check folder access.",
        ),
        (
            ErrorKind::StorageFull,
            "Keep storage is full. Free disk space.",
        ),
        (
            ErrorKind::NotFound,
            "Keep files unavailable. Check save folder.",
        ),
        (
            ErrorKind::Other,
            "Keep files unavailable. Check save folder.",
        ),
        (ErrorKind::WouldBlock, "Another Keep is active"),
        (
            ErrorKind::InvalidData,
            "Keep setup failed. Select PRE again.",
        ),
        (
            ErrorKind::InvalidInput,
            "Keep setup failed. Select PRE again.",
        ),
        (
            ErrorKind::AlreadyExists,
            "Keep setup failed. Select PRE again.",
        ),
    ] {
        for all in [false, true] {
            let feedback = feedback(all);
            let detail = "diagnostic-only: 識別子 and save location; ".repeat(16);
            assert!(detail.len() > 128);
            feedback.complete(Err(CaptureGenerationError::Io(Error::new(kind, detail))));
            let notice = feedback.notice.read().unwrap().clone().unwrap();
            assert_eq!(notice, expected, "error kind {kind:?}, All Keep={all}");
            assert!(notice.len() < 128, "leave room for the FFI terminator");
        }
    }
}

#[test]
fn invalid_or_unreadable_preparation_data_requests_pair_selection() {
    for all in [false, true] {
        let feedback = feedback(all);
        for error in [
            CaptureGenerationError::Invalid,
            CaptureGenerationError::Serde(
                serde_json::from_str::<serde_json::Value>("{").unwrap_err(),
            ),
        ] {
            feedback.complete(Err(error));
            assert_eq!(
                feedback.notice.read().unwrap().as_deref(),
                Some("Keep setup failed. Select PRE again.")
            );
        }
    }
}

#[test]
fn worker_start_failure_requests_retry_without_claiming_a_storage_fault() {
    for all in [false, true] {
        let feedback = feedback(all);
        feedback.dispatch_failed(
            &CaptureGenerationError::Io(Error::new(
                ErrorKind::PermissionDenied,
                "worker creation was denied",
            )),
            &feedback.control.lock().unwrap(),
        );
        assert_eq!(
            feedback.phase.load(Ordering::Acquire),
            KIRIN_KEEP_PHASE_IDLE
        );
        assert_eq!(
            feedback.notice.read().unwrap().as_deref(),
            Some("Keep could not start. Try again.")
        );
    }
}

#[test]
fn control_error_variants_preserve_their_cause() {
    use kirin_measure::{all_keep_signal::AllKeepError, storage::StorageError};
    let parse_error = || serde_json::from_str::<serde_json::Value>("{").unwrap_err();
    assert_eq!(
        KeepFailure::from_signal(&SignalError::Io(ErrorKind::WouldBlock.into())),
        KeepFailure::Busy
    );
    assert_eq!(
        KeepFailure::from_signal(&SignalError::Io(ErrorKind::InvalidData.into())),
        KeepFailure::SetupChanged
    );
    assert_eq!(
        KeepFailure::from_signal(&SignalError::Io(ErrorKind::AlreadyExists.into())),
        KeepFailure::SetupChanged
    );
    assert_eq!(
        KeepFailure::from_signal(&SignalError::Io(ErrorKind::PermissionDenied.into())),
        KeepFailure::StorageAccess
    );
    assert_eq!(
        KeepFailure::from_signal(&SignalError::Serde(parse_error())),
        KeepFailure::SetupChanged
    );
    assert_eq!(
        KeepFailure::from_broadcast(&AllKeepError::Io(ErrorKind::PermissionDenied.into())),
        KeepFailure::StorageAccess
    );
    assert_eq!(
        KeepFailure::from_broadcast(&AllKeepError::Io(ErrorKind::InvalidData.into())),
        KeepFailure::SetupChanged
    );
    assert_eq!(
        KeepFailure::from_broadcast(&AllKeepError::Serde(parse_error())),
        KeepFailure::SetupChanged
    );
    for error in [
        StorageError::CreateDir(ErrorKind::PermissionDenied.into()),
        StorageError::Write(ErrorKind::PermissionDenied.into()),
        StorageError::Read(ErrorKind::PermissionDenied.into()),
        StorageError::Rename(ErrorKind::PermissionDenied.into()),
    ] {
        assert_eq!(
            KeepFailure::from_storage(&error),
            KeepFailure::StorageAccess
        );
    }
    assert_eq!(
        KeepFailure::from_storage(&StorageError::Write(ErrorKind::StorageFull.into())),
        KeepFailure::StorageFull
    );
    assert_eq!(
        KeepFailure::from_storage(&StorageError::NoHome),
        KeepFailure::StorageUnavailable
    );
    assert_eq!(
        KeepFailure::from_storage(&StorageError::MissingEnv("SAVE_ROOT")),
        KeepFailure::StorageUnavailable
    );
    assert_eq!(
        KeepFailure::from_storage(&StorageError::Parse(parse_error())),
        KeepFailure::SetupChanged
    );
}

#[test]
fn fixed_summaries_cross_the_shell_notice_buffer_once_without_truncation() {
    use std::ffi::CStr;
    let mut engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    for reason in [
        KeepFailure::TimedOut,
        KeepFailure::StorageAccess,
        KeepFailure::StorageFull,
        KeepFailure::StorageUnavailable,
        KeepFailure::SetupChanged,
        KeepFailure::WorkerUnavailable,
        KeepFailure::ChoosePre,
        KeepFailure::PreUnavailable,
        KeepFailure::NoReadyPairs,
        KeepFailure::Busy,
    ] {
        reason.publish(&engine.keep_action_notice);
        let mut buffer = [0x7f as c_char; 128];
        assert!(unsafe {
            kirin_hypha_drain_keep_action_notice(&mut engine, buffer.as_mut_ptr(), buffer.len())
        });
        let bytes = buffer.map(|byte| byte as u8);
        let received = CStr::from_bytes_until_nul(&bytes)
            .expect("notice must terminate inside the 128-byte buffer")
            .to_str()
            .unwrap();
        assert_eq!(received, reason.message());
        assert!(received.len() < buffer.len());
        assert!(!unsafe {
            kirin_hypha_drain_keep_action_notice(&mut engine, buffer.as_mut_ptr(), buffer.len())
        });
        assert!(
            engine.record_error_message().is_none(),
            "a preparation result is a one-shot action notice"
        );
    }
}
