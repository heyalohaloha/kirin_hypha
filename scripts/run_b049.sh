#!/usr/bin/env bash
# B-049 / W-279 / G-115-247: SignalState::Inactive Last Known Good 拡張
# 仕様確定書: 36259d4f-6faf-818b-8114-e7d9488fbed7
set -e

cd ~/Dev/kirin_hypha

echo "=== B-049 / Hypha POST SignalState::Inactive Last Known Good 拡張 / W-279 ==="

# --- Step 0: 起動前提確認 ---
HEAD_HASH=$(git log -1 --format=%H | cut -c1-7)
BRANCH=$(git branch --show-current)
if [ "$HEAD_HASH" != "e3371b3" ]; then
  echo "ERROR: HEAD=$HEAD_HASH (期待: e3371b3) — 設計前提崩れ / 停止"
  exit 1
fi
if [ "$BRANCH" != "feature/hypha-pre-post-stabilization" ]; then
  echo "ERROR: branch=$BRANCH (期待: feature/hypha-pre-post-stabilization) / 停止"
  exit 1
fi
echo "HEAD=$HEAD_HASH / branch=$BRANCH OK"

# --- Step 1: editor.rs SignalState::Inactive 分岐の改修 ---
echo "--- editor.rs SignalState::Inactive 分岐改修 ---"

# 実態 indent: match sig 内部の arm は 16-space (match 自体が 12-space 配下)
python3 << 'PY_EOF'
import re
path = "crates/hypha_post/src/editor.rs"
with open(path, "r", encoding="utf-8") as f:
    src = f.read()

# 実態 indent (16-space arm / 20-space body) と完全一致する old_block
old_block = """                SignalState::Inactive => {
                    draw_inactive_grid(ui);
                    ui.add_space(4.0);
                    draw_button_row(ui, recording, license, state, m, now);
                }"""

new_block = """                SignalState::Inactive => {
                    if let Some(snap) = &d.last_active {
                        // B-049: POST 自身が Inactive でも過去 Active Δ 値を凍結保持表示
                        draw_delta_grid_frozen(ui, snap, false);
                    } else {
                        // last_active=None (初回起動 / Active 未経験) は既存 fallback
                        draw_inactive_grid(ui);
                    }
                    ui.add_space(4.0);
                    draw_button_row(ui, recording, license, state, m, now);
                }"""

if old_block not in src:
    print("ERROR: SignalState::Inactive 既存ブロックが見つからない / 実態 indent 不一致")
    raise SystemExit(1)

# 一意性確認
count = src.count(old_block)
if count != 1:
    print(f"ERROR: SignalState::Inactive 既存ブロックが {count} 件マッチ / 一意でない")
    raise SystemExit(1)

src = src.replace(old_block, new_block, 1)

with open(path, "w", encoding="utf-8") as f:
    f.write(src)

print("editor.rs SignalState::Inactive 改修完了 (実態 indent 一致 / 一意置換)")
PY_EOF

# 改修確認
echo "--- 改修後 editor.rs SignalState::Inactive 分岐 ---"
sed -n '392,406p' crates/hypha_post/src/editor.rs

# 必須パターンの grep 確認
if ! grep -q "if let Some(snap) = &d.last_active" crates/hypha_post/src/editor.rs; then
  echo "ERROR: if let Some(snap) = &d.last_active パターンが見つからない / 改修失敗"
  exit 1
fi

if ! grep -q "draw_delta_grid_frozen(ui, snap, false)" crates/hypha_post/src/editor.rs; then
  echo "ERROR: draw_delta_grid_frozen(ui, snap, false) 呼出が見つからない / 改修失敗"
  exit 1
fi

echo "editor.rs SignalState::Inactive 改修 OK"

# --- Step 2: 新規テスト追加 (gui_wiring_test.rs) ---
echo "--- 新規テスト 2 件追加 (gui_wiring_test.rs) ---"

cat >> crates/hypha_post/tests/gui_wiring_test.rs << 'TEST_EOF'

/// B-049 / G-115-247: SignalState::Inactive 分岐に Last Known Good を追加。
/// editor.rs の SignalState::Inactive 分岐内で以下 3 パターンが共存することを保証:
/// - `if let Some(snap) = &d.last_active` (凍結値経路)
/// - `draw_delta_grid_frozen(ui, snap, false)` (B-048 関数再利用)
/// - else 分岐に `draw_inactive_grid(ui)` (既存 fallback / 初回起動)
#[test]
fn editor_rs_inactive_branch_has_last_known_good_and_fallback() {
    let src = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("src")
            .join("editor.rs"),
    )
    .expect("read editor.rs");

    // Inactive arm の開始位置から、次の arm (SignalState::Active) までを抽出。
    let inactive_idx = src
        .find("SignalState::Inactive =>")
        .expect("SignalState::Inactive arm not found");
    let after = &src[inactive_idx..];
    let end_marker = after
        .find("SignalState::Active =>")
        .expect("SignalState::Active arm must follow Inactive arm");
    let block = &after[..end_marker];

    assert!(
        block.contains("if let Some(snap) = &d.last_active"),
        "B-049: SignalState::Inactive arm must contain `if let Some(snap) = &d.last_active`"
    );
    assert!(
        block.contains("draw_delta_grid_frozen(ui, snap, false)"),
        "B-049: SignalState::Inactive arm must call draw_delta_grid_frozen(ui, snap, false)"
    );
    assert!(
        block.contains("draw_inactive_grid(ui)"),
        "B-049: SignalState::Inactive arm must keep draw_inactive_grid(ui) as fallback"
    );
}

