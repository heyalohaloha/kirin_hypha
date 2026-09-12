# Kirin Hypha 未完了作業の引き継ぎ

> 履歴資料：2026-09-13のGit整理で、従来ローカルにのみ存在した文書を保存した。
> 以下の進捗、未push、CI、配置状況は文書作成・更新時点の記録であり、現在の状態を示さない。
> 現行の契約はREADMEと`docs/hypha_invariants.md`、B-836の修復と残る実機検証は`docs/hypha_structural_repair_plan_20260912.md`を参照する。

更新日：2026-09-10。

この文書は、[Hypha完成計画](hypha_completion_plan_20260907.md)のうち、B-794時点で残っている作業だけを次の実装セッションへ渡す。
C0の判定用HTMLは完了しているため、作り直さない。
過去文書の「Blindは未接続」「macOS AUのPDCが未実証」「Focus Trailの性能超過」という記述も現在地として扱わない。

## 1. 再開地点

| 項目 | 現在地 |
| --- | --- |
| 作業ブランチ | `codex/hypha-pdc-host-validation` |
| HEAD | `c369e8f7f4ec34ff516961d7d0d54cdb16450564`、B-794。local commitであり未push |
| Pull Request | [#22](https://github.com/heyalohaloha/kirin_hypha/pull/22)。open、mergeable。タイトルはB-756〜B-763のままで、remoteはB-787まで |
| HEADのCI | B-788〜B-794は未pushのためCI未実行。親B-787の[CI run 34374411931](https://github.com/heyalohaloha/kirin_hypha/actions/runs/34374411931)はmacOS release source、Windows VST3 preflight、arm64 AU validation、public historyがgreen |
| AAX Phase A CI | [run 34374411945](https://github.com/heyalohaloha/kirin_hypha/actions/runs/34374411945)はSDK混入検査がgreen。SDKを使うbuild jobのskipは正常 |
| macOSの配置 | システムへ配置済みなのはB-786。B-787は署名、公証、bundle buildまで完了しているが、管理者パスワードが必要なinstallを変更前に中止した |
| B-787の配置確認 | `cargo run --package xtask -- install --release --verify-only`は、配置済みPRE AUとB-787 source bundleのbinary mismatchを正しく検出した |
| 配布前PKG | `dist/LS_UPLOAD/Kirin-Hypha-1.1.49-macOS-Universal.pkg`。79,816,758 bytes。SHA-256は`5bac2e741f757f10dae251b36c633228fea1ccdc33230cd972463f58949840f6` |
| PKG検証 | Developer ID Installer署名、公証、staple、Gatekeeper、4 payload、LS dry-runがpass |

次のセッションは作業前に`AGENTS.md`を読む。
Windows検証機を使う場合は、先に`/Users/nishiodaisuke/Dev/kirin_sense_lens/docs/windows_validation_remote_access.md`を読む。
認証情報を画面、ログ、コマンド出力、リポジトリへ出さない。
Notionへの書込みは禁止されている。

次の既存作業は本件へ取り込まず、削除もしない。

```text
 m juce_shell/JUCE
?? build-aax-universal/
?? build-aax/
?? docs/hypha_completion_plan_20260907.md
?? docs/hypha_remaining_work_handoff_20260910.md
?? docs/hypha_trace_record_inbox_recovery_handoff_20260806.md
?? juce_shell/build-pdc-macos/
```

B-789は巨大なFFI正本からMeter Session制御境界を分離した。
B-790は別作業のrelease／platform test改善である。
B-791は通常再生中のHybrid VU選択とTP／Clip holdの解除を追加した。
B-792はVU専用Clip表示ラッチとSession累積Clip eventを分離し、CLEARとMeasure公開の競合も解消した。
B-793は保存済みMeter Contextとeditor寸法をeditor生成時に上書きしないよう修正し、出荷VST3のhost state往復契約へeditor実生成を追加した。
B-794はPRE/POST Blindの公開説明を現行試聴経路へ同期し、通常A経路の透明性と明示試聴の例外、POST基準の固定Gain Match、DAWのsolo／routing非操作、4秒と長尺Referenceの境界を固定した。

## 2. 完成計画の状態

| 工程 | 状態 | 残っている完了条件 |
| --- | --- | --- |
| C0 判定用HTML | 完了 | なし。旧HTMLを再作成しない |
| C1 PRE/POST Blind | 実装と三形式PDC実証まで完了 | 公開UIによる開始から通常復帰までの一巡、実音Gain Match、競合、障害、性能の実ホスト確認 |
| C2 軽量化と表示 | 部品回帰は合格。通常再生中のHybrid VUとTP／Clip `CLEAR`をB-791で実装し、B-792でSession正本との分離を修正 | 同じ最終候補を使った最大構成、10分以上、停止中負荷、全倍率、音と表示の同期、残る実画面確認 |
| C3 SPACEとATTACK | TRACK/STEM DRUMは実装。SPACE局所減衰の研究診断を追加。SPACE DECAYと2MIX ATTACKは未完成 | 局所episodeと人の知覚区間の対応を追加注釈で評価。2MIX ATTACKは別日に独立評価して接続 |
| C4 最終実機合格 | 未完了 | 同一release candidateのmacOS AU、macOS VST3、Windows VST3、障害系、音付き動画、最終HTML |
| C5 配布 | 未完了 | release version確定、PR統合、同一commitのmacOS PKG、macOS ZIP、Windows installer、GitHub Release、英日HP |

## 3. 最初に揃えるローカル状態

B-787の変更は、Kirin OSでReferenceを接続する案内を実際の製品操作へ合わせただけである。
表示文は「Kirin OSの保存済みWorkを開き、INSPECTでConnect Hypha POSTを選ぶ」と伝える。

次のセッションではDAWを閉じてから、必ず正規のinstall経路を使う。

```sh
cargo run --package xtask -- install --release
cargo run --package xtask -- install --release --verify-only
```

手動の`sudo cp`やuser-level VST3だけの差替えは行わない。
install時の管理者パスワード入力だけはDaisukeの操作が必要になる。
配置後はPREとPOSTの読込build ID、format、binary hashを記録し、古いインスタンスを見て合否を決めない。

現行の公開版はすでにv1.1.49である。
B-787から作った同名のv1.1.49 PKGは署名と公証の検証物として使用できるが、新しい公開版としてそのまま上書きしない。
C5へ入る前にv1.1.50以降の一意なversionを決め、同じrelease commitから全配布物を作り直す。

## 4. Referenceの実接続

ライセンス認識とHypha側の案内修正は完了している。
保存済みWorkを4件確認し、Reference preset projection 7件、source artifact 2件、実音源2件の存在とreceipt、byte数、SHA-256の一致を確認した。
保存済みWorkと実音源は失われていない。

一方、現在の`plugin_data/reference/v2`へ公開済みのmanifest 4件はすべてschema `2.0`である。
B-787以降のHyphaはglobal Preset catalogを含むschema `3.0`だけを受理するため、この旧manifestからは接続できない。
Kirin OS側の現行schema、manifest、projection、publisher試験28件は2026-09-10にgreenだった。
帰宅後は保存済みWorkを開き、INSPECTの`Connect Hypha POST`から通常のv3公開経路を実行する。
`plugin_data`やWork JSONを手動編集してschemaを偽装しない。

次の一巡をmacOSで行い、Windowsでも同じ製品契約を確認する。

1. Kirin OSでReference candidateを持つ保存済みWorkを用意する。
2. WorkのINSPECTで`Connect Hypha POST`を選び、対象POSTを明示する。
3. HyphaのREF画面に同じcandidateが現れることを確認する。
4. 利用者の明示操作だけでReference試聴を開始する。
5. 固定Gain Match、切替、終了、通常A経路への復帰を確認する。
6. 欠損ファイル、改変、不正Work、license不成立、DAW offline render、seek、停止、再読込でfail closedになることを確認する。
7. Reference開始中にBlind、Keep、All Keep、Record、別Reference開始が同時所有しないことを確認する。

Referenceファイル、通常入力、正本のPRE/POST測定、Recordを書き換えない。
接続、読込、DAW再起動だけでReference再生を自動開始しない。

## 5. PRE/POST Blindの残作業

B-768〜B-774で、明示PRE選択、共通入場判定、exact 4秒取得、Gain Match、Source 1／2、回答、Reveal、中断、減衰保持、通常復帰まで実装した。
Windows Studio Pro VST3、macOS Studio Pro VST3、macOS Studio Pro AUでは、4096 samplesの既知遅延を挟んだ取得がbit一致し、PDC残差0 sampleだった。
Blind入口は既定ONである。
B-794で、通常はPOSTを基準にPRE試聴コピーだけを固定Gain Matchし、PRE増幅がceilingを超える場合だけ明示承認後にPOSTを逆差分減衰する契約をREADMEと不変条件へ明記した。
TRACK/STEMはDAWをsoloにせず対象POST出力だけを同じ時刻の固定コピーへ置換し、残りのlive mixと共に聴く。2MIXは対象bus全体を置換する。
現行のPRE/POST BlindとReference Blindはいずれも4秒取得であり、8秒／16秒は4秒の製品一巡成立後に必要性を評価する。完成版の長尺／全曲比較は、WAVをKirin OSの不変versionとして登録し、通常Reference試聴の全曲Cueを使う。これを全曲Blind成立とは扱わない。

残っているのは公開UIと実音による製品一巡である。

### 5.1 必須の一巡

次をmacOS VST3、macOS AU、Windows VST3で確認する。

1. POSTでexact PREを候補一覧から選ぶ。
2. 2MIXまたはTRACK/STEMのMeter Contextを明示する。
3. Blindを開始し、4秒取得と準備の完了を待つ。
4. 減衰が必要な場合だけ承認画面が出ることを確認する。
5. Source 1とSource 2をそれぞれ完全に一巡する。
6. 片側を聞く前に回答できないことを確認する。
7. 回答後だけRevealでき、Reveal前にPRE/POST割当が漏れないことを確認する。
8. 終了後、Audio Threadの通常出力確認を経て減衰保持が解除されることを確認する。
9. PRE/POSTの通常計測、Record、音声が元の状態へ戻ることを確認する。

### 5.2 実音と境界条件

- 2MIX、短く疎なTRACK、STEMを分けて確認する。
- stereoを正本条件とし、monoは独立した試験として記録する。
- 強いEQ、limiter、tail、clip境界、純gain差、同一PCMを含める。
- Gain Match後の音量差と、切替時のclickや応答時間が割当の手掛かりにならないか人が聴いて確認する。
- seek、loop、transport停止、sample rate変更、pair変更、PRE削除、POST削除を試す。
- editor closeと再表示、host再初期化、worker停止、保存失敗、旧版混在を試す。
- 別trackとの同期を確認する。既存の同一track PDC実証だけから別trackを合格にしない。
- Analysis二枠、三つ目の待機、Reference、Keep、All Keep、下流Recordとの競合を製品UIから確認する。
- 125%、150%、200%と最小300×200で操作が欠けず、accessibilityやtooltipからReveal前の情報が漏れないことを確認する。

### 5.3 性能

Release buildで準備時間、最大取得長、総peak RAM、queue使用量、drop、Audio callback最大値とP99、worker CPUを記録する。
Audio Threadではallocation、lock、blocking I/O、decode、解析を行わない契約を維持する。

## 6. 表示と軽量化の残作業

2026-09-10の全画面レビューを受け、[文字階層とレスポンシブ表示の再発防止計画](hypha_responsive_typography_plan_20260910.md)を作成した。
親画面の表示コンテキスト、文字役割、余白、overflow処理を共通化し、直接指定を検出する検査と全画面比較を合わせて導入する。
計画はC2に属し、製品コードの実装は未着手である。C4の候補固定前に全対象を移行し、個別画面の文字調整だけで完了扱いにしない。

B-775で部品描画の固定予算を閉じた。
Focus Trailは100%の更新が4.14978 msで4.5 ms予算内となり、125%、150%、200%、300%も部品試験に合格した。
B-782とB-783でTRACK/STEM DRUMの中央標本を水中生命体へ置き換え、点滅とcompact配置を修正した。
B-784で長時間解析のbudget probeを安定させた。
B-785とB-786で、PREを選ばなくてもPOST単体Sharpnessを選択して0〜3 acumの絶対値を表示できるようにした。
B-791で既存Footerの`VU`ボタンから通常再生中もHybrid VUを開けるようにし、同じ面へ`CLEAR`を置いた。
手動VU選択は読み込まれているplugin instanceのeditor再表示まで保持するが、DAW project stateには保存しない。
`CLEAR`が解除するのは左右のheld TP markerとVU専用Clip表示ラッチだけであり、現在TP、300 ms VU、sample peak hold、I／LRA／全Session Max TP／PLR、LEVEL／CaptureのSession累積Clip event、TIME履歴、Record／Keepを維持する。
clipが継続中なら次の100 ms観測で表示だけ再点灯し、同じ連続runをTIME履歴へ二重算入しない。
B-792の対象検証ではClip／CLEAR 2件、FFI ABI 7件、Hybrid VU UI contractがgreenだった。UI契約はPRE／POSTと5サイズでSession累積ClipだけではVU表示を変えず、専用ラッチだけが表示を点灯することも確認した。

次の項目は実DAWの同一最終候補で閉じる。

- 通常再生から`VU`で入り、`CLEAR`後も針と現在TPが連続し、継続clipが再点灯し、`VU`で元のdomainへ戻ること。
- editor close／再表示では手動VU選択が維持され、plugin instanceの再読込では通常表示から始まること。
- DRUMのHISTORY、TRANSIENT、中央標本、Strength、Texture、Sharpnessが実音と同期して読めること。
- transport停止と無音で不要な点滅や再構築がなく、最後の完成標本とHOLD状態の意味が一致すること。
- Hybrid VU左上のタイトル枠がはみ出さないこと。
- 100%のSPACEで文字が重ならず、300%まで情報量と余白が破綻しないこと。
- POST単体SharpnessがmacOS実再生でも数値へ更新すること。Windowsでは`0.86 acum`の実動を確認済みである。
- pair選択後は同じSHARP画面が符号付きPOST−PRE差分へ切り替わり、pair解除後はPOST絶対値へ戻ること。
- PSBのPOST、ゼロ差分、非ゼロ差分、停止後消去、DAW再起動後の再現。
- Spectrum、hover、click、MARK、Focus Trailが同じ周波数位置を示し、短い欠測で偽の値を補間しないこと。
- Analysis一枠、二枠、三番目の待機、複数の非表示instanceを含む最大構成。
- 192 kHzを含む対応sample rate、可変block、10分以上の連続再生、停止、復帰。
- Audio Thread、計測worker、通信、描画、host全体を分けた負荷記録。

## 7. SPACE DECAYの残作業

現在のSPACE FIELD（MID/SIDE density、L/R balance、correlation）は成立済みであり、壊さない。
未完成なのはEARLYと20 dB相当減衰時間を扱うSPACE DECAYである。

追加判定では、完了4件のうち3件で人が追跡可能と判断した区間があり、合計5区間が指定された。
現行自動選択は5区間を一つも採用しなかった。
固定区間計算で20 dB以上低下した1区間も、直線適合度0.053、途中再上昇6.984 dBで現行契約を満たさなかった。
人が聴く複数の局所的な減衰と、単一の直線的なD20は同じ測定対象になっていない。

B-788では、製品runtimeと分離した手動`space_decay_probe`へ10 ms平均電力binの局所peak-to-trough診断を追加した。
再上昇幅0.5、1、2、3、6 dBを並行計算し、ノイズ床境界を越えた連結と補間を禁止した。
人指定5区間は6 dB条件でもすべて局所episodeを持ったが、6秒の長区間Eは34件へ分割された。
局所的な複数減衰と単一区間D20を分ける方向は実データで支持された一方、再上昇幅だけでは製品値を決められない。
詳細数値と限界は[C3開発評価](hypha_c3_development_evaluation_20260909.md)へ記録した。
B-788の局所診断は製品route、request、表示へ接続していない。

再開時は次の順序で構造から設計し直す。

1. 「局所的に追える複数減衰」と「単一直線D20」を別の観測として定義する。
2. どちらを製品表示するか、併記するか、D20を欠測のまま残すかを実測例付きで決める。
3. ALOHA配下とQobuzの使用許可済み音源から、現代的でSPACEを多く使う曲を含む開発素材を選ぶ。
4. 元音源、抜粋、使用履歴、hashをprivate台帳へ固定し、holdoutと混ぜない。
5. 人指定区間で計算器を検証し、自動区間選択とは別に合否を出す。
6. ノイズ床、次の音、再上昇、持続音、fade、gate、追跡区間なしを別の結果として保持する。
7. PREとPOSTで同じ起点と採用区間を使い、同一入力と固定gainの不変性を確認する。
8. 開発結果から採用Precision、Coverage、最低標本数、曲別集計を提案し、意味を変える条件だけDaisukeへ判断材料を渡す。
9. 定義ID、係数、候補hash、評価器を固定してから、未使用holdoutで評価する。
10. 合格するまで製品routeを有効化せず、数値を得るためだけに20 dB条件や直線適合度を緩めない。

## 8. 2MIX ATTACKの残作業

2MIX ATTACKはDaisukeの判断で別日に延期した。
延期は中止や完成扱いではない。
TRACK/STEM DRUMは独立した現行定義のまま使用し、2MIX検出器の代用にしない。

再開には、候補を見ない二人の独立した網羅注釈が必要である。
既存の代表40点はPrecisionとRecallの最終分母に使えない。
開発20本と未使用holdout 20本を使用権、隔離履歴、hash付きで固定し、対応許容幅と曖昧境界の扱いを評価前に決める。

既存提案値はPrecision 0.85以上、Recall 0.75以上、F1 0.80以上、時刻誤差P95 15 ms以下、偽陽性1回/秒以下、注釈間F1 0.90以上である。
これは未確定の提案値であり、合格済みの仕様として扱わない。
評価を通した後にだけ2MIX専用request、runtime、表示へ接続する。

## 9. C4の最終候補検証

機能修正と実機確認が終わった一つのcommitをrelease candidateとして固定する。
途中のcommitごとに全体suiteを繰り返さず、対象試験で進めて最後に一度だけ全体gateを実行する。

B-788では`cargo test --workspace --locked`、全target clippy、fmt、source line budgetがgreenだった。
`juce_shell/build-pdc-macos`のDebug native試験も12件すべてgreenであり、古い`kirin_local_blind_preparation`失敗は現行buildで再現しなかった。
これらは部品と配線の回帰であり、B-787製品bundleの実DAW合格へ読み替えない。

B-791では全体suiteを再実行せず、変更対象だけを検証した。
`kirin_measure`のTP／Clip解除2件、FFI ABI 7件、Hybrid VU native contract、PRE／POST Debug VST3 build、対象2 crateの全target clippy、fmt、source line budgetがgreenだった。
FFI必須のignored suiteは件数を20件と6件に実測し、Parity 20／20、Pairing candidates 6／6がsingle threadでgreenだった。
B-792ではSession累積ClipとVU専用ラッチをABI内で分離し、古いMeasure frameがCLEAR後へ遅れて公開されないよう更新順序も直した。最終HEADで変更箇所2件、FFI ABI 7件、Hybrid VU限定UI契約、fmt、source line budgetがgreen。対象clippyは長時間化したため途中で中止し、ignored suiteと全体gateも省エネ方針により再実行していない。これらは最終release candidateを固定した時に一度だけ実行する。
B-793では保存済みstateのVST3往復にeditor実生成を加え、PRE／POSTのTRACK/STEMと654×436を生成時と再読込後にも要求する契約を追加した。B-794ではこのaudio transparency targetを再実行していないため、最終候補の一回へ含める。
B-794では全体suiteを実行せず、`KirinUiRenderContractTests`のbuildと`--product-entry-only`だけを実行し、82 role/size layouts、41 Reference layouts、Local Blind UI契約を含む対象確認がgreenだった。先行する同ターゲット全体の一回もgreenだったが、今後の変更ごとに同じ全体renderを繰り返さず、最終候補まで温存する。

最終gateには次を含める。

- `bash scripts/test_release_source.sh`
- `cargo test --workspace --locked`
- `cargo clippy --workspace --all-targets --no-deps`
- `cargo fmt --all -- --check`
- `bash scripts/check_source_line_budget.sh`
- FFIを変更した場合、ignored parityとpairing_candidatesの件数を`--list`で実測し、単一threadで全件実行
- 共通JUCE UI、ATTACK、Blind、Reference、PDCのnative試験
- macOS AU validation、macOS VST3 pluginval、Windows VST3 preflight
- 通常A経路のbit identity、0 samples latency、offline、bypass、sample rate変更、worker障害、欠損ファイル、旧版混在

実機ではmacOS AU、macOS VST3、Windows VST3へ同じcandidateを配置する。
PREとPOSTのbuild IDとbinary hashを保存し、2MIX、TRACK/STEM、mono、stereo、全倍率、最大二枠、停止、復帰、長時間を確認する。

短い音付き操作動画と最終HTMLには、実際のcandidate、音声と映像の時刻差、対象format、build IDを記録する。
合成画像は合成と明記する。
Daisukeへ依頼するのは、音の切替、表示の読み取り、SPACE区間、2MIX ATTACK注釈など人の知覚が必要な判断だけにする。

## 10. C5の統合と配布

PR #22はB-787まで32 commitsを含み、HEADのCIはgreenである。
統合前にPRタイトルと説明を最終範囲へ更新し、厳しめレビューで既知の指摘を閉じる。
mainへ合流した一つのrelease commitから、同じversionの三チャネルを作る。

1. Lemon Squeezy用の署名、公証済みmacOS Universal PKG。
2. HP無料配布用の署名、公証済みmacOS Universal ZIP、GitHub Release、英日HPリンク。
3. 同じcommitのgreenなWindows CI artifactから作るAuthenticode署名済みInno Setup EXE。

WindowsはPREとPOST payload、installer、uninstallerの全署名を確認する。
専用機で新規install、同版reinstall、v1.1.49からのupgrade、uninstallを実行し、DAW scanとPRE/POST読込を確認する。
手動VST3 ZIPは公開installerの代用にしない。

HPの紹介と宣伝は、機能、実機、配布3チャネルが同じrelease commitで揃ってから開始する。
既存の`kirin_hp`側準備を現物と履歴で確認し、古い案内をそのまま公開しない。

## 11. 現行stereo完成とは分ける拡張

### 11.1 5.1

Daisukeは5.1対応を行う意向を示しているが、現行製品契約はmonoとstereoに限定されている。
EBUの5.0／5.1素材をdecodeして参照値を確認した実績は、プラグインの5.1対応を意味しない。

5.1はJUCE bus、channel map、Measure Thread、PRE/POST exchange、SPACE、Sharpness、Reference、Blind、Record、表示、配布形式を同時に定義する独立計画として作る。
チャンネル数だけを6へ増やさない。
現行mono／stereo版のC1〜C5へ混ぜず、現在の完成候補を不安定にしない。
開始時には対応DAW、bus layout、LFEの扱い、チャンネル別表示、PRE/POST比較単位、ReferenceとBlindの試聴出力を先に決める。

### 11.2 AAX

AAX Phase AのSDK非依存準備は完了している。
製品対応として残るのは、licensed AAX SDKの入手、実SDKによるmacOS／Windows build、category確認、Pro Toolsでのchannel、bypass、latency、state restore、offline bounce、PACE署名、installerと配布検証である。
SDKをGPLリポジトリへ含めない。
AAX対応済みとは、これらが揃うまで表示しない。

## 12. 完了判定

現在のmono／stereo AU／VST3版を完成と判定できるのは、次がすべて成立した時点である。

- Referenceの保存済みWork接続と実音試聴が三形式で完了している。
- PRE/POST Blindが公開UIと実音で開始から通常復帰まで一巡し、競合、障害、性能条件を通っている。
- DRUM、Hybrid VU、SPACE FIELD、Sharpness、PSB、Spectrum、Focus Trailが最終候補の実DAWで読め、停止と長時間の予算内に収まっている。
- SPACE DECAYの測定定義と自動区間評価が固定され、未使用holdoutに合格している。
- 別日に延期した2MIX ATTACKの独立注釈、評価、本体接続が完了している。
- 同じrelease candidateの最終自動gateとmacOS／Windows実機試験がgreenである。
- 新versionの三配布チャネルが同じrelease commitで検証済みである。
- 公開対象の英日HPが実際の出荷機能、version、配布物と一致している。

## 13. 正本と証拠

- [製品不変条件](hypha_invariants.md)
- [表示契約](hypha_meter_product_contract_20260831.md)
- [Blind実装進捗](hypha_local_blind_runtime_progress_20260906.md)
- [B1ホストとPDC実証](hypha_b1_host_observation_20260907.md)
- [軽量化と性能](hypha_lightweight_runtime_progress_20260906.md)
- [C3開発評価](hypha_c3_development_evaluation_20260909.md)
- [ATTACK表示提案](hypha_attack_visual_completion_proposal_20260909.md)
- [AAX Phase A](aax_phase_a_readiness_20260907.md)
- macOS／Windows PDC証拠：`/Users/nishiodaisuke/Downloads/Hypha_PDC_Evidence_20260908/`
- 初期ホスト証拠：`/Users/nishiodaisuke/Downloads/Hypha_B1_Host_Evidence_20260907/`
- SPACE追加回答：`/Users/nishiodaisuke/Downloads/Hypha_SPACE_Followup_01_20260907/evaluation_sidecar_recovered.json`

`/tmp`の画像やログは消える可能性があるため、最終合格の正本にしない。
実行できなかった試験は未検証のまま残し、部品試験を実機合格へ読み替えない。
