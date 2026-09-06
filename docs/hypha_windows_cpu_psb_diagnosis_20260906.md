# Windows の CPU 負荷と PSB の検証記録

日付: 2026-09-06。
対象: B-723 の責務分離と B-724 の診断。

## 現時点で確認できたこと

Windows に一時配置した Debug 版は、PRE/POST だけを読み込む独立ホストでも高い CPU 使用率を示した。
PC の性能不足だけを原因として扱わない。
一方、PSB の欠落、全画面の描画性能、通常配布版の性能を、この結果だけで同じ原因にまとめない。
PRE/POST Blind は開始 UI と入場許可が未接続であり、完成していない。

## 検証対象と操作範囲

ユーザーの一時差し替え許可を受け、Windows の既存 PRE/POST を両方バックアップした。
バックアップ先は `C:/Users/hello/Dev/vst3_backups/hypha-blind-b722-20260906-174826` である。
`baseline` は全ファイルを照合した複製で、`originals` に旧配置の両 bundle を保持している。
一時配置したのは B-722 の Debug 検証用 VST3 であり、公開リリースではない。

| 配置対象 | bytes | SHA-256 |
| --- | ---: | --- |
| PRE | 60,118,016 | `B7519645528EC788ACB2CA6C2AD0851C3CB2CEA26788339D5EBE39F17ACB22EA` |
| POST | 64,060,416 | `3DD703388DE7BF7EDD3F4513C8717079E701B8BD599453322E3CFD2689D8D067` |

配置先はユーザー用 VST3 ディレクトリである。
Studio Pro がそこから両バイナリを読み込んだことを module 情報で確認した。
ソースのコピー先には Git metadata がないため、画面の source identity を検証済み commit として扱わない。
DAW の曲データ、外部エフェクト、ANEMAN、認証、常設サービスは変更していない。

## Studio Pro の観測

検証機は論理 CPU 8 個である。
Windows のプロセス CPU 時間差を実測経過時間と論理 CPU 数で割り、PC 全体を 100% として表す。
メモリの確認時点では約 15.39 GiB 中に約 4.65 GiB の空きがあった。

| 条件 | Studio Pro の CPU | 限定 |
| --- | ---: | --- |
| 再生中、メイン PRE/POST 有効、LEVEL 表示 | 55.67% | ビルドが終わる時期の観測。音楽位置も他条件と異なる |
| 停止直後、メイン PRE/POST をバイパス | 94.10% | 後続の計測が残り得る。他の Hypha は残っている |
| 同じ停止状態で待機 | 23.68%、再測定 22.63% | POST 300%、PRE 125% の画面を含む |
| 停止状態で Hypha の全画面を閉じた後 | 8.83% | インスタンス自体は削除していない |

上の値は同じ音楽区間の厳密な有効／無効比較ではない。
メイン 2 個をバイパスしても、他インスタンスや非 RT worker は残る。
DAW のパフォーマンス表示が 5% のときにも Windows のプロセス CPU は約 23% だったため、DAW 表示だけで Hypha の負荷を除外しない。

## 独立ホストによる負荷の再現

診断ホストに、実際に配置した PRE/POST を 1 個ずつ読み込んだ。
48 kHz、stereo、512 frames、1 kHz の振幅 0.5 の正弦波を使い、12 秒を実時間で入力した。
画面、Record、Reference、Blind、任意解析は開始しない。
ホストの native message loop を動かし、プラグインの初期化後 timer と IO も動かす。
音声は機器へ出力せず、LOCALAPPDATA はテスト専用の一時ディレクトリへ分離する。

| 段階 | PC 全体を 100% とした CPU | 計測時間 |
| --- | ---: | ---: |
| 空のホスト | 0.033% | 6 秒 |
| 2 個を prepare、callback なし | 1.106% | 6 秒 |
| 再生入力、画面なし | 25.436% | 12 秒 |
| callback 停止直後 | 8.356% | 6 秒 |
| 停止後の次の区間 | 1.528% | 6 秒 |
| 2 個を破棄 | 0.065% | 6 秒 |

再生区間のプロセス CPU 時間は 24.422 秒で、2.035 コア相当だった。
1,125 callbacks の `processBlock` 呼出区間を合計した wall time は 0.500 秒、最大は 1.333 ms だった。
ホストのブロック周期に遅れた回数は 0 だったが、これは計測結果の鮮度や全 allocator の RT 安全性の合格を意味しない。
プロセス全体の CPU 時間は呼出区間の時間を大きく上回るため、主な負荷を音声 callback だけでは説明できない。
native message loop を動かさない最初の補助実験でも再生中 24.965% だったが、IO の起動を含まないため、上の再測定を主な記録とする。

