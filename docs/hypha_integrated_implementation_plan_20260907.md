**Hyphaのレビュー修正と完成へ進む実装計画**

**2026-09-07 / B-733後の改訂**

Reference修正・RG・AAX混入検査・初回回答取込みの現行計画は、[構造修正計画](hypha_structural_repair_plan_20260907.md)とする。
B-731〜733のレビューで再現したP1 4件・P2 1件は、構造修正計画のS0〜S4として実装済み。
HTML回答は6 / 6受領・検証済み。補足後のSPACEは3件とも「追える減衰なし」、ATTACKは非網羅の代表40点。
初回回答待ちは解消し、SPACE素材の偏りを是正する6件の追加判定用HTMLを作成した。
以下はB-730時点の初版計画であり、当時の未着手・回答待ち記述は現在地を示さない。
ローカルBlind、軽量化、製品評価、両OSと配布3チャネルの未変更範囲は引き続き参照する。

2026-09-07に競合状況資料をB-738のコードと現行の一次情報へ照合した。
競合資料は市場判断の入力であり、実装済み・未実装を決める正本にはしない。
この照合によって、現在の完成順序は変更しない。

- 現行BlindはPreference Listening Trialである。選択、Reveal、割当commitment、Audio callbackの
  実出力receiptは保存するが、正解を持つABX識別試験ではない。正答数やp値を現行Trialへ追加しない。
  反復回数や選択分布を示す場合も記述統計に限定し、ABXは目的、対照、停止規則を別に定義してから
  独立したListening Protocolとして判断する。
- サラウンドはチャンネルmapだけの追加ではない。現行JUCE bus、Measure Thread、PRE / POST交換、
  SPACE、心理音響、Reference、ローカルBlindがmono / stereo契約を持つため、現在のstereo完成工程へ
  混ぜない。対応市場へ進む判断後に、新しい製品範囲と不変条件から計画する。
- AAX Phase AはSDK非依存準備まで完了した。実SDK、macOS / Windows build、Pro Tools、category、
  PACE署名と配布が揃うまでAAX対応済みとは表示しない。
- 公開CPU値はAudio Thread、任意解析worker、描画、host全体を分け、同一候補・同一hostの測定条件を
  併記する。任意解析workerの構成予算を`process()`単体の値として公開しない。
- 「唯一」「前例なし」「権利問題ゼロ」などの絶対表現は、公開時点の一次情報と適用範囲を固定できる
  場合だけ使う。Hypha自身の検証済み挙動を主語にし、競合に存在しないことを完成条件にしない。

この整理を今後へ強制するため、`AGENTS.md`の旧Watch表、最小GUI、U-1〜U-8、固定テスト件数を
現行正本への参照へ置き換えた。

作成日：2026-09-07。
基準：B-730、`eaeee6a4a2572e255e1a26a0a63eabab1ad16487`と、計画作成時に確認した未コミット差分。
今回の依頼は実装計画の作成であり、添付文書にある実装依頼や過去の操作許可を、このセッションで実装、検証機操作、署名、公開を開始する指示としては扱わない。
本書は機能の完成記録ではない。

**Referenceの5件を修正してから、同じ試聴基盤を使うローカルPRE/POST Blindを接続する。**
その間のホスト検証待ちと回答待ちには、軽量化、SPACEとATTACKの評価準備、AAX Phase Aを進める。
作成済みの判定用HTMLを作り直す工程は置かない。
SPACEとATTACKの回答待ちを、他の実装の開始条件にはしない。

**1. 文書と現物を照合した現在地**