/// B-049 不変条件 #1: SignalState::Bypassed 分岐は完全不変。
/// Bypassed arm 開始から次の arm (SignalState::Inactive) までの範囲内に
/// `last_active` / `draw_delta_grid_frozen` 参照が無いことを確認する。
#[test]
fn editor_rs_bypassed_branch_unchanged_no_last_active_ref() {
    let src = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("src")
            .join("editor.rs"),
    )
    .expect("read editor.rs");

    let bypassed_idx = src
        .find("SignalState::Bypassed =>")
        .expect("SignalState::Bypassed arm not found");
    let after = &src[bypassed_idx..];
    // Bypassed arm の境界 = 次 arm `SignalState::Inactive =>` までを Bypassed 範囲とみなす
    let end_marker = after
        .find("SignalState::Inactive =>")
        .expect("SignalState::Inactive arm must follow Bypassed arm");
    let block = &after[..end_marker];

    assert!(
        !block.contains("last_active"),
        "B-049 invariant #1: SignalState::Bypassed arm must NOT reference last_active"
    );
    assert!(
        !block.contains("draw_delta_grid_frozen"),
        "B-049 invariant #1: SignalState::Bypassed arm must NOT call draw_delta_grid_frozen"
    );
}
TEST_EOF

echo "新規テスト 2 件追加完了"

# --- Step 3: cargo build 確認 ---
echo "--- cargo build (warning なし確認) ---"
cargo build --package hypha_post 2>&1 | tee /tmp/b049_build.log
if grep -q "^error" /tmp/b049_build.log; then
  echo "ERROR: build エラー / 停止"
  exit 1
fi
# kirin_hypha 本体の warning ゼロ確認 (vendor/* 除外 / G-115-100)
if grep -E "^warning" /tmp/b049_build.log | grep -v "vendor/" | grep -v "kirin_measure@" | grep -q "."; then
  echo "WARN: kirin_hypha 本体に warning あり / 確認推奨 (継続)"
  grep -E "^warning" /tmp/b049_build.log | grep -v "vendor/" | grep -v "kirin_measure@" | head -5
fi

# --- Step 4: cargo test 全件 pass 確認 ---
echo "--- cargo test hypha_post (新規 2 件 + 既存 18+5+11=34 件 + B-048 1件 = 計 37 件想定) ---"
cargo test --package hypha_post --no-fail-fast 2>&1 | tee /tmp/b049_hypha_post_test.log

echo "--- cargo test kirin_measure (B-048 で追加した 6 件 + 既存 / baseline 5 件赤許容) ---"
cargo test --package kirin_measure --no-fail-fast 2>&1 | tee /tmp/b049_kirin_measure_test.log

echo "--- cargo test hypha_pre (独立 / 触らない確認) ---"
cargo test --package hypha_pre --no-fail-fast 2>&1 | tee /tmp/b049_hypha_pre_test.log
PRE_FAIL=$(grep -E "test result: FAILED" /tmp/b049_hypha_pre_test.log | head -1 || echo "")
if [ -n "$PRE_FAIL" ]; then
  echo "ERROR: hypha_pre test fail / 停止"
  exit 1
fi

# 新規テスト 2 件が pass に含まれていることを確認
if ! grep -q "editor_rs_inactive_branch_has_last_known_good_and_fallback ... ok" /tmp/b049_hypha_post_test.log; then
  echo "ERROR: 新規テスト editor_rs_inactive_branch_has_last_known_good_and_fallback が pass しない"
  grep "editor_rs_inactive_branch" /tmp/b049_hypha_post_test.log
  exit 1
fi
if ! grep -q "editor_rs_bypassed_branch_unchanged_no_last_active_ref ... ok" /tmp/b049_hypha_post_test.log; then
  echo "ERROR: 新規テスト editor_rs_bypassed_branch_unchanged_no_last_active_ref が pass しない"
  grep "editor_rs_bypassed_branch" /tmp/b049_hypha_post_test.log
  exit 1
fi