この診断は固定の PRE/POST ペアリング、DAW の document identity、PDC、任意解析の性能を証明しない。
性能の合否閾値は設けず、測定値を出す診断モードとして既存の透明性試験ホストに追加した。
通常の透明性試験とは引数を分け、CPU の値を pass 表示に変換しない。

## 最適化条件を分けた比較

B-724 の同じ C++ source で、Rust の `dev` profile の最適化だけを 0 から 2 に変更した。
debug assertions と overflow checks は維持し、C++ は両方とも未最適化 Debug のままとした。
公開版や Release 配布物は作らず、別の Cargo target directory に診断用 static library を置いた。
最適化前の B-724 bundle は別ディレクトリに複製し、双方の PRE/POST を同じ診断ホストで順番に比較した。

| B-724 の条件 | 再生中 | 停止後の次の区間 | 備考 |
| --- | ---: | ---: | --- |
| Rust opt-level 0、PRE/POST 計 2 個 | 26.071% | 1.138% | 再生 1,125 callbacks、遅れ 0 |
| Rust opt-level 2、PRE/POST 計 2 個 | 5.029% | 1.073% | 再生 1,125 callbacks、遅れ 0 |
| Rust opt-level 2、PRE/POST 計 16 個 | 40.130% | 7.478% | 再生 1,125 callbacks、ホスト周期の遅れ 2 回 |

未最適化 Rust は再生中の負荷を大幅に増やしていた。
ただし、16 個では最適化後も約 40% を使い、停止後にも約 7.5% が残った。
「Debug だから」という説明だけで、複数配置と停止中の負荷を解決済みにしない。
ホスト周期の遅れには OS の scheduling と診断ホストの処理も含まれ、DAW の音切れを直接測った数ではない。

最適化した bundle の通常経路は、PRE/POST の stereo realtime、stereo offline、mono realtime で計 299,680 samples がビット一致し、報告 latency は 0 samples だった。
これは Watch の計測値の鮮度、音声全体の長時間安定性、Record の整合性、GUI、Blind の合格を代替しない。
最適化した B-724 診断版は通常 VST3 配置へ入れていない。

## B-723 と B-724 の変更範囲

B-723 は音声 callback 内で行っていたホスト時刻の読取りを独立ファイルへ先行分離した。
時刻の意味や音声出力を変えず、巨大ファイルの上限を 1197 行から 1144 行へ下げた。

B-724 は Debug 専用の読取り診断を追加した。
直近 callback の時刻情報を事前確保した atomic 値に記録し、明示して開いた情報メニューからだけ確認する。
nominal block size は実際の callback フレーム数と区別する。
ホスト識別情報は短縮 hash で表示し、hash を開始許可に使わない。
presentation latency の 0 は「未通知か既知のゼロかを区別できない値」として残す。
PSB の要求モード、status、has_data、時刻を表示できるが、これだけで PSB の問題を解決したとは扱わない。

## 常時計測の重い計算

Windows で Rust opt-level 2 の部品試験を実行した。
入力はリポジトリの実 WAV `S-1_1kHz_sine_m6dBFS_10s.wav` で、48 kHz、stereo、480,000 frames、peak 0.501187205 を読取り確認した。
下表は初回と最終再測定それぞれ 3 回の中央値で、音声 1 秒を処理する wall time を ms で示す。
デコードと計測器の生成時間は除外し、同じ入力をそれぞれの公開 API に渡した。
DAW の CPU 比率や、下表の包含関係のある行同士をそのまま加算しない。

| 部品または比較実験 | 初回 ms / 音声 1 秒 | 再測定 ms / 音声 1 秒 | 含む処理 |
| --- | ---: | ---: | --- |
| EBU の入力処理、True Peak なし | 1.55 | 1.12 | M/S/I/LRA を有効にした入力処理。明示読取りなし |
| EBU True Peak 単体 | 2.36 | 2.02 | True Peak の入力処理 |
| EBU 全項目の入力処理 | 3.20 | 2.65 | True Peak を含む。明示読取りなし |
| 上記に M を 10 ms ごとに読取り | 6.90 | 5.96 | 400 ms 窓の再走査 |
| 上記に S を 10 ms ごとに読取り | 32.28 | 29.29 | 3 秒窓の再走査 |
| 上記に M/S を 10 ms ごとに読取り | 41.87 | 32.63 | 現行 maxima の読取り頻度に対応 |
| 比較用に M/S を 100 ms ごとに読取り | 7.19 | 5.49 | 診断だけの頻度比較。製品の変更ではない |
| Watch の MeasureEngine | 70.65 | 39.14 | M/S maxima、True Peak、Crest/PSR、100 ms 公開 |
| StereoMeter | 4.44 | 3.20 | 別の True Peak、左右 peak、clip、相関、stereo field |
| MeterSession | 77.28 | 55.08 | 別の MeasureEngine、上の StereoMeter、履歴、summary |
| Watch と MeterSession の併走 | 145.36 | 107.89 | 通常 Watch の 1 インスタンスに対応する主な計測部品 |

