//! HMAC checksum of one plugin_data file (deterrent level, key embedded at build time).
use super::{HmacSha256, PluginDataFile};
use hmac::Mac;

/// `checksum=""` にした状態の JSON バイト列に対して HMAC-SHA256 を計算。
///
/// B-026 / Gap-9: 同 crate 内の `record_writer::sweep_stale_active_at_startup`
/// が startup sweep 時に再計算で利用する。HMAC 鍵 (`hmac_key`) は plugin_data
/// 内で閉じたままにしたいため `pub` ではなく `pub(crate)`。
pub(crate) fn compute_checksum(data: &PluginDataFile) -> Result<String, serde_json::Error> {
    let mut clone = data.clone();
    clone.checksum = String::new();
    let bytes = serde_json::to_vec(&clone)?;
    let mut mac =
        HmacSha256::new_from_slice(hmac_key()).expect("HMAC-SHA256 accepts any key length");
    mac.update(&bytes);
    Ok(hex::encode(mac.finalize().into_bytes()))
}

/// HMAC 鍵（identity.rs と同じ方針。ビルド時埋め込み）。
pub(crate) fn hmac_key() -> &'static [u8] {
    match option_env!("KIRIN_HYPHA_HMAC_KEY") {
        Some(k) => k.as_bytes(),
        None => DEFAULT_HMAC_KEY,
    }
}

const DEFAULT_HMAC_KEY: &[u8] = b"kirin-hypha-phase1.0-hmac-key-deterrent-level-20260417";

/// 検証用: 読み込んだ `PluginDataFile` の `checksum` が整合するか。
pub fn verify_checksum(data: &PluginDataFile) -> bool {
    match compute_checksum(data) {
        Ok(expected) => constant_time_eq(expected.as_bytes(), data.checksum.as_bytes()),
        Err(_) => false,
    }
}

/// 定数時間比較。
fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}
