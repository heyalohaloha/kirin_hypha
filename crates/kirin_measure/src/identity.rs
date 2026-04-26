//! Identity schema — `identity.json` の型・生成・HMAC-SHA256 署名検証。
//!
//! guardian_58_hypha_step4_identity_license.md T-1 / T-7 対応。
//!
//! # スキーマ（schema_version 1.0）
//! ```json
//! {
//!   "schema_version": "1.0",
//!   "installation_id": "UUID v4",
//!   "hardware_id": "SHA-256(IOPlatformUUID + system_serial_number + boot_disk_uuid)",
//!   "hardware_components": {
//!     "iop": "IOPlatformUUID 生値",
//!     "sn":  "system_serial_number 生値",
//!     "bd":  "boot_disk_uuid 生値"
//!   },
//!   "machine_signature": "HMAC-SHA256(installation_id, hardware_id)",
//!   "license": "sense | os",
//!   "created_at": "ISO 8601",
//!   "last_verified_at": "ISO 8601",
//!   "updated_at": "ISO 8601（最終書込時刻。touch_verified で更新）",
//!   "os_version": "sw_vers -productVersion の結果。取得失敗時は \"\""
//! }
//! ```
//!
//! `updated_at` / `os_version` は guardian_59 Step 5 Additive（G-60-03）で
//! 追加されたフィールド。旧 identity.json（既存ファイル）との互換のため
//! `#[serde(default)]` を付与、欠けていれば空文字で読み込む。
//!
//! `hardware_components` は 2-of-3 判定用に 3 要素を個別保持する内部フィールド。
//! `hardware_id` は 3 要素を結合した SHA-256 ハッシュ（外部参照用）。
//!
//! # HMAC 鍵（T-7）
//! Phase 1.0 は (a) ビルド時埋め込み固定鍵。GPLv3 公開前提のため「改ざん検出の
//! 抑止力」レベル。`KIRIN_HYPHA_HMAC_KEY` 環境変数でビルド時上書き可能。

use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use uuid::Uuid;

use crate::hardware::HardwareComponents;

type HmacSha256 = Hmac<Sha256>;

/// ライセンス種別。T-5 分岐の型。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum License {
    /// Kirin OS 同梱版（Record 機能 有効）。
    Os,
    /// Kirin Sense 同梱版（Watch のみ）。
    Sense,
    /// 未知値 → 安全側（Record 不可）。
    #[serde(other)]
    Unknown,
}

impl License {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Os => "os",
            Self::Sense => "sense",
            Self::Unknown => "unknown",
        }
    }

    /// 文字列 → License（未知は Unknown、パース失敗なしで安全側）。
    ///
    /// `std::str::FromStr` ではなく `parse_loose` として提供する理由:
    /// `FromStr` は `Result<Self, Err>` を返すが、本関数は常に成功させ
    /// 未知値を `Unknown` に倒す設計のため。
    pub fn parse_loose(s: &str) -> Self {
        match s {
            "os" => Self::Os,
            "sense" => Self::Sense,
            _ => Self::Unknown,
        }
    }
}

/// `identity.json` のルート構造。serde で JSON 相互変換。
///
/// # 不変条件
/// - `schema_version == "1.0"` を常に保証
/// - `installation_id` は UUID v4 形式（生成時）
/// - `machine_signature` は `installation_id` + `hardware_id` に対する HMAC-SHA256（hex）
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Identity {
    pub schema_version: String,
    pub installation_id: String,
    pub hardware_id: String,
    pub hardware_components: HardwareComponents,
    pub machine_signature: String,
    pub license: License,
    pub created_at: String,
    pub last_verified_at: String,
    /// 最終書込時刻（ISO 8601）。`touch_verified` で `last_verified_at` と同時に更新。
    /// 旧スキーマ互換のため `#[serde(default)]`（欠けていれば空文字）。
    #[serde(default)]
    pub updated_at: String,
    /// Identity 生成時の macOS バージョン（例 "15.0.1"）。取得失敗時は空文字。
    /// 旧スキーマ互換のため `#[serde(default)]`。
    #[serde(default)]
    pub os_version: String,
}

impl Identity {
    /// schema_version の固定値。
    pub const SCHEMA_VERSION: &'static str = "1.0";

    /// 新規生成。installation_id は UUID v4、created_at/last_verified_at/updated_at は現在時刻、
    /// machine_signature は自動計算、os_version は `sw_vers -productVersion`（取得失敗時は空文字）。
    pub fn new(components: HardwareComponents, license: License) -> Self {
        let installation_id = Uuid::new_v4().to_string();
        let hardware_id = components.hardware_id();
        let machine_signature = compute_signature(&installation_id, &hardware_id);
        let now = now_iso8601();
        Self {
            schema_version: Self::SCHEMA_VERSION.to_string(),
            installation_id,
            hardware_id,
            hardware_components: components,
            machine_signature,
            license,
            created_at: now.clone(),
            last_verified_at: now.clone(),
            updated_at: now,
            os_version: detect_os_version(),
        }
    }

