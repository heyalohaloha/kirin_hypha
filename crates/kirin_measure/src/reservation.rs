//! B-127 (G-115-364/365/366): per-pairing reservation — cross-process safe atomic claim。
//!
//! 記録 cap は **distinct pairing** 単位（[`crate::exclusion`]）。active marker が io_thread に
//! よって書かれるのは keep 確定の **後** であり、その間（keep→writer_start）に複数の keep が
//! 同じ count を読んで cap を超過する TOCTOU 窓があった（B-127 1309b9d が自認）。reservation は
//! keep 確定時に同期的に枠ファイルを **atomic claim**（tempfile + `hard_link` の EEXIST 排他 /
//! G-115-366 (A)）し、その窓を cross-process（同一 `~/Library/Application Support/Kirin OS/
//! plugin_data/` を共有する別 DAW/別プロセス含む）で閉じる。final 枠は link するまで dir entry を
//! 持たないため、reserve 進行中に sweep/count が「0-byte/incomplete な final」を観測する窓が無い。
//!
//! - 枠ファイル: `{plugin_data}/{project_hash}/record_reservation/{pairing_key}.json`
//! - `pairing_key` = `{pre_instance_id}__{post_instance_id}`（active marker の pairing key と一致）
//! - cap count（[`count_frames`]）は **`record_reservation/*.json` の物理存在数のみ**で数える。
//!   marker JSON は参照しない（第二の独立 count を持たない）/ parse もしない（壊れ枠も存在側で
//!   数える＝under-count しない）/ TTL を count から引かない（古い枠も存在すれば数える）。
//! - 孤児回収（grace 超過）は **sweep の責務**であって count ではない。keep が writer_start 前に
//!   クラッシュした枠は age（reserved_at）/ mtime grace 超過で sweep が削除する（B-103/B-119 合流）。
//! - 解放: POST stop（[`crate::record_signal`] 経路）で明示削除 + 孤児は grace-based sweep。

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

/// G-115-366 (A): atomic claim の temp 名一意化用 process 内連番（pid と併用で並行/別プロセス衝突回避）。
static TEMP_SEQ: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

/// pairing を一意識別する正規化キー（`pre_instance_id__post_instance_id`）。
/// active marker 側（POST→`(paired_pre, self)` / PRE→`(self, paired_post)`）と同じ規則で作る。
pub fn pairing_key(pre_instance_id: &str, post_instance_id: &str) -> String {
    // B-128 (G-115-370): within-base wall。reservation_path / reserve_build_temp の両 path builder が
    // 本キーを使うため、ここで pre/post を guard すれば枠ファイル名は常に base 内 path-safe。
    // 正常運用（valid UUID / 安全な literal）は無改変＝既存 pairing 契約・B-127 テスト不変。
    let pre =
        crate::path_identity::guard_path_component(pre_instance_id, "reservation.pairing_key.pre");
    let post = crate::path_identity::guard_path_component(
        post_instance_id,
        "reservation.pairing_key.post",
    );
    format!("{pre}__{post}")
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
    // B-128 (G-115-370): within-base wall（project_hash 成分）。
    let ph =
        crate::path_identity::guard_path_component(project_hash, "reservation.dir.project_hash");
    base_dir.join(&*ph).join(RESERVATION_SUBDIR)
}

fn reservation_path(base_dir: &Path, project_hash: &str, pre_iid: &str, post_iid: &str) -> PathBuf {
    reservation_dir(base_dir, project_hash).join(format!("{}.json", pairing_key(pre_iid, post_iid)))
}

