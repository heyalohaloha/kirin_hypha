//! PRE 側 filesystem-discovery (B-022 Phase 1B / G-115-36)。
//!
//! cdylib 隔離下で PRE / POST が独立した `static OnceLock` インスタンスを持ち、
//! `params.project_uuid` (chunk-restored) も別物となるため、PRE 側 plugin_data
//! が POST と別 project_uuid 空間に生成され、cross-instance pair 復元 v1.2 (a)
//! が破綻する。本モジュールはその対策として:
//!
//! 1. PRE IO Thread が `~/Library/Application Support/Kirin OS/plugin_data/`
//!    配下を 1 秒 throttle で scan
//! 2. 各 `{any_uuid}/record_signal/*.json` を読み、
//!    `target_pre_instance_id == 自 PRE の instance_id` を満たす signal を探す
//! 3. 該当 signal が存在する `{any_uuid}/` を **POST 側 project_uuid** として
//!    返す
//! 4. PRE IO Thread はその project_uuid を `effective_project_hash` として
//!    `poll_record_signal` / `run_record_tick` に渡し、PRE の plugin_data
//!    出力先を POST と同じ project_uuid 空間に揃える
//!
//! # 設計対比 (B-021 POST 側 `pre_discovery::discover_active_pre_dir`)
//! - POST 側: `$TMPDIR/kirin/{any_uuid}/{instance_id}/pre.json` の mtime 最新
//!   を返す (`pre_discovery.rs`)
//! - PRE 側 (本モジュール): `plugin_data/{any_uuid}/record_signal/*.json` の
//!   `target_pre_instance_id` 一致 + mtime 最新を返す
//!
//! いずれも 1 秒 throttle / stale 閾値 / mtime 最新優先 / R-28 機能的沈黙の
//! 同パターン。本モジュールは `PostDiscoveryState` と等価な
//! `PreSelfDiscoveryState` を持つ。
//!
//! # R-28 沈黙ゲート
//! - `plugin_data_root` 不在 / 権限なし → `None` (UI エラーなし)
//! - 個別 `{uuid}/record_signal/` の `read_dir` 失敗 → 当該 dir のみ skip
//! - JSON parse 失敗 → 当該 file のみ skip
//! - stale 閾値超え → 当該 signal のみ skip
//! - 一致 signal が 1 件もない → `None`
//!
//! # 公式 doc 参照 (R-11)
//! - `std::fs::read_dir`
//!   <https://doc.rust-lang.org/std/fs/fn.read_dir.html>
//! - `std::fs::Metadata::modified`
//!   <https://doc.rust-lang.org/std/fs/struct.Metadata.html#method.modified>
//!
//! # 段階 1 lazy-read との結合
//! 本関数は呼出側で lazy-read 済みの `&str` を受け取る。`Arc<RwLock<String>>`
//! を引数に取らない（責務分離 / 段階 1 で確立した lazy-read 経路を貫徹する
//! ため）。

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime};

use crate::record_signal::{RecordSignal, SIGNALS_SUBDIR};

/// `record_signal.json` の mtime が `now` から見て本値以上古ければ stale 扱い
/// で除外する閾値 (秒)。
///
/// `pre_discovery::DISCOVERY_STALE_SECS` (= 10) と同値で揃える。POST が
/// record_signal を最後に書き換えたのが本値以上前なら、その POST 自身が
/// 既に消失している可能性が高い。10 秒は POST 側 ack タイムアウト
/// (`record_signal::ACK_TIMEOUT_SECONDS = 30`) より短く設定し、ack 待ち中
/// でも mtime が直近で更新されている前提を残す。
pub const DISCOVERY_STALE_SECS: u64 = 10;

/// `discover_pair_post_project_dir` を呼ぶ通常の最低間隔。
const RESCAN_INTERVAL: Duration = Duration::from_secs(1);

/// POST project をまだ一度も捕捉できていない PRE は、Keep 直後の
/// offline bounce に間に合わせるため 100ms tick で短期探索する。
const FIRST_CAPTURE_RESCAN_INTERVAL: Duration = Duration::from_millis(100);

