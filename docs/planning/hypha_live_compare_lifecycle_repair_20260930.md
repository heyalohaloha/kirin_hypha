# Live比較の復元・再入場・MATCH適用境界

基点: `ca71a71f` / B-1100。2026-09-30、レビューで再現した2件への構造的対処。
Reference機能拡張、DAW配置、公開リリースはこの作業に含めない。

## 原因と責務

- state restoreは永続設定だけを扱い、非永続のLive比較出力許可を失効させていなかった。
  同じpairの復元では既存のpair監視も成立し続け、旧Source・聴取receipt・回答が有効だった。
- 新規開始と同じsessionの継続が区別されず、保持減衰をMENUから新規BLINDへ持ち越せた。
- MATCHの測定差と最終適用gainは異なるが、計画は最終gainを検証せず、適用はboolだけを返していた。
  測定−6.021 dB＋保持−20 dBの−26.021 dB計画が拒否されても、失敗理由のない準備中に残った。

`LiveCompareAuthority`はstate restoreと出力許可の非永続の境界だけを所有する。
hostからの復元開始でatomic世代を進め、重なる復元が全て終わるまで新しい許可を出さない。
遅い開始処理も取得時のticketが変われば許可を得られない。復元完了は開始操作ではない。
Audio Threadは旧許可を独立に拒否し、message threadだけがring退役・stage・scope解放を扱う。
POST減衰の実gain／目標gainとEND completionはこの失効世代へ統合せず、明示済みの安全な復帰を維持する。

`entryAdmission`をprocessor、主面、MENU、他試聴へのUIガードから共用する。
新規LISTEN／BLIND／PINには実gainと目標gainの両方のunityを要求する。
同じ有効sessionからBLINDへ移る場合だけ、そのsessionの承認済みPOST減衰を継承できる。
ENDの途中や復元の整理前は開始せず、拒否で音量を変更しない。

MATCHは測定、計画、適用を分け、`MatchFailure`を計画・適用結果まで伝える。
最終gainの±24 dBとfinite条件、承認choiceの整合を共通検証し、適用不能は理由付きのfailedへ進める。
世代失効による一時的な再計測待ちとは区別する。古い計画が現在のPOST targetを上げる場合もstaleとして
拒否する。MATCHから音量を上げず、通常復帰の権限はEND／RETURNだけが持つ。

回帰試験の`kirin_local_blind_trial`で120秒timeoutが発生した。単独再実行は通ったが、
収録型BlindのRT自動Source 2移行が無条件のcommand storeで、新しいSTOP／RETURNを上書きする
競合を確認した。`TrialCommandState`へ命令の更新規則を分離し、非RTの明示操作は世代を進め、
RT自動移行は実際にrenderした命令を期待値とする単発CASだけに制限する。RTでretryしない。
順序固定の契約テストで新しい選択・STOP・RETURNの全てを保護し、実rendererの並行退役試験も継続する。

## 影響範囲

状態読込、Live processorの開始・制御・音声出力・PIN、MATCH計画と適用、LISTEN／BLINDの表示・MENU、
収録型Blindの命令更新境界、英日全サイズの失敗表示、native lifecycle試験、source契約、関連する製品文書。
既存の大きいowned sourceへ行数を追加していない。新規owned sourceは全て500行以下。
Rust FFI source／ABI、正本計測、Record、通常A経路、Referenceの独自状態機械は変更しない。

## 検証と記録

- macOS x86_64 Debug `ALL_BUILD`: pass。最終buildは警告なし。
  `/tmp/hypha-lifecycle-build-command-boundary.log`。
- native: 選択37件中36件pass、Reference専用runtime 1件fail、444.24秒。
  `/tmp/hypha-lifecycle-native-verified.log`。Blind／Live／Editor／入口／pair previewは全件pass。
  追加9シナリオは同一pairの動作中復元、不正state、準備中、承認待ち、保持減衰、END途中、
  最終gain範囲外、同一session再利用、記名MATCH済み復元を実processor＋隔離FFI＋S-1で検証した。
  復元後最初のblockはmessage serviceを止めて検査し、POST以外のsampleは0件。
  pair previewの実unmapだけはdyldのRust TLV image保持によりskip。