/// pairing 枠を **atomic claim**（tempfile + `hard_link` / G-115-366 A）で予約する（cross-process safe）。
///
/// 既に同 pairing の枠があれば [`ReserveOutcome::AlreadyReserved`]（`hard_link` の EEXIST）。
/// 新規作成できれば [`ReserveOutcome::Created`]。final 枠は link した時点で必ず完全 JSON を指す
/// （temp に write_all + sync_all してから link するため・0-byte/incomplete な final は出現しない）。
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

    // G-115-366 (A): atomic claim = tempfile + hard_link。final(枠) は link するまで dir entry を
    // 持たない → reserve 進行中に sweep / count が final を観測できない（0-byte/incomplete が final に
    // 出る窓が消滅）。link は EEXIST で原子的に排他（cross-process safe / O_EXCL 同等プリミティブ）。
    // link 後の final は常に完全 JSON を指す inode（parse 必ず成功）。rename は使わない（上書きで
    // 既存 claim を奪うため）。temp は全経路で掃除（orphan temp を残さない）。
    let bytes = serde_json::to_vec(&ReservationFile {
        pre_instance_id: pre_iid.to_string(),
        post_instance_id: post_iid.to_string(),
        reserved_at: now.to_rfc3339(),
    })
    .map_err(std::io::Error::other)?;
    // step 1-2: temp に完全 JSON を write_all → sync_all（fsync）。失敗は temp 掃除して Err（reject）。
    let temp = reserve_build_temp(&dir, pre_iid, post_iid, &bytes)?;
    // step 3: hard_link(temp → final) で原子 claim（temp は内部で全経路掃除）。
    reserve_link_claim(&temp, &path)
}

/// G-115-366 (A) step 1-2: temp に完全 JSON を write_all → sync_all（fsync）し、temp path を返す。
/// temp は final と同一 dir（= 同一 fs / hard_link 制約クリア）。名は `.{pair_key}.tmp.{pid}.{seq}`
/// （process 内 atomic 連番 + pid で並行・別プロセスと衝突しない / 拡張子が `.json` でない＝sweep/
/// count が無視する）。write / sync 失敗時は temp を best-effort 掃除して Err（orphan temp を残さない）。
fn reserve_build_temp(
    dir: &Path,
    pre_iid: &str,
    post_iid: &str,
    bytes: &[u8],
) -> std::io::Result<PathBuf> {
    let seq = TEMP_SEQ.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let temp = dir.join(format!(
        ".{}.tmp.{}.{}",
        pairing_key(pre_iid, post_iid),
        std::process::id(),
        seq
    ));
    let res = (|| -> std::io::Result<()> {
        let mut f = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temp)?;
        f.write_all(bytes)?;
        f.sync_all()?;
        Ok(())
    })();
    match res {
        Ok(()) => Ok(temp),
        Err(e) => {
            let _ = fs::remove_file(&temp);
            Err(e)
        }
    }
}

/// G-115-366 (A) step 3: `hard_link(temp → final)` で原子 claim。
/// - Ok → 本呼び出しが枠を作った（final は完全内容 inode）→ [`ReserveOutcome::Created`]。
/// - EEXIST → 既占有 → [`ReserveOutcome::AlreadyReserved`]。
/// - その他 Err → reject（Created を返さない）。
///
/// temp は全経路で unlink（final は link 済＝temp 削除に影響されない / 未 link でも orphan を残さない）。
fn reserve_link_claim(temp: &Path, final_path: &Path) -> std::io::Result<ReserveOutcome> {
    let outcome = match fs::hard_link(temp, final_path) {
        Ok(()) => Ok(ReserveOutcome::Created),
        Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => {
            Ok(ReserveOutcome::AlreadyReserved)
        }
        Err(e) => Err(e),
    };
    let _ = fs::remove_file(temp);
    outcome
}

/// pairing 枠を解放する（ファイル削除）。不在は成功扱い（冪等）。
pub fn release_pairing(base_dir: &Path, project_hash: &str, pre_iid: &str, post_iid: &str) {
    let _ = fs::remove_file(reservation_path(base_dir, project_hash, pre_iid, post_iid));
}

