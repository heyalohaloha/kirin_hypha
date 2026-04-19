//! plugin_data v1.1 writer — Record mode 計測結果の永続化。
//!
//! guardian_58_hypha_step5_record_plugin_data.md T-2 対応。
//! スキーマ正本: `guardian_50_plugin_design_complete_v3.md` 行357-402。
//!
//! # ファイル命名（G-50-30）
//! ```text
//! plugin_data/{project_hash}/{bus}/pre/{compact_wall_clock}.json
//! plugin_data/{project_hash}/{bus}/post/{compact_wall_clock}.json
//! ```
//! `compact_wall_clock` = `%Y%m%dT%H%M%S`（例: `20260417T143208`）
//!
//! # 書込方法
//! 1. 一時ファイル `{filename}.tmp` に追記
//! 2. 30秒毎に `rename()` で atomic flush（D-1 対策）
//! 3. 最後に `checksum` フィールドへ HMAC-SHA256 埋め込み
//! 4. 正常終了時に `status="closed"` 更新
//!
//! # 解像度（G-50-17）
//! | モード | frames[] | psb_snapshots[] |
//! |--------|---------|-----------------|
//! | Record リアルタイム | 10 fps | 2 fps |
//! | Record バウンス | 93 fps | 2 fps |
//!
//! caller（Plugin 側、T-6 で統合）が解像度に従って `append_frame` / `append_psb`
//! を呼ぶ。本モジュールは間引きロジックを持たない。
//!
//! # 数値精度（正本 行405-414）
//! append 時点で丸める（JSON に現れる数値がそのまま丸め後）。
//!
//! # HMAC checksum
//! トップレベル `"checksum"` フィールド。`checksum=""` で JSON 化した全バイトに対する
//! HMAC-SHA256 hex。鍵は [`crate::identity`] と同じ（ビルド時埋め込み）。
//!
//! # T-2 のスコープ
//! - スキーマ型定義（serde 相互変換）
//! - `PluginDataWriter`（append / heartbeat / flush / close / compact filename）
//!
//! T-6 の Plugin 統合で実際に Phase D / Step-1 計測値を流し込む。

use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

type HmacSha256 = Hmac<Sha256>;

// ── Role ─────────────────────────────────────────────────────────────────────

/// Plugin role（PRE / POST）。ファイルパス構築 + `role` フィールドに使用。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "UPPERCASE")]
pub enum Role {
    Pre,
    Post,
}

impl Role {
    /// ディレクトリ名（lowercase）。
    pub fn dir_name(self) -> &'static str {
        match self {
            Self::Pre => "pre",
            Self::Post => "post",
        }
    }

    /// JSON `role` フィールド値（uppercase）。
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Pre => "PRE",
            Self::Post => "POST",
        }
    }
}

// ── Status ───────────────────────────────────────────────────────────────────

/// 計測ファイルの生存状態（G-50-33）。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Status {
    Active,
    Closed,
}

// ── Schema 型 ────────────────────────────────────────────────────────────────

/// 1 frame = Step-1 4 項目 + Phase D 2 項目（PSB は別ブロック）。
///
/// n_prime は **20 Bark 帯域別** のフィルタ後 N'(t,z)（sone）。
/// sharpness は **スカラー**（acum）。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Frame {
    pub t_ms: u64,
    pub n_prime: [f64; 20],
    pub sharpness: f64,
    pub lufs_m: f64,
    pub true_peak: f64,
    pub crest: f64,
}

/// PSB スナップショット（2 fps 別ブロック）。
///
/// `interpolatable=true` は「前後スナップショット間を線形補間してよい」のマーカ。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PsbSnapshot {
    pub t_ms: u64,
    pub psb: [f64; 20],
    pub interpolatable: bool,
}

/// 利用者メモ（「メモを残す」タップ時に追加）。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Annotation {
    /// ISO 8601 wall clock.
    pub t: String,
    pub memo: String,
}

/// バウンスマーカ（G-50-18）。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BounceMarker {
    pub wall_clock_start: String,
    pub wall_clock_end: String,
    pub duration_samples: u64,
    pub first_block_hash: String,
    pub last_block_hash: String,
}

