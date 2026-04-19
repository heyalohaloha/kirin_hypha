#!/bin/zsh
# Kirin Hypha — hypha-fork ログ取得（原因2: first-click 診断用）
#
# 仕組み:
#   nih-plug は nih-log crate を使い、環境変数 NIH_LOG=<path> のとき
#   指定ファイルに append でログを書く。macOS unified logging (log stream)
#   には書き込まないため、launchctl setenv で launchd にフォルトを設定し、
#   新規起動する GUI アプリ（Studio One）に NIH_LOG を継承させる。
#
# 使い方:
#   1. Studio One を完全に終了しておく（⌘Q）— すでに起動中のプロセスには
#      NIH_LOG が反映されないため。
#   2. このスクリプトを実行: ./tools/capture_hypha_log.sh
#   3. 別ウィンドウで Studio One を起動し、Hypha 画面を操作。
#   4. 作業後、このターミナルに戻って Ctrl+C → 自動で NIH_LOG を解除 +
#      hypha-fork 行だけ抽出した filtered ログを隣に生成する。
#   5. cat ~/Dev/kirin_hypha/hypha_fork_debug_filtered.log で内容確認し、
#      そのまま Adv-実装 に貼り付け。
#
# sudo は不要（launchctl setenv は user session domain なので）。

set -u

LOG_FILE="$HOME/Dev/kirin_hypha/hypha_fork_debug.log"
FILTERED_FILE="$HOME/Dev/kirin_hypha/hypha_fork_debug_filtered.log"

# ── Studio One 起動中チェック ───────────────────────────────────────────
if pgrep -f "Studio One.app/Contents/MacOS" > /dev/null 2>&1; then
    echo "ERROR: Studio One が起動中です。⌘Q で完全終了してから再実行してください。"
    echo "       （すでに起動中のプロセスには NIH_LOG が反映されません）"
    exit 1
fi

# ── ログファイル準備 ────────────────────────────────────────────────────
mkdir -p "$(dirname "$LOG_FILE")"
{
    echo ""
    echo "================================================================"
    echo "Session started: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "NIH_LOG        : $LOG_FILE"
    echo "================================================================"
} >> "$LOG_FILE"

# ── launchd に NIH_LOG をセット ────────────────────────────────────────
launchctl setenv NIH_LOG "$LOG_FILE"

cleanup() {
    echo ""
    echo "NIH_LOG を launchd から解除中..."
    launchctl unsetenv NIH_LOG
    {
        echo "================================================================"
        echo "Session ended:   $(date '+%Y-%m-%d %H:%M:%S %z')"
        echo "================================================================"
    } >> "$LOG_FILE"

    # hypha-fork 行だけ抽出した filtered ログを生成（既存は上書き）。
    # grep が 0 件ヒット（exit 1）でも空ファイルが作られ、スクリプトは継続する。
    grep "hypha-fork" "$LOG_FILE" > "$FILTERED_FILE" || true
    local match_count
    match_count=$(wc -l < "$FILTERED_FILE" | tr -d ' ')

    echo ""
    echo "完了。"
    echo "  生ログ         : $LOG_FILE"
    echo "  フィルタ済みログ: $FILTERED_FILE  ($match_count 行)"
    echo ""
    if [ "$match_count" = "0" ]; then
        echo "WARN: hypha-fork 行が 0 件です。"
        echo "  - フォーク版 VST3 が配置されていない可能性があります。"
        echo "  - 生ログを確認: cat \"$LOG_FILE\""
    else
        echo "このファイル内容を Adv-実装 に貼ってください:"
        echo "  cat \"$FILTERED_FILE\""
    fi
}
# INT/TERM で明示的に exit し、EXIT trap 経由で cleanup を 1 回だけ走らせる。
trap 'exit 130' INT TERM
trap cleanup EXIT

# ── 案内 ────────────────────────────────────────────────────────────────
echo "================================================================"
echo " NIH_LOG = $LOG_FILE"
echo " launchd にセット完了。sudo 不要。"
echo "================================================================"
echo ""
echo "次の手順:"
echo "  1. Studio One を通常どおり起動（Finder / Dock から）"
echo "  2. Hypha を挿入したプロジェクトを開く"
echo "  3. Hypha 画面を非アクティブにする（別ウィンドウをクリック）"
echo "  4. Hypha ボタン上にマウスをホバー"
echo "  5. ボタンを 1 クリック"
echo ""
echo "終了時: Studio One を ⌘Q → このターミナルで Ctrl+C"
echo ""
echo "-------- live log tail （Ctrl+C で停止） --------"
# ログファイルを live tail 表示。exec は使わない（シェルが置き換わると EXIT trap
# が走らず filtered ログが生成されないため）。Ctrl+C で tail 終了 → EXIT trap
# で cleanup。
tail -n 0 -F "$LOG_FILE"
