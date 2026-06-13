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
            let rf = ReservationFile {
                pre_instance_id: pre_iid.to_string(),
                post_instance_id: post_iid.to_string(),
                reserved_at: now.to_rfc3339(),
            };
            if let Ok(bytes) = serde_json::to_vec(&rf) {
                let _ = f.write_all(&bytes); // best-effort（枠 create は成立済）。
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

/// 現在有効な reservation の pairing key を列挙する（`reserved_at` age ≤ [`RESERVATION_TTL_SECS`]）。
/// TTL 超過の孤児は除外（count に含めない）。reserved_at 欠落/parse 不能は安全側 = 除外（孤児扱い）。
pub fn scan_active_reservation_keys(
    base_dir: &Path,
    project_hash: &str,
    now: DateTime<Utc>,
) -> Vec<String> {
    let dir = reservation_dir(base_dir, project_hash);
    let mut out = Vec::new();
    let Ok(entries) = fs::read_dir(&dir) else {
        return out;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        let Ok(bytes) = fs::read(&path) else { continue };
        let Ok(rf) = serde_json::from_slice::<ReservationFile>(&bytes) else {
            continue;
        };
        if reservation_is_fresh(&rf.reserved_at, now) {
            out.push(pairing_key(&rf.pre_instance_id, &rf.post_instance_id));
        }
    }
    out
}

fn reservation_is_fresh(reserved_at: &str, now: DateTime<Utc>) -> bool {
    match DateTime::parse_from_rfc3339(reserved_at) {
        Ok(t) => {
            let age = now.signed_duration_since(t.with_timezone(&Utc)).num_seconds();
            age < 0 || age <= RESERVATION_TTL_SECS
        }
        Err(_) => false, // parse 不能 = 安全側 stale（孤児扱い）。
    }
}

/// 孤児 reservation（age > [`RESERVATION_TTL_SECS`]）を `base_dir` 全 project_hash 横断で削除する
/// （B-103/B-119 startup sweep 合流）。削除件数を返す。reserved_at 欠落/parse 不能も孤児として削除。
pub fn sweep_stale_reservations_in(base_dir: &Path, now: DateTime<Utc>) -> usize {
    let mut removed = 0usize;
    let Ok(projects) = fs::read_dir(base_dir) else {
        return 0;
    };
    for project in projects.flatten() {
        let dir = project.path().join(RESERVATION_SUBDIR);
        let Ok(entries) = fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("json") {
                continue;
            }
            let stale = match fs::read(&path) {
                Ok(bytes) => match serde_json::from_slice::<ReservationFile>(&bytes) {
                    Ok(rf) => !reservation_is_fresh(&rf.reserved_at, now),
                    Err(_) => true, // 破損 = 孤児。
                },
                Err(_) => continue,
            };
            if stale && fs::remove_file(&path).is_ok() {
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

    #[test]
    fn scan_active_excludes_stale_reservation() {
        let base = isolated_dir();
        let now = Utc::now();
        // fresh + stale を 1 つずつ。
        reserve_pairing_at(&base, "ph", "pre-fresh", "post-fresh", now).unwrap();
        reserve_pairing_at(
            &base,
            "ph",
            "pre-stale",
            "post-stale",
            now - chrono::Duration::seconds(RESERVATION_TTL_SECS + 10),
        )
        .unwrap();
        let keys = scan_active_reservation_keys(&base, "ph", now);
        assert!(keys.contains(&pairing_key("pre-fresh", "post-fresh")), "fresh は count 対象");
        assert!(
            !keys.contains(&pairing_key("pre-stale", "post-stale")),
            "TTL 超過の孤児は count から除外"
        );
    }

    #[test]
    fn sweep_removes_stale_keeps_fresh() {
        let base = isolated_dir();
        let now = Utc::now();
        reserve_pairing_at(&base, "ph", "pre-fresh", "post-fresh", now).unwrap();
        reserve_pairing_at(
            &base,
            "ph",
            "pre-stale",
            "post-stale",
            now - chrono::Duration::seconds(RESERVATION_TTL_SECS + 10),
        )
        .unwrap();
        let removed = sweep_stale_reservations_in(&base, now);
        assert_eq!(removed, 1, "孤児 1 件のみ削除");
        let keys = scan_active_reservation_keys(&base, "ph", now);
        assert_eq!(keys, vec![pairing_key("pre-fresh", "post-fresh")], "fresh は残る");
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
}