/// plugin_data/ 1 ファイル分のルート（v1.1）。
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginDataFile {
    pub schema_version: String,
    pub installation_id: String,
    pub project_hash: String,
    pub timestamp: String,
    pub role: Role,
    pub bus: String,
    pub mode: String,
    pub chain_memo: String,
    pub sample_rate: u32,
    pub bounce_marker: BounceMarker,
    pub heartbeat: String,
    pub status: Status,
    pub frames: Vec<Frame>,
    pub psb_snapshots: Vec<PsbSnapshot>,
    pub annotations: Vec<Annotation>,
    pub validity: bool,
    pub checksum: String,
}

impl PluginDataFile {
    pub const SCHEMA_VERSION: &'static str = "1.1";

    /// 空の v1.1 ファイルを生成（heartbeat = timestamp = now, status=active, checksum=空）。
    pub fn new(
        installation_id: String,
        project_hash: String,
        role: Role,
        bus: String,
        sample_rate: u32,
    ) -> Self {
        let now = now_iso8601();
        Self {
            schema_version: Self::SCHEMA_VERSION.to_string(),
            installation_id,
            project_hash,
            timestamp: now.clone(),
            role,
            bus,
            mode: "record".to_string(),
            chain_memo: String::new(),
            sample_rate,
            bounce_marker: BounceMarker {
                wall_clock_start: now.clone(),
                wall_clock_end: now.clone(),
                duration_samples: 0,
                first_block_hash: String::new(),
                last_block_hash: String::new(),
            },
            heartbeat: now,
            status: Status::Active,
            frames: Vec::new(),
            psb_snapshots: Vec::new(),
            annotations: Vec::new(),
            validity: true,
            checksum: String::new(),
        }
    }
}

// ── Writer ───────────────────────────────────────────────────────────────────

/// 書込エラー。
#[derive(Debug)]
pub enum WriterError {
    Io(io::Error),
    Serde(serde_json::Error),
}

impl std::fmt::Display for WriterError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "IO error: {e}"),
            Self::Serde(e) => write!(f, "JSON error: {e}"),
        }
    }
}

impl std::error::Error for WriterError {}

impl From<io::Error> for WriterError {
    fn from(e: io::Error) -> Self {
        Self::Io(e)
    }
}

impl From<serde_json::Error> for WriterError {
    fn from(e: serde_json::Error) -> Self {
        Self::Serde(e)
    }
}

/// plugin_data/ への書込ハンドル。
///
/// 1 Record セッション = 1 Writer。
///
/// # 書込フロー
/// ```ignore
/// let paths = WriterPaths::build(&base, &project_hash, &bus, Role::Pre, &wall_clock_start);
/// let mut w = PluginDataWriter::create(paths, installation_id, project_hash, Role::Pre, bus, 48000)?;
///
/// // 計測中: caller が解像度に従って append
/// w.append_frame(frame)?;
/// w.append_psb(psb)?;
///
/// // 30 秒毎に flush（atomic rename）
/// w.heartbeat_now();
/// w.flush()?;
///
/// // 終了時
/// w.close()?;
/// ```
#[derive(Debug)]
pub struct PluginDataWriter {
    paths: WriterPaths,
    data: PluginDataFile,
}

/// ファイルパス一式（tmp + final）。
#[derive(Debug, Clone)]
pub struct WriterPaths {
    /// 最終 JSON（`{compact_wall_clock}.json`）。
    pub final_path: PathBuf,
    /// 書込中の tmp（`{compact_wall_clock}.json.tmp`）。
    pub tmp_path: PathBuf,
}

impl WriterPaths {
    /// `plugin_data/{project_hash}/{bus}/{role_dir}/{compact}.json` を構築。
    ///
    /// `wall_clock_start` は ISO 8601（秒精度）。compact 形式への変換は
    /// [`compact_wall_clock`] を使用。
    pub fn build(
        base_dir: &Path,
        project_hash: &str,
        bus: &str,
        role: Role,
        wall_clock_start_iso: &str,
    ) -> Self {
        let compact = compact_wall_clock(wall_clock_start_iso);
        let dir = base_dir
            .join(project_hash)
            .join(bus)
            .join(role.dir_name());
        let final_path = dir.join(format!("{compact}.json"));
        let tmp_path = dir.join(format!("{compact}.json.tmp"));
        Self { final_path, tmp_path }
    }
}