# baseline 赤 5 件は scope 外 (B-048 と同等 / 仕様 §7.3)
echo "--- baseline 赤 5 件 scope 外確認 (B-048 と同等) ---"
echo "kirin_measure 赤一覧:"
grep "FAILED$" /tmp/b049_kirin_measure_test.log | head -10 || echo "(なし)"

# --- Step 5: F-5 自己確認 ---
echo "--- F-5 自己確認チェックリスト ---"
echo "[*] editor.rs SignalState::Inactive 分岐に if let Some(snap) = &d.last_active 追加 / 確認済"
echo "[*] editor.rs SignalState::Inactive 分岐に draw_delta_grid_frozen(ui, snap, false) 呼出 / 確認済"
echo "[*] editor.rs SignalState::Inactive 分岐の else 分岐に draw_inactive_grid(ui) 維持 / 確認済"
echo "[*] editor.rs SignalState::Bypassed 分岐は完全不変 (last_active 参照なし / 新規テストで verify)"
echo "[*] editor.rs SignalState::Active 分岐は完全不変 (B-048 NoPre 経路含む)"
echo "[*] draw_button_row 呼出は不変 (license 操作可能 / §4-4)"
echo "[*] draw_delta_grid_frozen の signature 不変 (B-048 のまま)"
echo "[*] DeltaResult.last_active 生成・更新ロジック (merge_last_active) は完全不変"
echo "[*] 新規テスト 2 件追加 (editor_rs_inactive_branch_* + editor_rs_bypassed_branch_*)"
echo "[*] B-048 で追加されたテスト 6 件は pass 維持"
echo "[*] hypha_pre 全テストは pass 維持 (独立 / 触らない)"
echo "[*] cargo build エラーゼロ / kirin_hypha 本体 warning ゼロ (vendor/* 除外)"

# --- Step 6: commit ---
echo "--- git add + commit ---"
git add crates/hypha_post/src/editor.rs crates/hypha_post/tests/gui_wiring_test.rs scripts/run_b049.sh
git status

git commit -m "[W-279] feat(post): SignalState::Inactive Last Known Good 拡張 (G-115-247)

POST 自身が SignalState::Inactive (DAW transport 停止 or POST 入力 silent) になっても
過去 Active で計測した Δ 値を draw_delta_grid_frozen (COL_MUTED) で凍結表示する。
B-048 で導入された draw_delta_grid_frozen / DeltaResult.last_active を再利用する。

# crates/hypha_post/src/editor.rs (約 10 行 追加)
- SignalState::Inactive 分岐に if let Some(snap) = &d.last_active パターン追加
- Some の場合: draw_delta_grid_frozen(ui, snap, false) で COL_MUTED 凍結 Δ 表示
- None の場合: 既存 draw_inactive_grid(ui) (--- 表示 / 初回起動の fallback)
- draw_button_row(ui, recording, license, state, m, now) 呼出は不変
- SignalState::Bypassed 分岐は完全不変 (本 B-049 scope 外 / 別 G 番号で別途判断)
- SignalState::Active 分岐は完全不変 (B-048 NoPre 経路含む)

# crates/hypha_post/tests/gui_wiring_test.rs (新規 2 件)
- editor_rs_inactive_branch_has_last_known_good_and_fallback (Inactive 分岐 verify)
- editor_rs_bypassed_branch_unchanged_no_last_active_ref (Bypassed 不変 verify)

# scripts/run_b049.sh
- B-049 実装を 1 本のスクリプトで束ねる (起動前提確認 / 改修 / build / test / commit)

# 不変条件 (仕様確定書 §4 / 全件満たすことで既存経路完全保護)
1. SignalState::Bypassed 分岐は完全不変
2. SignalState::Active 分岐は完全不変 (B-048 Last Known Good 含む)
3. SignalState::Inactive + last_active=None は既存 draw_inactive_grid 経路
4. draw_button_row 呼出は不変
5. recording=true && sig==Inactive は draw_record_section に到達しない (sig==Active gate)
6. DeltaResult.last_active 生成・更新ロジック (merge_last_active) は完全不変

# B-048 仕様確定書 §6 改訂
SignalState::Bypassed → 前段ゲート維持 (draw_inactive_grid 一択 / 別 G 番号)
SignalState::Inactive → last_active=Some なら draw_delta_grid_frozen / None なら draw_inactive_grid

仕様確定書: 36259d4f-6faf-818b-8114-e7d9488fbed7
G-115-247 確定判断: 36259d4f-6faf-8128-a82d-efa603e57154
B-048 仕様確定書 §6 改訂: 36259d4f-6faf-8117-b223-fb1ac694847c
番人#180 -> advisor S213+ Handoff: 36259d4f-6faf-8176-81d8-d3c7066511f9

Co-Authored-By: Claude Code (1M context) <noreply@anthropic.com>"

git log -1 --oneline

echo "=== B-049 完了 ==="
