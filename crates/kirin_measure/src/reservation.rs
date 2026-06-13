//! B-127 (G-115-364): per-pairing reservation — cross-process safe O_EXCL atomic-create。
//!
//! 記録 cap は **distinct pairing** 単位（[`crate::exclusion`]）。active marker が io_thread に
//! よって書かれるのは keep 確定の **後** であり、その間（keep→writer_start）に複数の keep が
//! 同じ count を読んで cap を超過する TOCTOU 窓があった（B-127 1309b9d が自認）。reservation は
//! keep 確定時に同期的に枠ファイルを **O_EXCL atomic-create**（`create_new(true)`）し、その窓を
//! cross-process（同一 `~/Library/Application Support/Kirin OS/plugin_data/` を共有する別 DAW/
//! 別プロセス含む）で閉じる。
//!
//! - 枠ファイル: `{plugin_data}/{project_hash}/record_reservation/{pairing_key}.json`
//! - `pairing_key` = `{pre_instance_id}__{post_instance_id}`（active marker の pairing key と一致）
//! - exclusion count は marker の pairing key と reservation の pairing key を **同一集合**に入れて
//!   重複排除する（同じ pairing は marker と reservation の双方があっても 1 枠）。
//! - reservation は [`RESERVATION_TTL_SECS`] 以内のみ count に含める。active marker（fresh
//!   heartbeat）が現れた後は marker が同一 pairing key を保持するため、reservation の失効は枠に
//!   影響しない。TTL 超過の reservation は孤児（keep が writer_start 前にクラッシュ等）として
//!   count から除外し sweep で削除する（B-103/B-119 合流）。
//! - 解放: POST stop（[`crate::record_signal`] 経路）で明示削除 + 孤児は age-based sweep。

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::plugin_data::{PluginDataFile, Role as PluginDataRole, Status};

/// `{project_hash}/` 直下の予約サブディレクトリ名（instance_id ではない / exclusion scan は skip）。
pub const RESERVATION_SUBDIR: &str = "record_reservation";

/// reservation が枠を保持できる最大秒数。active marker が現れる窓（keep→writer_start, 通常 1 tick
/// =100ms 程度）を十分覆う保守値。超過は孤児として count 除外 + sweep 対象。`STALE_SECONDS` と同値。
pub const RESERVATION_TTL_SECS: i64 = 60;

/// pairing を一意識別する正規化キー（`pre_instance_id__post_instance_id`）。
/// active marker 側（POST→`(paired_pre, self)` / PRE→`(self, paired_post)`）と同じ規則で作る。
pub fn pairing_key(pre_instance_id: &str, post_instance_id: &str) -> String {
    format!("{pre_instance_id}__{post_instance_id}")
}

#[derive(Serialize, Deserialize)]
struct ReservationFile {
    pre_instance_id: String,
    post_instance_id: String,
    /// rfc3339。TTL / sweep の age 判定に使う。
    reserved_at: String,
}

/// [`reserve_pairing`] の結果。
#[derive(Debug, PartialEq, Eq)]
pub enum ReserveOutcome {
    /// 本呼び出しが枠を新規作成した（reject 時は呼び出し側が解放する責務を持つ）。
    Created,
    /// 既に同 pairing の reservation が存在した（EEXIST）。枠は既に確保済み。
    AlreadyReserved,
}

fn reservation_dir(base_dir: &Path, project_hash: &str) -> PathBuf {
    base_dir.join(project_hash).join(RESERVATION_SUBDIR)
}

fn reservation_path(base_dir: &Path, project_hash: &str, pre_iid: &str, post_iid: &str) -> PathBuf {
    reservation_dir(base_dir, project_hash).join(format!("{}.json", pairing_key(pre_iid, post_iid)))
}

/// pairing 枠を **O_EXCL atomic-create** で予約する（cross-process safe）。
///
/// 既に同 pairing の枠があれば [`ReserveOutcome::AlreadyReserved`]（`create_new` の EEXIST）。
/// 新規作成できれば [`ReserveOutcome::Created`]。create 後の metadata 書込失敗は best-effort
/// （枠の存在＝atomic-create 自体は成立済なので無視する。reserved_at 不在は sweep が age 不明として扱う）。
pub fn reserve_pairing(
    base_dir: &Path,
    project_hash: &str,
    pre_iid: &str,
    post_iid: &str,
) -> std::io::Result<ReserveOutcome> {
    reserve_pairing_at(base_dir, project_hash, pre_iid, post_iid, Utc::now())
}