/// PRE 側の動的 POST 検出キャッシュ。
///
/// PRE IO Thread の `run_tick` 内で `should_rescan(now)` が true のときだけ
/// `record_scan` を呼ぶ。それ以外のループでは `cached_post_project_dir()` /
/// `cached_daw_session_id()` で前回結果を再利用する。
///
/// B-021 `pre_discovery::PostDiscoveryState` と等価な構造。
///
/// # B-022 段階 3 拡張
/// `cached_daw_session_id` を追加。`discover_pair_post_project_dir` が
/// 発見した signal の `daw_session_id` を保持し、PRE 側 io_thread で
/// `effective_daw_session_id_ref` として利用する。これにより cdylib 隔離下
/// (PRE/POST が別 OnceLock 実体) で `daw_session_id_cell()` 値が乖離しても、
/// PRE は **POST が書いた signal の daw_session_id を信じる** 設計で同期を
/// 達成する (G-115-36 補完 / 案 (a))。
#[derive(Debug, Default)]
pub struct PreSelfDiscoveryState {
    cached_post_project_dir: Option<PathBuf>,
    cached_daw_session_id: Option<String>,
    last_scan: Option<Instant>,
}

impl PreSelfDiscoveryState {
    pub fn new() -> Self {
        Self::default()
    }

    /// 前回 scan 結果 (project dir cache)。`run_tick` の毎ループで参照される。
    pub fn cached_post_project_dir(&self) -> Option<&Path> {
        self.cached_post_project_dir.as_deref()
    }

    /// 前回 scan 結果 (daw_session_id cache)。
    ///
    /// B-022 段階 3: cross-process 防壁を「PRE 側 cell 値との比較」から
    /// 「signal 自身が持つ daw_session_id を信じる」方式に切替えるための値。
    /// `target_pre_instance_id` (UUID v4) で 1 PRE↔1 POST の決定論性が確保
    /// されているため (衝突確率 ≈ 2^-122)、二重チェックは冗長。
    pub fn cached_daw_session_id(&self) -> Option<&str> {
        self.cached_daw_session_id.as_deref()
    }

    /// scan から十分経過しているか、まだ一度も scan していない場合のみ true。
    ///
    /// POST project 未捕捉の間だけ 100ms で探索する。1 回捕捉できた後は cache を
    /// None scan で消さないため、通常の 1s cadence に戻る。
    pub fn should_rescan(&self, now: Instant) -> bool {
        let interval = if self.cached_post_project_dir.is_some() {
            RESCAN_INTERVAL
        } else {
            FIRST_CAPTURE_RESCAN_INTERVAL
        };
        match self.last_scan {
            None => true,
            Some(t) => now.duration_since(t) >= interval,
        }
    }

    /// scan 結果を記録する。`should_rescan(now)` が true だった直後に呼ぶ。
    ///
    /// # 入力
    /// `result`: `Some((project_dir, daw_session_id))` で発見成功、`None` で不在。
    ///
    /// B-047 / G-115-247: `None` でも cache はクリアしない。「一度 Some を
    /// 見つけたら以降 None で上書きしない」セマンティクス。Watch mode
    /// (partner=None) 中に discover が miss → cache クリア →
    /// `effective_project_hash_ref` が PRE 自身に flip back → pre.json
    /// 書込先切替で POST 側 ▼ 消失 / DISCOVERY_STALE_SECS 脱落が起きる
    /// 構造的 flipping を抑止する。
    pub fn record_scan(&mut self, now: Instant, result: Option<(PathBuf, String)>) {
        self.last_scan = Some(now);
        if let Some((path, session_id)) = result {
            self.cached_post_project_dir = Some(path);
            self.cached_daw_session_id = Some(session_id);
        }
    }
}

/// `plugin_data_root` (= `~/Library/Application Support/Kirin OS/plugin_data/`)
/// 配下を scan して `target_pre_instance_id == my_instance_id` を満たす
/// `record_signal.json` を持つ `{any_uuid}/` と、その signal の `daw_session_id`
/// を返す。
///
/// # 入力
/// - `plugin_data_root`: 探索ルート (`StoragePaths::default_platform().plugin_data_dir()`
///   の戻り値を呼出側で渡す)
/// - `my_instance_id`: PRE 自身の永続 instance UUID。**呼出側で
///   `read_instance_id_arc(&self.params.instance_id)` 等で lazy-read 済みの
///   値を `&str` で渡す** (段階 1 lazy-read 経路との結合点)
///
/// # 戻り値 (B-022 段階 3 拡張)
/// - 一致 + 非 stale な signal が 1 件以上見つかった →
///   `Some((post_project_dir, signal_daw_session_id))`
///   - `post_project_dir`: mtime 最新の signal を持つ `{any_uuid}/` の `PathBuf`
///   - `signal_daw_session_id`: その signal の `daw_session_id` フィールド
///     値 (PRE 側 io_thread が `effective_daw_session_id_ref` として
///     `poll_record_signal` の filter 条件に使う)
/// - それ以外 → `None`
///
/// # 失敗 mode (R-28 沈黙ゲート)
/// 詳細は module doc 参照。いずれの失敗ケースでも UI エラーは出さず `None` を
/// 返す。
pub fn discover_pair_post_project_dir(
    plugin_data_root: &Path,
    my_instance_id: &str,
) -> Option<(PathBuf, String)> {
    discover_pair_post_project_dir_at(plugin_data_root, my_instance_id, SystemTime::now())
}

