//! Short, fixed user summaries. Error details are kept in debug logs, never in the notice.
use kirin_measure::{
    all_keep_signal::AllKeepError, storage::StorageError, CaptureGenerationError, SignalError,
};
use std::io::ErrorKind;
use std::sync::RwLock;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum KeepFailure {
    TimedOut,
    StorageAccess,
    StorageFull,
    StorageUnavailable,
    SetupChanged,
    WorkerUnavailable,
    ChoosePre,
    PreUnavailable,
    NoReadyPairs,
    Busy,
}

impl KeepFailure {
    pub(crate) fn message(self) -> &'static str {
        match self {
            Self::TimedOut => "PRE and POST timed out. Try Keep again.",
            Self::StorageAccess => "Cannot write Keep. Check folder access.",
            Self::StorageFull => "Keep storage is full. Free disk space.",
            Self::StorageUnavailable => "Keep files unavailable. Check save folder.",
            Self::SetupChanged => "Keep setup failed. Select PRE again.",
            Self::WorkerUnavailable => "Keep could not start. Try again.",
            Self::ChoosePre => "Choose a PRE before Keep.",
            Self::PreUnavailable => "PRE unavailable. Choose PRE again.",
            Self::NoReadyPairs => "No pairs ready. Select PRE, then retry.",
            Self::Busy => "Another Keep is active",
        }
    }

    pub(crate) fn from_generation(error: &CaptureGenerationError) -> Self {
        match error {
            CaptureGenerationError::Io(error) => match error.kind() {
                ErrorKind::TimedOut => Self::TimedOut,
                ErrorKind::WouldBlock => Self::Busy,
                ErrorKind::InvalidData | ErrorKind::InvalidInput | ErrorKind::AlreadyExists => {
                    Self::SetupChanged
                }
                kind => Self::from_storage_io(kind),
            },
            CaptureGenerationError::Serde(_) | CaptureGenerationError::Invalid => {
                Self::SetupChanged
            }
        }
    }

    pub(crate) fn from_signal(error: &SignalError) -> Self {
        match error {
            SignalError::Io(error) if error.kind() == ErrorKind::WouldBlock => Self::Busy,
            SignalError::Io(error)
                if matches!(
                    error.kind(),
                    ErrorKind::InvalidData | ErrorKind::InvalidInput | ErrorKind::AlreadyExists
                ) =>
            {
                Self::SetupChanged
            }
            SignalError::Io(error) => Self::from_storage_io(error.kind()),
            SignalError::Serde(_) => Self::SetupChanged,
        }
    }

    pub(crate) fn from_broadcast(error: &AllKeepError) -> Self {
        match error {
            AllKeepError::Io(error)
                if matches!(
                    error.kind(),
                    ErrorKind::InvalidData | ErrorKind::InvalidInput
                ) =>
            {
                Self::SetupChanged
            }
            AllKeepError::Io(error) => Self::from_storage_io(error.kind()),
            AllKeepError::Serde(_) => Self::SetupChanged,
        }
    }

    pub(crate) fn from_storage(error: &StorageError) -> Self {
        match error {
            StorageError::CreateDir(error)
            | StorageError::Write(error)
            | StorageError::Read(error)
            | StorageError::Rename(error) => Self::from_storage_io(error.kind()),
            StorageError::Parse(_) => Self::SetupChanged,
            StorageError::NoHome | StorageError::MissingEnv(_) => Self::StorageUnavailable,
        }
    }

    pub(crate) fn from_storage_io(kind: ErrorKind) -> Self {
        match kind {
            ErrorKind::PermissionDenied => Self::StorageAccess,
            ErrorKind::StorageFull => Self::StorageFull,
            _ => Self::StorageUnavailable,
        }
    }

    pub(crate) fn publish(self, notice: &RwLock<Option<String>>) {
        if let Ok(mut notice) = notice.write() {
            *notice = Some(self.message().to_string());
        }
    }

    pub(crate) fn report(
        self,
        notice: &RwLock<Option<String>>,
        _context: &str,
        _error: impl std::fmt::Display,
    ) {
        #[cfg(debug_assertions)]
        eprintln!("Keep preparation failed ({_context}): {_error}");
        self.publish(notice);
    }
}