初回の Windows source は整形前のコピーだったため、最終整形後に再転送して再ビルドした。
最終測定では両 OS の source SHA-256 が `533d71d21e6eb76d3cd84d1d371526ddd36af8fd95feb32bfb2c4421116868af` で一致した。
整形以外の benchmark 処理と最適化条件は同じだが、wall time には上表の変動があった。
OS の scheduling などの影響を個別に分離していないため、低い側だけを製品性能として採用しない。

**大きいのは True Peak 単体より、M/S の窓を繰り返し読み直す処理である。**
`MeasureEngine::update_loudness_maxima` は 10 ms ごとに M と S を読み、使用中の ebur128 0.1.10 は各読取りで窓内サンプルの二乗和を取り直す。
48 kHz stereo の S は 1 回に 288,000 サンプル分を走査する。
通常 Watch では Watch 本体と MeterSession に MeasureEngine があるため、S の maxima 読取りだけで 1 インスタンスあたり毎秒約 5,760 万サンプル分を再走査する。
追加の 100 ms 公開時の読取りはこの数に含めていない。

2 つの MeasureEngine は保持期間と reset 条件が異なる。
MeterSession はさらに StereoMeter の True Peak を計算するため、48 kHz 通常 Watch では同じ入力に対して True Peak の計算が計 3 系統ある。
ただし、別目的の状態を単純に削除したり、Record 用の基準と交換したりするのは適切ではない。
Phase D はこの Watch 経路では実行条件が false で、ローカル Blind も開始していない。
この再現の主因を SPACE、ATTACK、Blind の実行に帰属させない。

根拠となる source は `engine.rs` の `update_loudness_maxima`、`measure_thread.rs` の `feed_meter_session` と通常 `engine.push_observed`、`meter_session.rs`、`stereo_meter.rs` である。
ebur128 の実装は `energy_in_interval` から `Filter::calc_gating_block` に入り、対象窓を走査する。
ベンチマークは `crates/kirin_measure/examples/watch_cpu_components.rs`、実測ログは `/tmp/hypha-b724-components-opt2.log` と `/tmp/hypha-b724-components-opt2-final.log` に残した。

## 停止中の更新と描画

最初のホストは LOCALAPPDATA だけを分離しており、Watch の TEMP と identity の APPDATA は通常の場所を参照していた。
後続の診断ホストでは TEMP/TMP、APPDATA、LOCALAPPDATA をすべて新しい専用ディレクトリに分離した。
これによって、既存の Watch セルや保存設定の影響を外して再確認した。
初期の試験が残した可能性のある Watch セルは、所有元を特定せず一括削除しない。

完全分離した 2 個の最適化 Debug 版でも、再生中 4.817%、停止後 0.845% だった。
停止後の 6.006 秒には write が 221 回、その他 I/O が 11,217 回あり、プロセス CPU 時間 0.406 秒のうち kernel time が 0.297 秒だった。
空のホストと破棄後はいずれも、この区間の I/O カウンタが 0 だった。
カウンタはプロセス全体で、その他 I/O をすべてファイル探索と断定するものではない。
API の定義は [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes) と [GetProcessIoCounters](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessiocounters) に従った。

source 上、POST は Inactive でも 100 ms tick ごとに最小 JSON を再生成して atomic write する。
ディレクトリ確認、lease の確認、Record 制御の確認も別に続く。
どのファイル API が kernel time を何割使ったかは未計測であり、CPU カウンタだけから断定しない。
ただし、停止中に「計測の計算だけを最適化すれば無負荷になる」という構造ではない。

表示側では `refreshObservatory` が毎 tick `setConnection` を呼び、値が同じでも全体への `repaint()` を要求する。
`setObservatoryFrame` と `setWatchDisplay` も変更判定なしに本文の再描画を要求し、履歴を渡すたびに再集計する。
Studio Pro で全 Hypha 画面を閉じたときの約 23% から約 9% への低下は、この表示負荷を分離して見る必要があることを示す。
ただし、個々の paint 関数の費用や最適化済み C++ の描画性能はまだ計測していない。

完全分離ホストのログは `/tmp/hypha-b724-cpu-io-isolated-opt2.log` である。