- `kirin_local_blind_trial`: 修正後20回連続pass（29.61秒、計2,000回の並行退役、RT new/delete=0）。
  `/tmp/hypha-lifecycle-trial-stress.log`。新しい選択・STOP・RETURNを保護する順序固定契約も通過。
- 英日×5サイズ×11状態のLive Blind表示110ケース、RETURN footer 20ケース、product entry契約pass。
  `/tmp/hypha-lifecycle-ui.log`と最終native再実行。300pxの範囲外／不正MATCH表示はPNGでも確認した。
- `cargo test --workspace -- --test-threads=1`: 2,165 pass、41 ignored、0 fail。
  `/tmp/hypha-lifecycle-cargo-final.log`。その後追加した命令境界のsource契約を含むxtaskは157 pass、
  `/tmp/hypha-lifecycle-xtask-verified.log`。FFI source／ABIは未変更のためignored parity／pairingは今回skip。
  workspaceのpassをRecord／pairingの実証には用いない。
- `cargo clippy --workspace --all-targets -- -D warnings`: pass。本体のclippy警告なし。
  vendor/baseview 127件、vendor/nih-plug 3件の既存警告は監査対象外。build scriptのHMAC方式通知は残る。
  `/tmp/hypha-lifecycle-clippy-verified.log`。
- fmt、screen text、source line budget、diff check: pass。legacy oversized 29件のratchetを維持。
  既存JUCE変更はupstream `4f43011`系の承認済み10patchとして照合し、今回変更・stageしていない。

### 残る失敗と未検証

Reference専用targetは今回のLive／Local Blind変更をリンクせず、Reference sourceとfixture、Rust FFI／
計測sourceに基点との差分がないことを確認した。ただしその全体試験は2回とも
`later B gains a receipt only through matching four-unit A evidence`で失敗した。
1回目は`reference_capture_live_sharing_test.cpp:103`、2回目は`reference_capture_evidence_test.cpp:53`。
`--capture-repair-only`（start race＋live sharing）はexit 0で、保持有無ともLIVE 80 binsと第2POST枠を確認。
`/tmp/hypha-lifecycle-native-final.log`、`/tmp/hypha-lifecycle-native-verified.log`、
`/tmp/hypha-lifecycle-reference-isolated.log`。原因を試験順序や負荷と断定せず、Reference全体は未解決とする。
次は合成hostの100ms分の供給と12ms待機、worker完了待ち、対応証跡の失効条件を切り分ける。
そのため今回の結果を全製品のgreenや公開可能の根拠にはしない。

前回の全UI試験のFREQ landscape 900×600／DPI 2は18.6429 ms/frameで、`median < 12.0`に未達。
`/tmp/hypha-one-pass-ui-full.log`。今回はBlind表示を再検証し、FREQ性能gateの閾値や実装は変更していない。
実DAW、Windows、30分負荷matrix、Universal／release build、配布署名・公証・配置・公開は未実施。
nativeの疑似AU／VST3／AAX hostを実DAW・Windowsの結果へ代用しない。

### 修正と残る検証

- 修正: B-1101。復元許可・再入場・最終MATCH検証を共通化し、
  収録型Blindの自動進行と明示終了の競合も命令更新境界で解消。Reference／FREQのgateは未解決。
  隔離したnative fixtureとmacOS x86_64 Debugで検証。実機・本番へ接続せず、PR／push／公開なし。
  What: Reference対応確認の全体試験失敗、FREQ性能gate、実DAW／Windows／長時間負荷matrix。
  Why: 今回のBlind lifecycle修正の合格と、公開に必要な全体gateは別であるため。
  Next: 上記Referenceの2地点をfixture供給／worker／証跡から切り分け、性能閾値を維持してFREQを確認。
  実機・配置・配布作業は承認範囲と所定runbookを確認してから実施する。Ref: 本書と上記log。

上記を未送信記録とする。LS／HP macOS／HP Windowsは全てskip（Debugのadhoc署名のみ、配布作業なし）。
