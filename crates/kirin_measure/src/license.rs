//! License gating — `identity.license` による機能分岐（G-50-46 / G-50-47）。
//!
//!
//! # 機能マトリクス
//! | 機能 | `os` | `sense` | `unknown` |
//! |------|------|---------|-----------|
//! | Watch 計測（LUFS-M / TP / Crest） | ✅ | ✅ | ✅ |
//! | Record 計測 | ✅ | ❌ | ❌ |
//! | plugin_data/ 書込 | ✅ | ❌ | ❌ |
//! | preset/ 読込 | ✅ | ❌ | ❌ |
//! | 「残す」ボタン | ✅ | ❌ | ❌ |
//! | 「記録を止める」 | Record 時のみ | ❌ | ❌ |
//! | 「コピー」 | ✅ | ✅ | ✅ |
//! | 「メモを残す」 | Record 時のみ | ❌ | ❌ |
//!
//! # 保険（E-21）
//! `can_enter_record` が false の時に内部から Record 遷移が呼ばれたら即拒否。
//! GUI ボタン非表示だけでなくロジック側でも二重 gate する。
//!
//! # Sense 案内
//! Sense 版で「残す」位置に表示する 1 行案内を [`SENSE_RECORD_HINT`] で提供。
//! タップ時遷移先 URL は [`SENSE_UPSELL_URL`]。

use crate::identity::License;
use std::path::Path;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::Arc;

const LICENSE_OS: u8 = 0;
const LICENSE_SENSE: u8 = 1;
const LICENSE_UNKNOWN: u8 = 2;

/// A live entitlement shared by UI, Record state and IO workers.
///
/// The atomic contains only a compact enum code. Filesystem observation stays
/// outside the audio thread; readers can safely query the current value from
/// any non-RT or RT context without allocation or locking.
#[derive(Debug, Clone)]
pub struct LiveLicense {
    code: Arc<AtomicU8>,
}

impl LiveLicense {
    pub fn new(initial: License) -> Self {
        Self {
            code: Arc::new(AtomicU8::new(license_code(initial))),
        }
    }

    pub fn load(&self) -> License {
        license_from_code(self.code.load(Ordering::Acquire))
    }

    pub fn store(&self, license: License) {
        self.code.store(license_code(license), Ordering::Release);
    }

    /// Refresh from Kirin OS storage. This performs filesystem IO and must be
    /// called only from GUI, message or IO threads.
    pub fn refresh_from_disk(&self) -> License {
        let observed = load_license_safe();
        self.store(observed);
        observed
    }
}

impl From<License> for LiveLicense {
    fn from(value: License) -> Self {
        Self::new(value)
    }
}

impl From<Arc<License>> for LiveLicense {
    fn from(value: Arc<License>) -> Self {
        Self::new(*value)
    }
}

fn license_code(license: License) -> u8 {
    match license {
        License::Os => LICENSE_OS,
        License::Sense => LICENSE_SENSE,
        License::Unknown => LICENSE_UNKNOWN,
    }
}

fn license_from_code(code: u8) -> License {
    match code {
        LICENSE_OS => License::Os,
        LICENSE_SENSE => License::Sense,
        _ => License::Unknown,
    }
}

