//! Record JSON persistence; filesystem work remains outside Audio/Measure locks.
use super::{
    compute_checksum, hmac_key, refresh_record_quality, HmacSha256, PluginDataFile,
    PluginDataWriter, WriterError,
};
use hmac::Mac;
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
        let json = encode_with_checksum(&mut self.data)?;
        fs::write(&tmp_path, &json)?;
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
    let json = encode_with_checksum(data)?;
    crate::atomic_file::write_bytes_atomic(path, &json)?;
    Ok(())
}

const EMPTY_CHECKSUM_SUFFIX: &[u8] = b"\"checksum\":\"\"}";

/// The derived struct serializer puts the unconditional checksum last. HMAC signs that exact
/// empty-checksum JSON; its ASCII hex then occupies the same field without serializing twice.
fn encode_with_checksum(data: &mut PluginDataFile) -> Result<Vec<u8>, WriterError> {
    encode_using(data, serde_json::to_vec, serde_json::to_vec)
}

struct RestoreChecksum<'a> {
    data: &'a mut PluginDataFile,
    previous: String,
}

impl Drop for RestoreChecksum<'_> {
    fn drop(&mut self) {
        self.data.checksum = std::mem::take(&mut self.previous);
    }
}

fn encode_using(
    data: &mut PluginDataFile,
    serialize: impl FnOnce(&PluginDataFile) -> Result<Vec<u8>, serde_json::Error>,
    serialize_fallback: impl FnOnce(&PluginDataFile) -> Result<Vec<u8>, serde_json::Error>,
) -> Result<Vec<u8>, WriterError> {
    let previous = std::mem::take(&mut data.checksum);
    let mut guard = RestoreChecksum { data, previous };
    let mut bytes = serialize(&*guard.data)?;
    if !bytes.ends_with(EMPTY_CHECKSUM_SUFFIX) {
        // A future unsupported serializer shape retains the legacy path; never search nested
        // user strings or publish bytes whose final checksum field cannot be established.
        guard.data.checksum = compute_checksum(guard.data)?;
        let bytes = serialize_fallback(&*guard.data)?;
        guard.previous = std::mem::take(&mut guard.data.checksum);
        return Ok(bytes);
    }
    let mut mac =
        HmacSha256::new_from_slice(hmac_key()).expect("HMAC-SHA256 accepts any key length");
    mac.update(&bytes);
    let checksum = hex::encode(mac.finalize().into_bytes());
    bytes.truncate(bytes.len() - 2);
    bytes.extend_from_slice(checksum.as_bytes());
    bytes.extend_from_slice(b"\"}");
    guard.previous = checksum;
    Ok(bytes)
}

#[cfg(test)]
#[path = "plugin_data_write_tests.rs"]
mod tests;
