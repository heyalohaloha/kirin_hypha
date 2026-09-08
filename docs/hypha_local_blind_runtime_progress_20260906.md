# PRE/POST Blind の実装状況

更新日: 2026-09-08
対象: B-719、B-720、B-721、B-722、B-723、B-724、B-743〜B-747、B-751、B-752
前提: [実装承認記録](hypha_implementation_approval_20260906.md)、[Blind 計画](hypha_pre_post_blind_feasibility_20260906.md)

2026-09-07追記：host固有のparticipant IDをpairingや開始許可の必須条件にしない。
利用者が選んだexact PREをinstance ID、locator、generationで固定し、名前は任意の表示ラベルとして扱う。
以下はB-724までの履歴であり、現在の完成条件は [統合実装計画](hypha_integrated_implementation_plan_20260907.md) を正本とする。

B-743は、通常pairingからローカルBlindへexact pair authorityを渡す境界を追加した。
POSTが明示選択したPREのinstance ID、locator、pair generationを一つのsnapshotとして読み、名前やhost固有IDを含めない。
名前なしPREも選択済みとして保持し、内部解放後のWaitingと利用者による選択解除を分離した。
取得受領のbarrierは、同じpair generation、capture generation、sample rate、channel layout、frames、各native startを要求する。
pair変更と不一致受領は、完了後を含めて要求全体を失効させる。
PREへの要求配信、時刻対応、PCM回収、開始排他、開始UIは未接続である。

B-744は、exact pairに固定した取得要求とPREのarmed応答を、既存解析payloadとは別のslotで定義した。
要求にはpair ownerとcanonical claim、pair／capture／clock generation、PRE／POSTのnative範囲、形式、4秒上限、15秒以下の期限を含める。
PREは自分のlocatorと形式に加えて現在のpair ownershipを照合し、応答は要求全体のSHA-256を返す。
pair解放、期限切れ、旧世代、別PRE、形式違い、範囲overflowでは無言で受理しない。
macOSはatomic file、Windowsはpagefile-backed v4の専用request／armed slotを使い、異なる共有memory layoutの旧版とは接続しない。
このprotocolをJUCEの非RT開始所有者とAudio Thread captureへ接続する処理はまだない。

B-745は、取得要求protocolをRust C ABIとJUCE共通shellの非RT操作へ接続した。
POSTの要求発行、PREのpollとarmed応答、POSTのarmed確認をroleごとに分け、失敗時のC ABI出力は変更しない。
JUCEへ渡すenvelopeはcanonical request ID、exact pair、pair／capture／clock generation、sample rate、mono／stereo、4秒以下の両側native範囲、期限を一体で検証する。
名前なしPREの明示pairを使ったC ABI往復試験では、canonical claim公開後だけ要求が通り、pair解除後は既存armed応答が失効した。
これらのmethodを呼ぶ非RT開始所有者とAudio Thread captureはまだなく、製品の開始操作は有効にしていない。

B-746は、各roleの取得用PCMを非RT所有者から単一Audio Threadへ渡す`ExactRangeCaptureSlot`を追加した。
事前確保したcaptureだけを公開し、Audio Threadは入力を変更せずにコピーする。
取消と完了後の回収では先に公開pointerを外し、Audio Threadのreaderが残る間はstorageを保持する。
このslotは既存2解析枠、PRE／POST間のPCM transport、試聴出力用`LocalBlindSlot`とは別責務であり、まだprocessor本体へ接続していない。

B-747は、requestの期限、実prepare形式、role別native範囲を一つの`LocalBlindCaptureLane`へ固定した。
共通processorは正本計測の完了後、ReferenceやBlindの出力切替前に同laneへA入力を渡す。
position不明、timeline停止、bypass、offlineは取得を失効させ、通常の音声出力は変更しない。
非RT開始所有者がlaneをarmする経路はまだないため、この接続はdormantである。

