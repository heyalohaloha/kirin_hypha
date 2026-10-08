//! Record JSON persistence; filesystem work remains outside Audio/Measure locks.
use super::{
    compute_checksum, refresh_record_quality, PluginDataFile, PluginDataWriter, WriterError,
};
use std::fs;
use std::path::{Path, PathBuf};

impl PluginDataWriter {
    pub(super) fn write_atomic(&mut self, target_path: PathBuf) -> Result<(), WriterError> {
        self.write_atomic_with_tmp(target_path, self.paths.tmp_path.clone())
    }

    pub(super) fn write_atomic_with_tmp(
        &mut self,
        target_path: PathBuf,
        tmp_path: PathBuf,
    ) -> Result<(), WriterError> {
        if let Some(parent) = target_path.parent() {
            if !parent.is_dir() {
                fs::create_dir_all(parent)?;
            }
        }
        self.data.checksum = compute_checksum(&self.data)?;
        let json = serde_json::to_string(&self.data)?;
        fs::write(&tmp_path, json.as_bytes())?;
        fs::rename(&tmp_path, target_path)?;
        Ok(())
    }
}

pub(super) fn write_plugin_data_atomic(
    path: &Path,
    data: &mut PluginDataFile,
) -> Result<(), WriterError> {
    if data.commit_status.is_some() {
        refresh_record_quality(data);
    }
    data.checksum = compute_checksum(data)?;
    let json = serde_json::to_vec(data)?;
    crate::atomic_file::write_bytes_atomic(path, &json)?;
    Ok(())
}
