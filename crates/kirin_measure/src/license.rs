//! License gating — `identity.license` による機能分岐（G-50-46 / G-50-47）。
//!
//! .md T-5 対応。
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

/// `identity.json` から license を安全に読み込む（GUI 起動時用）。
///
/// - 本番パス: `~/Library/Application Support/Kirin OS/identity.json`
/// - `$HOME` 不在 / ファイル不在 / 不正 JSON / license フィールド欠落 / 未知値 → `License::Unknown`
/// - 常に成功する（`Result` を返さない）。GUI が起動時に 1 回だけ呼ぶ想定。
/// - 降格（例 Os → Sense）の即時反映は Step 4 T-6 で別途実装。
///
/// # loose パース
/// `Identity` 構造体は 8 フィールド必須（HMAC 署名含む）だが、本関数は
/// **`license` フィールドだけ抽出する loose 仕様**。理由:
/// - GUI は HMAC 検証や 2-of-3 判定を行わない（本番検証は `storage::load_or_recover` で別途実施）
/// - Phase 1.0 手動テストや Kirin OS 本体未完成時点でも license 値は独立して読める必要あり
pub fn load_license_safe() -> License {
    let paths = match crate::storage::StoragePaths::default_macos() {
        Ok(p) => p,
        Err(_) => {
            log::info!("[license] loaded: Unknown (no $HOME)");
            return License::Unknown;
        }
    };
    load_license_from(&paths.primary_path())
}

/// 任意パスから license を loose 抽出（テスト・`load_license_safe` 共用）。
pub(crate) fn load_license_from(path: &Path) -> License {
    let text = match std::fs::read_to_string(path) {
        Ok(t) => t,
        Err(_) => {
            log::info!(
                "[license] loaded: Unknown (file missing or unreadable: {})",
                path.display()
            );
            return License::Unknown;
        }
    };
    let value: serde_json::Value = match serde_json::from_str(&text) {
        Ok(v) => v,
        Err(e) => {
            log::info!("[license] loaded: Unknown (JSON parse error: {})", e);
            return License::Unknown;
        }
    };
    match value.get("license").and_then(|v| v.as_str()) {
        Some(s) => {
            let license = License::parse_loose(s);
            log::info!("[license] loaded: {:?} (raw: {:?})", license, s);
            license
        }
        None => {
            log::info!("[license] loaded: Unknown (license field missing)");
            License::Unknown
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