/// `identity.json` から license を安全に読み込む（非RTの定期更新用）。
///
/// - 本番パス: `StoragePaths::default_platform().primary_path()`
/// - primary のファイル不在 / 不正 JSON / field 欠落は secondary を read-only fallback
/// - platform path 解決不能 / 両方不在 / 未知値 → `License::Unknown`
/// - 常に成功する（`Result` を返さない）。UI/IO thread から定期的に再読込できる。
///
/// # loose パース
/// `Identity` 構造体は 8 フィールド必須（HMAC 署名含む）だが、本関数は
/// **`license` フィールドだけ抽出する loose 仕様**。理由:
/// - GUI は HMAC 検証や 2-of-3 判定を行わない（本番検証は `storage::load_or_recover` で別途実施）
/// - Phase 1.0 手動テストや Kirin OS 本体未完成時点でも license 値は独立して読める必要あり
pub fn load_license_safe() -> License {
    let paths = match crate::storage::StoragePaths::default_platform() {
        Ok(p) => p,
        Err(_) => {
            log::info!("[license] loaded: Unknown (platform path unresolved)");
            return License::Unknown;
        }
    };
    if let Some(license) = try_load_license_from(&paths.primary_path()) {
        return license;
    }
    // Read-only recovery. The plugin never creates or upgrades identity; it
    // merely avoids remaining Unknown when Kirin OS already left its mirrored
    // identity and the primary file is temporarily unavailable.
    try_load_license_from(&paths.secondary_path()).unwrap_or(License::Unknown)
}

/// 任意パスから license を loose 抽出（テスト・`load_license_safe` 共用）。
#[cfg(test)]
pub(crate) fn load_license_from(path: &Path) -> License {
    try_load_license_from(path).unwrap_or(License::Unknown)
}

fn try_load_license_from(path: &Path) -> Option<License> {
    let text = match std::fs::read_to_string(path) {
        Ok(t) => t,
        Err(_) => {
            log::info!(
                "[license] loaded: Unknown (file missing or unreadable: {})",
                path.display()
            );
            return None;
        }
    };
    let value: serde_json::Value = match serde_json::from_str(&text) {
        Ok(v) => v,
        Err(e) => {
            log::info!("[license] loaded: Unknown (JSON parse error: {})", e);
            return None;
        }
    };
    match value.get("license").and_then(|v| v.as_str()) {
        Some(s) => {
            let license = License::parse_loose(s);
            log::info!("[license] loaded: {:?} (raw: {:?})", license, s);
            Some(license)
        }
        None => {
            log::info!("[license] loaded: Unknown (license field missing)");
            None
        }
    }
}

/// Record モードへ遷移してよいか（G-50-47 保険: 未知 license は安全側で拒否）。
pub fn can_enter_record(license: License) -> bool {
    matches!(license, License::Os)
}

/// plugin_data/ へ書込してよいか。Record と同条件。
pub fn can_write_plugin_data(license: License) -> bool {
    matches!(license, License::Os)
}

/// preset/ から読込 + LED 点灯してよいか。Record と同条件。
pub fn can_read_preset(license: License) -> bool {
    matches!(license, License::Os)
}

/// 「残す」ボタンを GUI に表示してよいか。
pub fn show_save_button(license: License) -> bool {
    matches!(license, License::Os)
}

/// 「記録を止める」ボタンを GUI に表示してよいか（Record 中のみ表示するかは呼び出し元の責務）。
pub fn show_stop_record_button(license: License) -> bool {
    matches!(license, License::Os)
}

/// 「メモを残す」ボタンを GUI に表示してよいか（Record 中のみ表示するかは呼び出し元の責務）。
pub fn show_note_button(license: License) -> bool {
    matches!(license, License::Os)
}

/// Sense 版「Keep」位置に表示する 1 行案内（R-28 準拠: 控えめ）。
pub const SENSE_RECORD_HINT: &str = "Record mode available in Kirin OS";

