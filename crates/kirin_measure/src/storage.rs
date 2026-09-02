//! Identity storage — 2 箇所保存 + deterministic 3 段階復旧。
//!
//!
//! # 保存先
//! | 種別 | パス | 管理者 |
//! |------|------|--------|
//! | 一次 | `~/Library/Application Support/Kirin OS/identity.json` | Kirin OS 本体 |
//! | 二次 | `~/Library/Application Support/Kirin OS/plugin_data/.identity_backup.json` | Hypha（自動同期） |
//!
//! **設計決定:** macOS では `plugin_data/` を
//! `~/Library/Application Support/Kirin OS/plugin_data/` 配下に置く。
//! Windows では `PlatformPaths` 経由で identity root と plugin_data root を分けられる
//! ようにし、APPDATA / LOCALAPPDATA の差を storage 利用側へ漏らさない。
//!
//! # 3 段階復旧フロー
//! ```text
//! 1. 一次を読む  → OK → 2-of-3 判定 → 通常稼働 / 計測停止
//!                失敗 → 2 へ
//! 2. 二次を読む  → OK → 2-of-3 判定 → 一次に復元
//!                失敗 → 3 へ
//! 3. 新規生成    → 一次・二次両方書込
//! ```
//!
//! 過去の plugin_data 全履歴から identity を推測しない。一次・二次という所有された固定パス
//! だけを読み、両方失われた場合は新規 identity を作る。

use crate::hardware::{HardwareComponents, Match};
use crate::identity::{Identity, License};
use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};

#[path = "storage_cleanup.rs"]
mod cleanup;
pub use cleanup::{cleanup_legacy_v1, CleanupReport, CLEANUP_V1_DONE_FILENAME};

/// 起動時の identity 取得結果。
///
/// - `identity` : 正当に使える Identity
/// - `status`   : どの段階で取得したか + 2-of-3 判定結果
#[derive(Debug, Clone)]
pub struct LoadedIdentity {
    pub identity: Identity,
    pub status: LoadStatus,
}

/// 起動時 Identity 取得ステータス。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LoadStatus {
    /// 段階 1: 一次から読込、2-of-3 OK。
    PrimaryOk,
    /// 段階 2: 二次から読込、一次復元済み。
    RecoveredFromSecondary,
    /// 段階 3: 新規生成。
    FreshlyGenerated,
    /// 別マシン検出: 計測停止が必要（GUI 警告）。
    DifferentMachine,
    /// 判定不能（2-of-3 比較可能要素 < 2）→ permissive に継続。
    Insufficient,
}

impl LoadStatus {
    /// 計測を継続してよいか。`DifferentMachine` のみ false。
    pub fn allow_measurement(self) -> bool {
        !matches!(self, Self::DifferentMachine)
    }
}

/// storage 層のパス群。テスト時はカスタムルートで注入する。
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StoragePaths {
    /// Kirin OS ディレクトリのルート（通常 `~/Library/Application Support/Kirin OS`）。
    pub kirin_os_root: PathBuf,
    /// plugin_data のルート。macOS では `kirin_os_root/plugin_data`。
    ///
    /// Windows では identity と plugin_data の配置先を APPDATA / LOCALAPPDATA に
    /// 分けられるよう、root から独立して保持する。
    pub plugin_data_root: PathBuf,
}

/// Platform-specific path bundle used before crossing into storage / IO code.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlatformPaths {
    pub kind: PlatformKind,
    pub storage: StoragePaths,
    pub kirin_tmp_root: PathBuf,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlatformKind {
    MacOS,
    Windows,
}

impl StoragePaths {
    /// 本番用のデフォルトパス。`$HOME` が取れない場合は `Err`。
    pub fn default_macos() -> Result<Self, StorageError> {
        Ok(PlatformPaths::default_macos()?.storage)
    }

    /// 現在の build target に対応するデフォルトパス。
    pub fn default_platform() -> Result<Self, StorageError> {
        Ok(PlatformPaths::default_current()?.storage)
    }

    /// テスト用に任意ルートを指定。
    pub fn with_root(root: impl Into<PathBuf>) -> Self {
        let root = root.into();
        Self {
            plugin_data_root: root.join("plugin_data"),
            kirin_os_root: root,
        }
    }