| 入力 | 確認した内容 | 計画への反映 |
| --- | --- | --- |
| [B-730レビュー](/Users/nishiodaisuke/Downloads/Hypha_review_B730_20260907/review.md) | 直近45コミットを確認。P1が4件、P2が1件。無変更の製品コードによる挙動再現3件、割込み順を固定した再現1件、静的な競合確認1件 | Reference修正を最初の製品変更にする。修正後は5件を個別に閉じる |
| [既存完成計画](/Users/nishiodaisuke/Dev/kirin_hypha/docs/hypha_completion_plan_20260907.md) | C0からC5の完成順序。Blind、軽量化、SPACE、2MIX ATTACK、両OS、配布3チャネルに未完了条件がある | 機能範囲と数値条件を維持し、C1の前にReference修正を挿入する |
| [AAX Phase A引継ぎ](/Users/nishiodaisuke/Downloads/hypha_aax_phase_a_handoff.md) | B-701時点のSDK非依存準備。SDKとPACEの入手は文書時点で未完了 | A-1からA-5を準備工程として組み込む。SDK実ビルドとPro Tools試験はPhase Aの完了に含めない |
| [作成済みHTML](/Users/nishiodaisuke/Downloads/Hypha_Listening_Review_01_20260907/index.html) | `hypha-pilot-20260907-01`、SPACE 3件とATTACK 3件。manifest記載のHTML hashと実ファイルが一致し、音声6ファイルが存在する | C0のHTML作成は完了扱い。回答JSONの取込みから再開する。今回ブラウザで再生試験をやり直したわけではない |
| 現在の作業ツリー | Referenceのプリセット連携、Gain Match、プレビュー、Spectrumなどに未コミット変更がある | 既存差分の所有範囲を確定してから統合する。B-730のレビュー結果を、そのまま未コミット版の再試験結果とはしない |

HTMLの回答形式は`hypha.review.answers.v1`で、pack ID、manifest hash、protocol、判定者、候補閲覧の有無、整数sampleの印を持つ。
SPACEの区間とATTACKの時点を記録できるため、解析側でこの出力を受け取る。
回答の提出済み件数は今回確認していない。
6件の初回パックを、最終精度評価の標本数として数えない。

ReferenceAudioPagesとReferenceRuntimeV2Realtimeは、計画作成時点でもB-730と同じだった。
ControllerとSelectionには未コミット変更があるが、指摘した共有sourceへのアクセスとBlindのclearに相当する箇所は残っている。
この観察は現行差分での再現試験を代替しない。

