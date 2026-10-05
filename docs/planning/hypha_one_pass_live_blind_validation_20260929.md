# One-pass Live Blind / END — ローカル検証記録

日付: 2026-09-29。基点 `ca50b04f`、先行抽出 `ad58619e` / B-1099。
macOS x86_64 / JUCE Debug。実機DAW・Windows検証機には接続していない。

## 実装した契約

- 直接BLINDは3秒以上の対応を確認した入力から固定MATCHを自動準備する。有効なMATCHがあれば再解析せず再利用する。
- matched判定はprocessor所有とし、gain適用のRT receiptとpair/format/run/連続性の世代へ結び付ける。古い計測・承認結果は拒否する。
- Source 1/2は同じ連続再生中に切替可能。交差fadeだけ、遅い旧要求のreceipt、安全退避POSTは聴取に数えない。
- 4回答を維持。実出力が両側とも確認された場合だけ回答・Revealを許可し、割当を出力PCMの符号と照合する。
- ENDは匿名要求を失効→PREを5 msで退避→POSTを通常音量へramp→同じ世代の実unity確認、の順。追加RETURNは不要。
- 音量上昇を押下前に表示。close/hideと故障だけなら減衰を保持し、RETURNが明示復帰。無callback/offline/bypass/他ownerは完了receiptに使わない。
- 比較出力前の計測・Recordを維持。ordinary/offline出力はfixtureの入力とbit比較。DAWのsolo/mute/fader/routingは操作しない。

## ローカル試験

音声関連とeditor surfaceのCTestは24件すべてpass（340.08秒）。試験数は実行したCTest一覧から取得した。

| 試験 | 結果 | 内容 |
| --- | --- | --- |
| Live Blind session / completion | pass | 両割当、旧epoch/sequence、両側gate、fault、END、再入場、CSPRNG失敗、未復帰receipt拒否 |
| 実processorの直接 / MATCH再利用 / PDC fault / 減衰承認 | pass | disposable storage、S-1加工fixture、連続1回再生、実PCM、FFI排他、8 dB保持と復帰、offline不変 |
| Exact 4 S / PIN / AAX mono-group回帰 | pass | 実processorと共通editor、mono/stereoとwrapper条件。実DAWのPDC証跡ではない |
| editor surface | pass | 実editorの再表示、Reference／VU／通常domainの切替、Record表示、保存復元、48 surface cases |
| Blind UI | pass | 5サイズ×英日×9状態=90。文字切れ、操作重複、匿名title、回答gate、復帰待ち |
| END / RETURN表示 | pass | 英日×全5サイズ×active/held=20。小サイズでも最大24 dBの上昇量を省略しない。表示・accessibilityを照合 |
| 全体UI描画 | **fail** | FREQ 900×600/DPI 2: 18.1043 ms、単独再実行18.6429 ms、基準12 ms未満。該当描画source/thresholdは今回未変更 |
| Rust workspace | pass | `cargo test --workspace -- --test-threads=1`: 2,165 passed / 0 failed / 41 ignored。並行実行で時間制限に達した既存3件も、build並行を止めた全suite再実行でpass |
| Clippy | pass | `cargo clippy --workspace --all-targets -- -D warnings`。vendor nih_plugの既知警告は監査対象外 |
| source契約 / 行数 / 画面翻訳 | pass | RT境界、source契約、500行ratchet、screen text、cargo fmt、whitespace |
| Live renderer RT new/delete | pass | 記名／匿名のPRE転送・POST描画、正常・gap・guardでnew=0、delete=0。旧callbackの退役確認前にrendererを再確保しない |

数値照合: 再利用MATCHは−6.0206 dBに対して誤差0.002 dB未満、匿名Sourceの出力振幅比は±0.5に対して誤差0.0001未満。
48 kHzの終了rampはgain 0.5→1に12,001 frames（約250 ms）、PRE／POST切替は240 frames（5 ms）。
8 dBの承認減衰をclose後に保持し、callback停止中とoffline中には終了せず、realtime再開でunityへ到達することを検証した。

## 未実施・残るgate

- 30分のCPU/RSS負荷matrix（48/96/192 kHz、64/256/2048 frames）と実DAWのUndo履歴は未検証。通常source契約のpassを負荷実測の代用にしない。
- `kirin_hypha_ffi`のソース／ABIは未変更。ignored parity / pairing_candidatesは今回は実行していない（通常workspaceのignoredへ含まれる）。
- 新しいフローのmacOS VST3 / AU / AAX、Windows VST3 / AAX実機matrixは未実施。旧commitの結果を流用しない。
- 聴取での操作感、DAW stop/seek/loop/睡眠、PDC変更直後、最大減衰からの復帰は実機で確認が必要。
- 全体UI性能が未達のため、全試験green・公開準備完了とはしない。FREQ性能の閾値緩和や無関係な製品変更はしていない。
- release build/install/notarize/upload未実施。LS skip、HP macOS skip、Windows skip。

## 再実行入口

`juce_shell/build` は既存Debug構成を再生成して使用。`KirinLiveBlindProductTests` に通常、`--reuse`、`--fault`、`--approval`を用意した。
音声fixtureは `test_signals/S-1_1kHz_sine_m6dBFS_10s.wav`。`ValidationStorageSandbox`で実データ・実機から隔離。
関連CTestは `kirin_live_blind|kirin_live_compare|kirin_local_blind|kirin_editor_surface_product`。
UIは `KirinUiRenderContractTests --product-entry-only`、全体gateは引数なし。
ログは `/tmp/hypha-one-pass-*.log`、目視用PNGは `/tmp/hypha-live-blind-preview.A1r4sk/`（一時物、永続証跡ではない）。