    /// identity root と plugin_data root を別々に指定する。
    pub fn with_roots(
        kirin_os_root: impl Into<PathBuf>,
        plugin_data_root: impl Into<PathBuf>,
    ) -> Self {
        Self {
            kirin_os_root: kirin_os_root.into(),
            plugin_data_root: plugin_data_root.into(),
        }
    }

    pub fn primary_path(&self) -> PathBuf {
        self.kirin_os_root.join("identity.json")
    }

    pub fn plugin_data_dir(&self) -> PathBuf {
        self.plugin_data_root.clone()
    }

    pub fn secondary_path(&self) -> PathBuf {
        self.plugin_data_dir().join(".identity_backup.json")
    }
}

impl PlatformPaths {
    pub fn current_kirin_tmp_root() -> PathBuf {
        std::env::temp_dir().join("kirin")
    }

    pub fn for_macos(home: impl Into<PathBuf>, temp_dir: impl Into<PathBuf>) -> Self {
        let kirin_os_root = home
            .into()
            .join("Library")
            .join("Application Support")
            .join("Kirin OS");
        let storage = StoragePaths::with_root(kirin_os_root);
        Self {
            kind: PlatformKind::MacOS,
            storage,
            kirin_tmp_root: temp_dir.into().join("kirin"),
        }
    }

    pub fn for_windows(
        appdata: impl Into<PathBuf>,
        local_appdata: impl Into<PathBuf>,
        temp_dir: impl Into<PathBuf>,
    ) -> Self {
        let kirin_os_root = appdata.into().join("Kirin OS");
        let plugin_data_root = local_appdata.into().join("Kirin OS").join("plugin_data");
        let storage = StoragePaths::with_roots(kirin_os_root, plugin_data_root);
        Self {
            kind: PlatformKind::Windows,
            storage,
            kirin_tmp_root: temp_dir.into().join("kirin"),
        }
    }

    pub fn default_macos() -> Result<Self, StorageError> {
        let home = std::env::var("HOME").map_err(|_| StorageError::NoHome)?;
        Ok(Self::for_macos(home, std::env::temp_dir()))
    }

    pub fn default_windows() -> Result<Self, StorageError> {
        let appdata = std::env::var("APPDATA").map_err(|_| StorageError::MissingEnv("APPDATA"))?;
        let local_appdata =
            std::env::var("LOCALAPPDATA").map_err(|_| StorageError::MissingEnv("LOCALAPPDATA"))?;
        Ok(Self::for_windows(
            appdata,
            local_appdata,
            std::env::temp_dir(),
        ))
    }

    pub fn default_current() -> Result<Self, StorageError> {
        #[cfg(target_os = "windows")]
        {
            Self::default_windows()
        }
        #[cfg(target_os = "macos")]
        {
            Self::default_macos()
        }
        #[cfg(not(any(target_os = "windows", target_os = "macos")))]
        {
            Self::default_macos()
        }
    }
}

/// ストレージ操作のエラー。
#[derive(Debug)]
pub enum StorageError {
    /// `$HOME` 環境変数が取れない。
    NoHome,
    /// Platform-specific environment variable is missing.
    MissingEnv(&'static str),
    /// ディレクトリ作成失敗（権限等）。
    CreateDir(std::io::Error),
    /// 書き込み失敗。
    Write(std::io::Error),
    /// 読み込み失敗。
    Read(std::io::Error),
    /// JSON パース失敗。
    Parse(serde_json::Error),
    /// rename atomic 失敗。
    Rename(std::io::Error),
}

impl std::fmt::Display for StorageError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::NoHome => write!(f, "HOME environment variable not set"),
            Self::MissingEnv(name) => write!(f, "{} environment variable not set", name),
            Self::CreateDir(e) => write!(f, "create dir: {}", e),
            Self::Write(e) => write!(f, "write: {}", e),
            Self::Read(e) => write!(f, "read: {}", e),
            Self::Parse(e) => write!(f, "parse: {}", e),
            Self::Rename(e) => write!(f, "rename: {}", e),
        }
    }
}

impl std::error::Error for StorageError {}

// ── 個別の保存 / 読込 操作（T-2） ─────────────────────────────────────────

