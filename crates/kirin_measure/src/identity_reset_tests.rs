// ── B-110: 共有セル本番リセットの単体テスト（C-1〜C-5 / 全て非実機）─────────
//
// A-1 判定が **per-binary**（rlib 静的リンク・各バンドルが自前 static）のため、C-1〜C-5 は
// 単一バイナリ内の意味論として記述する（横断整合は filesystem 経路）。C-1〜C-5 はローカル
// `IdentityLifecycle` + ローカルセルで駆動し、global static を触らない（並列テスト安全 /
// B-106 と同方針）。global path（`clear_shared_identity_cells` が実セルを空にする）は専用 1
// テストで検証する（この 1 件のみが global セルを触る）。

use crate::{
    clear_shared_identity_cells, daw_session_id_cell, peek_project_uuid, project_uuid_cell,
    set_daw_session_id, set_project_uuid, IdentityLifecycle,
};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, RwLock};

type Cell = Arc<RwLock<String>>;

fn new_cell() -> Cell {
    Arc::new(RwLock::new(String::new()))
}
fn read(cell: &Cell) -> String {
    cell.read().unwrap().clone()
}
fn clear(cell: &Cell) {
    cell.write().unwrap().clear();
}
/// `set_project_uuid` 相当: 上書き seed（egui initialize の chunk-persist 反映）。
fn set_value(cell: &Cell, v: &str) {
    *cell.write().unwrap() = v.to_string();
}
/// `resolve_shared_id` 相当: first-wins set-if-empty（FFI enable の seed）。
fn seed_if_empty(cell: &Cell, candidate: &str) -> String {
    let mut g = cell.write().unwrap();
    if g.is_empty() {
        *g = candidate.to_string();
    }
    g.clone()
}
/// `process_project_hash` 相当: 空なら一意値を生成、非空なら現値（lazy fallback）。
fn seed_or_generate(cell: &Cell, gen: &str) -> String {
    let mut g = cell.write().unwrap();
    if g.is_empty() {
        *g = gen.to_string();
    }
    g.clone()
}

// C-1 [番人固定 i]: 全削除→即追加 = 新 UUID（grace なし / 裁定 a のエッジ仕様化）。
#[test]
fn c1_delete_all_then_immediate_add_seeds_new_uuid() {
    let lc = IdentityLifecycle::new();
    let cell = new_cell();
    // インスタンス 1 生成 → lazy 生成で seed。
    lc.attach();
    let uuid1 = seed_or_generate(&cell, "uuid-gen-1");
    // 全削除（refcount 0）→ clear。
    lc.detach(|| clear(&cell));
    assert_eq!(lc.count(), 0);
    assert!(
        read(&cell).is_empty(),
        "refcount 0 で共有セルが clear される"
    );
    // 即追加（新インスタンス）→ 空セルなので新しい値を生成。
    lc.attach();
    let uuid2 = seed_or_generate(&cell, "uuid-gen-2");
    assert_ne!(
        uuid1, uuid2,
        "全削除→即追加は前 UUID を引き継がず新 UUID を seed する（grace なし）"
    );
}

// C-2 [番人固定 ii]: プロジェクト切替で clear（P1 回帰: 0→0 遷移後の enable が
// 前プロジェクト値を引き継がない）。
#[test]
fn c2_project_switch_does_not_inherit_previous_value() {
    let lc = IdentityLifecycle::new();
    let cell = new_cell();
    // プロジェクト 1: initialize が chunk-persist 値を set。
    lc.attach();
    set_value(&cell, "project-1-uuid");
    assert_eq!(read(&cell), "project-1-uuid");
    // プロジェクト 1 を閉じる（全インスタンス破棄）→ clear。
    lc.detach(|| clear(&cell));
    // プロジェクト 2（chunk に project_uuid 未保存＝空）: lazy 生成。
    lc.attach();
    let v = seed_or_generate(&cell, "project-2-fresh");
    assert_ne!(
        v, "project-1-uuid",
        "次プロジェクトは前プロジェクトの project_uuid を引き継がない（leak 解消）"
    );
}

