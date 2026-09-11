# Hypha 操作階層・測定面積の整理案

Status: 実装済み。2026-09-11、B-819を基点に共通shellへ反映。

## 狙い

測定を主役にし、操作を探す負担と移動を減らす。
文字を小さくして収めず、常設する操作の役割と場所を整理する。
通常CE2226の暗い計器面、細い連続した輪郭、余白、意味色を継承する。
VUの構図は維持する。破線・点線、新しい装飾アニメーションは追加しない。

## 確認した現状

- 共通Headerは2行。title・context・PAIRと、domain navigation。
- Footerは全5サイズで2行。POST/Δ・各操作と、状態・秒数・version・size。
- Guideは受信内容があるときだけ独立railを確保する。
- 同じrailがCONNECT承認、INSPECT、MASKING、停止・保持状態を担う。
  Guideの存在と、現在の再生位置で対象がACTIVEかどうかは別である。
- FREQ内にもchannel/subview/MARK、凡例・readoutの高さがある。
- FREQ Δの大きい表示は、Focus未選択でもTrail用の下部領域を予約する。
- TIME HISTORYはsubview navigationに加えてrange/scale用の行を使う。
- サイズボタンは5サイズの循環選択。拡大の都度ボタン位置も動く。
- PAIRメニューに表示設定、Keep操作、ペア選択が同居する。
- 現行editorの旧PostControlsは非表示。常設Keep行があるとは扱わない。

参照: `HyphaObservatoryContract.h`、`HyphaObservatoryViewLayout.cpp`、
`HyphaObservatoryViewFooter.cpp`、`HyphaObservatoryViewState.cpp`、
`HyphaSpectrumGeometry.h`、`PluginEditor.cpp`、`PluginEditorMenu.cpp`。
実画像は既存native renderの`/tmp/hypha-final-polish.q9Bpgl/`から、
POST LEVELの全5サイズ、FREQの900、TIME・SPACE・VUの600を確認した。
DAW実機の今回の撮影ではない。View単体fixtureに含まれないeditor所有の操作はコードで確認した。

## 推奨する構成

1. 上部は2行を維持し、対象と画面選択を安定した場所に置く。
   1行目はrole/title・TRACK/STEM/2MIX・PAIR。
   2行目はdomainとPOST/Δ。測定対象をplotの近くへ移す。
   300/375はdomainを名前付きメニューから直接選ぶ。
   450以上はLEVEL/TIME/FREQ/SPACE/REFを直接選ぶtabを維持する。
2. Footerを全サイズで1行へ。VU・操作・表示・サイズを固定した場所へまとめる。
   通常時のACTIVE/INACTIVEと経過秒数は常設しない。
   Session経過時間は意味を持つLEVEL/TIMEの詳細から確認できるようにする。
   測定待ち、取得不可、bypass、保持中の古い値は現在の事実と誤認させず、
   当該測定面の状態表示に集約する。LEDの色だけへ意味を押し込まない。
   600以上は使用頻度の高いRESET/CAPTUREも直接置く余地を検証する。
   versionは600×400以上のFooter余白へ控えめに表示し、全サイズで情報メニューから確認できる。
   versionのための専用行は作らない。利用者操作の失敗通知は削除せず、同じFooterで提示し、
   長文は押して全文を読める。通知の出現でグラフの高さを変えない。
   通知よりSTOPなどの中断操作を優先し、通知が操作を覆わないよう領域を分ける。
3. PAIRメニューは接続先の選択に集中させる。Keep/All Keep/All Stopは操作メニューへ。
   Keep準備中・実行中は状態slotに状態と明示的Stopを出す。
   NOTEはKeep中だけ直接操作候補にし、普段は操作メニューで条件を説明する。
   STOP、ReferenceのA/B・再生中の音源、Blindの中断は隠さない。
