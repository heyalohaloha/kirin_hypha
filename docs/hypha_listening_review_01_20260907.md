# 判定用HTML 01の納品記録

日付：2026-09-07。
基準：`eaeee6a4` / B-730。
プラグイン、Windows配置、音源原本は変更していない。

## 成果物

[判定用HTMLを開く](/Users/nishiodaisuke/Downloads/Hypha_Listening_Review_01_20260907/index.html)。
Chromeで使用する。
SPACE 3件と2MIX ATTACK 3件で、各30秒の原音コピーの中央10秒を判定する。
SPACEは区間、ATTACKは立ち上がり位置を記録し、判断不能と再生不可も別の回答として残す。
初回は代表的な印を集めるため、この回答から最終PrecisionやRecallを計算しない。

自動保存、未回答からの再開、JSONとTSVの書出し、JSONの再読込に対応した。
途中のJSONをCodexへ添付すれば、その時点の回答を受け取れる。
音源フォルダはHTMLと一緒に保持する。
音源や回答の外部送信、公開、リポジトリへの追加は行っていない。

HTML本体は1,909,590 bytes、6音源は合計135,432,596 bytes、180秒。
全音源はstereoで、44.1 kHzが3件、48 kHzが1件、192 kHzが2件。
検証用WAVからのコピーは6件ともSHA-256一致。
未使用評価用音源は開いていない。

## 検証

- Nodeの素材台帳と新規モデル試験：17 pass。
- Chrome 152.0.7977.76のオフラインdecode：6件すべてでnative sample rate、チャンネル数、サンプル数が一致。ピーク差は1e-7未満、RMS差は1e-10未満。
- ブラウザ操作試験：pass。再生進行、区間と点の追加、確定、途中再開、JSON / TSV書出し、別ブラウザ状態への読込み、異なるmanifestの拒否、画面切替での停止を確認。
- 異常系：音源不在、保存失敗、壊れた保存データの保護を確認。JavaScript例外0件、HTTP通信0件。
- 表示：幅1440 / 780 / 390 pxとCSS zoom 200%を確認。メモ欄のはみ出しを修正し、最終版で枠内に収まることを再試験。
- Clippy：pass。本体の新規警告なし、既存vendor警告あり。
- Rust workspace：初回HTML作成時には完走未確認。RustとFFIのsourceはこのHTML作成で変更していない。後続の全体ゲート結果は`hypha_structural_repair_plan_20260907.md`に記録する。
- 新規sourceの行数と空白：pass。後続の構造修正でReference責務を分離し、リポジトリ全体のsource line budgetもpassした。

ブラウザ操作試験は、出力機器と切り離したfake audio sinkで実施した。
通常のChromeでは、このMacの出力環境で`AUDIO_RENDERER_ERROR`または再生時刻の停止が発生した。
読取り時の既定出力は`DAW_OUTPUT`、96 kHzだったが、その設定を原因と断定していない。
OSと出力機器の設定は変更していない。
したがって、実際のスピーカーからの音出しは未確認である。
音源不在と音声出力の失敗は画面で区別する。

ブラウザ検証資料は`/tmp/hypha-review-browser-evidence-20260907-final`にある。
検証で作った回答は隔離したブラウザと同資料内だけに置き、利用者用ブラウザへ入力していない。
HTMLのSHA-256は`71f00f3872a6f752cda6973ec93c805f4ddacffa6372f4b65bbdf2bbb533f3df`。
最終生成後も、この検証済みHTMLと6音源が同一であることを確認した。

## 申し送り

受領した回答は開発用の人によるフィードバックとして読み込み、SPACEと2MIX ATTACKを別々に改善する。
二人の独立注釈と未使用素材による最終評価、Blind本体接続、WindowsのDAW確認は別の残作業である。
LS：skip。HP：macOS skip、Windows skip。