    /// 既知の installation_id を使って再構築（4段階復旧 段階3 用）。
    pub fn reconstruct(
        installation_id: String,
        components: HardwareComponents,
        license: License,
    ) -> Self {
        let hardware_id = components.hardware_id();
        let machine_signature = compute_signature(&installation_id, &hardware_id);
        let now = now_iso8601();
        Self {
            schema_version: Self::SCHEMA_VERSION.to_string(),
            installation_id,
            hardware_id,
            hardware_components: components,
            machine_signature,
            license,
            created_at: now.clone(),
            last_verified_at: now.clone(),
            updated_at: now,
            os_version: detect_os_version(),
        }
    }

    /// HMAC 検証。`machine_signature` が `installation_id + hardware_id` と整合するか。
    pub fn verify_signature(&self) -> bool {
        let expected = compute_signature(&self.installation_id, &self.hardware_id);
        constant_time_eq(expected.as_bytes(), self.machine_signature.as_bytes())
    }

    /// `last_verified_at` と `updated_at` を現在時刻に更新（起動毎の検証成功時）。
    pub fn touch_verified(&mut self) {
        let now = now_iso8601();
        self.last_verified_at = now.clone();
        self.updated_at = now;
    }

    /// JSON 文字列にシリアライズ（pretty）。
    pub fn to_json_pretty(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string_pretty(self)
    }

    /// JSON 文字列からデシリアライズ。
    pub fn from_json(s: &str) -> Result<Self, serde_json::Error> {
        serde_json::from_str(s)
    }
}

/// `installation_id + hardware_id` の HMAC-SHA256 hex 文字列。
fn compute_signature(installation_id: &str, hardware_id: &str) -> String {
    let mut mac = HmacSha256::new_from_slice(hmac_key())
        .expect("HMAC-SHA256 accepts any key length");
    mac.update(installation_id.as_bytes());
    mac.update(b"|");
    mac.update(hardware_id.as_bytes());
    hex::encode(mac.finalize().into_bytes())
}

/// HMAC 鍵（T-7 (a) ビルド時埋め込み固定鍵）。
///
/// GPLv3 公開前提のため「改ざん検出の抑止力」レベルのセキュリティ。
/// 完璧な改ざん耐性は提供しない（ソース公開時点で鍵も公開される）。
///
/// 将来 (c) macOS Keychain 方式へ移行する際は、この関数の実装のみ差し替える。
fn hmac_key() -> &'static [u8] {
    // ビルド時に KIRIN_HYPHA_HMAC_KEY が設定されていればそれを使用。
    // デフォルトは Phase 1.0 固定鍵（ソースに埋め込み）。
    match option_env!("KIRIN_HYPHA_HMAC_KEY") {
        Some(k) => k.as_bytes(),
        None => DEFAULT_HMAC_KEY,
    }
}

const DEFAULT_HMAC_KEY: &[u8] =
    b"kirin-hypha-phase1.0-hmac-key-deterrent-level-20260417";

/// 定数時間比較（タイミング攻撃耐性。HMAC 比較で使用）。
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

/// 現在時刻を ISO 8601（秒精度 + UTC）で返す。
fn now_iso8601() -> String {
    chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string()
}

/// macOS バージョンを `sw_vers -productVersion` から取得する。
///
/// 失敗時は空文字。サンドボックスで `sw_vers` が使えないケースなど
/// 取得不能な環境でも identity.json の生成を妨げない（R-28 沈黙原則）。
fn detect_os_version() -> String {
    match std::process::Command::new("sw_vers")
        .arg("-productVersion")
        .output()
    {
        Ok(out) if out.status.success() => {
            String::from_utf8_lossy(&out.stdout).trim().to_string()
        }
        _ => String::new(),
    }
}