4. 表示メニューへhover help、Record時VU、Jungleの既存設定をまとめる。
   Jungle項目は現在と同じ初回解放条件を守る。
   複数階層のサブメニューを作らず、見える名前から一段で到達させる。
   小サイズでGuide・STOPと競合する場合は、低頻度の表示設定を操作メニュー内の
   名前付き区分へまとめる。2行への折返しや文字の圧縮で解決しない。
5. サイズはFooter右端の現在値から開く直接選択メニュー。
   `100% 300×200 / 125% 375×250 / 150% 450×300 / 200% 600×400 / 300% 900×600`。
   実寸がプリセットと一致するときだけcheckを付ける。
   自由リサイズ中は実際の倍率または寸法を示し、近いプリセットを現在値と偽らない。
   同じ選択肢を押しても再配置せず、Esc/外側クリックでサイズを変えず閉じる。
   現行の3:2比率、ドラッグリサイズ、instanceごとの保存を継承する。
6. Guide専用行は、情報がある時だけ出す名前付きボタンへ置き換える案を第一候補とする。
   詳細と常時必要な観測contextを分離する。下記のMASKING/INSPECT契約で評価する。

## 画面ごとの扱い

| 画面 | 直接残す操作 | 整理する領域 |
|---|---|---|
| LEVEL | 対象、必要なCURRENT/MAX、Resetへの短い経路 | 小サイズのversion・共通設定・常時disabledのNOTE |
| FREQ | LR/MID/SIDE/M/S、POST/Δ、ΔのMARK、probeと解除 | subviewを名前付き選択へ、凡例とreadoutの整列 |
| TIME | HISTORY/ATTACK/SHARP/LIVEと現在のrange | range/scaleを同一context rowに整理。小サイズは明示名のpopup |
| SPACE | 現在の観測情報・凡例 | 無効なΔを選択可能に見せず、無関係な操作を外す |
| Reference | A/B、再生・停止、Gain Match状態、音源 | 共通設定だけを移す。出音の状態を隠さない |
| VU | 既存の復帰・CLEAR | 本体の構図を保ち、サイズは既存情報入口からも直接選べるようにする |

M/SとΔの排他、monoでのSIDE不可、PREの機能境界を維持する。

Focus Trailは明示的なprobe lockで開き、解除で閉じる案を試す。
信号の有無やマウスhoverだけで自動開閉させない。
操作時の周波数x位置を保ち、ロック後のy軸表示変化を確認する。
毎回の高さ変化が観測を妨げる場合は、固定表示の選択肢を比較して決める。
空いた面積へ新しい指標・装飾・説明行を追加しない。

## MASKING / INSPECT：要約ボタンと観測context

### 調査からの判断

受信情報をすべて歯車やhover helpへ隠す案は採用しない。
GOV.UKのDetailsは必要な人だけ読む詳細の折り畳みを推奨する一方、
多くの利用者に必要な情報を隠す用途を避けている。
NN/gのTooltip Guidelinesも、作業に必須の情報を消えるtooltipへ入れないとしている。
これらはWebの指針であり、Hyphaでの有効性を実証した資料ではない。
Hyphaでは、専用行を減らしながら「Guideの存在・現在の観測対象」を残す設計へ適用する。

| 案 | 面積 | 観測中の負担 | 判断 |
|---|---|---|---|
| 現行の専用行 | 受信時に18〜30pxと間隔を使用 | 要約を常に読める | 比較基準 |
| 無名アイコンだけに全情報を格納 | 行を回収できる | 存在を見落とし、帯域・時刻を覚える必要がある | 非推奨 |
| MASKING/INSPECTボタン＋画面内の帯域・時刻 | 行を回収する余地がある | 詳細だけ開き、対象は見ながら操作できる | 第一候補 |

### 表示・操作契約

- 有効な受信済みGuideがある間だけ、Footer内の共通context位置に`MASKING`または
  `INSPECT`ボタンを出す。空の枠、未受信ラベル、通知件数は出さない。
  ボタンの出現・更新では測定枠の高さを変えず、サイズと中断操作の位置を保つ。
