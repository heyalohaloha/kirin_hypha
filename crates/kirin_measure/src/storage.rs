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

// ── 旧構造 cleanup（1a-6 / Q4）────────────────────────────────────────────────

/// `~/Library/Application Support/Kirin OS/.cleanup_v1_done` フラグファイル名。
pub const CLEANUP_V1_DONE_FILENAME: &str = ".cleanup_v1_done";

/// 旧構造（`PROJECT_HASH_PHASE1="default"` / `BUS_PHASE1="MIX"` 時代）の残骸を 1 度
/// だけ削除する。flag ファイル `.cleanup_v1_done` の有無で冪等性を保証。
///
/// 削除対象:
/// - `~/Library/.../plugin_data/default/MIX/`（旧 Watch + Record データ）
/// - `~/Library/.../plugin_data/default/preset/`（旧 default プロジェクトの preset）
/// - `$TMPDIR/kirin/default/MIX/`（旧 /tmp/ Watch ファイル）
///
/// flag ファイルは `~/Library/.../Kirin OS/.cleanup_v1_done`。2 回目起動時はスキップ。
/// 失敗時はログのみ出力して continue（破壊的にならない）。
pub fn cleanup_legacy_v1(paths: &StoragePaths) -> CleanupReport {
    let flag = paths.kirin_os_root.join(CLEANUP_V1_DONE_FILENAME);
    if flag.exists() {
        return CleanupReport {
            ran: false,
            removed: 0,
            errors: 0,
        };
    }

    let mut removed = 0usize;
    let mut errors = 0usize;

    let pd = paths.plugin_data_dir();
    let legacy_mix = pd.join("default").join("MIX");
    let legacy_preset = pd.join("default").join("preset");
    let legacy_tmp = PlatformPaths::current_kirin_tmp_root()
        .join("default")
        .join("MIX");

    for target in [&legacy_mix, &legacy_preset, &legacy_tmp] {
        if !target.exists() {
            continue;
        }
        match fs::remove_dir_all(target) {
            Ok(()) => {
                log::info!("[cleanup_v1] removed: {}", target.display());
                removed += 1;
            }
            Err(e) => {
                log::warn!("[cleanup_v1] failed to remove {}: {}", target.display(), e);
                errors += 1;
            }
        }
    }

    // 親 `default/` ディレクトリが空になっていれば一緒に消す（残骸を残さない）
    let legacy_default = pd.join("default");
    if legacy_default.exists() {
        let _ = fs::remove_dir(&legacy_default);
    }
    let legacy_tmp_default = PlatformPaths::current_kirin_tmp_root().join("default");
    if legacy_tmp_default.exists() {
        let _ = fs::remove_dir(&legacy_tmp_default);
    }

    // flag ファイル書込
    if let Some(parent) = flag.parent() {
        let _ = fs::create_dir_all(parent);
    }
    if let Err(e) = fs::write(&flag, b"cleanup v1 completed\n") {
        log::warn!(
            "[cleanup_v1] failed to write flag {}: {}",
            flag.display(),
            e
        );
        errors += 1;
    }

    CleanupReport {
        ran: true,
        removed,
        errors,
    }
}

