use super::budget::{Budget, Stop};
use std::fs;
use std::path::Path;

pub(super) fn identity(path: &Path, budget: &mut Budget<'_>) -> Result<(u64, u64), Stop> {
    let metadata = budget.operation(|| fs::symlink_metadata(path))?;
    if !metadata.is_dir() {
        return Err(Stop::Uncertain);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::MetadataExt;
        Ok((metadata.dev(), metadata.ino()))
    }
    #[cfg(windows)]
    {
        use std::fs::OpenOptions;
        use std::os::windows::{fs::OpenOptionsExt, io::AsRawHandle};
        use windows_sys::Win32::Storage::FileSystem::{
            GetFileInformationByHandle, BY_HANDLE_FILE_INFORMATION, FILE_ATTRIBUTE_REPARSE_POINT,
            FILE_FLAG_BACKUP_SEMANTICS, FILE_FLAG_OPEN_REPARSE_POINT,
        };
        // Stable API: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle
        let file = budget.operation(|| {
            OpenOptions::new()
                .read(true)
                .custom_flags(FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT)
                .open(path)
        })?;
        let mut info: BY_HANDLE_FILE_INFORMATION = unsafe { std::mem::zeroed() };
        budget.operation(|| {
            if unsafe { GetFileInformationByHandle(file.as_raw_handle(), &mut info) } != 0 {
                Ok(())
            } else {
                Err(std::io::Error::last_os_error())
            }
        })?;
        if info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            return Err(Stop::Uncertain);
        }
        Ok((
            info.dwVolumeSerialNumber as u64,
            ((info.nFileIndexHigh as u64) << 32) | info.nFileIndexLow as u64,
        ))
    }
    #[cfg(not(any(unix, windows)))]
    {
        Err(Stop::Uncertain)
    }
}
