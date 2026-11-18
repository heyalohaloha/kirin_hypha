//! Private non-RT sync fallback policy; retain the original OS error unchanged.
use std::io;

pub(super) fn run(
    mut primary: impl FnMut() -> io::Result<()>,
    fallback: impl FnOnce() -> io::Result<()>,
    report: impl FnOnce(&io::Error, &io::Error),
) -> io::Result<()> {
    loop {
        match primary() {
            Ok(()) => return Ok(()),
            Err(error) if error.kind() == io::ErrorKind::Interrupted => continue,
            Err(error) => {
                return match fallback() {
                    Ok(()) => Ok(()),
                    Err(fallback_error) => {
                        report(&error, &fallback_error);
                        Err(error)
                    }
                };
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    #[test]
    fn primary_success_never_calls_fallback_or_diagnostic() {
        run(
            || Ok(()),
            || panic!("unexpected fallback"),
            |_, _| panic!("unexpected report"),
        )
        .unwrap();
    }

    #[test]
    fn successful_fallback_remains_success_without_diagnostic() {
        let calls = Cell::new(0);
        run(
            || Err(io::Error::from_raw_os_error(13)),
            || {
                calls.set(calls.get() + 1);
                Ok(())
            },
            |_, _| panic!("unexpected report"),
        )
        .unwrap();
        assert_eq!(calls.get(), 1);
    }

    #[test]
    fn distinct_failures_are_reported_and_primary_os_error_is_preserved() {
        let calls = Cell::new(0);
        let expected = io::Error::from_raw_os_error(13);
        let result = run(
            || Err(io::Error::from_raw_os_error(13)),
            || Err(io::Error::from_raw_os_error(5)),
            |primary, fallback| {
                calls.set(calls.get() + 1);
                assert_eq!(primary.raw_os_error(), Some(13));
                assert_eq!(fallback.raw_os_error(), Some(5));
                assert_ne!(primary.to_string(), fallback.to_string());
            },
        )
        .unwrap_err();
        assert_eq!(calls.get(), 1);
        assert_eq!(result.kind(), expected.kind());
        assert_eq!(result.raw_os_error(), expected.raw_os_error());
        assert_eq!(result.to_string(), expected.to_string());
    }

    #[test]
    fn interrupted_primary_retries_without_calling_fallback() {
        let calls = Cell::new(0);
        run(
            || {
                calls.set(calls.get() + 1);
                if calls.get() < 3 {
                    Err(io::Error::from(io::ErrorKind::Interrupted))
                } else {
                    Ok(())
                }
            },
            || panic!("unexpected fallback"),
            |_, _| panic!("unexpected report"),
        )
        .unwrap();
        assert_eq!(calls.get(), 3);
    }

    #[test]
    fn interrupted_fallback_is_reported_once_not_retried() {
        let primary_calls = Cell::new(0);
        let fallback_calls = Cell::new(0);
        let reports = Cell::new(0);
        let result = run(
            || {
                primary_calls.set(primary_calls.get() + 1);
                if primary_calls.get() == 1 {
                    Err(io::Error::from(io::ErrorKind::Interrupted))
                } else {
                    Err(io::Error::from_raw_os_error(13))
                }
            },
            || {
                fallback_calls.set(fallback_calls.get() + 1);
                Err(io::Error::from(io::ErrorKind::Interrupted))
            },
            |primary, fallback| {
                reports.set(reports.get() + 1);
                assert_eq!(primary.raw_os_error(), Some(13));
                assert_eq!(fallback.kind(), io::ErrorKind::Interrupted);
            },
        )
        .unwrap_err();
        assert_eq!(primary_calls.get(), 2);
        assert_eq!(fallback_calls.get(), 1);
        assert_eq!(reports.get(), 1);
        assert_eq!(result.raw_os_error(), Some(13));
    }
}