- 「情報がある」はGuideの保持状態で判定し、音の有無、再生中、現在の区間のACTIVEとは分離する。
  再生停止・時刻範囲外・通信lease失効だけで消さない。明示clear・置換など既存寿命を継承する。
  不正な新規artifactは既存の有効Guideを壊さず、内部失敗の通知も増やさない。
- 通常は名前と既存の意味色で存在を示す。停止・保持された対象を現時点の事実と誤認する
  場面では短い状態を同じ位置に残す。点滅、点線、破線、自動popup、受信での画面切替はしない。
- FREQでは既存のfrequency focusとmeasured band、TIMEでは対象時刻・区間を維持する。
  正確な帯域・区間は、その面の既存readout/凡例にも読める形で置く。
  色だけに意味を依存させず、OS由来の対象をLIVE POST/Δの測定事実と区別する。
  周波数不明のGuideに帯域を補わない。SPACE/VUに根拠のない帯域や時刻を新設しない。
- クリックまたはキーボード操作で小さな非モーダル詳細を開く。
  `OS GUIDE`の出所、種類、source pair、時刻/区間、band、frequency basis、状態を
  既存snapshotに存在する範囲だけ示す。開いただけで接続承認、受領、Jungle操作は発生させない。
- 詳細は自動timeoutで閉じず、閉じる操作とEscを用意する。必要な対象帯域・中断操作を覆う
  配置を避ける。小サイズではスクロール可能な詳細とし、文字を縮めたり別の常設行を作らない。
  閉じた後も頻用する対象情報は画面に残ることを採用条件とする。
- 未承認の接続要求は`CONNECT`という別の明示操作として残す。
  Guide詳細を開く操作と承認操作を同一クリックにしない。期限切れ後の承認失敗は通知する。
  既存Guideと新規要求が同時にある場合の到達経路も検証する。
- INSPECTにも同じ文法を適用する。VUは現状Guide railを表示していないため、
  メーター本体を変えず既存情報入口からの到達を検証し、専用行は追加しない。
- CaptureのGuide包含は既存の明示opt-inを維持する。ボタンが見えるだけで画像へ含めない。
  表示整理はJungleの初回発動条件・利用者の選択・永続化に影響させない。

ボタンの横幅は全5サイズの実フォントで測り、特に300/375でKeep/Stopと同居させて確認する。
位置と短縮ラベルはこの確認後に確定する。外部資料だけで「全サイズに収まる」とは断定しない。

## 実装後の面積（Footer 1行化、Guide contextをFooterへ統合）

共通`ShellLayout`のGuide表示中の幾何値を比較する。
Guide有無でbody寸法は変化しない。

| editor | Footer変更前→実装後 | body高さ 変更前→実装後 | 増加 |
|---|---:|---:|---:|
| 300×200 | 44→24 | 78→118 | 51.3% |
| 375×250 | 48→26 | 108→154 | 42.6% |
| 450×300 | 48→28 | 146→192 | 31.5% |
| 600×400 | 52→30 | 228→278 | 21.9% |
| 900×600 | 64→34 | 386→450 | 16.6% |

FREQの未選択Focus Trail領域回収はこの数値に含めない。
Headerをさらに1行へ押し込む案は、PAIR名と操作幅の競合が大きく推奨しない。

## 実装へ進む際の範囲と確認

- 共通shell geometry、View layout/footer/state、editor resize/menu/Guide表示、
  各画面のcontext row、Capture geometry、native source契約とinteraction試験が影響範囲。
- Guide表示の正本であるproduct contract、OS Guide integration plan、CE2226 visual systemも
  実装と同時に更新する。通信protocol・Rust計測coreは今回の変更対象にしない。