impl PluginDataWriter {
    /// tmp ディレクトリを作成して Writer を起動。最初の flush で空ファイルが rename される。
    pub fn create(
        paths: WriterPaths,
        installation_id: String,
        project_hash: String,
        role: Role,
        bus: String,
        sample_rate: u32,
    ) -> Result<Self, WriterError> {
        if let Some(parent) = paths.final_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let data = PluginDataFile::new(installation_id, project_hash, role, bus, sample_rate);
        Ok(Self { paths, data })
    }

    /// 書込対象の内部データへの読取参照（テスト / デバッグ用）。
    pub fn data(&self) -> &PluginDataFile {
        &self.data
    }

    /// chain_memo を設定（利用者記入。Record 開始時 or 途中更新）。
    pub fn set_chain_memo(&mut self, memo: String) {
        self.data.chain_memo = memo;
    }

    /// バウンス開始時情報を記録（最初のブロックハッシュ等）。
    pub fn set_bounce_start(&mut self, wall_clock: String, first_block_hash: String) {
        self.data.bounce_marker.wall_clock_start = wall_clock;
        self.data.bounce_marker.first_block_hash = first_block_hash;
    }

    /// バウンス終了時情報を更新（呼出時点の累積情報）。
    pub fn set_bounce_end(
        &mut self,
        wall_clock: String,
        duration_samples: u64,
        last_block_hash: String,
    ) {
        self.data.bounce_marker.wall_clock_end = wall_clock;
        self.data.bounce_marker.duration_samples = duration_samples;
        self.data.bounce_marker.last_block_hash = last_block_hash;
    }

    /// 1 frame を追加。数値を精度表に従って丸める。
    pub fn append_frame(
        &mut self,
        t_ms: u64,
        n_prime: [f64; 20],
        sharpness: f64,
        lufs_m: f64,
        true_peak: f64,
        crest: f64,
    ) {
        let mut rounded_n = [0.0; 20];
        for (i, v) in n_prime.iter().enumerate() {
            rounded_n[i] = round1(*v);
        }
        self.data.frames.push(Frame {
            t_ms,
            n_prime: rounded_n,
            sharpness: round2(sharpness),
            lufs_m: round1(lufs_m),
            true_peak: round1(true_peak),
            crest: round1(crest),
        });
    }

    /// 1 PSB スナップショットを追加。
    pub fn append_psb(&mut self, t_ms: u64, psb: [f64; 20], interpolatable: bool) {
        let mut rounded = [0.0; 20];
        for (i, v) in psb.iter().enumerate() {
            rounded[i] = round1(*v);
        }
        self.data.psb_snapshots.push(PsbSnapshot {
            t_ms,
            psb: rounded,
            interpolatable,
        });
    }

    /// 利用者メモを追加（「メモを残す」タップ）。
    pub fn append_annotation(&mut self, memo: String) {
        self.data.annotations.push(Annotation {
            t: now_iso8601(),
            memo,
        });
    }

    /// heartbeat を現在時刻に更新（T-3 と共通。30 秒毎に caller が呼ぶ）。
    pub fn heartbeat_now(&mut self) {
        self.data.heartbeat = now_iso8601();
    }

    /// validity フラグ（T-8 セルフチェック結果）。
    pub fn set_validity(&mut self, valid: bool) {
        self.data.validity = valid;
    }

    /// atomic flush: checksum 計算 → `.tmp` 書込 → `rename()` で最終パスに置換。
    ///
    /// rename は POSIX では同一 FS 内で atomic。途中クラッシュ時も最終ファイルは
    /// 旧状態 or 新状態のどちらかにしかならない（D-1 対策）。
    pub fn flush(&mut self) -> Result<(), WriterError> {
        self.data.checksum = compute_checksum(&self.data)?;
        let json = serde_json::to_string(&self.data)?;
        // `.tmp` へ書込
        fs::write(&self.paths.tmp_path, json.as_bytes())?;
        // atomic rename
        fs::rename(&self.paths.tmp_path, &self.paths.final_path)?;
        Ok(())
    }