/// Identity を指定パスに atomic write する（unique tmp → rename）。
pub fn write_identity_atomic(path: &Path, identity: &Identity) -> Result<(), StorageError> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(StorageError::CreateDir)?;
    }
    let json = identity.to_json_pretty().map_err(StorageError::Parse)?;
    crate::atomic_file::write_bytes_atomic(path, json.as_bytes()).map_err(StorageError::Write)?;
    Ok(())
}

/// Identity を指定パスから読み込む（パースは呼び出し元の責務）。
pub fn read_identity(path: &Path) -> Result<Identity, StorageError> {
    let text = fs::read_to_string(path).map_err(StorageError::Read)?;
    Identity::from_json(&text).map_err(StorageError::Parse)
}

/// 一次と二次の両方に書き込む（復旧の最終ステップ等で使用）。
pub fn write_both(paths: &StoragePaths, identity: &Identity) -> Result<(), StorageError> {
    write_identity_atomic(&paths.primary_path(), identity)?;
    write_identity_atomic(&paths.secondary_path(), identity)?;
    Ok(())
}

// ── 4 段階復旧フロー（T-4） ──────────────────────────────────────────────

/// 起動時に Identity を取得する。4 段階復旧 + 2-of-3 判定を実行。
///
/// # 引数
/// - `paths` : ストレージパス（本番は `StoragePaths::default_platform()`）
/// - `current_hw` : 現在マシンの 3 要素（`HardwareComponents::current()`）
/// - `default_license` : 新規生成時の license（OS 版配布は `License::Os`）
pub fn load_or_recover(
    paths: &StoragePaths,
    current_hw: HardwareComponents,
    default_license: License,
) -> Result<LoadedIdentity, StorageError> {
    // ── 段階 1: 一次 ─────────────────────────────────────────────────
    if let Ok(mut identity) = read_identity(&paths.primary_path()) {
        if identity.verify_signature() {
            let m = identity.hardware_components.compare(&current_hw);
            match m {
                Match::Same => {
                    identity.touch_verified();
                    let _ = write_identity_atomic(&paths.primary_path(), &identity);
                    let _ = write_identity_atomic(&paths.secondary_path(), &identity);
                    return Ok(LoadedIdentity {
                        identity,
                        status: LoadStatus::PrimaryOk,
                    });
                }
                Match::Different => {
                    return Ok(LoadedIdentity {
                        identity,
                        status: LoadStatus::DifferentMachine,
                    });
                }
                Match::Insufficient => {
                    // 3 要素取得困難 → permissive に継続
                    identity.touch_verified();
                    let _ = write_identity_atomic(&paths.primary_path(), &identity);
                    let _ = write_identity_atomic(&paths.secondary_path(), &identity);
                    return Ok(LoadedIdentity {
                        identity,
                        status: LoadStatus::Insufficient,
                    });
                }
            }
        }
        // HMAC 検証失敗 → 改ざん。段階 2 へ進む（permissive: 二次から回復試行）
        log::warn!("[identity] primary HMAC verification failed; falling through to secondary");
    }

    // ── 段階 2: 二次から復元 ─────────────────────────────────────────
    if let Ok(mut identity) = read_identity(&paths.secondary_path()) {
        if identity.verify_signature() {
            let m = identity.hardware_components.compare(&current_hw);
            match m {
                Match::Same => {
                    identity.touch_verified();
                    write_identity_atomic(&paths.primary_path(), &identity)?;
                    write_identity_atomic(&paths.secondary_path(), &identity)?;
                    return Ok(LoadedIdentity {
                        identity,
                        status: LoadStatus::RecoveredFromSecondary,
                    });
                }
                Match::Different => {
                    return Ok(LoadedIdentity {
                        identity,
                        status: LoadStatus::DifferentMachine,
                    });
                }
                Match::Insufficient => {
                    identity.touch_verified();
                    write_identity_atomic(&paths.primary_path(), &identity)?;
                    write_identity_atomic(&paths.secondary_path(), &identity)?;
                    return Ok(LoadedIdentity {
                        identity,
                        status: LoadStatus::Insufficient,
                    });
                }
            }
        }
        log::warn!("[identity] secondary HMAC verification failed; creating a fresh identity");
    }

    // ── 段階 3: 新規生成 ─────────────────────────────────────────────
    let identity = Identity::new(current_hw, default_license);
    write_both(paths, &identity)?;
    Ok(LoadedIdentity {
        identity,
        status: LoadStatus::FreshlyGenerated,
    })
}