/// [`reserve_pairing`] の時刻注入版（テスト用）。`now` は reserved_at に焼く。
pub fn reserve_pairing_at(
    base_dir: &Path,
    project_hash: &str,
    pre_iid: &str,
    post_iid: &str,
    now: DateTime<Utc>,
) -> std::io::Result<ReserveOutcome> {
    let dir = reservation_dir(base_dir, project_hash);
    fs::create_dir_all(&dir)?;
    let path = reservation_path(base_dir, project_hash, pre_iid, post_iid);
    match OpenOptions::new().write(true).create_new(true).open(&path) {
        Ok(mut f) => {
            // G-115-365 (3): 枠 metadata（pre/post/reserved_at — sweep の marker 照合 + age 用）を
            // 書く。serde / write_all 失敗 = 不完全枠 → unlink して Err を返す（Created を返さない＝
            // 枠を claim したことにしない）。呼び出し側は Err を reject 扱いする。
            let bytes = serde_json::to_vec(&ReservationFile {
                pre_instance_id: pre_iid.to_string(),
                post_instance_id: post_iid.to_string(),
                reserved_at: now.to_rfc3339(),
            })
            .map_err(std::io::Error::other)?;
            if let Err(e) = f.write_all(&bytes) {
                drop(f);
                let _ = fs::remove_file(&path);
                return Err(e);
            }
            Ok(ReserveOutcome::Created)
        }
        Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => Ok(ReserveOutcome::AlreadyReserved),
        Err(e) => Err(e),
    }
}

/// pairing 枠を解放する（ファイル削除）。不在は成功扱い（冪等）。
pub fn release_pairing(base_dir: &Path, project_hash: &str, pre_iid: &str, post_iid: &str) {
    let _ = fs::remove_file(reservation_path(base_dir, project_hash, pre_iid, post_iid));
}

/// G-115-365 (2): cap の真実源。`{project_hash}/record_reservation/` 配下に**物理的に存在する**
/// 枠ファイル（`.json`）の数を数える（= distinct pairing 数 / 第二の独立 count を持たない）。
/// **parse は一切しない**ため、内容が壊れた/書込途中の枠も「存在」側で数える（under-count しない）。
/// **TTL を差し引かない**（古い枠も存在すれば数える）。孤児回収は sweep の責務であって count ではない。
pub fn count_frames(base_dir: &Path, project_hash: &str) -> usize {
    let dir = reservation_dir(base_dir, project_hash);
    let Ok(entries) = fs::read_dir(&dir) else {
        return 0;
    };
    entries
        .flatten()
        .filter(|e| e.path().extension().and_then(|x| x.to_str()) == Some("json"))
        .count()
}

/// 枠の reserved_at（rfc3339）。parse 不能/欠落は None。
fn read_frame_reserved_at(path: &Path) -> Option<DateTime<Utc>> {
    let bytes = fs::read(path).ok()?;
    let rf: ReservationFile = serde_json::from_slice(&bytes).ok()?;
    DateTime::parse_from_rfc3339(&rf.reserved_at)
        .ok()
        .map(|t| t.with_timezone(&Utc))
}

/// 枠の (pre, post) instance_id。parse 不能は None。
fn read_frame_pair(path: &Path) -> Option<(String, String)> {
    let bytes = fs::read(path).ok()?;
    let rf: ReservationFile = serde_json::from_slice(&bytes).ok()?;
    Some((rf.pre_instance_id, rf.post_instance_id))
}

/// pairing に対応する active+fresh marker（PRE か POST のどちらか）が存在するか。
/// `{base}/{ph}/{pre}/pre/*.json` か `{base}/{ph}/{post}/post/*.json` に status=Active かつ
/// heartbeat fresh があれば true（= 録音継続中）。sweep が長時間 Record の枠を誤回収しないために使う。
fn pairing_has_fresh_marker(
    base_dir: &Path,
    project_hash: &str,
    pre_iid: &str,
    post_iid: &str,
    now: DateTime<Utc>,
) -> bool {
    let proj = base_dir.join(project_hash);
    role_dir_has_fresh_marker(&proj.join(pre_iid).join(PluginDataRole::Pre.dir_name()), now)
        || role_dir_has_fresh_marker(&proj.join(post_iid).join(PluginDataRole::Post.dir_name()), now)
}

fn role_dir_has_fresh_marker(dir: &Path, now: DateTime<Utc>) -> bool {
    let Ok(entries) = fs::read_dir(dir) else {
        return false;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        if let Ok(bytes) = fs::read(&path) {
            if let Ok(file) = serde_json::from_slice::<PluginDataFile>(&bytes) {
                if file.status == Status::Active
                    && crate::exclusion::is_heartbeat_fresh(&file.heartbeat, now, RESERVATION_TTL_SECS)
                {
                    return true;
                }
            }
        }
    }
    false
}