    /// status=closed に変更して最終 flush。正常終了時に呼ぶ。
    pub fn close(mut self) -> Result<(), WriterError> {
        self.data.status = Status::Closed;
        self.flush()
    }
}

// ── Annotation 追記（サブ2-C / Note ボタン）──────────────────────────────────

/// `plugin_data/{project_hash}/{bus}/{role_dir}/` 配下の最新 `*.json` に 1 件
/// annotation を追記して atomic rename で書き戻す。checksum は再計算される。
///
/// # 戻り値
/// - `Ok(true)`: 対象ファイルが見つかり追記成功
/// - `Ok(false)`: 対象ディレクトリが空 or `*.json` 不在（Record 開始前など）。
///   呼出側はスタブ動作として扱う（toast は表示するが実ファイル更新なし）。
/// - `Err(WriterError)`: 読み込み / JSON parse / 書込エラー
///
/// 「最新」は同一ディレクトリ内の filename 辞書順最大を使う（`{compact}.json` が
/// `YYYYMMDDTHHMMSS.json` 形式なので辞書順 = 時系列順）。mtime ではなく filename を
/// 使うことで再現性・テスタビリティを確保。
pub fn append_annotation_to_latest(
    base_dir: &Path,
    project_hash: &str,
    bus: &str,
    role: Role,
    memo: String,
) -> Result<bool, WriterError> {
    let dir = base_dir
        .join(project_hash)
        .join(bus)
        .join(role.dir_name());
    let Some(latest) = find_latest_json(&dir) else {
        return Ok(false);
    };
    let bytes = fs::read(&latest)?;
    let mut data: PluginDataFile = serde_json::from_slice(&bytes)?;
    data.annotations.push(Annotation {
        t: now_iso8601(),
        memo,
    });
    data.checksum = compute_checksum(&data)?;
    let json = serde_json::to_vec(&data)?;
    let mut tmp_os = latest.as_os_str().to_os_string();
    tmp_os.push(".tmp");
    let tmp = PathBuf::from(tmp_os);
    fs::write(&tmp, &json)?;
    fs::rename(&tmp, &latest)?;
    Ok(true)
}

/// `dir` 直下の `*.json` のうち filename 辞書順最大を返す。
/// 不在・読込失敗は `None`。
fn find_latest_json(dir: &Path) -> Option<PathBuf> {
    let entries = fs::read_dir(dir).ok()?;
    entries
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|e| e == "json"))
        .max()
}

// ── ヘルパ ────────────────────────────────────────────────────────────────────

/// ISO 8601（秒精度・UTC）。
fn now_iso8601() -> String {
    chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string()
}

/// ISO 8601 (`2026-04-17T14:32:08Z`) → compact (`20260417T143208`).
///
/// タイムゾーン記号（`Z` / `+09:00`）と区切り（`-` / `:`）を除去するだけ。
/// 秒未満（`.123`）も除去。書式が想定と違えば「非数字・非 T を除去」の安全側に倒す。
pub fn compact_wall_clock(iso: &str) -> String {
    iso.chars()
        .take_while(|c| c.is_ascii_digit() || matches!(*c, '-' | ':' | 'T'))
        .filter(|c| c.is_ascii_digit() || *c == 'T')
        .collect()
}

/// 1 桁丸め。
fn round1(v: f64) -> f64 {
    if v.is_finite() {
        (v * 10.0).round() / 10.0
    } else {
        v
    }
}

/// 2 桁丸め。
fn round2(v: f64) -> f64 {
    if v.is_finite() {
        (v * 100.0).round() / 100.0
    } else {
        v
    }
}

/// `checksum=""` にした状態の JSON バイト列に対して HMAC-SHA256 を計算。
fn compute_checksum(data: &PluginDataFile) -> Result<String, serde_json::Error> {
    let mut clone = data.clone();
    clone.checksum = String::new();
    let bytes = serde_json::to_vec(&clone)?;
    let mut mac = HmacSha256::new_from_slice(hmac_key())
        .expect("HMAC-SHA256 accepts any key length");
    mac.update(&bytes);
    Ok(hex::encode(mac.finalize().into_bytes()))
}