B-751は、要求protocolとrole別laneの間に`LocalBlindCaptureOwner`を置いた。
PREは現在も有効なexact requestをpollし、実prepare形式でlaneを公開できた後にだけarmed応答を返す。
POSTは同じrequest IDの応答と現在のexact pairを再確認してからlaneを公開する。
各instanceに個別threadを増やさず、準備済みinstanceはplugin module内で一つの低優先度schedulerを共有する。
pair変更、要求消失、期限切れ、応答失敗、形式不一致、Audio Threadでの取得失敗はその要求だけを破棄する。
POSTの要求予約を先に確保するため、protocolへ発行したのにPOST側が所有しない孤立要求を作らない。
この段階では両laneのPCMはrole-localのままであり、PRE PCM transport、両receiptのpair barrier、PDC実証、開始排他、開始UIは未接続である。

B-752は、完了したPRE PCMをrequest IDごとの不変artifactとしてPOSTへ運び、role-local POST PCMとのpair barrierを成立させた。
PRE receiptはrequest digest、pair／capture／clock generation、native範囲、形式、期限、PCM全体のSHA-256を持つ。
POSTは現在のexact pairを再確認し、artifact全体を検証して自分のstorageへコピーした後だけPRE／POST両receiptを受理する。
短いbuffer、欠損、改変、非有限値、旧pair、別generation、別範囲は出力を変更せず拒否する。
POSTの消費応答は同じrequestとPCM hashへ固定し、PREは応答確認とAudio Thread reader退場後にlocal captureとartifactを破棄する。
pair変更はbarrier成立後も保持結果を失効させる。
request／armedのWindows共有memory契約は変更せず、大容量で一回限りのPCMだけを両OS共通の不変fileへ分離した。

## 現在の到達点

**PRE/POST Blind は、まだ利用者が DAW で開始できる状態ではない。**
B-718 の同一区間取得部品に、固定 Gain Match の準備、比較コピーの出力、回答と Reveal、中断後の減衰保持、通常復帰の確認、PCM 回収を追加した。
これらを独立試験で検証し、本体の計測後に試聴出力を選ぶ入口を設けた。
本体の非RT所有者は要求からcapture objectをAudio Threadへ公開できる。
PRE PCMのPOST取込みと両側完了barrierまでは成立した。
一方、PDCを含む時刻対応、開始排他、製品の開始操作はまだないため、試聴出力は起動しない。

B-723 以降は Windows で B-722 検証版を一時配置し、Studio Pro で確認した。
PSB の欠落と高い CPU 使用率の指摘を受け、性能の切り分けを優先している。
独立ホストで現行 Debug 版 PRE/POST だけの高負荷を再現したため、PC の性能不足だけを原因としない。
詳しい条件と未確認事項は [Windows 負荷の検証記録](hypha_windows_cpu_psb_diagnosis_20260906.md) を参照する。

日本語技術文書の規範に沿い、部品試験、本体への接続、DAW 実機での確認を分けて記録する。
B-719〜B-721 では Windows 検証機を操作していない。
B-722 では使用許可を受け、隔離した場所で Windows のビルドと試験を実施した。
B-722 の時点ではインストール、公開リリース、Kirin OS への変更、Notion 書き込みも行っていない。

## B-722 の本体接続と開始待ちの修正

VST3 のホスト識別情報を読む処理を PRE/POST 両方の本体へ接続した。
JUCE の公開された client extension を使い、ホスト名、document ID、active document ID、channel ID をホストから読む。
保存済みの Hypha session UUID、Work identity、PID を代用しない。
AU にはこの VST3 の情報を捏造せず、未取得のままにする。
読取り入口は message thread に限定し、音声 callback ではホストへの問い合わせを行わない。