/// Sense 版案内タップ時の遷移先 URL。
pub const SENSE_UPSELL_URL: &str = "https://kirinmastering.com";

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static LOOSE_COUNTER: AtomicUsize = AtomicUsize::new(0);

    fn isolated_path(name: &str) -> PathBuf {
        let n = LOOSE_COUNTER.fetch_add(1, Ordering::SeqCst);
        let pid = std::process::id();
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let root = std::env::temp_dir()
            .join("kirin_hypha_license_test")
            .join(format!("{}-{}-{}", pid, now, n));
        let _ = std::fs::remove_dir_all(&root);
        std::fs::create_dir_all(&root).unwrap();
        root.join(name)
    }

    #[test]
    fn loose_parses_os_with_license_field_only() {
        let path = isolated_path("identity.json");
        std::fs::write(&path, r#"{"license": "os"}"#).unwrap();
        assert_eq!(load_license_from(&path), License::Os);
    }

    #[test]
    fn loose_parses_sense_with_license_field_only() {
        let path = isolated_path("identity.json");
        std::fs::write(&path, r#"{"license": "sense"}"#).unwrap();
        assert_eq!(load_license_from(&path), License::Sense);
    }

    #[test]
    fn loose_parses_trial_as_unknown() {
        let path = isolated_path("identity.json");
        std::fs::write(&path, r#"{"license": "trial"}"#).unwrap();
        assert_eq!(load_license_from(&path), License::Unknown);
    }

    #[test]
    fn loose_invalid_json_falls_back_to_unknown() {
        let path = isolated_path("identity.json");
        std::fs::write(&path, "not a json").unwrap();
        assert_eq!(load_license_from(&path), License::Unknown);
    }

    #[test]
    fn loose_missing_file_falls_back_to_unknown() {
        let path = isolated_path("does_not_exist.json");
        assert_eq!(load_license_from(&path), License::Unknown);
    }

    #[test]
    fn loose_missing_license_field_falls_back_to_unknown() {
        let path = isolated_path("identity.json");
        std::fs::write(&path, r#"{"other_field": "value"}"#).unwrap();
        assert_eq!(load_license_from(&path), License::Unknown);
    }

    #[test]
    fn loose_reads_license_from_full_identity_schema() {
        // 本番形式（Identity::to_json_pretty と同等の完全スキーマ）でも読める
        let path = isolated_path("identity.json");
        let full = r#"{
            "schema_version": "1.0",
            "installation_id": "dummy",
            "hardware_id": "dummy",
            "hardware_components": {"iop": "a", "sn": "b", "bd": "c"},
            "machine_signature": "dummy",
            "license": "os",
            "created_at": "2026-04-17T00:00:00Z",
            "last_verified_at": "2026-04-17T00:00:00Z"
        }"#;
        std::fs::write(&path, full).unwrap();
        assert_eq!(load_license_from(&path), License::Os);
    }

    #[test]
    fn live_license_updates_every_shared_reader_without_recreation() {
        let live = LiveLicense::new(License::Unknown);
        let ui_reader = live.clone();
        let io_reader = live.clone();
        live.store(License::Os);
        assert_eq!(ui_reader.load(), License::Os);
        assert_eq!(io_reader.load(), License::Os);
        io_reader.store(License::Sense);
        assert_eq!(live.load(), License::Sense);
    }

    #[test]
    fn os_enables_record_features() {
        assert!(can_enter_record(License::Os));
        assert!(can_write_plugin_data(License::Os));
        assert!(can_read_preset(License::Os));
        assert!(show_save_button(License::Os));
        assert!(show_stop_record_button(License::Os));
        assert!(show_note_button(License::Os));
    }

    #[test]
    fn sense_blocks_record_features() {
        assert!(!can_enter_record(License::Sense));
        assert!(!can_write_plugin_data(License::Sense));
        assert!(!can_read_preset(License::Sense));
        assert!(!show_save_button(License::Sense));
        assert!(!show_stop_record_button(License::Sense));
        assert!(!show_note_button(License::Sense));
    }

    #[test]
    fn unknown_defaults_to_safe_side() {
        assert!(!can_enter_record(License::Unknown));
        assert!(!can_write_plugin_data(License::Unknown));
        assert!(!can_read_preset(License::Unknown));
        assert!(!show_save_button(License::Unknown));
        assert!(!show_stop_record_button(License::Unknown));
        assert!(!show_note_button(License::Unknown));
    }

    #[test]
    fn sense_hint_constants_present() {
        assert!(!SENSE_RECORD_HINT.is_empty());
        assert!(SENSE_UPSELL_URL.starts_with("https://"));
    }
}