/// 3 要素を結合して SHA-256 ハッシュを取る（hardware_id 計算用）。
///
/// 区切り文字 `|` で結合。順序は `IOPlatformUUID | serial_number | boot_disk_uuid` 固定。
pub(crate) fn hash_hardware(iop: &str, sn: &str, bd: &str) -> String {
    let mut hasher = Sha256::new();
    hasher.update(iop.as_bytes());
    hasher.update(b"|");
    hasher.update(sn.as_bytes());
    hasher.update(b"|");
    hasher.update(bd.as_bytes());
    hex::encode(hasher.finalize())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_components() -> HardwareComponents {
        HardwareComponents {
            iop: "11111111-2222-3333-4444-555555555555".to_string(),
            sn: "C02ABCDEFGHI".to_string(),
            bd: "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE".to_string(),
        }
    }

    #[test]
    fn identity_new_has_schema_1_0_and_uuid_v4() {
        let id = Identity::new(sample_components(), License::Os);
        assert_eq!(id.schema_version, "1.0");
        // UUID v4: 36 文字 + ハイフン 4 つ
        assert_eq!(id.installation_id.len(), 36);
        // version バージョン部分が "4" で始まる（UUID v4 の marker）
        assert_eq!(id.installation_id.chars().nth(14), Some('4'));
    }

    #[test]
    fn signature_roundtrip_verifies() {
        let id = Identity::new(sample_components(), License::Os);
        assert!(id.verify_signature());
    }

    #[test]
    fn tampered_installation_id_fails_verify() {
        let mut id = Identity::new(sample_components(), License::Os);
        id.installation_id = "00000000-0000-4000-8000-000000000000".to_string();
        assert!(!id.verify_signature());
    }

    #[test]
    fn tampered_hardware_id_fails_verify() {
        let mut id = Identity::new(sample_components(), License::Os);
        id.hardware_id = hash_hardware("other", "other", "other");
        assert!(!id.verify_signature());
    }

    #[test]
    fn json_roundtrip_preserves_all_fields() {
        let id = Identity::new(sample_components(), License::Sense);
        let json = id.to_json_pretty().unwrap();
        let back = Identity::from_json(&json).unwrap();
        assert_eq!(id.installation_id, back.installation_id);
        assert_eq!(id.hardware_id, back.hardware_id);
        assert_eq!(id.machine_signature, back.machine_signature);
        assert_eq!(id.license, back.license);
        assert!(back.verify_signature());
    }

    #[test]
    fn license_unknown_parses_safely() {
        let id = Identity::new(sample_components(), License::Os);
        let mut json = id.to_json_pretty().unwrap();
        json = json.replace(r#""license": "os""#, r#""license": "trial""#);
        let back = Identity::from_json(&json).unwrap();
        assert_eq!(back.license, License::Unknown);
    }

    #[test]
    fn license_parse_loose_safe_default() {
        assert_eq!(License::parse_loose("os"), License::Os);
        assert_eq!(License::parse_loose("sense"), License::Sense);
        assert_eq!(License::parse_loose(""), License::Unknown);
        assert_eq!(License::parse_loose("trial"), License::Unknown);
    }

    #[test]
    fn reconstruct_preserves_installation_id() {
        let original = Identity::new(sample_components(), License::Os);
        let rebuilt = Identity::reconstruct(
            original.installation_id.clone(),
            sample_components(),
            License::Os,
        );
        assert_eq!(original.installation_id, rebuilt.installation_id);
        assert_eq!(original.hardware_id, rebuilt.hardware_id);
        assert_eq!(original.machine_signature, rebuilt.machine_signature);
    }

    #[test]
    fn hash_hardware_is_deterministic() {
        let h1 = hash_hardware("a", "b", "c");
        let h2 = hash_hardware("a", "b", "c");
        assert_eq!(h1, h2);
        // SHA-256 hex = 64 文字
        assert_eq!(h1.len(), 64);
    }

    #[test]
    fn hash_hardware_differs_on_change() {
        let h1 = hash_hardware("a", "b", "c");
        let h2 = hash_hardware("a", "b", "d");
        assert_ne!(h1, h2);
    }

    // ── G-60-03 Additive: updated_at / os_version ────────────────────────

    #[test]
    fn new_populates_updated_at_and_os_version_fields_exist() {
        let id = Identity::new(sample_components(), License::Os);
        assert!(!id.updated_at.is_empty(), "updated_at should be set at creation");
        assert_eq!(
            id.updated_at, id.created_at,
            "updated_at should equal created_at on fresh creation"
        );
        // os_version は環境によって空文字になりうる（非 macOS CI）。型の存在のみ検証。
        let _: &str = id.os_version.as_str();
    }

    #[test]
    fn touch_verified_advances_updated_at() {
        let mut id = Identity::new(sample_components(), License::Os);
        let before = id.updated_at.clone();
        // 時刻が進むまで 1 秒ちょうど待つのは過剰。last_verified_at と updated_at が
        // 同値更新されることだけ検証し、clone を比較する。
        std::thread::sleep(std::time::Duration::from_millis(1100));
        id.touch_verified();
        assert_eq!(id.updated_at, id.last_verified_at);
        assert_ne!(id.updated_at, before, "updated_at should advance");
    }

    #[test]
    fn old_json_without_updated_at_still_parses() {
        // 旧 identity.json（updated_at / os_version なし）を復元できること（serde default）
        let legacy = r#"{
            "schema_version": "1.0",
            "installation_id": "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
            "hardware_id": "0000000000000000000000000000000000000000000000000000000000000000",
            "hardware_components": { "iop": "", "sn": "", "bd": "" },
            "machine_signature": "deadbeef",
            "license": "os",
            "created_at": "2026-04-19T00:00:00Z",
            "last_verified_at": "2026-04-19T00:00:00Z"
        }"#;
        let id = Identity::from_json(legacy).expect("legacy JSON should parse");
        assert_eq!(id.updated_at, "", "missing updated_at defaults to empty");
        assert_eq!(id.os_version, "", "missing os_version defaults to empty");
    }

    #[test]
    fn json_roundtrip_preserves_additive_fields() {
        let mut id = Identity::new(sample_components(), License::Os);
        id.os_version = "15.0.1".to_string();
        id.updated_at = "2026-04-20T12:34:56Z".to_string();
        let json = id.to_json_pretty().unwrap();
        let back = Identity::from_json(&json).unwrap();
        assert_eq!(back.updated_at, "2026-04-20T12:34:56Z");
        assert_eq!(back.os_version, "15.0.1");
    }
}