ホスト変更通知は revision を失効させるだけで、新しい開始許可を発行しない。
未対応、未通知、空文字、壊れた UTF-16、終端なし、buffer 上限に達した切詰めの疑い、非 active document、読取り中の変更は識別不能とする。
ホストが通知 interface を processor の破棄後まで保持した場合にも、破棄済み processor を参照しない。
これらの事実を取得できても、全参加インスタンス、旧版の混在、ルーティング、PDC が証明されたことにはならない。
参照した公式定義は [PreSonus Context Information Interface](https://github.com/fenderdigital/presonus-plugin-extensions/blob/main/ipslcontextinfo.h) である。

試聴出力には、停止中の開始要求が即失効する不具合があった。
明示要求後の **開始待ち** を追加し、停止中の位置移動では通常入力を変更しない。
最初の再生 callback が取得区間の先頭に一致した時点でコピーを出力し、その確認後に試聴中へ移る。
途中の位置からの開始、失効後の自動再開、準備完了だけでの再生は認めない。

音量変更の承認と、変更済みの出力も分離した。
承認を受けても最初のコピーを出す前に失敗した場合、後続 callback の入力を減衰させない。
実際に承認済みの音量で出力した後は、従来どおり中断時の減衰保持と別操作による通常復帰を維持する。

これらは開始 UI、同一区間の取得 barrier、共通の Analysis/Record 入場許可の完成を意味しない。
本体のローカル Blind 出力スロットは引き続き未公開であり、DAW で比較を開始できる状態ではない。
特に、保存済みの Windows Record JSON を新しい順に 60 件調べた範囲では presentation latency の記録を得られず、現在の Studio Pro の時刻対応を確定できなかった。
この結果を「Studio Pro が通知しない」という証明には使わない。

## B-722 の検証区分

Windows では通常の設置場所を変更せず、専用の `validation_staging/hypha_blind_b722_20260906` 配下を使用した。
既存の Windows/Mac の JUCE 差分、別件の未追跡 handoff、常設 SSH/RustDesk、起動中の Kirin OS は変更していない。
確認時に Studio Pro は起動しておらず、この作業では起動していない。

- Mac：PRE/POST × AU/VST3 の Debug ビルドと、取得、試聴、固定 Gain 準備、ホスト情報の 4 試験が pass。
- Windows：PRE/POST VST3 の Debug ビルドと、同じ 4 試験が pass。
- Windows 実 VST3：PRE/POST それぞれの stereo realtime、stereo offline、mono realtime で計 299,680 samples がビット一致、報告 latency は 0 samples。
- ホスト情報と試聴出力：AddressSanitizer/UndefinedBehaviorSanitizer が pass。試聴出力の ThreadSanitizer も pass。
- Rust：`cargo test --workspace --lib` は 1,582 pass、9 ignored。`cargo test -p xtask` は 136 pass。clippy は pass し、既存 vendor 警告と既知の build 通知だけだった。

Windows の実 VST3 試験は検証用ホストによる通常経路の試験であり、Studio Pro の PDC、Blind 操作、音の切替、他トラックとの同期を確認した結果ではない。
FFI の Rust source は変更していないため、Record/pairing の ignored suite は今回再実行していない。
全 UI の描画性能、実 DAW の操作確認、短く疎な TRACK の Gain policy も未完了である。
以前の「インストールを含めない」という操作境界が残っているため、実機確認用の一時差し替えを確認中である。

検証ログ：`/tmp/hypha-b722-windows-verified.log`、`/tmp/hypha-b722-macos-verified-build.log`、`/tmp/hypha-b722-macos-verified-tests.log`、`/tmp/hypha-b722-xtask-final.log`、`/tmp/hypha-b722-rust-tests.log`、`/tmp/hypha-b722-clippy.log`。
LS アップ用と HP アップ用は両 OS とも skip。

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
開始は明示要求だけで行い、準備完了、条件復旧、失効後の再生再開から自動で始めない。
B-722 の開始待ちは、利用者の明示要求後に最初の正しい再生 callback を待つ状態である。
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
2. **同一区間の取得**：B-751で同じ要求envelopeからB-747のrole別laneを非RTでarmし、B-752でPRE PCMをPOSTへ運んで両receiptを一つのpair barrierで照合した。B-754で任意のPRE／POST開始位置を廃止し、POSTのlive host clockから作る単一native範囲を両roleへ配るv2要求へ移行した。試聴出力用スロットと取得用スロットを混同しない。
3. **時刻対応**：B-754で要求発行時のhost positionを将来の取得範囲とともに固定した。各roleは最初のcallbackをその区間内に限定し、以後のclock source、連続sample位置、optional presentation通知を固定する。発行後の巻戻し、late arm、動的変更、別周回、seek、停止callbackは当該要求だけで拒否し、presentation通知をPDC offsetとして足し引きしない。既知遅延による残差 0 sample と、他トラックとの同期は形式別の実機確認が未完了である。
4. **製品の固定 Gain policy**：現行の連続 3 秒条件に入らない短音と疎な TRACK を別途評価する。既存 policy の条件を同名のまま緩めない。強い EQ、limiter、tail、clip 境界も含める。
5. **開始から終了までの画面**：対象選択、取得待ち、音量変更への承認、1 / 2、回答、Reveal、減衰保持、通常復帰を接続する。他方の大きな表示、小さな表示、別ウインドウ、Capture、tooltip、accessibility にも非開示条件を適用する。
6. **所有権と復元**：OS の安全な乱数による割当を単一の準備所有者へ接続する。編集画面を閉じた場合や host の再初期化、instance 削除、worker 停止、旧版との混在、結果保存失敗を扱う。再読込で試聴や承認済み減衰を自動再開しない。
7. **実機と性能**：両 OS、2MIX / TRACK / STEM、mono / stereo、125% / 150% / 200% 表示、重い session、CPU、準備時間、総 peak RAM、切替音を検証する。Windows の使用許可と共通 runbook の確認は B-722 で完了した。検証版の一時差し替えと Studio Pro の操作確認は未実施である。

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

B-724までの前回検証では、本体の Debug PRE/POST × AU/VST3 と上記 3 テストをビルド対象にした。
3 テストは全件 pass、本体 4 ターゲットの build も pass した。
比較出力部品に対する AddressSanitizer / UndefinedBehaviorSanitizer と ThreadSanitizer の試験も pass した。
Rust の本体単体テストは 1,582 件 pass、9 件 ignored、xtask は 135 件 pass した。
xtask の初回全実行では 8 件失敗したため、B-703/B-705 以後のファイル分離と表示 API の変更を現物と履歴で照合した。
メニューの実ファイル、関数ごとの範囲、producer 所有の meter snapshot を検査するよう更新し、停止操作、exact pair 選択、Keep 準備表示の条件は維持して再実行した。
`cargo clippy --workspace --all-targets --no-deps` は pass し、警告は既存 vendor と既知の build 通知だけだった。
試験更新後の xtask clippy、fmt、ソース行数上限も pass した。
Windows CI には 3 テストの build と実行を登録したが、この作業では CI を起動していない。
この前回検証ではFFIのRust sourceを変更していないため、parityとpairing_candidatesのignored suiteは再実行していない。

2026-09-08のB-752/B-753候補では、release source contractを一度だけ実行した。
通常の`kirin_measure`、`kirin_hypha_ffi`前半、native UI／Reference、source contractは通過したが、通常parityのPhase D 1件が並列負荷下で失敗した。
製品コードは各audio callbackでActiveを再通知する一方、試験は開始時の一度しか通知せず、3秒のwatchdog失効後にPhase Dを消去し得る差だった。
B-753で試験駆動を出荷JUCE callbackと同じにし、Record遷移を空ringで観測してから音声を投入するよう修正した。
修正後は通常parity 16件、残りのRT handoff 1件、性能2件、xtask 138件、実測inventory 20件／6件のignored suite、Clippyがすべてpassした。
新規local Blind C ABI 5本をstatic archiveの定義symbol gateにも追加し、既存3本と合わせて全8本を確認した。
利用者指定に従ってrelease source contract全体はローカル再実行せず、単一コマンドとしての最終passはCIで確認する。
最初のCIはparity sourceの行数ratchetを検出したため、追加コードを保ったまま同じ範囲の説明を整理してbaseline 3007行へ戻した。
次のCIはRust 1.98の`chunks_exact_to_as_chunks`を検出したため、長さとhash確認後のPCM decodeを固定4byte arrayの走査へ置き換えた。

LS アップ用: skip。
HP アップ用: macOS skip、Windows skip。
公開リリースは行っておらず、署名済み配布物の準備完了を表すものではない。