// C-3 [番人固定 iii]: 同一 chunk 復元で同値に再収束。
#[test]
fn c3_chunk_restore_reconverges_to_same_value() {
    let lc = IdentityLifecycle::new();
    let cell = new_cell();
    lc.attach();
    set_value(&cell, "chunk-uuid-X");
    lc.detach(|| clear(&cell));
    assert!(read(&cell).is_empty(), "clear 後は空");
    // 同一プロジェクトを再オープン: chunk が同 UUID を復元 → set。
    lc.attach();
    set_value(&cell, "chunk-uuid-X");
    assert_eq!(
        read(&cell),
        "chunk-uuid-X",
        "同一 chunk の復元値で同値に再収束する"
    );
}

// C-4: destroy×create レース。0 遷移 clear と並行 create seed が lock で相互排除され、
// 生存インスタンスの seed が消されない（生存 refcount のセルは決して空でない）。
#[test]
fn c4_destroy_create_race_preserves_surviving_seed() {
    use std::thread;
    for i in 0..400 {
        let lc = Arc::new(IdentityLifecycle::new());
        let cell = new_cell();
        // 初期状態: インスタンス 1 が生存・seed 済み。
        lc.attach();
        set_value(&cell, "old");

        // 破棄側: 旧インスタンスを detach（refcount 0 へ落ちれば clear）。
        let lc_d = Arc::clone(&lc);
        let cell_d = cell.clone();
        let d = thread::spawn(move || {
            lc_d.detach(move || clear(&cell_d));
        });
        // 生成側: 新インスタンスを attach し first-wins で seed。
        let lc_c = Arc::clone(&lc);
        let cell_c = cell.clone();
        let c = thread::spawn(move || {
            lc_c.attach();
            seed_if_empty(&cell_c, "new");
        });
        d.join().unwrap();
        c.join().unwrap();

        // attach 1（old）+ attach 1（new）− detach 1 = refcount 1（new が生存）。
        assert_eq!(lc.count(), 1, "iter {i}: 生存インスタンスは 1");
        assert!(
            !read(&cell).is_empty(),
            "iter {i}: 生存インスタンスのセルが空になってはならない（new seed が clear に消されない）"
        );
    }
}

// C-5: refcount 増減の単体（create/destroy で正しく増減・0 でのみ clear・二重 destroy 防御）。
#[test]
fn c5_refcount_inc_dec_and_double_detach_is_noop() {
    let lc = IdentityLifecycle::new();
    let cleared = AtomicUsize::new(0);
    let bump = || {
        cleared.fetch_add(1, Ordering::SeqCst);
    };
    assert_eq!(lc.count(), 0);
    assert_eq!(lc.attach(), 1);
    assert_eq!(lc.attach(), 2);
    assert_eq!(lc.detach(bump), 1);
    assert_eq!(
        cleared.load(Ordering::SeqCst),
        0,
        "0 でないので clear しない"
    );
    assert_eq!(lc.detach(bump), 0);
    assert_eq!(
        cleared.load(Ordering::SeqCst),
        1,
        "0 到達でちょうど 1 度 clear する"
    );
    // 過剰 destroy: underflow させず・再 clear もしない。
    assert_eq!(lc.detach(bump), 0);
    assert_eq!(lc.count(), 0);
    assert_eq!(
        cleared.load(Ordering::SeqCst),
        1,
        "二重 detach は no-op（再 clear なし / underflow なし）"
    );
}

// global path: `clear_shared_identity_cells` が実 global セルを空に戻す。
// ※ 本テストのみが global project_uuid / daw_session_id セルを触る（並列衝突回避）。
#[test]
fn global_clear_empties_real_identity_cells() {
    set_project_uuid("global-proj".to_string());
    set_daw_session_id("global-daw".to_string());
    assert_eq!(peek_project_uuid(), "global-proj");
    assert_eq!(daw_session_id_cell().read().unwrap().clone(), "global-daw");
    clear_shared_identity_cells();
    assert!(
        peek_project_uuid().is_empty(),
        "clear 後 project_uuid セルは空（次 seed で埋め直し）"
    );
    assert!(
        project_uuid_cell().read().unwrap().is_empty(),
        "project_uuid セル内側が空"
    );
    assert!(
        daw_session_id_cell().read().unwrap().is_empty(),
        "clear 後 daw_session_id セルは空"
    );
}