## 軽量化の修正順序

1. M/S の 10 ms 観測と maxima の定義を維持し、窓の全再走査を増分集計や再利用へ置き換えられる境界を先に設計する。100 ms への間引きをそのまま製品修正にせず、数値の一致、停止再開、reset、SR 変更、Record を検証する。
2. Watch、MeterSession、左右表示で、共有可能な入力処理と別々に保持する必要がある状態を分ける。保持期間を混ぜずに、ラウドネスと True Peak の重複する計算を整理する。
3. 停止中のスナップショット、pair の生存確認、Record の明示操作への応答を分ける。不変な値の再生成と再公開を削減し、消失検出や再開、旧版との互換性を壊さない。
4. 表示更新を値と領域の変更に結び付ける。静的背景と動的描画を分け、無効化や無音の消去は省略しない。PRE/POST と全ページで 100% から 300%、DPI、複数画面を測る。
5. 配置前の検証に、最適化条件を明示した単体と複数配置の CPU、停止中 CPU、描画時間、計測の鮮度を加える。機能試験が通っただけの未最適化 Debug 版を、体感性能の評価対象にしない。

これは軽さの基準を緩める提案ではなく、保持する計測の定義を変えずに処理を減らす順序である。
このセッションでは製品の計算式や通信周期、GUI の描画方針はまだ変更していない。

## 2MIX と TRACK/STEM の分類案

ユーザーの「2MIX と TRACK である程度、機能を分類した方が良さそう」という意見を受けた設計案であり、この節の追加によって新たな機能制限や計測変更を実装したものではない。
分類は、利用目的に応じた初期表示、素材に適合する解析定義、実際の解析の起動条件の三つに分ける。
TRACK を単なる低精度版にしたり、2MIX を全解析が常時動くモードにしたりしない。

### 現行コードとの差

`HyphaMeterContext.h` は TRACK/STEM の初期スケールを WIDE、2MIX を FOCUS とし、DRUM ATTACK の適用先を TRACK/STEM に限っている。
`PluginProcessorDisplayState.cpp` と `PluginEditorAnalysis.cpp` は、2MIX へ切り替えた際の DRUM 停止とページ退出を行う。
FREQ、PSB、SHARP、LIVE、ATTACK は既に任意解析の実行枠を共有し、HISTORY と RUN はその枠を解放する。
一方、今回重いと確認した Watch と MeterSession の併走を、context 切替で軽い経路に変更する処理はない。
したがって、機能の分類だけを今回の共通負荷の修正と見なさない。

### 推奨する配置と実行条件

| 分類 | TRACK/STEM | 2MIX | 実行上の境界 |
| --- | --- | --- | --- |
| 基本計測 | レベル、ピーク、Crest、処理前後の変化を読みやすく配置 | 同じ精度の基本計測を維持 | LUFS-M、True Peak、Crest、PSR の常時計測契約を勝手に削らない。PSR を Watch の表示に追加しない |
| 長い時間の観測 | 必要なときに HISTORY、RUN へ入れる | LUFS-I/S、LRA、PLR、HISTORY への導線を優先 | 観測期間と準備不足を明示。非表示にすることと収集を止めることを混同せず、既存履歴・Record を欠落させない |
| ATTACK | drum/percussion に DRUM を明示。vocal、pad などへ一般化しない | 独立評価を通った 2MIX 定義を使う | 現行 DRUM を改名して代用しない。2MIX 用はまだ完成扱いにしない |
| 詳細な周波数・知覚解析 | FREQ、PSB、SHARP、LIVE を必要時に選択 | 同じく必要時に選択 | 対応する入力・pair と実行枠を確認し、未使用の解析が裏で残らないことを検証 |
| SPACE | 適合する素材での空間・減衰の観測候補 | 完成ミックス用の区間採用基準を満たした観測候補 | 両用途で自動解析の成立条件を検証。2MIX 専用または TRACK 専用と先に決めない |
| PRE/POST Blind | mono/stereo の track、stem に対応する計画を維持 | 2MIX の比較にも対応する計画を維持 | 拡大 2 枠のうち Blind 所有は最大 1 枠。もう 1 枠の同時 Blind は不可。未接続の開始処理を完成扱いにしない |

基本計測の軽量化は両 context に適用する。
詳細解析を使わない多数の TRACK と、2MIX で詳細解析を開く配置を別々に測る。
基本計測、開いている解析、明示した Record／比較試聴の要求を分け、画面を閉じただけで必要な収集や安全な通常復帰が失われないようにする。

