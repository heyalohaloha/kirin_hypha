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
fn same_pre_cannot_be_reserved_by_a_different_post() {
    let base = isolated_dir();
    let now = Utc::now();
    assert_eq!(
        reserve_pairing_at(&base, "ph", "pre", "post-a", now).unwrap(),
        ReserveOutcome::Created
    );
    assert_eq!(
        reserve_pairing_at(&base, "ph", "pre", "post-b", now).unwrap(),
        ReserveOutcome::PreInUse,
        "PRE ownership is the cross-process truth; a second POST cannot overwrite it"
    );
    assert_eq!(count_frames(&base, "ph"), 1);
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
fn late_release_from_old_post_cannot_remove_new_owner() {
    let base = isolated_dir();
    let now = Utc::now();
    reserve_pairing_at(&base, "ph", "pre", "post-old", now).unwrap();
    release_pairing(&base, "ph", "pre", "post-old");
    assert_eq!(
        reserve_pairing_at(&base, "ph", "pre", "post-new", now).unwrap(),
        ReserveOutcome::Created
    );

    release_pairing(&base, "ph", "pre", "post-old");

    assert_eq!(
        reserve_pairing_at(&base, "ph", "pre", "post-new", now).unwrap(),
        ReserveOutcome::AlreadyReserved,
        "cleanup must compare payload ownership before unlinking the PRE claim"
    );
    assert_eq!(count_frames(&base, "ph"), 1);
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

/// v2 lease: stale mtime の孤児だけを回収し、active owner が exact inode を refresh した
/// reservation は保持する。active 判定に plugin_data history scan は不要。
#[test]
fn sweep_reclaims_orphan_but_protects_fresh_and_refreshed_lease() {
    let base = isolated_dir();
    let now = Utc::now();
    let old = now - chrono::Duration::seconds(RESERVATION_TTL_SECS + 10);
    // (1) fresh 枠（grace 内）→ 保持。
    reserve_pairing_at(&base, "ph", "fresh-pre", "fresh-post", now).unwrap();
    // (2) 古い枠 + marker 無し → 孤児 → 回収。
    reserve_pairing_at(&base, "ph", "orphan-pre", "orphan-post", old).unwrap();
    // (3) 古い枠を active owner が exact refresh → 保持。
    reserve_pairing_at(&base, "ph", "live-pre", "live-post", old).unwrap();
    let old_time: SystemTime = old.into();
    for pre in ["orphan-pre", "live-pre"] {
        let file = OpenOptions::new()
            .write(true)
            .open(reservation_path(&base, "ph", pre))
            .unwrap();
        file.set_times(FileTimes::new().set_modified(old_time))
            .unwrap();
    }
    let now_time: SystemTime = now.into();
    assert!(refresh_pairing_at(
        &base,
        "ph",
        "live-pre",
        "live-post",
        now_time
    ));

    let removed = sweep_stale_reservations_in(&base, now);
    assert_eq!(removed, 1, "孤児 1 件のみ回収");
    assert_eq!(
        count_frames(&base, "ph"),
        2,
        "fresh 枠 + refreshed lease は残る"
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
    let final_path = reservation_path(&base, "ph", "x");
    let bytes = serde_json::to_vec(&ReservationFile {
        pre_instance_id: "x".to_string(),
        post_instance_id: "y".to_string(),
        reserved_at: now.to_rfc3339(),
        lease_version: RESERVATION_LEASE_VERSION,
    })
    .unwrap();

    // step: temp 書込後・link 前。
    let temp = reserve_build_temp(&dir, "x", &bytes).unwrap();
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
                Ok(ReserveOutcome::PreInUse) => {}
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
        Ok(ReserveOutcome::PreInUse) => false,
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