// ── installation_id loose reader（サブ3-A-2 / Adv-実装 承認 β 案）──────
//
// PluginDataWriter が Frame.installation_id を埋めるための最小読取り。
// HMAC 検証・2-of-3 判定は行わず、identity JSON の `installation_id` フィールド
// のみを loose に抽出する。license.rs `load_license_safe` と同位相:
// - 本番パス: `StoragePaths::default_platform().primary_path()`
// - platform path 解決不能 / ファイル不在 / 不正 JSON / フィールド欠落 → `None`
// - 書込は行わない（`load_or_recover` と分離）
//
// ログ分岐は以下の 5 系統:
// - `[installation_id] loaded: <uuid>` — 正常
// - `[installation_id] loaded: None (no $HOME)` — $HOME 解決不能
// - `[installation_id] loaded: None (file missing)` — identity.json 不在
// - `[installation_id] loaded: None (JSON parse error: <detail>)` — 破損
// - `[installation_id] loaded: None (installation_id field missing)` — フィールド欠落

/// `identity.json` から installation_id を安全に読み込む（IO Thread 起動時用）。
///
/// HMAC 署名検証・2-of-3 判定はしない。GUI の license 読取りと同位相の
/// loose reader（Phase 1.0 手動テストや Kirin OS 本体未完成時点でも
/// installation_id 値は独立して読める必要あり）。
pub fn load_installation_id_safe() -> Option<String> {
    let paths = match StoragePaths::default_platform() {
        Ok(p) => p,
        Err(_) => {
            log::info!("[installation_id] loaded: None (platform path unresolved)");
            return None;
        }
    };
    load_installation_id_from(&paths.primary_path())
}

/// 任意パスから installation_id を loose 抽出（テスト・`load_installation_id_safe` 共用）。
pub(crate) fn load_installation_id_from(path: &Path) -> Option<String> {
    let text = match fs::read_to_string(path) {
        Ok(t) => t,
        Err(_) => {
            log::info!(
                "[installation_id] loaded: None (file missing: {})",
                path.display()
            );
            return None;
        }
    };
    let value: Value = match serde_json::from_str(&text) {
        Ok(v) => v,
        Err(e) => {
            log::info!("[installation_id] loaded: None (JSON parse error: {})", e);
            return None;
        }
    };
    match value.get("installation_id").and_then(|v| v.as_str()) {
        Some(s) if !s.is_empty() => {
            log::info!("[installation_id] loaded: {}", s);
            Some(s.to_string())
        }
        _ => {
            log::info!("[installation_id] loaded: None (installation_id field missing)");
            None
        }
    }
}

// ── 一次 / 二次 同期（起動中のアップグレード検出・T-6 準備） ─────────

/// 一次を読み、mtime が変化していれば再読込した Identity を返す。
/// T-6 Sense→OS アップグレード即時反映に使用。
#[derive(Debug, Clone)]
pub struct IdentityCache {
    cached: Identity,
    primary_mtime: Option<std::time::SystemTime>,
    paths: StoragePaths,
}

impl IdentityCache {
    pub fn new(paths: StoragePaths, initial: Identity) -> Self {
        let primary_mtime = fs::metadata(paths.primary_path())
            .and_then(|m| m.modified())
            .ok();
        Self {
            cached: initial,
            primary_mtime,
            paths,
        }
    }

    /// 現在の cached Identity を取得（mtime チェックで必要時のみ再読込）。
    pub fn get(&mut self) -> &Identity {
        let current_mtime = fs::metadata(self.paths.primary_path())
            .and_then(|m| m.modified())
            .ok();
        if current_mtime != self.primary_mtime {
            if let Ok(fresh) = read_identity(&self.paths.primary_path()) {
                if fresh.verify_signature() {
                    self.cached = fresh;
                    self.primary_mtime = current_mtime;
                }
            }
        }
        &self.cached
    }

    /// ライセンスの直近値を返す（mtime チェック込み）。
    pub fn current_license(&mut self) -> License {
        self.get().license
    }
}

#[cfg(test)]
#[path = "storage_tests.rs"]
mod tests;