/// HMAC 鍵（identity.rs と同じ方針。ビルド時埋め込み）。
fn hmac_key() -> &'static [u8] {
    match option_env!("KIRIN_HYPHA_HMAC_KEY") {
        Some(k) => k.as_bytes(),
        None => DEFAULT_HMAC_KEY,
    }
}

const DEFAULT_HMAC_KEY: &[u8] =
    b"kirin-hypha-phase1.0-hmac-key-deterrent-level-20260417";

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

// ── Tests ────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};

    /// 各テスト専用ディレクトリ（並列実行でも衝突しない）。
    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_plugin_data_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn sample_writer(base: &Path, role: Role) -> PluginDataWriter {
        let paths = WriterPaths::build(
            base,
            "project_hash_test",
            "MIX",
            role,
            "2026-04-17T14:32:08Z",
        );
        PluginDataWriter::create(
            paths,
            "11111111-2222-4333-8444-555555555555".to_string(),
            "project_hash_test".to_string(),
            role,
            "MIX".to_string(),
            48000,
        )
        .unwrap()
    }

    #[test]
    fn role_string_conversion() {
        assert_eq!(Role::Pre.as_str(), "PRE");
        assert_eq!(Role::Post.as_str(), "POST");
        assert_eq!(Role::Pre.dir_name(), "pre");
        assert_eq!(Role::Post.dir_name(), "post");
    }

    #[test]
    fn compact_wall_clock_strips_separators() {
        assert_eq!(
            compact_wall_clock("2026-04-17T14:32:08Z"),
            "20260417T143208"
        );
        assert_eq!(
            compact_wall_clock("2026-04-17T14:32:08.123Z"),
            "20260417T143208"
        );
        assert_eq!(
            compact_wall_clock("2026-04-17T14:32:08+09:00"),
            "20260417T143208"
        );
    }

    #[test]
    fn writer_paths_build_hierarchy() {
        let base = Path::new("/tmp/kirin_base");
        let p = WriterPaths::build(base, "ph", "MIX", Role::Pre, "2026-04-17T14:32:08Z");
        assert_eq!(
            p.final_path,
            Path::new("/tmp/kirin_base/ph/MIX/pre/20260417T143208.json")
        );
        assert_eq!(
            p.tmp_path,
            Path::new("/tmp/kirin_base/ph/MIX/pre/20260417T143208.json.tmp")
        );
    }

    #[test]
    fn new_file_has_schema_1_1_defaults() {
        let f = PluginDataFile::new(
            "iid".to_string(),
            "ph".to_string(),
            Role::Post,
            "DRUM".to_string(),
            48000,
        );
        assert_eq!(f.schema_version, "1.1");
        assert_eq!(f.role, Role::Post);
        assert_eq!(f.bus, "DRUM");
        assert_eq!(f.mode, "record");
        assert_eq!(f.status, Status::Active);
        assert!(f.frames.is_empty());
        assert!(f.psb_snapshots.is_empty());
        assert!(f.annotations.is_empty());
        assert_eq!(f.sample_rate, 48000);
        assert!(f.validity);
        assert!(f.checksum.is_empty());
    }

    #[test]
    fn append_frame_applies_rounding() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        let n = [1.2345; 20];
        w.append_frame(123, n, 1.2345, -14.2345, -1.1234, 12.3456);
        let frame = &w.data().frames[0];
        assert_eq!(frame.t_ms, 123);
        assert_eq!(frame.n_prime[0], 1.2); // round1
        assert_eq!(frame.sharpness, 1.23); // round2
        assert_eq!(frame.lufs_m, -14.2);
        assert_eq!(frame.true_peak, -1.1);
        assert_eq!(frame.crest, 12.3);
    }

    #[test]
    fn append_psb_applies_rounding_and_flag() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        let psb = [-12.3456; 20];
        w.append_psb(500, psb, true);
        let snap = &w.data().psb_snapshots[0];
        assert_eq!(snap.t_ms, 500);
        assert!(snap.interpolatable);
        assert_eq!(snap.psb[0], -12.3);
    }

    #[test]
    fn append_annotation_records_wallclock_and_memo() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_annotation("低域が重い".to_string());
        let a = &w.data().annotations[0];
        assert_eq!(a.memo, "低域が重い");
        assert!(!a.t.is_empty());
    }

    #[test]
    fn heartbeat_updates_on_demand() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        let before = w.data().heartbeat.clone();
        std::thread::sleep(std::time::Duration::from_secs(1));
        w.heartbeat_now();
        let after = w.data().heartbeat.clone();
        assert_ne!(before, after);
    }

    #[test]
    fn flush_writes_final_file_atomically() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [0.5; 20], 1.0, -20.0, -3.0, 10.0);
        w.flush().unwrap();
        let final_path = &w.paths.final_path;
        let tmp_path = &w.paths.tmp_path;
        assert!(final_path.exists(), "final file must exist: {final_path:?}");
        assert!(!tmp_path.exists(), "tmp file must be renamed away");
    }

    #[test]
    fn flush_produces_valid_checksum_roundtrip() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.5, -14.0, -1.0, 12.0);
        w.append_psb(0, [-10.0; 20], false);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert!(verify_checksum(&loaded), "checksum must round-trip");
        assert_eq!(loaded.schema_version, "1.1");
        assert_eq!(loaded.frames.len(), 1);
    }

    #[test]
    fn tampered_frame_fails_checksum() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.5, -14.0, -1.0, 12.0);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.final_path).unwrap();
        let mut loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        // 改ざん
        loaded.frames[0].lufs_m = -99.0;
        assert!(!verify_checksum(&loaded));
    }

    #[test]
    fn close_marks_status_closed_and_flushes() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0);
        let final_path = w.paths.final_path.clone();
        w.close().unwrap();
        let bytes = fs::read(&final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.status, Status::Closed);
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn post_writes_to_post_dir() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.flush().unwrap();
        let p = w.paths.final_path.to_string_lossy().to_string();
        assert!(p.contains("/post/"), "path should contain /post/: {p}");
    }

    #[test]
    fn bounce_marker_roundtrip() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.set_bounce_start("2026-04-17T14:32:08Z".to_string(), "hash_first".into());
        w.set_bounce_end(
            "2026-04-17T14:37:08Z".to_string(),
            14_400_000,
            "hash_last".into(),
        );
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.bounce_marker.wall_clock_start, "2026-04-17T14:32:08Z");
        assert_eq!(loaded.bounce_marker.duration_samples, 14_400_000);
        assert_eq!(loaded.bounce_marker.first_block_hash, "hash_first");
        assert_eq!(loaded.bounce_marker.last_block_hash, "hash_last");
    }

    #[test]
    fn chain_memo_set_and_persisted() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.set_chain_memo("test memo".to_string());
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.chain_memo, "test memo");
    }

    #[test]
    fn validity_flag_defaults_true_can_flip_false() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        assert!(w.data().validity);
        w.set_validity(false);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert!(!loaded.validity);
    }

    #[test]
    fn multiple_flushes_keep_consistency() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Pre);
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0);
        w.flush().unwrap();
        w.append_frame(100, [1.5; 20], 1.2, -13.0, -0.5, 11.0);
        w.flush().unwrap();
        let bytes = fs::read(&w.paths.final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.frames.len(), 2);
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn round1_handles_negative_and_special() {
        assert_eq!(round1(-1.25), -1.3);
        assert_eq!(round1(0.0), 0.0);
        assert!(round1(f64::NAN).is_nan());
        assert_eq!(round1(f64::INFINITY), f64::INFINITY);
    }

    #[test]
    fn round2_handles_negative_and_special() {
        assert_eq!(round2(-1.235), -1.24);
        assert_eq!(round2(0.0), 0.0);
        assert!(round2(f64::NAN).is_nan());
    }

    // ── append_annotation_to_latest（サブ2-C / Note ボタン）─────────────────

    #[test]
    fn append_annotation_to_latest_returns_false_when_dir_missing() {
        let base = isolated_dir();
        // project_hash/bus/post ディレクトリ自体が無い状態
        let result = append_annotation_to_latest(
            &base,
            "ph",
            "MIX",
            Role::Post,
            "Good".to_string(),
        )
        .unwrap();
        assert!(!result, "missing dir should yield stub (Ok(false))");
    }

    #[test]
    fn append_annotation_to_latest_returns_false_when_no_json() {
        let base = isolated_dir();
        let dir = base.join("ph").join("MIX").join("post");
        fs::create_dir_all(&dir).unwrap();
        // .tmp だけあって .json が無い
        fs::write(dir.join("20260417T140000.json.tmp"), b"{}").unwrap();
        let result = append_annotation_to_latest(
            &base,
            "ph",
            "MIX",
            Role::Post,
            "Good".to_string(),
        )
        .unwrap();
        assert!(!result);
    }

    #[test]
    fn append_annotation_to_latest_updates_file_and_checksum() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.append_frame(0, [1.0; 20], 1.0, -14.0, -1.0, 12.0);
        w.flush().unwrap();
        let path = w.paths.final_path.clone();

        // append_annotation_to_latest 実行
        let ok = append_annotation_to_latest(
            &base,
            "project_hash_test",
            "MIX",
            Role::Post,
            "Fix".to_string(),
        )
        .unwrap();
        assert!(ok);

        // 再読込: annotation が 1 件追加され、checksum 整合
        let bytes = fs::read(&path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.annotations.len(), 1);
        assert_eq!(loaded.annotations[0].memo, "Fix");
        assert!(!loaded.annotations[0].t.is_empty());
        assert!(verify_checksum(&loaded), "checksum must re-sign after append");
    }

    #[test]
    fn append_annotation_to_latest_picks_lexicographic_max_filename() {
        let base = isolated_dir();
        let dir = base.join("ph").join("MIX").join("post");
        fs::create_dir_all(&dir).unwrap();

        // 2 ファイルを手動作成。compact filename の辞書順 = 時系列順
        for stamp in ["20260417T140000", "20260417T150000", "20260417T143000"] {
            let paths = WriterPaths {
                final_path: dir.join(format!("{stamp}.json")),
                tmp_path: dir.join(format!("{stamp}.json.tmp")),
            };
            let mut w = PluginDataWriter::create(
                paths,
                "iid".to_string(),
                "ph".to_string(),
                Role::Post,
                "MIX".to_string(),
                48000,
            )
            .unwrap();
            w.flush().unwrap();
        }

        let ok = append_annotation_to_latest(
            &base,
            "ph",
            "MIX",
            Role::Post,
            "Hold".to_string(),
        )
        .unwrap();
        assert!(ok);

        // 最新ファイル（15:00:00）にのみ annotation が入る
        let latest = fs::read(dir.join("20260417T150000.json")).unwrap();
        let other = fs::read(dir.join("20260417T140000.json")).unwrap();
        let latest: PluginDataFile = serde_json::from_slice(&latest).unwrap();
        let other: PluginDataFile = serde_json::from_slice(&other).unwrap();
        assert_eq!(latest.annotations.len(), 1);
        assert_eq!(latest.annotations[0].memo, "Hold");
        assert!(other.annotations.is_empty());
    }

    #[test]
    fn append_annotation_to_latest_appends_to_existing_annotations() {
        let base = isolated_dir();
        let mut w = sample_writer(&base, Role::Post);
        w.append_annotation("初期メモ".to_string());
        w.flush().unwrap();

        let ok = append_annotation_to_latest(
            &base,
            "project_hash_test",
            "MIX",
            Role::Post,
            "Good".to_string(),
        )
        .unwrap();
        assert!(ok);

        let bytes = fs::read(&w.paths.final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.annotations.len(), 2);
        assert_eq!(loaded.annotations[0].memo, "初期メモ");
        assert_eq!(loaded.annotations[1].memo, "Good");
        assert!(verify_checksum(&loaded));
    }

    #[test]
    fn append_annotation_to_latest_errors_on_corrupt_json() {
        let base = isolated_dir();
        let dir = base.join("ph").join("MIX").join("post");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("20260417T140000.json"), b"not a json").unwrap();
        let err = append_annotation_to_latest(
            &base,
            "ph",
            "MIX",
            Role::Post,
            "Good".to_string(),
        );
        assert!(err.is_err(), "corrupt JSON should yield error");
    }
}