/// `cleanup_legacy_v1` の実行レポート。テスト・ログ用。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CleanupReport {
    /// この呼び出しで cleanup が実行されたか（false = 既に実行済みでスキップ）。
    pub ran: bool,
    /// 削除に成功したディレクトリ数。
    pub removed: usize,
    /// 削除に失敗したディレクトリ数（flag 書込失敗も含む）。
    pub errors: usize,
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
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    static TEST_COUNTER: AtomicUsize = AtomicUsize::new(0);

    /// テスト用隔離ディレクトリ。各テストが独立した一時ディレクトリを使う。
    fn isolated_paths() -> (StoragePaths, PathBuf) {
        let n = TEST_COUNTER.fetch_add(1, Ordering::SeqCst);
        let pid = std::process::id();
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let root = std::env::temp_dir()
            .join("kirin_hypha_test")
            .join(format!("{}-{}-{}", pid, now, n));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        (StoragePaths::with_root(&root), root)
    }

    fn hc(a: &str, b: &str, c: &str) -> HardwareComponents {
        HardwareComponents {
            iop: a.to_string(),
            sn: b.to_string(),
            bd: c.to_string(),
        }
    }

    #[test]
    fn platform_paths_macos_fixture_preserves_current_storage_layout() {
        let paths = PlatformPaths::for_macos("/Users/daisuke", "/tmp");
        let expected_root = PathBuf::from("/Users/daisuke")
            .join("Library")
            .join("Application Support")
            .join("Kirin OS");

        assert_eq!(paths.kind, PlatformKind::MacOS);
        assert_eq!(paths.storage.kirin_os_root, expected_root);
        assert_eq!(
            paths.storage.plugin_data_dir(),
            expected_root.join("plugin_data")
        );
        assert_eq!(
            paths.storage.primary_path(),
            expected_root.join("identity.json")
        );
        assert_eq!(
            paths.storage.secondary_path(),
            expected_root
                .join("plugin_data")
                .join(".identity_backup.json")
        );
        assert_eq!(paths.kirin_tmp_root, PathBuf::from("/tmp").join("kirin"));
    }

    #[test]
    fn platform_paths_windows_fixture_splits_appdata_and_localappdata() {
        let appdata = PathBuf::from(r"C:\Users\daisuke\AppData\Roaming");
        let local_appdata = PathBuf::from(r"C:\Users\daisuke\AppData\Local");
        let temp = PathBuf::from(r"C:\Users\daisuke\AppData\Local\Temp");
        let paths =
            PlatformPaths::for_windows(appdata.clone(), local_appdata.clone(), temp.clone());
        let expected_identity_root = appdata.join("Kirin OS");
        let expected_plugin_data = local_appdata.join("Kirin OS").join("plugin_data");

        assert_eq!(paths.kind, PlatformKind::Windows);
        assert_eq!(paths.storage.kirin_os_root, expected_identity_root);
        assert_eq!(paths.storage.plugin_data_dir(), expected_plugin_data);
        assert_eq!(
            paths.storage.primary_path(),
            expected_identity_root.join("identity.json")
        );
        assert_eq!(
            paths.storage.secondary_path(),
            expected_plugin_data.join(".identity_backup.json")
        );
        assert_eq!(paths.kirin_tmp_root, temp.join("kirin"));
    }

    #[test]
    fn stage4_fresh_generation_writes_both() {
        let (paths, root) = isolated_paths();
        let loaded = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(loaded.status, LoadStatus::FreshlyGenerated);
        assert_eq!(loaded.identity.license, License::Os);
        assert!(paths.primary_path().exists());
        assert!(paths.secondary_path().exists());
        let primary = read_identity(&paths.primary_path()).unwrap();
        let secondary = read_identity(&paths.secondary_path()).unwrap();
        assert_eq!(primary.installation_id, secondary.installation_id);
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn stage1_primary_ok_returns_same_identity() {
        let (paths, root) = isolated_paths();
        let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(second.status, LoadStatus::PrimaryOk);
        assert_eq!(
            first.identity.installation_id,
            second.identity.installation_id
        );
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn stage2_secondary_restores_primary() {
        let (paths, root) = isolated_paths();
        let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        // 一次削除
        fs::remove_file(paths.primary_path()).unwrap();
        let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(second.status, LoadStatus::RecoveredFromSecondary);
        assert_eq!(
            first.identity.installation_id,
            second.identity.installation_id
        );
        // 一次復元確認
        assert!(paths.primary_path().exists());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn stage3_does_not_scan_plugin_history_for_identity() {
        let (paths, root) = isolated_paths();
        let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        let original_id = first.identity.installation_id.clone();

        // 新構造: plugin_data/{project_hash}/{instance_id}/pre/*.json
        let pre_dir = paths
            .plugin_data_dir()
            .join("ph-test")
            .join("iid-test")
            .join("pre");
        fs::create_dir_all(&pre_dir).unwrap();
        let pre_file = pre_dir.join("20260417T120000.json");
        let json = format!(r#"{{"installation_id":"{}","other":"data"}}"#, original_id);
        fs::write(&pre_file, json).unwrap();

        // 一次と二次を削除 → 段階 3 経路を強制
        fs::remove_file(paths.primary_path()).unwrap();
        fs::remove_file(paths.secondary_path()).unwrap();

        let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(second.status, LoadStatus::FreshlyGenerated);
        assert_ne!(second.identity.installation_id, original_id);
        // 一次・二次は新しい同一 identity で復元済み。
        assert!(paths.primary_path().exists());
        assert!(paths.secondary_path().exists());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn stage3_is_independent_of_plugin_history_mtime() {
        let (paths, root) = isolated_paths();
        let pre_dir = paths
            .plugin_data_dir()
            .join("p1")
            .join("iid-newest")
            .join("pre");
        fs::create_dir_all(&pre_dir).unwrap();
        let old_file = pre_dir.join("a.json");
        let new_file = pre_dir.join("b.json");
        fs::write(&old_file, r#"{"installation_id":"old-id"}"#).unwrap();
        std::thread::sleep(std::time::Duration::from_millis(50));
        fs::write(&new_file, r#"{"installation_id":"new-id"}"#).unwrap();

        let loaded = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(loaded.status, LoadStatus::FreshlyGenerated);
        assert_ne!(loaded.identity.installation_id, "old-id");
        assert_ne!(loaded.identity.installation_id, "new-id");
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn different_machine_detected() {
        let (paths, root) = isolated_paths();
        let _ = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        // 別マシンの 3 要素で再起動
        let second = load_or_recover(&paths, hc("X", "Y", "Z"), License::Os).unwrap();
        assert_eq!(second.status, LoadStatus::DifferentMachine);
        assert!(!second.status.allow_measurement());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn two_of_three_matches_counts_as_same() {
        let (paths, root) = isolated_paths();
        let _ = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        // 2 要素一致 (bd のみ変化) → Same
        let second = load_or_recover(&paths, hc("A", "B", "CHANGED"), License::Os).unwrap();
        assert_eq!(second.status, LoadStatus::PrimaryOk);
        assert!(second.status.allow_measurement());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn insufficient_components_permissive_continues() {
        let (paths, root) = isolated_paths();
        // 1 要素のみで登録
        let _ = load_or_recover(&paths, hc("A", "", ""), License::Os).unwrap();
        // 同じ 1 要素で再起動 → Insufficient
        let second = load_or_recover(&paths, hc("A", "", ""), License::Os).unwrap();
        assert_eq!(second.status, LoadStatus::Insufficient);
        assert!(second.status.allow_measurement());
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn atomic_write_does_not_leave_tmp() {
        let (paths, root) = isolated_paths();
        let _ = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(
            crate::atomic_file::remove_temp_siblings(&paths.primary_path()).unwrap(),
            0
        );
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn identity_cache_detects_mtime_change() {
        let (paths, root) = isolated_paths();
        let loaded = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(loaded.identity.license, License::Os);

        let mut cache = IdentityCache::new(paths.clone(), loaded.identity.clone());

        // 一次を license="sense" に書き換える
        let mut modified = loaded.identity.clone();
        modified.license = License::Sense;
        // mtime を確実に変化させる
        std::thread::sleep(std::time::Duration::from_millis(20));
        write_identity_atomic(&paths.primary_path(), &modified).unwrap();

        // キャッシュ経由で再取得 → Sense が返る
        assert_eq!(cache.current_license(), License::Sense);
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn stage2_corrupt_primary_falls_through() {
        let (paths, root) = isolated_paths();
        let first = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        // 一次を壊す
        fs::write(paths.primary_path(), "not a json").unwrap();
        let second = load_or_recover(&paths, hc("A", "B", "C"), License::Os).unwrap();
        assert_eq!(second.status, LoadStatus::RecoveredFromSecondary);
        assert_eq!(
            first.identity.installation_id,
            second.identity.installation_id
        );
        let _ = fs::remove_dir_all(root);
    }

    // ── load_installation_id_from（サブ3-A-2）─────────────────────────

    fn isolated_id_path(name: &str) -> PathBuf {
        let n = TEST_COUNTER.fetch_add(1, Ordering::SeqCst);
        let pid = std::process::id();
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or(0);
        let root = std::env::temp_dir()
            .join("kirin_hypha_id_test")
            .join(format!("{}-{}-{}", pid, now, n));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root.join(name)
    }

    #[test]
    fn load_installation_id_reads_valid_field() {
        let path = isolated_id_path("identity.json");
        fs::write(&path, r#"{"installation_id": "abc-123-uuid"}"#).unwrap();
        assert_eq!(
            load_installation_id_from(&path),
            Some("abc-123-uuid".to_string())
        );
    }

    #[test]
    fn load_installation_id_reads_from_full_schema() {
        let path = isolated_id_path("identity.json");
        let full = r#"{
            "schema_version": "1.0",
            "installation_id": "full-schema-uuid",
            "hardware_id": "x",
            "hardware_components": {"iop": "a", "sn": "b", "bd": "c"},
            "machine_signature": "x",
            "license": "os",
            "created_at": "2026-04-19T00:00:00Z",
            "last_verified_at": "2026-04-19T00:00:00Z"
        }"#;
        fs::write(&path, full).unwrap();
        assert_eq!(
            load_installation_id_from(&path),
            Some("full-schema-uuid".to_string())
        );
    }

    #[test]
    fn load_installation_id_returns_none_on_missing_file() {
        let path = isolated_id_path("does_not_exist.json");
        assert_eq!(load_installation_id_from(&path), None);
    }

    #[test]
    fn load_installation_id_returns_none_on_invalid_json() {
        let path = isolated_id_path("identity.json");
        fs::write(&path, "not a json").unwrap();
        assert_eq!(load_installation_id_from(&path), None);
    }

    #[test]
    fn load_installation_id_returns_none_on_missing_field() {
        let path = isolated_id_path("identity.json");
        fs::write(&path, r#"{"license": "os"}"#).unwrap();
        assert_eq!(load_installation_id_from(&path), None);
    }

    #[test]
    fn load_installation_id_returns_none_on_empty_field() {
        let path = isolated_id_path("identity.json");
        fs::write(&path, r#"{"installation_id": ""}"#).unwrap();
        assert_eq!(load_installation_id_from(&path), None);
    }

    #[test]
    fn load_installation_id_returns_none_on_non_string_field() {
        // Guardrail: future schema change storing installation_id as number must fail safely.
        let path = isolated_id_path("identity.json");
        fs::write(&path, r#"{"installation_id": 42}"#).unwrap();
        assert_eq!(load_installation_id_from(&path), None);
    }

    // ── cleanup_legacy_v1 (1a-6 / Q4) ─────────────────────────────────────

    #[test]
    fn cleanup_legacy_v1_removes_default_mix_and_writes_flag() {
        let (paths, root) = isolated_paths();
        // 旧構造を擬似的に作成
        let pd = paths.plugin_data_dir();
        let legacy_pre = pd.join("default").join("MIX").join("pre");
        let legacy_post = pd.join("default").join("MIX").join("post");
        let legacy_preset = pd.join("default").join("preset");
        fs::create_dir_all(&legacy_pre).unwrap();
        fs::create_dir_all(&legacy_post).unwrap();
        fs::create_dir_all(&legacy_preset).unwrap();
        fs::write(legacy_pre.join("a.json"), b"{}").unwrap();
        fs::write(legacy_post.join("b.json"), b"{}").unwrap();
        fs::write(legacy_preset.join("c.json"), b"{}").unwrap();

        let report = cleanup_legacy_v1(&paths);
        assert!(report.ran);
        assert_eq!(report.errors, 0);
        assert!(
            report.removed >= 2,
            "MIX + preset must be removed: {report:?}"
        );

        // 旧構造が消えている
        assert!(!pd.join("default").join("MIX").exists());
        assert!(!pd.join("default").join("preset").exists());
        // flag が書かれている
        assert!(paths.kirin_os_root.join(CLEANUP_V1_DONE_FILENAME).exists());

        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn cleanup_legacy_v1_is_idempotent_via_flag() {
        let (paths, root) = isolated_paths();
        // 初回（旧構造なし）
        let r1 = cleanup_legacy_v1(&paths);
        assert!(r1.ran);
        // 2 回目: flag が立っているのでスキップ
        let r2 = cleanup_legacy_v1(&paths);
        assert!(!r2.ran, "second run must be skipped: {r2:?}");
        assert_eq!(r2.removed, 0);
        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn cleanup_legacy_v1_preserves_new_structure() {
        let (paths, root) = isolated_paths();
        let pd = paths.plugin_data_dir();
        // 新構造（残しておくべき）
        let new_pre = pd.join("ph-new").join("iid-A").join("pre");
        fs::create_dir_all(&new_pre).unwrap();
        fs::write(new_pre.join("keep.json"), b"{}").unwrap();
        // 旧構造（消えるべき）
        let legacy = pd.join("default").join("MIX").join("pre");
        fs::create_dir_all(&legacy).unwrap();
        fs::write(legacy.join("drop.json"), b"{}").unwrap();

        cleanup_legacy_v1(&paths);

        assert!(
            new_pre.join("keep.json").exists(),
            "new structure preserved"
        );
        assert!(!legacy.exists(), "legacy structure removed");
        let _ = fs::remove_dir_all(root);
    }
}
