# PRE/POST Blind の実装状況

更新日: 2026-09-06
対象: B-719、B-720、B-721
前提: [実装承認記録](hypha_implementation_approval_20260906.md)、[Blind 計画](hypha_pre_post_blind_feasibility_20260906.md)

## 現在の到達点

**PRE/POST Blind は、まだ利用者が DAW で開始できる状態ではない。**
B-718 の同一区間取得部品に、固定 Gain Match の準備、比較コピーの出力、回答と Reveal、中断後の減衰保持、通常復帰の確認、PCM 回収を追加した。
これらを独立試験で検証し、本体の計測後に試聴出力を選ぶ入口を設けた。
本体には開始操作、取得要求、入場許可を発行する処理がまだなく、追加した出力は起動しない。

日本語技術文書の規範に沿い、部品試験、本体への接続、DAW 実機での確認を分けて記録する。
今回 Windows 検証機は操作していない。
インストール、公開リリース、Kirin OS への変更、Notion 書き込みも行っていない。

## 実装した責務

| 実装 | 行うこと | 行わないこと |
| --- | --- | --- |
| `PluginProcessorAudition.cpp` | 正本入力の計測後に試聴出力を選択する。ローカル Blind が出力を所有する callback では Reference を後から重ねない | 開始を許可しない。Reference の OS 権限を外さない |
| `LocalBlindPreparation` | 完了した同世代の native PCM を既存 Gain policy で解析し、固定コピーを準備する。解析後とコピー後にも取消を確認する | PDC や参加範囲を証明しない。自動再生しない。PRE を架空の Work version にしない |
| `LocalBlindTrial` | 1 / 2 の出力、要求番号付きの出力確認、最低試聴量、回答、Reveal、失効と通常復帰を扱う | 音声スレッドで割当抽選、解析、通信、メモリ確保をしない |
| `LocalBlindSlot` | 非 RT 所有者が PCM を公開し、通常復帰の確認後に音声処理との参照競合を避けて回収する | 既存 AnalysisLease の代わりにならない。3 枠目を増設しない |
| `LocalBlindEpochSnapshot` | 許可、pair、capture、clock の世代を一貫して読む。更新途中は許可なしを返す | PID や UI 選択から安全な DAW scope を作らない |

巨大な `PluginProcessor.cpp` の出力責務は B-719 で先に分離した。
行数を 1208 から 1197 へ減らし、同じコミットで既存行数上限を下げた。
B-720 で検証用 source fixture も先に分離し、既存のファイル配置に依存した試験の更新を独立させた。
B-721 の新規 owned source はそれぞれ 500 行以下に収めた。

## 出力と復帰の条件

試聴用 PCM は POST と PRE の同じフレーム数を持ち、試聴中は変更しない。
開始は明示要求だけで行い、準備完了、条件復旧、再生再開から自動で始めない。
各 callback は native sample 位置に対応するコピーを出力する。
範囲末尾をまたぐ callback は全体を拒否し、modulo による別周回の混入や末尾の継ぎ足しを行わない。

ループを許す部品試験では、DAW から得た正確な sample 境界と取得範囲が一致することを要求する。
現在の本体接続はその事実を提供しないため、ループ許可を立てない。
PPQ からの推測や、単に取得長で折り返す実装にはしない。

固定補正で PRE の peak が比較用 ceiling を超える場合、POST 側を下げる候補にする。
承認前は開始できず、承認後は PRE を原音量、POST を固定減衰で再生する。
中断後も realtime の入力には承認済みの減衰を保持する。
通常音量への復帰は別の明示操作で要求し、実際の非空 callback で通常入力を返してから復帰済みとする。
offline と bypass の出力は加工しないが、それだけで保持状態や予約を解除しない。

選択要求と出力済み番号は分離している。
連打で後から届いた要求を、先行 callback が出力済みと誤認しないよう、要求番号と刺激番号を一つの atomic command に格納した。
両側の出力量が指定された最低量に達するまで回答できず、回答後の明示 Reveal まで PRE/POST の対応を返さない。
UI、tooltip、accessibility、他のウインドウからの漏洩防止はまだ接続していない。

## 部品試験の証拠

入力はリポジトリの実ファイル `test_signals/S-1_1kHz_sine_m6dBFS_10s.wav` である。
48 kHz、stereo、IEEE float、10 秒、3,840,068 bytes を確認し、先頭 4 秒を試験に使用した。
mono 試験では同じ左右の片側を使用した。
既知の純 gain 差は検証用コピーだけに加え、原本は変更していない。

| 確認項目 | 結果と限定 |
| --- | --- |
| 0.5 / 1 / 2 倍の既知 gain、mono と stereo | 対応 block は各 37。補正は +6.021 / 0 / -6.021 dB。期待値との差は 0.002 dB 未満 |
| 補正後 PCM | 最大 sample 誤差 0.000023067。同一 PCM の対照はビット一致 |
| 試聴コピーの保持量 | 4 秒 mono は 1,536,000 bytes、stereo は 3,072,000 bytes。取得元や解析領域を含む総 peak RAM ではない |
| 無音、1 秒の短音、疎なイベント | 現行 policy の計測条件を満たさず開始不可。TRACK 対応完了の証拠にはしない |
| 不正な範囲、世代、予算、取消、抽選失敗 | 準備失敗として扱い、未補正や固定割当へ fallback しない |
| 停止、offline、bypass、sample rate 変更、世代変更、seek、範囲超過 | 試聴を失効させ、条件が戻っても自動再開しない |
| 承認済みの減衰 | 中断後も保持し、mono/stereo の変更後にも明示復帰を待つ |
| 通常復帰 | 要求だけ、null buffer、空 callback では完了しない。出力確認後に非 RT 回収する |
| 同時操作 | 100 回の公開と回収、10,000 回の世代更新を含む競合試験 |
| 音声スレッドの確保と破棄 | テストで捕捉した通常の C++ `new/delete` は 0。すべての allocator、OS 動作、実機負荷の証明ではない |