実装前には PRE/POST、pair なし、mono/stereo、context 切替、旧保存状態の復元、再起動を含む実行条件表を確定する。
選択中の解析が新 context で無効になる場合、古い計算と表示を残さず、既存の履歴を別定義で読み替えない。
切替や復元だけで Blind を開始せず、Record の保存範囲も変えない。
この分類案は [SPACE / ATTACK 計画](hypha_space_attack_plan_20260906.md) の利用文脈と解析 profile を分ける方針を引き継ぐ。

## 未完了の確認

- 最適化後も残る計測 worker と IO の費用を、計測部品別に分ける。重複計測、同じデータの再公開、探索の頻度を実測し、計測値と正本 Record を変えずに対処する。
- 停止中の継続再描画と、100% から 300% の DPI 込みの描画負荷。
- PSB を明示選択した状態で、要求モード、PRE の送信、POST の受信、表示条件の一致を確認する。
- 「PSB WARMING」が Active だが has_data なしの状態も隠す点と、実際の欠落原因を区別する。
- 同一条件の実 DAW、複数インスタンス、通常配布用の最適化条件での性能検証。
- Blind の開始排他、同区間取得、PDC、固定 Gain、画面、試聴と通常復帰の実機確認。

公開リリースは製品検証未完了の blocker とする。
LS と HP の両 OS の配布物を ready とせず、検証前の署名やパッケージ作成を始めない。

## 検証ログ

- B-723 の Mac PRE/POST VST3 ビルド: `/tmp/hypha-b723-extraction-rebuild.log`。
- Rust: `/tmp/hypha-b723-rust-tests.log` の 1,582 pass / 9 ignored、`/tmp/hypha-b724-xtask.log` の 136 pass。
- B-724 の clippy: `/tmp/hypha-b724-clippy-final.log`、部品 benchmark 追加後の `/tmp/hypha-b724-clippy-components.log`。vendor の既存警告と既知の build 通知だけで完了。
- Mac PRE/POST AU/VST3 の Debug ビルド: `/tmp/hypha-b724-mac-build.log`。
- Windows PRE/POST の Debug ビルド: `/tmp/hypha-b724-windows-rebuild.log`、ローカル Blind 部品 4 試験: `/tmp/hypha-b724-windows-tests.log`。
- HostContext と ClockProbe の Mac 試験: `/tmp/hypha-b724-probe-test.log`。
- 配置 B-722 の独立ホスト: `/tmp/hypha-b724-cpu-one-pair-pumped.log`。
- 同じ B-724 source の最適化比較: `/tmp/hypha-b724-cpu-optimization-comparison.log`。
- 最適化 B-724 の 16 個: `/tmp/hypha-b724-cpu-eight-pairs-opt2.log`。
- 最適化 B-724 の通常経路: `/tmp/hypha-b724-optimized-debug-transparency.log`。
- 全保存先を分離した最終の通常経路: `/tmp/hypha-b724-isolated-transparency-final.log`。6 条件、299,680 samples がビット一致、latency 0。
- 最適化 Rust と再リンクしたローカル Blind 部品 4 試験: `/tmp/hypha-b724-opt2-contracts-final.log`、4 pass。
- 診断の不在 WAV、範囲外 pair 数、不正な pair 数文字列の拒否: `/tmp/hypha-b724-diagnostic-negative-tests.log`、3 pass。
- 最終 xtask: `/tmp/hypha-b724-xtask-final.log`、136 pass。fmt、diff check、source line budget も pass。

FFI の Rust source は変更していないため、Record/pairing の ignored suite は今回再実行していない。
描画と PSB の実機評価は未解決であり、上記の部品試験を完成や公開の合格として使わない。

## 検証終了時の状態

最後に画面を確認した 18:37 時点で、Studio Pro は停止中、メインの PRE/POST は両方有効へ戻し、Hypha の編集画面はすべて閉じている。
記録画像は `/tmp/hypha-b724-power-restored.png` で、その後 DAW の操作はしていない。
曲は未保存マークを維持しており、保存、終了、変更の破棄をしていない。
通常配置には負荷を確認した B-722 の未最適化 Debug が残っているため、検証機を通常作業向けの性能合格状態とは扱わない。
最適化した B-724 は隔離ステージだけに置き、旧配置のバックアップも残している。

このセッションで作成した一時タスク `HyphaBlindCaptureB723` と `HyphaBlindProbeB723` は、所有スクリプトと非実行状態を照合して登録を解除した。
再作成に使うスクリプトと検証画像・ログは保持し、既存の UI bridge と SSH/RustDesk の常設サービスは止めていない。
解除結果は `/tmp/hypha-b724-task-cleanup.log` に残した。