- 全5サイズと自由リサイズ境界、PRE/POST、Guide有無、通常/Jungle、
  無音・停止・warming、Keep中、VU復帰を同じgeometry設計で扱う。
- Captureは操作UIを整理しても出所・version・測定条件を残す独立出力。
- popupはクリック時のみ生成し、既存JUCE非同期menuとSafePointerを利用する。
  timer、FFT、背景poll、texture、常時animationは追加しない。
- 最初にnative fixtureとgeometryの対象確認をまとめて1回行う。
  製品変更後の必要suiteは1回に集約し、失敗箇所だけを限定再検証する。
- PRE/POST Debug build、pure geometry contract、5サイズUI render contractを実施した。
  install、release、配布物生成は行わない。

## 実施順序と採用条件

1. 操作配置の静止案を全5サイズで比較する。Guideあり/なし、Keep/Stop、長いPAIR名を含め、
   フッター1行化とMASKING要約ボタンを一つの設計として確認する。
   使用頻度の分類は実測analyticsではなく既存動線からの仮説なので、頻用操作の隠し過ぎを確認する。
2. 文字幅・クリック領域・重なりを確定してから共通レイアウトとmenu責務を実装する。
   既存500行超の変更対象は責務抽出を先に行い、無関係な分割をしない。
3. FREQ/TIME内部の行を整理し、全domain・Reference・VUへの影響を同時に確認する。
   Trail開閉による軸の変化だけは静止案で判断できないため、実際のlock/解除操作で決める。
4. 対象検証をまとめて実施する。正常・無効操作・未受信・停止後保持・接続要求期限切れ・
   サイズ保存後の再起動・editorを閉じた状態でのpopup callbackを含める。

採用条件は、全サイズで重なり・意図しない省略・2行折返しがないこと、
サイズ選択が「開く→選ぶ」で完了すること、観測しながら必要な対象情報を読めること、
受信・再生状態の変化でplotが上下に動かないこと、既存の測定値と保存内容が変わらないこと。
測定面積は画像の印象だけでなくgeometry値で比較する。
描画負荷は対象native計測で一度比較し、新規timerや継続的な処理を追加しない。
macOS/Windows、PRE/POST、AU/VST3は共通shellの変更対象とし、実機未確認を確認済みとは報告しない。

## 外部根拠

- Nielsen Norman Group, Progressive Disclosure:
  https://www.nngroup.com/articles/progressive-disclosure/
  頻用操作を前面に保ち、低頻度操作を明示的に到達できる二段目へ移す。
- FabFilter Pro-Q 4, Full Screen mode, resizing and scaling:
  https://www.fabfilter.com/help/pro-q/using/fullscreenandresize
  下部のサイズメニューからプリセットを選び、VST3ではドラッグ変更も併用する。
  Hyphaへの適用案は独自の設計判断であり、同じUI・保存仕様の模倣ではない。
- Nielsen Norman Group, Aesthetic and Minimalist Design:
  https://www.nngroup.com/articles/aesthetic-minimalist-design/
  ブランドの美観を保ち、必要な情報の視認性を下げるノイズを整理する。無装飾化そのものが目的ではない。
- GOV.UK Design System, Details:
  https://design-system.service.gov.uk/components/details/
  必要な人向けの詳細を折り畳むが、多くの人が必要とする情報を隠す用途には使わない。
- Nielsen Norman Group, Tooltip Guidelines:
  https://www.nngroup.com/articles/tooltip-guidelines/
  必須情報はtooltipだけに入れず、名前付き入口とクリックで開く詳細を検討する根拠にした。
- W3C WAI, Understanding SC 1.4.13 Content on Hover or Focus:
  https://www.w3.org/WAI/WCAG22/Understanding/content-on-hover-or-focus.html
  hover/focusで追加表示する場合の予測可能性・閉じやすさ・読める時間を確認した。
  今回はhoverだけを入口にせず、明示操作で開く案。nativeアプリの適合を宣言するものではない。
