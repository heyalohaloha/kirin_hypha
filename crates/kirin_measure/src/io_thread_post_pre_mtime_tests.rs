use super::*;

/// B-128 reopen / G-115-376 gate ①: `find_pre_json_mtime` は他 instance content 由来の
/// `pre_iid` (peer pre.json instance_id) を `.join()` する **唯一** の path builder。
/// path-unsafe な peer instance_id (`..` traversal / 絶対 / 区切り / 制御文字 / overlength /
/// `_q_` 詐称) では within-base wall が reject し、`.join()`→stat に到達せず base 外の
/// pre.json の存在・mtime を **観測しない** (mtime オラクル封鎖)。同時に valid な peer
/// instance_id は従来どおり `Some(mtime)` を返す (over-reject なし = 正常系の pairing 不変)。
///
/// guard を外すと "../../SECRET" 経路が base/SECRET/pre.json を stat して `Some` を返すため
/// 本 test は fail する (= guard 感度の確証)。
#[test]
fn find_pre_json_mtime_rejects_path_unsafe_pre_iid_no_external_stat() {
    let base = std::env::temp_dir().join(format!("kirin_b128_mtime_oracle_{}", std::process::id()));
    let _ = fs::remove_dir_all(&base);
    fs::create_dir_all(&base).unwrap();
    let kirin_root = base.join("kirin");
    // read_dir(kirin_root) が 1 件以上返すよう project subdir を作る (loop body 到達条件)。
    let project_dir = kirin_root.join("proj-uuid");
    fs::create_dir_all(&project_dir).unwrap();

    // (control / over-reject 封じ) valid peer instance_id 配下の正規 pre.json → Some(mtime)。
    let legit_iid = "pre-iid-legit";
    let legit_dir = project_dir.join(legit_iid);
    fs::create_dir_all(&legit_dir).unwrap();
    fs::write(legit_dir.join("pre.json"), b"{}").unwrap();
    assert!(
        find_pre_json_mtime(&kirin_root, legit_iid).is_some(),
        "valid peer instance_id は従来どおり pre.json mtime を返す (over-reject 不可)"
    );

    // base 外 (kirin_root の 2 つ上 = base) に SECRET/pre.json を置く。無 guard なら
    // pre_iid="../../SECRET" で project_dir/../../SECRET/pre.json = base/SECRET/pre.json を
    // stat し mtime を観測できてしまう (mtime オラクル)。
    let secret_dir = base.join("SECRET");
    fs::create_dir_all(&secret_dir).unwrap();
    fs::write(secret_dir.join("pre.json"), b"{}").unwrap();
    // traversal target が実在し project_dir から到達可能であることを test 自身で確認。
    assert!(
        project_dir
            .join("..")
            .join("..")
            .join("SECRET")
            .join("pre.json")
            .exists(),
        "precondition: traversal target base/SECRET/pre.json は project_dir から到達可能"
    );

    // 各攻撃ベクタ: guard reject → None (`.join()`→stat に到達せず base 外を観測しない)。
    let attacks = [
        "../../SECRET",   // traversal (base 外の実在 pre.json を狙う = mtime オラクル本体)
        "..",             // 親
        ".",              // 自身
        "/etc",           // 絶対パス
        "..\\..\\SECRET", // backslash 区切り
        "_q_deadbeef",    // quarantine prefix 詐称 (cap-bypass 封止と同基準)
        "evil\u{0}null",  // null byte / 制御文字
        "tab\tname",      // 制御文字 (tab)
        "",               // empty (既存 early-return None だが網羅)
    ];
    for a in attacks {
        assert_eq!(
                find_pre_json_mtime(&kirin_root, a),
                None,
                "path-unsafe pre_iid {a:?} は reject され base 外 stat に到達しない (mtime オラクル封鎖)"
            );
    }
    // overlength (MAX_COMPONENT_LEN 超) も reject (D4 と同基準)。
    let overlong = "a".repeat(crate::path_identity::MAX_COMPONENT_LEN + 1);
    assert_eq!(
        find_pre_json_mtime(&kirin_root, &overlong),
        None,
        "overlength pre_iid は reject (D4 と同基準)"
    );
}