/// 孤児 reservation 枠を `base_dir` 全 project_hash 横断で回収する（B-103/B-119 startup sweep 合流）。
/// G-115-365: 回収条件 = 枠 age > [`RESERVATION_TTL_SECS`]（keep→writer_start 窓の grace）**かつ**
/// その pairing に fresh active marker が無い（= 録音継続していない / crash・終了）。長時間 Record
/// （marker fresh）は age 超過でも保持する（pure-age 回収による誤剥がしを防ぐ）。reserved_at parse
/// 不能の枠は不完全/破損孤児として回収する。**live count（[`count_frames`]）には一切干渉しない。**
pub fn sweep_stale_reservations_in(base_dir: &Path, now: DateTime<Utc>) -> usize {
    let mut removed = 0usize;
    let Ok(projects) = fs::read_dir(base_dir) else {
        return 0;
    };
    for project in projects.flatten() {
        let project_path = project.path();
        let Some(ph) = project_path.file_name().and_then(|n| n.to_str()) else {
            continue;
        };
        let ph = ph.to_string();
        let dir = project_path.join(RESERVATION_SUBDIR);
        let Ok(entries) = fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("json") {
                continue;
            }
            let reclaim = match read_frame_reserved_at(&path) {
                Some(reserved_at) => {
                    let age = now.signed_duration_since(reserved_at).num_seconds();
                    // grace 内は保持。grace 超過は marker 無し（録音継続せず）のときだけ回収。
                    age > RESERVATION_TTL_SECS
                        && match read_frame_pair(&path) {
                            Some((pre, post)) => {
                                !pairing_has_fresh_marker(base_dir, &ph, &pre, &post, now)
                            }
                            None => true,
                        }
                }
                // reserved_at parse 不能 = 不完全/破損孤児 → 回収。
                None => true,
            };
            if reclaim && fs::remove_file(&path).is_ok() {
                removed += 1;
            }
        }
    }
    removed
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::sync::Arc;

    fn isolated_dir() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let dir = std::env::temp_dir().join(format!(
            "kirin_reservation_test_{}_{}",
            std::process::id(),
            n
        ));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    #[test]
    fn reserve_then_same_pairing_is_already_reserved() {
        let base = isolated_dir();
        let now = Utc::now();
        assert_eq!(
            reserve_pairing_at(&base, "ph", "pre", "post", now).unwrap(),
            ReserveOutcome::Created
        );
        assert_eq!(
            reserve_pairing_at(&base, "ph", "pre", "post", now).unwrap(),
            ReserveOutcome::AlreadyReserved,
            "同一 pairing の二度目は EEXIST = AlreadyReserved"
        );
    }

    #[test]
    fn release_allows_re_reserve() {
        let base = isolated_dir();
        let now = Utc::now();
        reserve_pairing_at(&base, "ph", "pre", "post", now).unwrap();
        release_pairing(&base, "ph", "pre", "post");
        assert_eq!(
            reserve_pairing_at(&base, "ph", "pre", "post", now).unwrap(),
            ReserveOutcome::Created,
            "解放後は再予約できる（枠が空く）"
        );
    }

    /// fresh active marker（録音継続中）を `{base}/{ph}/{post}/post/` に書く（sweep 保護テスト用）。
    fn write_active_post_marker(base: &Path, ph: &str, post_iid: &str) {
        use crate::plugin_data::{PluginDataWriter, WriterPaths};
        let paths = WriterPaths::build(base, ph, post_iid, PluginDataRole::Post, "2026-06-14T00:00:00Z");
        let mut w = PluginDataWriter::create(
            paths,
            "i".to_string(),
            ph.to_string(),
            post_iid.to_string(),
            PluginDataRole::Post,
            None,
            48000,
            None,
            None,
        )
        .unwrap();
        w.flush().unwrap(); // status=Active, heartbeat=now（fresh）
    }

    /// G-115-365 (2): count は枠の物理存在のみ（parse/TTL 非依存）。古い枠も壊れた枠も数える。
    #[test]
    fn count_frames_is_pure_existence() {
        let base = isolated_dir();
        let now = Utc::now();
        reserve_pairing_at(&base, "ph", "a", "b", now).unwrap();
        // 古い枠（TTL 超過）も数える。
        reserve_pairing_at(&base, "ph", "c", "d", now - chrono::Duration::seconds(RESERVATION_TTL_SECS + 50))
            .unwrap();
        // 0byte（parse 不能）の枠も存在として数える。
        let dir = reservation_dir(&base, "ph");
        std::fs::write(dir.join("e__f.json"), b"").unwrap();
        assert_eq!(count_frames(&base, "ph"), 3, "新/旧/壊れ いずれも存在として数える");
    }

    /// G-115-365 sweep: 孤児（age>TTL かつ fresh marker 無し）を回収。fresh 枠（grace 内）と
    /// fresh marker に裏付けられた古い枠（長時間 Record）は保持する（pure-age 誤剥がし防止）。
    #[test]
    fn sweep_reclaims_orphan_but_protects_fresh_and_marker_backed() {
        let base = isolated_dir();
        let now = Utc::now();
        let old = now - chrono::Duration::seconds(RESERVATION_TTL_SECS + 10);
        // (1) fresh 枠（grace 内）→ 保持。
        reserve_pairing_at(&base, "ph", "fresh-pre", "fresh-post", now).unwrap();
        // (2) 古い枠 + marker 無し → 孤児 → 回収。
        reserve_pairing_at(&base, "ph", "orphan-pre", "orphan-post", old).unwrap();
        // (3) 古い枠 + fresh active marker（録音継続中）→ 保持。
        reserve_pairing_at(&base, "ph", "live-pre", "live-post", old).unwrap();
        write_active_post_marker(&base, "ph", "live-post");

        let removed = sweep_stale_reservations_in(&base, now);
        assert_eq!(removed, 1, "孤児 1 件のみ回収");
        assert_eq!(count_frames(&base, "ph"), 2, "fresh 枠 + marker 裏付け枠は残る");
    }

    /// (iii) O_EXCL atomic-create cross-process safety: 同一 pairing key を多スレッドが同時に
    /// reserve しても **ちょうど 1 つだけ** Created（残りは AlreadyReserved）。`create_new(true)` の
    /// 原子性（OS が保証・cross-process でも同一プリミティブ）を並行 race で実証する。
    #[test]
    fn concurrent_reserve_exactly_one_wins() {
        let base = isolated_dir();
        let created = Arc::new(AtomicU64::new(0));
        let already = Arc::new(AtomicU64::new(0));
        let handles: Vec<_> = (0..16)
            .map(|_| {
                let base = base.clone();
                let created = Arc::clone(&created);
                let already = Arc::clone(&already);
                std::thread::spawn(move || match reserve_pairing(&base, "ph", "pre", "post") {
                    Ok(ReserveOutcome::Created) => {
                        created.fetch_add(1, Ordering::Relaxed);
                    }
                    Ok(ReserveOutcome::AlreadyReserved) => {
                        already.fetch_add(1, Ordering::Relaxed);
                    }
                    Err(_) => {}
                })
            })
            .collect();
        for h in handles {
            h.join().unwrap();
        }
        assert_eq!(
            created.load(Ordering::Relaxed),
            1,
            "O_EXCL: 同一 pairing 枠は並行 race でちょうど 1 つだけ Created"
        );
        assert_eq!(already.load(Ordering::Relaxed), 15, "残り 15 は AlreadyReserved");
    }

    /// engine gate と同型: reserve → 枠数 > MAX なら release して false（13 本目 reject）。
    fn try_claim_via_gate(base: &Path, ph: &str, pre: &str, post: &str) -> bool {
        match reserve_pairing(base, ph, pre, post) {
            Ok(ReserveOutcome::Created) => {
                if count_frames(base, ph) > crate::exclusion::MAX_ACTIVE_PER_PROJECT {
                    release_pairing(base, ph, pre, post);
                    false
                } else {
                    true
                }
            }
            Ok(ReserveOutcome::AlreadyReserved) => true,
            Err(_) => false,
        }
    }

    /// (c) 並行 cross-process な 13 本目: cap 満杯(12)から複数スレッドが各々 **別 pairing** を
    /// 同時 claim しても、確保成功は 0（全 reject）・枠数は 12 を超えない・leak も無い。
    /// reserve-then-count>MAX-release ゲート（FFI/egui と同型）の cross-process atomicity を実証。
    #[test]
    fn concurrent_thirteenth_attempts_never_exceed_twelve() {
        let base = isolated_dir();
        let now = Utc::now();
        for i in 0..crate::exclusion::MAX_ACTIVE_PER_PROJECT {
            reserve_pairing_at(&base, "ph", &format!("p{i}"), &format!("q{i}"), now).unwrap();
        }
        let base_arc = Arc::new(base.clone());
        let succeeded = Arc::new(AtomicU64::new(0));
        let handles: Vec<_> = (0..16)
            .map(|t| {
                let base = Arc::clone(&base_arc);
                let succeeded = Arc::clone(&succeeded);
                std::thread::spawn(move || {
                    let pre = format!("new-pre-{t}");
                    let post = format!("new-post-{t}");
                    if try_claim_via_gate(&base, "ph", &pre, &post) {
                        succeeded.fetch_add(1, Ordering::Relaxed);
                    }
                })
            })
            .collect();
        for h in handles {
            h.join().unwrap();
        }
        assert_eq!(
            succeeded.load(Ordering::Relaxed),
            0,
            "12 満杯で 13 本目（全 16 並行試行）は全 reject"
        );
        assert_eq!(
            count_frames(&base, "ph"),
            crate::exclusion::MAX_ACTIVE_PER_PROJECT,
            "枠数は 12 を超えない・over-cap も leak も無い"
        );
    }
}