/// G-115-365 (2): cap の真実源。`{project_hash}/record_reservation/` 配下に**物理的に存在する**
/// 枠ファイル（`.json`）の数を数える（= distinct pairing 数 / 第二の独立 count を持たない）。
/// **parse は一切しない**ため、内容が壊れた/書込途中の枠も「存在」側で数える（under-count しない）。
/// **TTL を差し引かない**（古い枠も存在すれば数える）。孤児回収は sweep の責務であって count ではない。
/// B-128 (G-115-370 / C3): quarantine 枠（`_q_` を含む / restore 攻撃値の wall 畳み込み結果）は
/// **正規 cap に数えない**。攻撃者が traversal 値で cap を消費（DoS）/ bypass することを構造的に防ぐ。
pub fn count_frames(base_dir: &Path, project_hash: &str) -> usize {
    let dir = reservation_dir(base_dir, project_hash);
    let Ok(entries) = fs::read_dir(&dir) else {
        return 0;
    };
    entries
        .flatten()
        .filter(|e| e.path().extension().and_then(|x| x.to_str()) == Some("json"))
        .filter(|e| {
            // quarantine 枠（攻撃値）は cap 対象外。valid UUID 枠名は `_q_` を含まないため誤除外しない。
            e.path()
                .file_stem()
                .and_then(|s| s.to_str())
                .map(|stem| !crate::path_identity::is_quarantine_component(stem))
                .unwrap_or(true)
        })
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

/// G-115-366 (B): 枠ファイルの mtime が `now` から [`RESERVATION_TTL_SECS`] を超えて古いか。
/// parse 不能枠の grace 判定に使う（reserved_at が読めないため mtime を age 代理にする）。
/// mtime 取得不能 / 未来 mtime（負 age）は保守側 false（= 回収しない / 保持）。grace 窓は Some
/// アームの `age > RESERVATION_TTL_SECS` と同一に揃える。
fn frame_mtime_age_exceeds_grace(path: &Path, now: DateTime<Utc>) -> bool {
    let Ok(meta) = fs::metadata(path) else {
        return false;
    };
    let Ok(modified) = meta.modified() else {
        return false;
    };
    let modified: DateTime<Utc> = modified.into();
    now.signed_duration_since(modified).num_seconds() > RESERVATION_TTL_SECS
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
    // B-128 (G-115-370): within-base wall。pre_iid/post_iid は枠ファイル**内容**（raw）由来のため、
    // marker 探索 path に出る前に guard する（攻撃値枠の read-traversal 防止）。valid/safe 値は無改変。
    let ph =
        crate::path_identity::guard_path_component(project_hash, "reservation.marker.project_hash");
    let pre = crate::path_identity::guard_path_component(pre_iid, "reservation.marker.pre");
    let post = crate::path_identity::guard_path_component(post_iid, "reservation.marker.post");
    let proj = base_dir.join(&*ph);
    role_dir_has_fresh_marker(&proj.join(&*pre).join(PluginDataRole::Pre.dir_name()), now)
        || role_dir_has_fresh_marker(
            &proj.join(&*post).join(PluginDataRole::Post.dir_name()),
            now,
        )
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
                    && crate::exclusion::is_heartbeat_fresh(
                        &file.heartbeat,
                        now,
                        RESERVATION_TTL_SECS,
                    )
                {
                    return true;
                }
            }
        }
    }
    false
}

/// 孤児 reservation 枠を `base_dir` 全 project_hash 横断で回収する（B-103/B-119 startup sweep 合流）。
/// G-115-365/366: 回収条件 = 枠 age > [`RESERVATION_TTL_SECS`]（keep→writer_start 窓の grace）**かつ**
/// その pairing に fresh active marker が無い（= 録音継続していない / crash・終了）。長時間 Record
/// （marker fresh）は age 超過でも保持する（pure-age 回収による誤剥がしを防ぐ）。reserved_at parse
/// 不能の枠は **mtime grace 超過のときのみ**回収する（G-115-366 (B) / 最近 mtime の枠は保持）。
/// **live count（[`count_frames`]）には一切干渉しない。**
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
                // G-115-366 (B): reserved_at parse 不能枠は mtime grace 超過のときだけ回収する。
                // atomic claim 後の final は常に parse 可（A）。それでも parse 不能な final が出るのは
                // 旧形式残骸 / FS 破損のみ。最近 mtime のもの（万一の生成途中等）は保守的に保持し、
                // grace 超過のものだけ孤児として回収する。
                None => frame_mtime_age_exceeds_grace(&path, now),
            };
            if reclaim && fs::remove_file(&path).is_ok() {
                removed += 1;
            }
        }
    }
    removed
}