/// `discover_pair_post_project_dir` のテスト用変種 (`now` を注入できる)。
///
/// `now` は stale 判定の基準時刻。`SystemTime::now()` を渡すと本番動作と等価。
pub fn discover_pair_post_project_dir_at(
    plugin_data_root: &Path,
    my_instance_id: &str,
    now: SystemTime,
) -> Option<(PathBuf, String)> {
    if my_instance_id.is_empty() {
        // 段階 1 lazy-read が空文字を返すケース (RwLock poisoned 等)。
        // false-positive で全 signal を一致扱いするのを防ぐため早期 return。
        return None;
    }
    let stale_threshold = Duration::from_secs(DISCOVERY_STALE_SECS);

    let project_entries = fs::read_dir(plugin_data_root).ok()?;

    // 候補: (project_dir, daw_session_id, signal_mtime). 最新 mtime を勝者とする。
    let mut best: Option<(PathBuf, String, SystemTime)> = None;

    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }
        // record_signal/ 予約名そのものは project_dir として扱わない
        if project_dir.file_name().and_then(|n| n.to_str()) == Some(SIGNALS_SUBDIR) {
            continue;
        }

        let signals_dir = project_dir.join(SIGNALS_SUBDIR);
        let signal_entries = match fs::read_dir(&signals_dir) {
            Ok(e) => e,
            Err(_) => continue,
        };

        for signal_entry in signal_entries.flatten() {
            let signal_path = signal_entry.path();
            if signal_path.extension().and_then(|s| s.to_str()) != Some("json") {
                continue;
            }
            let bytes = match fs::read(&signal_path) {
                Ok(b) => b,
                Err(_) => continue,
            };
            let parsed: RecordSignal = match serde_json::from_slice(&bytes) {
                Ok(s) => s,
                Err(_) => continue,
            };

            if parsed.target_pre_instance_id != my_instance_id {
                continue;
            }

            let meta = match fs::metadata(&signal_path) {
                Ok(m) => m,
                Err(_) => continue,
            };
            let mtime = match meta.modified() {
                Ok(t) => t,
                Err(_) => continue,
            };

            // stale 判定: now - mtime > threshold なら除外。
            // future mtime (clock skew) は安全側で fresh 扱い。
            if let Ok(age) = now.duration_since(mtime) {
                if age > stale_threshold {
                    continue;
                }
            }

            // mtime 最新を残す。tie-break は後続走査結果で上書き = ファイル
            // 探索順序依存だが、現実には 2 signal が同 mtime に揃うことは
            // ほぼ無く、揃った場合でも結果はいずれかの正規 project_dir。
            //
            // B-022 段階 3: signal 自身の `daw_session_id` を best と一緒に
            // 保持する。空文字でも信じる (legacy schema fallback /
            // `record_signal.rs:90` `#[serde(default)]`)。
            best = match best {
                Some((_, _, prev)) if prev > mtime => best,
                _ => Some((project_dir.clone(), parsed.daw_session_id.clone(), mtime)),
            };
        }
    }

    best.map(|(p, s, _)| (p, s))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::record_signal::{self, write_pending};
    use std::fs::{File, FileTimes};
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::time::SystemTime;

    fn unique_root(label: &str) -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let root = std::env::temp_dir().join(format!("kirin_pre_self_discovery_{label}_{pid}_{n}"));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }

    fn touch_signal(
        plugin_data_root: &Path,
        project_uuid: &str,
        post_instance_id: &str,
        target_pre_instance_id: &str,
    ) -> PathBuf {
        write_pending(
            plugin_data_root,
            project_uuid,
            post_instance_id,
            target_pre_instance_id.to_string(),
            "daw-test".to_string(),
        )
        .expect("write_pending must succeed");
        record_signal::signal_path(plugin_data_root, project_uuid, post_instance_id)
    }

    fn set_mtime(path: &Path, t: SystemTime) {
        let times = FileTimes::new().set_modified(t).set_accessed(t);
        let f = File::options().write(true).open(path).unwrap();
        f.set_times(times).unwrap();
    }

    #[test]
    fn returns_none_when_root_missing() {
        let root = std::env::temp_dir().join("kirin_pre_self_discovery_missing_root");
        let _ = fs::remove_dir_all(&root);
        assert!(discover_pair_post_project_dir(&root, "pre-A").is_none());
    }

    #[test]
    fn returns_none_when_root_empty() {
        let root = unique_root("empty");
        assert!(discover_pair_post_project_dir(&root, "pre-A").is_none());
    }

    #[test]
    fn returns_none_when_my_instance_id_is_empty() {
        let root = unique_root("empty_iid");
        // signal は存在するが、my_instance_id="" は false-positive 防止で None
        touch_signal(&root, "post-proj", "post-1", "");
        assert!(discover_pair_post_project_dir(&root, "").is_none());
    }

    #[test]
    fn finds_single_matching_signal() {
        let root = unique_root("single_match");
        touch_signal(&root, "post-proj-A", "post-1", "pre-target");
        let result = discover_pair_post_project_dir(&root, "pre-target");
        assert_eq!(
            result,
            Some((root.join("post-proj-A"), "daw-test".to_string()))
        );
    }

    #[test]
    fn ignores_non_matching_target_pre_instance_id() {
        let root = unique_root("non_match");
        touch_signal(&root, "post-proj-A", "post-1", "other-pre");
        // target_pre_instance_id が "pre-self" 以外 → 一致無し
        assert!(discover_pair_post_project_dir(&root, "pre-self").is_none());
    }

    #[test]
    fn picks_latest_mtime_when_multiple_match() {
        let root = unique_root("multi_match");
        // 2 つの POST が同じ PRE を target にしている (= 同一 PRE が 2 POST に
        // 跨がるシナリオ。実機では稀だが、最新 mtime で勝者決定)。
        let path_old = touch_signal(&root, "post-old", "post-old-1", "pre-target");
        let path_new = touch_signal(&root, "post-new", "post-new-1", "pre-target");

        // 古い方の mtime を 5 秒前に巻き戻す
        let now = SystemTime::now();
        set_mtime(&path_old, now - Duration::from_secs(5));
        // 新しい方は now のまま (= now)
        set_mtime(&path_new, now);

        let result = discover_pair_post_project_dir_at(&root, "pre-target", now);
        assert_eq!(
            result,
            Some((root.join("post-new"), "daw-test".to_string()))
        );
    }

    #[test]
    fn excludes_stale_signal_beyond_threshold() {
        let root = unique_root("stale");
        let path = touch_signal(&root, "post-proj", "post-1", "pre-target");

        let now = SystemTime::now();
        // DISCOVERY_STALE_SECS = 10 を超える 11 秒前
        set_mtime(&path, now - Duration::from_secs(DISCOVERY_STALE_SECS + 1));

        let result = discover_pair_post_project_dir_at(&root, "pre-target", now);
        assert!(result.is_none(), "stale signal must be excluded");
    }

    #[test]
    fn ignores_corrupt_signal_file() {
        let root = unique_root("corrupt");
        // 一致 signal の隣に壊れた json を置く
        let proj_dir = root.join("post-proj");
        let signals_dir = proj_dir.join(SIGNALS_SUBDIR);
        fs::create_dir_all(&signals_dir).unwrap();
        fs::write(signals_dir.join("bad.json"), b"{ not json").unwrap();
        // 正規の一致 signal も置く
        touch_signal(&root, "post-proj", "post-good", "pre-target");

        let result = discover_pair_post_project_dir(&root, "pre-target");
        assert_eq!(
            result,
            Some((proj_dir, "daw-test".to_string())),
            "corrupt file must be skipped silently"
        );
    }

    #[test]
    fn ignores_non_json_files_in_signals_dir() {
        let root = unique_root("non_json");
        let proj_dir = root.join("post-proj");
        let signals_dir = proj_dir.join(SIGNALS_SUBDIR);
        fs::create_dir_all(&signals_dir).unwrap();
        fs::write(signals_dir.join("readme.txt"), b"ignore me").unwrap();
        touch_signal(&root, "post-proj", "post-1", "pre-target");

        let result = discover_pair_post_project_dir(&root, "pre-target");
        assert_eq!(result, Some((proj_dir, "daw-test".to_string())));
    }

    #[test]
    fn skips_record_signal_dir_at_root_level() {
        let root = unique_root("subdir_reserved");
        // 一致 signal が `{root}/record_signal/...` 直下にあっても
        // record_signal/ を project_uuid として扱わない
        let signals_at_root = root.join(SIGNALS_SUBDIR);
        fs::create_dir_all(&signals_at_root).unwrap();
        // post の filename の path 構造を踏襲して、root 直下の `record_signal/`
        // に signal を配置 (異常配置だが、防御的に skip 確認)
        let bogus = signals_at_root.join("post-1.json");
        fs::write(
            &bogus,
            br#"{"status":"pending","requested_by":"post-1","target_pre_instance_id":"pre-target","daw_session_id":"daw-test","t":"2026-01-01T00:00:00Z","started_at":"2026-01-01T00:00:00Z"}"#,
        )
        .unwrap();

        let result = discover_pair_post_project_dir(&root, "pre-target");
        assert!(
            result.is_none(),
            "record_signal/ at root must not be treated as project_uuid"
        );
    }

    #[test]
    fn picks_among_multiple_uuids_only_matching_one() {
        let root = unique_root("multi_uuid");
        // 3 つの POST project_uuid: A=他 PRE 向け、B=自分向け、C=他 PRE 向け
        touch_signal(&root, "post-A", "post-A-1", "other-pre-A");
        touch_signal(&root, "post-B", "post-B-1", "pre-self");
        touch_signal(&root, "post-C", "post-C-1", "other-pre-C");

        let result = discover_pair_post_project_dir(&root, "pre-self");
        assert_eq!(result, Some((root.join("post-B"), "daw-test".to_string())));
    }

    /// B-022 段階 3: discover が見つけた signal の `daw_session_id` を
    /// `PreSelfDiscoveryState::cached_daw_session_id()` で取得できることを
    /// 構造的に固定する。これは `effective_daw_session_id_ref` 経路で
    /// PRE 側 io_thread が POST が書いた値を採用する設計の根幹。
    #[test]
    fn discovers_signal_with_matching_daw_session_id() {
        let root = unique_root("discover_session_id");
        // POST が書いた signal は daw_session_id="daw-test" (touch_signal 既定)
        touch_signal(&root, "post-proj-X", "post-X-1", "pre-target");

        let result = discover_pair_post_project_dir(&root, "pre-target");
        let (project_dir, session_id) = result.expect("一致 signal が 1 件あれば必ず Some を返す");
        assert_eq!(project_dir, root.join("post-proj-X"));
        assert_eq!(
            session_id, "daw-test",
            "discover は signal 自身の daw_session_id を返す \
             (PRE 側 cell 値ではなく POST が書いた値を信じる設計)"
        );

        // PreSelfDiscoveryState 経由でも同じ値が取り出せる
        let mut state = PreSelfDiscoveryState::new();
        state.record_scan(Instant::now(), Some((project_dir, session_id)));
        assert_eq!(state.cached_daw_session_id(), Some("daw-test"));
    }

    // ── PreSelfDiscoveryState ──────────────────────────────────────────

    #[test]
    fn state_should_rescan_initially() {
        let s = PreSelfDiscoveryState::new();
        assert!(s.should_rescan(Instant::now()));
        assert!(s.cached_post_project_dir().is_none());
    }

    #[test]
    fn state_throttles_within_one_second() {
        let mut s = PreSelfDiscoveryState::new();
        let t0 = Instant::now();
        s.record_scan(
            t0,
            Some((PathBuf::from("/tmp/post-A"), "daw-test".to_string())),
        );
        // 0.5 秒以内は再走査しない
        assert!(!s.should_rescan(t0 + Duration::from_millis(500)));
        // 1 秒経過時点で再走査可
        assert!(s.should_rescan(t0 + Duration::from_millis(1000)));
    }

    #[test]
    fn state_uses_fast_rescan_until_first_capture() {
        let mut s = PreSelfDiscoveryState::new();
        let t0 = Instant::now();
        s.record_scan(t0, None);
        assert!(
            !s.should_rescan(t0 + Duration::from_millis(99)),
            "POST project 未捕捉でも 100ms 未満は throttle"
        );
        assert!(
            s.should_rescan(t0 + Duration::from_millis(100)),
            "POST project 未捕捉中は Keep→offline bounce に追従するため 100ms で再探索"
        );
    }

    #[test]
    fn state_cached_value_preserved_across_none_rescan() {
        // B-047 / G-115-247: 一度 Some を見つけたら以降 None で上書きしない。
        // Watch mode (partner=None) で discover miss しても cache を捨てない。
        let mut s = PreSelfDiscoveryState::new();
        let t0 = Instant::now();
        let dir = PathBuf::from("/tmp/post-A");
        s.record_scan(t0, Some((dir.clone(), "daw-test".to_string())));
        assert_eq!(s.cached_post_project_dir(), Some(dir.as_path()));
        assert_eq!(s.cached_daw_session_id(), Some("daw-test"));
        s.record_scan(t0 + Duration::from_secs(1), None);
        assert_eq!(s.cached_post_project_dir(), Some(dir.as_path()));
        assert_eq!(s.cached_daw_session_id(), Some("daw-test"));
    }
}