準備試験の全ケースをまとめた Debug 実行は wall time 10.66 秒、maximum resident set size は 31,973,376 bytes だった。
これは複数回の準備とエラー試験を含むプロセス全体の観測値であり、単一 Trial の開始時間や製品の性能合格値ではない。
本体の Release 性能と最大取得長での総メモリ予算は未検証である。

切替方式は callback 境界での切替であり、crossfade を追加していない。
クリックや応答時間が割当の手掛かりにならないという知覚検証は未完了である。
部品試験の最小試聴量は試験入力であり、製品の秒数を新たに決定したものではない。

## 未接続の製品機能

1. **参加範囲と開始排他**：同じ DAW の検証済み participant scope を定め、既存 AnalysisLease の 2 枠と単一 Blind 所有者へ接続する。Reference、Keep、All Keep、下流 Record の準備から finalize までを共通の開始判定に通す。別 process の存在を PID だけでは除外しない。ローカル source の識別、Trial ID、content hash、取得世代、失効条件を製品契約に追加し、承認済みの比較試聴を R-12 に明記する。
2. **同一区間の取得**：exact PRE/POST の双方へ同じ取得世代と開始 barrier を配り、PCM の受け渡しを非 RT で完了する。現在の出力用スロットは取得用スロットではない。
3. **時刻対応**：host の事実から PRE 範囲と POST 範囲の対応を一度だけ確定する。PDC 不明、動的変更、別周回、seek、停止再開、片側欠落は開始不可にする。既知遅延による残差 0 sample と、他トラックとの同期を実機で確認する。
4. **製品の固定 Gain policy**：現行の連続 3 秒条件に入らない短音と疎な TRACK を別途評価する。既存 policy の条件を同名のまま緩めない。強い EQ、limiter、tail、clip 境界も含める。
5. **開始から終了までの画面**：対象選択、取得待ち、音量変更への承認、1 / 2、回答、Reveal、減衰保持、通常復帰を接続する。他方の大きな表示、小さな表示、別ウインドウ、Capture、tooltip、accessibility にも非開示条件を適用する。
6. **所有権と復元**：OS の安全な乱数による割当を単一の準備所有者へ接続する。編集画面を閉じた場合や host の再初期化、instance 削除、worker 停止、旧版との混在、結果保存失敗を扱う。再読込で試聴や承認済み減衰を自動再開しない。
7. **実機と性能**：両 OS、2MIX / TRACK / STEM、mono / stereo、125% / 150% / 200% 表示、重い session、CPU、準備時間、総 peak RAM、切替音を検証する。Windows は他作業の使用終了後に共通 runbook を読んで実施する。

これらは未完了作業であり、後段へ移すという採否変更ではない。
今回の部品実装を「Review 0 件」や完成の証拠にはしない。
既存 UI render 試験の Focus Trail 100% の性能超過も未解決の別項目として残る。

## 検証コマンド

```sh
cmake -S juce_shell -B juce_shell/build \
  -DKIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS=ON \
  -DKIRIN_FFI_LIB=/Users/nishiodaisuke/Dev/kirin_hypha/target/debug/libkirin_hypha_ffi.a
cmake --build juce_shell/build --config Debug --target \
  KirinLocalBlindCaptureTests KirinLocalBlindTrialTests KirinLocalBlindPreparationTests
ctest --test-dir juce_shell/build -C Debug --output-on-failure -R '^kirin_local_blind_'
cargo test -p xtask
cargo test --workspace --lib
cargo clippy --workspace --all-targets --no-deps
cargo fmt --all --check
bash scripts/check_source_line_budget.sh
```

本体の Debug PRE/POST × AU/VST3 と上記 3 テストをビルド対象にした。
3 テストは全件 pass、本体 4 ターゲットの build も pass した。
比較出力部品に対する AddressSanitizer / UndefinedBehaviorSanitizer と ThreadSanitizer の試験も pass した。
Rust の本体単体テストは 1,582 件 pass、9 件 ignored、xtask は 135 件 pass した。
xtask の初回全実行では 8 件失敗したため、B-703/B-705 以後のファイル分離と表示 API の変更を現物と履歴で照合した。
メニューの実ファイル、関数ごとの範囲、producer 所有の meter snapshot を検査するよう更新し、停止操作、exact pair 選択、Keep 準備表示の条件は維持して再実行した。
`cargo clippy --workspace --all-targets --no-deps` は pass し、警告は既存 vendor と既知の build 通知だけだった。
試験更新後の xtask clippy、fmt、ソース行数上限も pass した。
Windows CI には 3 テストの build と実行を登録したが、この作業では CI を起動していない。
FFI の Rust source を変更していないため、今回 parity と pairing_candidates の ignored suite は再実行していない。

LS アップ用: skip。
HP アップ用: macOS skip、Windows skip。
公開リリースは行っておらず、署名済み配布物の準備完了を表すものではない。