CIは2026-09-07にGitHubから読取り確認した。
[PR #17](https://github.com/heyalohaloha/kirin_hypha/pull/17)のheadはB-696の`f23db958`で、release source contractとWindows VST3 preflightがfailure、public historyとauvalがsuccessだった。
B-730のcommit指定によるrun照会は0件だった。
AAX文書の古いCI状況を現在の候補の合否として引き継がず、実装対象の同一コミットで再検証する。

**2. 実装順序と依存関係**

| 工程 | 作業 | 開始条件 | 完了して次へ渡すもの |
| --- | --- | --- | --- |
| G0 | 対象ソースの固定、既存差分の整理、必要な責務抽出、基準試験 | なし | 再現可能な候補、差分の所属表、基準ログ |
| R1 | AudioPagesの所有権と追出しを一括修正 | G0 | ページ競合と6秒地点の失敗を閉じた音声ページ実装 |
| R2 | workerとUIの公開状態を分離 | G0の抽出後。統合順はR1の後 | source、比較モード、承認key、世代が整合する公開snapshot |
| R3 | Blind中断後の減衰保持と通常復帰を分離 | R2 | binding失効に耐える保持状態とRT出力確認 |
| R4 | Cue境界をまたぐ通常Bの出力を修正 | R1、R2 | loop、非loop、短いCueで範囲外PCMを出さない出力 |
| RG | 5件の回帰試験とReference全体の再検証 | R1からR4 | 5件ごとの合否、RT安全性、既存Referenceの回帰結果 |
| B1 | ローカルBlindの参加範囲とPDCを実ホストで実証 | RG | 対応形式ごとのhost事実と既知遅延の残差 |
| B2 | ローカルBlindの取得、開始排他、操作一巡を接続 | B1の成立条件 | 開始から通常復帰まで使える製品経路 |
| U | DRUM、Spectrum、Focus Trail、PSB、停止負荷を収束 | G0。共有ファイルの変更を順番に統合 | 既存予算と全画面の回帰表を満たす候補 |
| M | HTML回答の取込み、SPACEとATTACKを別々に評価して接続 | 評価器はG0後。定義確定は各対象の回答が必要 | 定義ID、固定した候補、独立評価、PRE/POST実出力 |
| X | AAX Phase Aの準備 | RG後の候補について既存CIがgreen | 既定OFFのCMake、混入検査、識別子、skip可能なCI骨格 |
| V | 同じ完成候補でmacOSとWindowsの製品検証 | RG、B2、U、M、Xの既存形式回帰 | 実機結果、性能、全画面、音付き動画、未解決一覧 |
| D | 配布3チャネルの準備 | Vの合格 | 同版のLS PKG、HP macOS ZIP、Windows署名installer |

U、M、XはB1や人の回答を待つ間にも進められる。
これは複数エージェントの起動指示ではなく、作業を切り替えられる依存関係を示している。
同じController、CMake、CIを別々の作業から同時に書き換えない。
既知の安全性不具合を残したままB2の開始機能を有効にしない。

G0とRGで修正の規模を測り、B1でホストごとの成立条件を確認した後に残りの所要時間を見積もる。
SDK到着日、注釈の提出日、未実証のPDCを仮定して完成日を置かない。

**3. G0で固定するソースと基準**

既存の未コミット変更を、Reference連携、Spectrumと共通描画、研究用HTML、別件文書に分類する。
差分、未追跡ファイル、JUCE patch状態を保存し、実装に取り込む変更と、別作業で保持する変更を記録する。
作業中の差分を一括commitしたり、元の作業ツリーをresetしたりしない。
必要なら`codex/`配下の作業ブランチと独立checkoutを使い、取り込んだ変更の内容を記録する。

現在のReferenceRuntimeV2Controllerは574行、ReferenceRuntimeV2Selectionは618行で、両方とも行数baselineに登録がない。
新しい例外枠を足すのではなく、追加済みのプリセット選択とworkspace解決の責務を500行以下のモジュールへ先に抽出する。
抽出は挙動変更と別の先行commitにし、R2の共有状態整理で再び混ぜない。
既存巨大ファイルの別責務を、今回の前提として一括分割することはしない。

前回の再現コードを、共有の一時ディレクトリに依存しない試験fixtureへ移す。
ページ競合はテスト用の制御点で割込み順を固定し、製品のRT経路に待機処理を入れない。
試験fixtureはリポジトリのテスト信号または生成した既知PCMを使い、私用音源をcommitしない。

基準試験では、製品不具合、古いソース文字列契約、実行環境の問題を記録上でも分ける。
前回の`repo_dist_rejects_unsigned_smoke`は`/tmp`配下のcheckoutに依存して失敗したため、通常のrepo配置と同じ前提のcheckoutを使う。
guardを緩めて試験だけをgreenにしない。
CIの旧失敗箇所を再現できない場合も、候補hashと実行結果を残して閉じる。

**4. Referenceの5件を閉じる変更単位**

| 指摘 | 原因と再現 | 設計する変更 | 必須の回帰試験 |
| --- | --- | --- | --- |
| F1 / P1 | startとgenerationの確認後、stateのCASまでにslotが再利用される。再現ではpageOffsetが−336,000 | R1でslotの所有権を先に取得し、保持中にsource世代とページidentityを検証する。close、retire、reopenも同じ所有規則に揃える | メタデータ確認と再利用の交差、close/reopen、source交換、sample rate変更。別identityを受理せず、取得失敗時にAを部分的に書き換えない |
| F2 / P1 | cache満杯時に先頭のready slotを使い、nextの先読みがcurrentを追い出す | R1でcurrentと前後の必要ページを先に求め、保持集合の外だけを追い出す。inUseはworkerが再利用しない | 10秒sourceの0から8秒、cache一巡以上の連続再生、往復seek、ページ境界、worker遅延と回復。必要ページの自己追出しが0件 |
| F4 / P1 | workerがactiveSourceを書き、UIだけがlock内で読む | R2でactiveSourceをworker専有にする。UIにはsource参照、比較モード、承認key、世代を一緒に公開し、変更要求は同じ世代で検証する | プリセット変更、source再読込み、B選択、sample rate承認、resetの競合。古い承認を別sourceに適用しない。対象経路のTSan |
| F3 / P1 | binding失効後のblind.clearが中断後の減衰保持も消す。入力0.5が0.249988から0.5へ戻る | R3で準備済みPCMの寿命と、実際に出力した減衰を保持する状態を分ける。sourceやbindingの失効は新規試聴を止め、保持状態は許可された通常復帰まで維持する | binding削除、lease失効、workspace交換、worker再起動、UI閉鎖。承認のみで未出力なら減衰しない。出力済みなら保持し、非空callbackの通常出力確認後に予約を解放 |
| F5 / P2 | callback先頭だけCueを写像し、末尾までfile PCMをコピーする。Cue [100,1000)のsample 999から8 framesで範囲外を出力 | R4でCue終端に沿って出力区間を分割する。loop時は同じCueへ戻す。必要ページを全て確保してからbufferへ書く | Cue終端の直前、境界ぴったり、跨ぎ、非loop、file終端、1 frameを含む短いCue、同callback内の複数周回。連続PCM参照とのsample比較 |

R1はF1とF2を一つの所有モデルで直す。
slot単位の確認だけを増やして、追出しや解放を別契約のまま残さない。
必要ページを確保できないcallbackは、全体をAで維持する。
有効なloopではCue外のPCMを出さず、非loopで終端を跨ぐcallbackは全体をAへ戻す方式を基本とし、既存の出力通知も同じ結果に揃える。

R2の公開snapshotのcopyと破棄は非RTで行う。
stateLockをdecodeやファイルI/Oの間まで保持せず、Audio Threadへshared_ptrの参照更新や破棄を持ち込まない。
RTへ渡すgain、mapping、選択世代は一貫した状態として読む。
未コミットのプリセット連携と承認keyも、この公開境界へ統合する。

R3はReference BlindとローカルBlindの両方で使う復帰条件に揃える。
試聴の準備状態、最後に実出力した音量状態、Recordと他試聴の予約状態を別々に記録し、出力確認で結び付ける。
offlineでは通常Aを変更せず、再読込みで試聴を自動開始しない。
保持中にUIが閉じた場合も、復帰操作へ戻れる既存画面の状態を維持する。

変更対象は`juce_shell/src/reference_audition/`のAudioPages、Controller、Lifecycle、Selection、Realtime、Blind、BlindRealtime、BlindStateと対応header、`juce_shell/tests/reference_*`、CMakeの試験登録である。
このうち巨大化したControllerとSelectionの抽出先は、G0で確定する。
試験用制御点や補助実装も新規owned sourceとして500行制限に含める。

RGではASan/UBSan、競合を制御した再現、対象経路のTSan、RT確保と解放の検出を組み合わせる。
サニタイザだけをRT性能の証拠にはせず、Releaseでcallbackの最大値とP99も測る。
既存Reference選択、権限、プリセット準備、Gain Match、回答とReveal、保存失敗の通知を再確認する。

**5. ローカルPRE/POST Blindの本体接続**

B1では、まず出力を切り替えない検証で、participant scope、exact PRE owner、host clock、PDCの情報が取れるかを確かめる。
Windows Studio Proの既存検証曲を使い、macOS VST3とAUも形式ごとに結果を残す。
既知遅延を挿入し、PREとPOSTの共通区間に対応付けた後の残差が0 sampleであることと、他トラックとの同期を確認する。
PID、保存済みUUID、PPQの推測だけで対応済みにしない。
成立しない形式は具体的な不足を報告し、対応範囲の削減を勝手に決めない。

B2は次の順に既存部品へ接続する。

1. 非RTの開始所有者にAnalysisLease、単一試聴所有権、Keep / All KeepとRecordの排他を集約する。
   PREはPOSTの取得元として従属させ、既存2解析枠のうちBlindは1枠だけを使う。
   予約の取得順序を固定し、失敗時はその要求が新たに取得した予約だけを返す。
2. PREとPOSTへ同じ取得世代と開始barrierを配り、4秒の不変コピーを非RTで準備する。
   seek、別周回、PDC変更、片側欠落、旧世代、旧版混在は受理しない。
3. 固定Gain Matchを接続する。
   mono/stereoと2MIX、TRACK/STEMを含め、連続3秒条件を満たさない短音と疎な音は別policyとして検証し、既存policyを同名のまま緩めない。
4. 明示開始、開始待ち、1 / 2切替、実出力確認、回答、Reveal、中断、減衰保持、通常復帰を一巡させる。
   出力していない側には回答できず、条件回復や復元だけで再開しない。
5. PRE、別の解析枠、tooltip、accessibility、Capture、clipboard、更新案内から回答前の対応が漏れないことを確認する。
   同一PCM対照、純gain差、加工差で、クリックや応答時間が割当の手掛かりになるかを検証する。

主な対象は`juce_shell/src/local_blind/`、PluginProcessorAudition、processor/editorの開始操作、host contextとclock、`crates/kirin_measure/src/analysis_lease.rs`、Record lifecycle、必要なFFIである。
現行のLocalBlindSlotは試聴出力用なので、同一区間の取得要求と混同しない。
製品契約とR-12に承認済みローカル比較コピーの境界を明記してから、開始操作を有効にする。
登録Referenceの権限と、Hypha単体で使えるローカルBlindの権限は維持する。

**6. 作成済みHTMLの回答からSPACEとATTACKへ接続する**

回答取込みは、pack ID、manifest hash、protocol、音声hash、sample rate、区間、候補閲覧履歴を検証する。
未回答、判断不能、再生不可、該当なしを別々に集計し、途中提出を受け入れる。
原本の回答は私用フォルダに保持し、別版の設問へ回答を黙って移さない。
HTMLの印が表す終端と、計算器の半開区間の境界を明文化し、区間変換に1 sampleのずれを作らない。

| 対象 | 回答前に進めること | 回答後に進めること | 合格条件 |
| --- | --- | --- | --- |
| SPACE | 既存固定区間計算器、研究用probe、表示の定義を照合。独立参照と欠測理由の試験。1,775候補の棄却理由を集計 | 人の区間で計算器を検証し、自動区間選択との違いを分析。PRE/POSTで共通の起点と採用区間を使う | EARLYとD20の定義を固定。初期案の独立参照差0.01 dB、既知直線の傾き相対誤差1e-6を試験前に確定。採用Precision 0.95、Coverage 0.70は提案値として検討し、共通区間の条件を決めてから最終評価 |
| 2MIX ATTACK | 注釈を候補から独立して読み込む評価器、曲単位の分割、時刻差の集計。既存DRUMとのprofile分離 | 提示区間内の全イベントを照合。二人の独立注釈、曖昧な境界の扱い、許容幅を固定して検出器を評価 | Precision ≥0.85、Recall ≥0.75、F1 ≥0.80、時刻誤差P95 ≤15 ms、FP ≤1回/秒、signed median絶対値 ≤48 kHzの1 hop。注釈間F1 ≥0.90 |

SPACEの広帯域EARLYと20 dB相当減衰時間という範囲を維持する。
奥行きやRT60へ言い換えず、D20が0件だったことを理由に採用閾値を下げない。
ATTACKの候補の過分割をSPACEへそのまま渡さず、人の指定区間との比較で原因を切り分ける。
2MIX ATTACKをTRACK/STEMのDRUMで代用しない。

初回6件は開発用の回答として使い、続く評価は既存の開発32本と評価予約32本の台帳に沿う。
予約素材の過去の使用履歴を確認し、SPACE開発でATTACKの評価曲を開くことも避ける。
候補hash、定義ID、係数、評価器、分割、曖昧な注釈の扱いを固定してから、未使用の評価素材を開く。
二人目の注釈者を確保できない場合は該当する評価のみ未完了とし、他工程を継続する。

主な対象は`crates/kirin_measure/examples/mix_space_probe/`、`space_decay_probe/`、測定計算器、attack_runtime、PRE/POST exchange、`crates/kirin_hypha_ffi/`、PluginProcessorAnalysis、PluginEditorAnalysis、HyphaSpacePainter、HyphaAttack、ObservationとCaptureの契約である。
既存の計算器を正本へ接続し、同じ名前の別実装を新設しない。
不合格の検出器は製品で有効にせず、改善対象と必要な判断を記録する。

**7. 軽量化と全画面の回帰**

| 対象 | 修正対象 | 合格に使う条件 |
| --- | --- | --- |
| DRUM | 上部包絡と時間軸、下部四量の膜、停止と無音、実音との同期 | 更新中央値は1枠12 ms、2枠16 ms以内。最大24 ms、初回80 ms以内。macOSの119/120条件合格で残った1件も閉じる |
| Spectrum / Focus Trail | 共通の描画とhit testの幾何、履歴、POST単体、snapshot比較、再描画範囲 | 既存4.5 ms予算の超過を解消。hoverとclickが表示位置に一致し、履歴が途切れない |
| PSB | PRE/POSTの非ゼロ差分、20帯域、遮蔽、停止時消去、再起動 | 同一入力、既知の加工差、無音、停止、復帰で値と表示を確認 |
| 計測と通信 | 停止中、待機枠、非表示インスタンスの残余処理 | 正本の窓と精度、heartbeat、pair喪失検出、Recordを維持し、不要な解析と再描画を止める |
| 共通UI | Typography、全サイズ、ABS / POST / Δ、HOLD、RUN、HISTORY、Keep、LEVEL、SHARPNESS、LIVE | 100 / 125 / 150 / 200%で既存完成計画の全指摘に根拠付きの結果を残す |

試験条件は1枠、2枠、3番目の待機、複数の非表示インスタンス、192 kHzを含む対応sample rate、可変block、10分以上の連続再生、停止と復帰とする。
Audio Thread、計測worker、通信、描画を分け、同じRelease条件と入力で前後を比較する。
部品の削減率を製品全体の削減率として報告しない。
新規workerとコピーの最大bytes、準備時間、queue、drop、callback最大/P99、CPUの予算は、利用者向け最大構成を決めて測定前に固定する。
通常Audio Thread単体0.1%未満の目標は、CPU換算の分母と測定方法も記録する。

主な対象は既存未コミットのHyphaSpectrum系、HyphaObservationEquality、Observatory、HyphaTypography、HyphaThemeと対応native試験である。
既に変更済みという理由で合格にせず、統合候補で確認する。

**8. AAX Phase Aの具体化**

AAX文書にあるAvid回答は、依頼者から提供された設計前提として扱う。
今回その原本やSDKの入手状況を再確認したものではない。
SDKを公開リポジトリへ含めず、ローカルの外部パスから供給する構成を採る。
引継ぎ文書のアカウント情報や私信を公開用の設定へ転記しない。

固定JUCEの[CMake実装](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/JUCE/extras/Build/CMake/JUCEUtils.cmake:2255)では、`juce_set_aax_sdk_path()`はSDKの`Interfaces`と`Interfaces/ACF`を確認する。
呼出し位置はJUCEの追加後、`juce_add_plugin()`の前とする。
SDKが実在することと、対応する完全なSDKをビルドに使えることは別に検証する。

| 単位 | 実装計画 | 完了条件 |
| --- | --- | --- |
| A-1 / A-2 | CMakeの小さな専用moduleに`KIRIN_HYPHA_AAX_SDK_PATH`、`KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED`、`KIRIN_HYPHA_REQUIRE_AAX`をまとめる。外部パスを検査し、macOS/Windowsで明示供給された場合だけFORMATSへ追加 | 下記の設定試験がpass。SDK未指定でAU/VST3の設定と既存出力を変えない |
| A-3 | `scripts/check_aax_sdk_absence.mjs`と専用試験を追加。追跡ファイルと公開対象へのSDK混入を暫定パターンで検出。独立CI jobはSDKの有無に関係なく実行 | 正常repoはpass。SDKを模した名前と内容のfixtureを一時git repoへ強制追加するとfail。SDK実物による最終パターン検証は未完了として残す |
| A-4 | PRE/POSTのidentifierは既存BUNDLE_IDを使い、manufacturer `Kirn`とproduct `Khpr` / `Khpo`を維持。カテゴリ候補は`ePlugInCategory_None` | PRE/POSTの識別が一意。SDK入手後にカテゴリを最終確認。AU/VST3の既存識別子を変えない |
| A-5 | 手動実行用のAAX workflow骨格を用意。既存の公開runnerではSDKを取得しない。将来のSDK配備済み実行環境だけを対象にする | 未配備時はjobをqueueする前にskip。配備を明示した環境で設定が壊れていればfail。通常のPRと既存配布CIを停止させない |

カテゴリは、固定JUCEの[変換テーブル](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/JUCE/extras/Build/CMake/JUCEUtils.cmake:1685)にAnalyzerがないことを確認した。
この版のCMakeが変換する名前は`ePlugInCategory_*`であり、文書中の`AAX_ePlugInCategory_*`をそのまま設定値にしない。
`None`はこの非synthの既定値でもあるため、SDKがない段階の候補とする。
AAX用の明示設定はAAX有効時だけに限定し、既存形式へ不要な差分を持ち込まない。

設定試験は次の組合せを対象にする。

| SDKパスと設定 | 期待 |
| --- | --- |
| 未指定、REQUIRE=OFF | configure成功。AAX targetなし |
| 指定、LICENSE_CONFIRMED=OFF | configure失敗 |
| 存在しないパス、または必要な階層がないパス | configure失敗 |
| repo内のパス、repo内へ解決されるsymlink | configure失敗。ignore済みの場所も例外にしない |
| 未指定、REQUIRE=ON | configure失敗 |
| macOS/Windows、外部の有効なSDK、確認済み | AAX追加。実SDKでのconfigureとbuildは入手後に検証 |
| Linux、SDK未指定 | 従来のVST3のみ |
| LinuxでAAXを明示要求 | 未対応としてconfigure失敗。成功したように無視しない |
| 一度有効化したbuild directoryでSDKパスを空へ戻す | 再configureでAAX targetと依存が消える |

パス検査は正規化した実パスで行い、単なる文字列の前方一致にしない。
混入検査は`.gitignore`への登録を除外理由にせず、`AAX_*.h`、SDK固有階層、既知のSDK配布archive名などを対象にする。
JUCE自身のGPL対応wrapperや設計文書のAAX言及を誤検出しない。
既知パターンのガードを「名前を変えた未知のSDKまで完全検出できる保証」とは表現しない。
SDK入手後に実物のファイル一覧で補強し、公開履歴と配布物にも混入がないか確認する。

CI骨格は、手動要求とrunner配備済みを示す非機密設定で入口を制御する。
SDKパスだけをpublic runnerへ渡して、存在しないローカルディレクトリからビルドできる構成にはしない。
将来の専用runnerは信頼済み手動実行に限定し、PRコードを秘密資材のある環境で自動実行しない。
Phase Aでrunnerや常設サービスを新設する作業は含めない。
合成した空のSDK階層で検査分岐を試す場合も、実SDKビルドのpassには数えない。

既定OFFの不変性は、AAX変更直前と直後で、同一toolchain、JUCE、FFI、build設定、製品資材を使って比較する。
FORMATS、識別子、compile定義、リンク入力、生成設定と未署名AU/VST3 payloadを照合し、shell parity、auval、pluginval、Windows preflight、release source contractを通す。
同じソース同士の再ビルドも比較し、署名時刻やbuild pathなどの非決定要因を先に切り分ける。
引継ぎの「1 byteも変わらない」という条件を、試験passだけで達成済みとはしない。
byte一致を示せない差分が残れば、原因と未達条件を報告する。

主な変更先は`juce_shell/CMakeLists.txt`、新規CMake module、SDK混入検査と試験、独立CI workflow、AAXの準備状況文書である。
SDK実ビルド、PACE署名、Pro Tools、AAX配布は入手後の別工程として明記し、Phase A完了をAAX対応版の出荷完了とは呼ばない。
既存の3配布チャネルをAAXの準備物で置き換えない。

**9. 最終検証と配布へ渡す証拠**

| 検証層 | 合格として残すもの |
| --- | --- |
| Rustとソース | `cargo test --workspace --locked`、Clippy本体警告ゼロ、行数予算、public history、RT safety、shell parity、release source contract。FFI変更時はignored parityとpairing_candidatesを実測件数で全件実行 |
| 数値と音声 | 通常Aのbit identityと0 samples latency、測定精度、offline、bypass、sample rate変更、worker障害と再起動、欠損ファイル。EBU cache変更時は依存crate単独の試験も実行 |
| Reference | F1からF5を全件閉じたログ、長時間とseek、減衰保持、通常復帰、プリセット更新、旧版混在、出力確認 |
| ローカルBlind | 形式別のPDCと参加範囲、Record排他、2枠制約、短音、mono、操作一巡、非開示、実音の切替 |
| SPACEとATTACK | 独立評価、固定した定義と素材、PRE/POST対応、欠測、容量上限、評価への漏洩がない記録 |
| 描画と性能 | 製品フォント、全画面、全倍率、PSB、DRUM、Focus Trail、停止と復帰、CPUとメモリ、音と表示の時刻差 |
| 実機 | 同一候補のmacOS AU/VST3とWindows VST3。PRE/POSTの読込みbuild IDとbinary hashを記録した実ホスト結果 |

FFIのignored suiteは前回20件と5件だったが、実装後も同じ件数と決めつけず、テスト一覧から実測する。
Windows操作前には[共通Runbook](/Users/nishiodaisuke/Dev/kirin_sense_lens/docs/windows_validation_remote_access.md)を読む。
既存完成計画の保存済み検証曲とStereo条件を使い、monoは別の試験として記録する。
GUI経路が使えない場合は、その実機工程だけを未検証として残す。
音付き動画は実候補から作り、HTMLには知覚と読み取りの判定に必要なものを追加する。

配布へ進む場合は[LS Runbook](/Users/nishiodaisuke/Dev/kirin_hypha/docs/ls_release/kirin_hypha_ls_runbook.md)に従い、同版の3チャネルを揃える。
LS用は署名とnotarize済みmacOS Universal PKG、HP用は署名とnotarize済みmacOS Universal ZIP、GitHub Releaseと英日リンク、Windows用は同一コミットのgreenなCI artifactから作る署名済みinstallerとする。
Windowsはpayload、installer、uninstallerの署名と、install、同版再install、旧公開版からのupgrade、uninstallを検証する。
release build、notarize、配置を行う実装セッションでは、AGENTS.mdに従ってLS用PKGまで準備し、証明書や外部検証の不足はblockerとして記録する。
macOSだけで公開完了にはせず、公開操作は具体的な候補と検証結果が揃った時点で扱う。

**10. 変更の渡し方と残る判断**

commitは、必要な責務抽出、R1、R2、R3、R4と回帰、Blindのhost実証、製品接続、軽量化、各測定機能、AAX準備という責務で分ける。
修正と必要な試験を同じ変更単位に含め、合格したcommitを統合する。
B番号は既存履歴との重複を検査して採番し、計画段階では予約しない。
各完了報告にcommit hash、B番号、変更ファイル数と増減、Test、LS、HP macOS/Windows、未処理申し送りを残す。
Notionへの書込みは行わない。

人の回答が必要になるのは、SPACEの区間、ATTACKの立ち上がり、表示と切替音の知覚、二人目の独立注釈、未確定の測定条件である。
SDKとPACEの入手、Pro Toolsの準備はAAXの実ビルド以降に必要になる。
対応範囲の縮小や測定意味の変更が必要と分かった場合は、実測結果と推奨案を提示して判断を受ける。
これらを理由に、Reference修正や軽量化まで止めない。

最初の実装成果物は、G0で固定した候補に対するR1からR4の変更と、5件の再現を閉じた検証記録とする。
作成済みHTMLへの回答は随時取り込み、SPACEとATTACKへ別々に反映する。

今回の変更：計画書1ファイルのみ。
Test：製品試験はskip。文書と現物、HTML台帳、固定JUCEのCMake実装、GitHubのCI状態を読取り確認した。
LSアップ用：skip。
HPアップ用：macOS skip、Windows skip。
未処理：本書の実装工程は全て未着手。レビュー5件は未修正。