/// [`sweep_stale_reservations_in`] の本番用 wrapper（現在時刻で評価）。
pub fn sweep_stale_reservations(base_dir: &Path) -> usize {
    sweep_stale_reservations_in(base_dir, Utc::now())
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
        let paths = WriterPaths::build(
            base,
            ph,
            post_iid,
            PluginDataRole::Post,
            "2026-06-14T00:00:00Z",
        );
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
        reserve_pairing_at(
            &base,
            "ph",
            "c",
            "d",
            now - chrono::Duration::seconds(RESERVATION_TTL_SECS + 50),
        )
        .unwrap();
        // 0byte（parse 不能）の枠も存在として数える。
        let dir = reservation_dir(&base, "ph");
        std::fs::write(dir.join("e__f.json"), b"").unwrap();
        assert_eq!(
            count_frames(&base, "ph"),
            3,
            "新/旧/壊れ いずれも存在として数える"
        );
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
        assert_eq!(
            count_frames(&base, "ph"),
            2,
            "fresh 枠 + marker 裏付け枠は残る"
        );
    }

    // ── G-115-366 (e): parse 不可枠の mtime grace ──────────────────────────────
    /// 最近 mtime の parse 不可（0-byte）枠は grace 内 → 保持（生成途中の取り違え防止）。
    #[test]
    fn sweep_grace_protects_recent_unparseable_frame() {
        let base = isolated_dir();
        let now = Utc::now();
        let dir = reservation_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("recent__frame.json"), b"").unwrap(); // parse 不可・mtime=now
        let removed = sweep_stale_reservations_in(&base, now);
        assert_eq!(
            removed, 0,
            "最近 mtime の parse 不可枠は mtime grace で保護（回収しない）"
        );
        assert_eq!(count_frames(&base, "ph"), 1, "枠は残る");
    }

    /// mtime-stale な parse 不可枠は孤児として回収（now を grace 超過の未来に進めて評価）。
    #[test]
    fn sweep_reclaims_mtime_stale_unparseable_frame() {
        let base = isolated_dir();
        let dir = reservation_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join("stale__frame.json"), b"").unwrap(); // parse 不可・mtime≈now
                                                                // sweep の now を mtime から grace 超過だけ未来へ（filetime 改変不要で mtime-stale を再現）。
        let future = Utc::now() + chrono::Duration::seconds(RESERVATION_TTL_SECS + 30);
        let removed = sweep_stale_reservations_in(&base, future);
        assert_eq!(removed, 1, "mtime grace 超過の parse 不可枠は孤児回収");
        assert_eq!(count_frames(&base, "ph"), 0);
    }

    // ── G-115-366 (d): sweep × in-progress reserve 並行安全 ────────────────────
    /// reserve をステップ分割（temp 書込後・link 前 / link 後）し各段で sweep を走らせる:
    /// - link 前: final 未生成 → sweep は当該枠を消せない（temp は非 .json で count/sweep が無視）。
    /// - link 後: final は完全枠（parse 必ず OK）・reserved_at fresh → age grace で保護。
    #[test]
    fn sweep_does_not_delete_in_progress_or_claimed_reservation() {
        let base = isolated_dir();
        let now = Utc::now();
        let dir = reservation_dir(&base, "ph");
        fs::create_dir_all(&dir).unwrap();
        let final_path = reservation_path(&base, "ph", "x", "y");
        let bytes = serde_json::to_vec(&ReservationFile {
            pre_instance_id: "x".to_string(),
            post_instance_id: "y".to_string(),
            reserved_at: now.to_rfc3339(),
        })
        .unwrap();

        // step: temp 書込後・link 前。
        let temp = reserve_build_temp(&dir, "x", "y", &bytes).unwrap();
        assert!(
            !final_path.exists(),
            "link 前: final は dir entry を持たない（sweep 非観測）"
        );
        assert_eq!(
            count_frames(&base, "ph"),
            0,
            "link 前は count 0（final 未生成 / temp は非 .json）"
        );
        let removed_before = sweep_stale_reservations_in(&base, now);
        assert_eq!(
            removed_before, 0,
            "link 前 sweep: final 不在で当該枠は消せない"
        );
        assert!(
            temp.exists(),
            "temp(.tmp) は非 .json のため sweep は無視（消さない）"
        );

        // step: link 後。
        assert_eq!(
            reserve_link_claim(&temp, &final_path).unwrap(),
            ReserveOutcome::Created
        );
        assert!(final_path.exists());
        assert!(!temp.exists(), "temp は link 後に掃除済（orphan 無し）");
        assert!(
            read_frame_pair(&final_path).is_some(),
            "link 後 final は必ず parse 可（完全 JSON）"
        );
        assert_eq!(
            count_frames(&base, "ph"),
            1,
            "link 後は完全枠 1（count 反映）"
        );
        let removed_after = sweep_stale_reservations_in(&base, now);
        assert_eq!(removed_after, 0, "link 後 sweep: 完全枠・age fresh で保護");
        assert_eq!(count_frames(&base, "ph"), 1, "保護され枠は残る");
    }

    /// 12 枠満杯 + 並行 sweep 連打の下でも 13 本目は全 reject（race 下で cap=12 維持）。
    #[test]
    fn thirteenth_rejected_under_concurrent_sweep() {
        let base = isolated_dir();
        let now = Utc::now();
        for i in 0..crate::exclusion::MAX_ACTIVE_PER_PROJECT {
            reserve_pairing_at(&base, "ph", &format!("p{i}"), &format!("q{i}"), now).unwrap();
        }
        let base_arc = Arc::new(base.clone());
        let stop = Arc::new(std::sync::atomic::AtomicBool::new(false));
        // sweep を別スレッドで連打（create→link 間の race を誘発）。
        let sweeper = {
            let base = Arc::clone(&base_arc);
            let stop = Arc::clone(&stop);
            std::thread::spawn(move || {
                while !stop.load(Ordering::Relaxed) {
                    let _ = sweep_stale_reservations_in(&base, Utc::now());
                }
            })
        };
        // 13 本目を 16 スレッドで試行（各別 pairing / gate = reserve→count>MAX→release）。
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
        stop.store(true, Ordering::Relaxed);
        sweeper.join().unwrap();
        assert_eq!(
            succeeded.load(Ordering::Relaxed),
            0,
            "race 下でも 13 本目（全 16 並行試行）は全 reject"
        );
        assert_eq!(
            count_frames(&base, "ph"),
            crate::exclusion::MAX_ACTIVE_PER_PROJECT,
            "cap=12 維持（over-cap も leak も無い）"
        );
    }

    /// (iii) atomic claim cross-process safety: 同一 pairing key を多スレッドが同時に reserve しても
    /// **ちょうど 1 つだけ** Created（残りは AlreadyReserved）。`hard_link` の EEXIST 原子性
    /// （OS が保証・cross-process でも同一プリミティブ / G-115-366 A）を並行 race で実証する。
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
            "atomic claim (hard_link): 同一 pairing 枠は並行 race でちょうど 1 つだけ Created"
        );
        assert_eq!(
            already.load(Ordering::Relaxed),
            15,
            "残り 15 は AlreadyReserved"
        );
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
