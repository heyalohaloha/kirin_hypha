# Hypha通常版のCE 2226統一とJungle連動の実装計画

更新日：2026-09-11。
状態：通常版の質感統一とJungleによる生命感の加速、連動方針はDaisuke承認済み。P1の通常外観、OS側のAND条件publisher、Hypha共有service、P2の全5サイズ共通Jungle差分、Displayメニューの独立ON/OFFは実装済み。初回の連動ポップアップと由来表示は2026-09-11の判断で採用しない。
調査基準：Hypha `2908d601`（B-812）、Kirin OS `0bd9db7a8`（W-3045）。
改訂理由：通常版を現状固定する計画から、現在のVUの品位を全画面へ広げ、その完成した通常版をJungleで深める計画へ変更した。

### 2026-09-11 実装到達点

`HyphaSurfaceMaterial.h`へgraphiteの面、内外二段の連続輪郭、上側の弱い反射を集約した。
LEVEL、TIME、FREQ、SPACE、ATTACK、SHARP、LIVE、Reference、Local Blind、Captureと共通button、selector、popup、tooltipへ同じ素材を適用した。
Hybrid VU、PRESENCE、意味色、測定描画、hit領域、Audio Thread、Rust measurementは変更していない。
bitmap、gradient、animation timer、追加解析、追加I/Oは導入していない。

native UI contractはPRE/POSTと全5サイズを含めてpassした。
同一機での変更前後の900×600 Spectrumは7.50887から7.53996 ms/frame、M/S Spectrumは1.82294から2.01299 ms/frameで、いずれも追加0.5 ms/frame以内だった。
OS publisherはKirin OSのJungle発動と、MASKING GuideのHyphaへのpublish成功を別々に検証し、両方が成立した場合だけ発動証明を作る。
Hypha共有serviceは可視editorの既存timerから最大1 Hzで起こし、filesystem、JSON、排他、保存を一個のbackground workerへ隔離した。
macOS/Windows実ホスト検証は次工程に残る。

P2では追加bitmapを採用せず、通常版と同じnative描画へ連続した青緑／琥珀の菌糸と低明度の縁光を追加した。
PRE／POST、5基準サイズ、全domain、Hybrid VU、Captureを同じ描画経路で比較し、OFF復帰は同一snapshotで画素差ゼロを確認した。
VU全面を覆う半透明膜は現行VUの深いガラス感を損なうため不採用とし、目盛り、針、TP rail、数値の負空間を保護した。

## 1. 完成させる体験

Kirin OSでJungle Modeが実際に発動し、MASKING GuideをHyphaへ一度でも正常送信すると、同じ端末の同じOS利用者で動くHyphaも初回だけJungle外観になる。
二条件の順序は問わず、両方が成立するまでは通常外観を維持する。
発動後はHyphaのメニューでON/OFFを選べる。
選択はPRE/POSTと全インスタンスで共有し、OS再起動、DAW再起動、Project Folder変更によって上書きしない。

Hyphaは通常時からCE 2226の菌糸先端であり、Jungleは時代の変更ではなく、同じ生態系の活動密度が増す状態として表現する。
現在のVUの筐体、ガラス、暗部、数値の品位を基準に、通常版の共通枠、パネル、操作部の質感を揃える。
通常版だけで完成した製品とし、その質感向上をOS所有やJungle発動の条件にしない。
Jungleでは同じ素材の透明な組織、琥珀の内部光、奥の層を強め、VUを主な見せ場にする。
OFFで戻る先は質感を揃えた新しい通常版であり、旧外観を選ぶ第三の設定は作らない。
破線、点線、周期的な線の欠けは使わない。

軽さは完成条件に含める。
静止素材と状態変化時の切替を基本とし、追加の音声解析、常時アニメーション、装飾用の独立描画タイマーは導入しない。
OS連動と外観選択を計測値、Record、Reference試聴から分離する。

## 2. 調査で確認した既存構造

| 根拠 | 確認した事実と今回の扱い |
| --- | --- |
| OS `docs/world_setting/kirin_os_world_setting_master_20260529.md` | Hyphaは通常状態からCE 2226の菌糸先端である。発動後は、その奥の生態系が見える表現にする |
| OS `世界観素材/2026-04-14_advisor_visual_bible_v2.md` | 生物光、透明体、琥珀とティールが素材言語。古い点線の指示は今回の利用者指示で採用しない |
| OS `src/App.jsx`、`src/hooks/useJungleDiscovery.js` | 利用可能、発見済み、実際の発動は別状態。DOM属性やfeature flagだけを発動の根拠にしない |
| OS `src/utils/studioProfileFirstUse.cjs` | 有効な発見日時がある場合だけ`jungle_mode_active`を成立させる |
| OS `docs/jungle_studio_profile_lifecycle.md`、`main.cjs` | profileはProject Folderごと。検証、回復、保存はmain processが所有する |
| OS `src/port/pre_display/ipcRouter.cjs`、`store.cjs` | MASKINGの送信成功はGuideの完全なpublication後にだけ成立する。失敗した送信やINSPECTを外観発動へ数えない |
| OS `src/utils/platformPaths.cjs` | Hyphaの共有rootはmacOSのApplication Support、WindowsのLOCALAPPDATA。WindowsのRoamingと混同しない |
| Hypha `HyphaHoverHelpPreference.*` | 利用者共通の表示設定とバイナリ単位のcacheがある。ただし当該ファイルはhover help専用に全内容を書き換える |
| Hypha `PluginEditorInformation.cpp`、`PluginEditorMenu.cpp` | InformationとPOSTのPairメニューにDisplay入口がある。両方から同じ外観操作を呼ぶ |
| Hypha `HyphaHybridVuPainter.*` | 筐体素材とnativeの目盛り、針、数値が分離されている。Jungleは素材側の追加で実現する |
| Hypha `HyphaObservatoryWorld.*`、`HyphaObservatoryCapture.cpp` | 共通外周とAperture、Captureの再描画がある。外観snapshotを明示して引き継ぐ |

HyphaにJungle発動を受信する製品実装は、今回読んだsourceには存在しない。
既存のGuide接続やOSライセンス認識をJungle発動の証拠へ読み替えない。
本計画は外観設定用のローカル連携を追加し、`work.json`、PRE/POSTのwire schema、`identity.json`の権限モデルは変更しない。

### 2.1 通常版の画面確認

2026-09-11に保存済みのnative描画previewと現行sourceを照合した。
今回DAWを起動して現在の配置済みbinaryを確認したわけではなく、以下の画像を現行commitの全機能合格証跡とは扱わない。
古いfixtureの数値配置、Referenceの文字raster、Captureの内容欠落などを、新しい意匠の正本へ引き継がない。

| 確認した面 | 画像で確認した特徴 | 計画に反映する判断 |
| --- | --- | --- |
| POST VU 300/900、PRE VU 600 | 黒い筐体、曲面ガラスの反射、琥珀の縁、下端の有機組織。数値と針の周囲には暗い空間がある | 通常版の質感の基準。計器を作り直さず、他画面へ仕上げの規則を展開する |
| LEVELの全5サイズ | 小サイズは平坦な暗色panelとAperture。600/900では背景の菌糸、主値の端の組織、History下層が既にある | 世界観の不足より、筐体とpanelの一体感を先に改善する。小サイズへ全面背景を持ち込まない |
| TIME HISTORY、DRUM、SHARP、LIVE | 共通外周の内側に、history、標本、黒いplotなど異なる観測面が置かれている | 観測面の意味と個性は保持し、収納する枠と操作部を共通化する |
| FREQ全体とM/S部品 | 暗いplot、cyan/violetの連続線、操作列。M/S画像はshellを含まない部品preview | 線の色と精密さを保持。部品だけで全体の統一感を合格にしない |
| SPACE | 菌糸の外周、中央density field、右の独立した数値panel | fieldを装飾で補強せず、外枠と補助panelの仕上げを揃える |
| Reference部品、Capture | ReferenceはselectorとA/B操作の道具性が強い。Captureには親shellの背景と外枠が出る | Referenceの操作部と出力用frameまで同じ仕上げを適用する。別製品のような質感を残さない |

画像の調査元は次のローカル保存先である。
これらは一時保存物なので、実装前にP0で同一commitから再取得し、寸法、表示条件、hashとともに永続的な証跡へ保存する。

- `/private/tmp/hypha-typography-review.mtl6sl/`の`post-hybrid-vu-manual-{300,900}.png`と`pre-hybrid-vu-manual-600.png`。
- `/private/tmp/hypha-typography-final.6VBZkf/`の`post-domain-0-{300,375,450,600,900}.png`、`post-domain-{1,3}-900.png`、`post-{attack,sharp,live,freq}-900x600.png`、`capture-attack-1200x630.png`。
- `/private/tmp/hypha-ms-final-solid-900.png`と`/private/tmp/hypha-reference-review-900-v2.png`。

sourceでも仕上げの分散を確認した。
`HyphaHybridVuPainter.cpp`は埋込chassisと専用描画を使い、`HyphaObservatoryView.cpp`と`HyphaObservatoryMetrics.cpp`はpanelをそれぞれ描く。
Referenceにも`HyphaReferenceVisuals.cpp`、`HyphaReferenceMetricPainter.cpp`のpanel描画と専用button、selectorがある。
共通paletteは既に`HyphaTheme.h`へ集約されているので、新しい色体系を横に増やすより、素材と枠の共通責務を設ける。

## 3. 採用する視覚表現

### 3.1 通常版とJungleの共通規則

通常版の高級感は、暗さだけでなく、縁の反射と面の奥行き、文字の読みやすさを揃えることで作る。
以下の共通仕上げを通常版へ先に適用し、Jungleでも同じ構造を使う。

| 要素 | 新しい通常版 | Jungleで増すもの |
| --- | --- | --- |
| 筐体と外枠 | graphiteの基材、細い連続輪郭、上側からの弱い反射と下側の暗い縁 | 同じ縁の内側に透明な組織と琥珀の内部光。枠の幅や形は増やさない |
| 計測panel | 表面と内部の二段の暗部で、筐体に収まった測定窓として見せる | 観測窓の外側の深度だけを増す。plot中央の色とcontrastは維持 |
| 有機組織 | 現在の下端と角の素材を活かし、枠との接点、光の方向、明度を整理する | 同じ組織の内側が透け、奥の層が見える。巻き込みや面積の増加を主効果にしない |
| buttonとselector | 共通の縁、面、余白の規則。hover、pressed、selected、disabled、focusを読み分けられる | 状態色と操作感はそのまま。生物光を選択や接続の印へ転用しない |
| typography | 現行の書体、tabular数値、役割別階層を保持し、labelとunitの読みやすさを揃える | 書体、文字の大きさ、数値色は変更しない |

生態系の「加速」は静止素材の透過と内部光で表し、アニメーション速度、針の応答、解析頻度を変えない。
通常版を無彩色の廉価版にせず、Jungleを金色や緑色の全面washにしない。
VUの曲面ガラスを各グラフへ複製せず、グラフは平面の精密な観測窓として仕上げる。
描画用の素材tokenと測定や接続の意味を持つ色tokenを分離し、既存の`HyphaTheme.h`と文字契約を正本にする。

### 3.2 各画面への配分

| 面 | 通常版で整えるもの | Jungle時の差分と保持するもの |
| --- | --- | --- |
| Hybrid VU | 現在のfaceとchassisを基準に維持。共通ヘッダーと下段の計測窓、操作部の接続を整える | face下端と軸受け周辺の透明な組織、内部の琥珀光。筐体の構図、針、scale、TP rail、数値、全操作は維持 |
| 共通ヘッダー、Footer | VUと同じ暗部、縁の反射、Information入口、接続領域の仕上げ | 構造部分だけが深まる。PRE/POST色、Pair表示名、接続事実は維持。追加の常設badgeは作らない |
| LEVEL | 主値と補助値のpanel、channel strip、Historyの枠を一つの筐体として揃える | 既存の端の組織と共通外周を深める。文字、bar、履歴の下へ新たな強い素材を敷かない |
| TIME全体 | HISTORY、DRUM、SHARP、LIVEの観測窓と二階層目の操作を同じ仕上げへ揃える | 周縁だけを変え、DRUM specimenや波形の測定応答には外観設定を掛けない。RUN等の追加面も同じ枠を継承 |
| FREQ | shellとLR/MID/SIDE/M/S、Δ、MARK、Focus Trailの操作枠の仕上げを揃える | cyan/violet実線、probe、測定色、M/SとΔの排他、plotのgeometryを維持 |
| SPACE | density fieldの窓とbalance/correlation panelを同じ筐体へ収める | 共通外周だけを深める。densityや残響のように読める架空の光を中央へ加えない |
| Reference | 親shellに加え、A/B、selector、比較panel、接続案内の面と縁を揃える | 共通構造だけを継承。試聴状態、Bへの明示切替、Blindの開示境界は維持 |
| popup、tooltip、dialog | Information、Pair、Referenceの操作に同じ静かな基材と輪郭を使う | 機能的な色、focus、文字のcontrastを維持。装飾画像は載せず、OS標準file dialogは対象外 |
| Capture | 通常版の共通仕上げを出力frameと合成した解析bodyへ反映 | 開始時の外観を静止snapshotへ固定。表示名のopt-in、測定値、構図を維持 |

素材は数値、axis、目盛り、針の可動範囲と重ならないmaskで制約する。
利用可能なlabel、unit、説明文は背景との4.5:1以上の既存可読性契約を満たし、高級感のために薄くしない。
Inactive、Bypassed、PRE不在でも構造の素材は残すが、成立していない測定や接続の光は作らない。
PRESENCE overlay、M/S識別色、Guide、Record、警告、Blind中の開示に関わる値は変更しない。

### 3.3 全5サイズの扱い

| 基準サイズ | 通常版とJungleで共通の制約 |
| --- | --- |
| 300×200 | 生態系の意匠はHeaderのAperture一か所。panelとbuttonは軽いnative描画で仕上げ、全面画像や新しい欄は追加しない |
| 375×250 | 300と同じ構造。余白と線幅の既存scaleを使い、装飾箇所を増やさない |
| 450×300 | 詳細計測窓と外枠の反射を整える。通常版、Jungleとも全面背景とdomain固有の装飾を追加しない |
| 600×400 | 既存の角と下端の組織を筐体へ馴染ませる。Jungleはその内側を深める |
| 900×600 | 600と同じ素材配置で解像感を上げる。余った面積を装飾で埋めない |

VUは全5サイズで同じ3:2構図を保ち、縮小時は装飾の細部を抑える。
中央の測定領域を狭めたり、文字を縮めたりして装飾の場所を作らない。
色、反射、余白の規則を揃えることと、全画面を同じ情報密度にすることを混同しない。

### 3.4 試作と素材の扱い

承認済みの[通常版LEVEL試作](media/hypha_normal_level_concept_20260911.png)は、暗いgraphite、連続輪郭、抑えた琥珀反射の方向を確認した資料として残す。
実装はこの画像を背景へ使用せず、全サイズでnative描画する。
承認済みの[比較試作](media/hypha_jungle_concept_20260911.png)は、Jungle時の透明体と内部光の方向の参照として残す。
imagegenによる試作であり、左側も含めて新しい通常版の完成案やpixel比較正本にはしない。
試作内の小さい宣伝文、計器文字の再生成結果、独立した装飾アイコンは製品仕様として採用しない。
製品版は試作より菌糸の巻き込みと発光を抑え、透明体の厚みで差を出す。

通常版は既存のnative frameと素材を活かして仕上げ、全画面を別々の生成画像で作り直さない。
新素材はHypha向けに独立作成し、既存素材を上書きしない。
OSのプロプライエタリなコードや配布素材をGPLリポジトリへそのまま移植しない。
既存CE 2226文字画像には出典markがあるため使用せず、目盛りと文字はnative描画を維持する。

## 4. 保存の正本と連動ファイル

永続ファイルは次の三種類に分ける。
各ファイルは4 KiB以下のJSONとし、同一directory内の一時ファイルからatomic replaceする。
最後に検証したbackupを各一個保持する。

| ファイル案 | 書き手 | 読み手 | 意味 |
| --- | --- | --- | --- |
| `appearance/v1/os-hypha-masking-handoff.json` | OS mainのみ | OS main | MASKING GuideをHyphaへ一度正常送信した最小事実 |
| `appearance/v1/os-jungle-activation.json` | OS mainのみ | OS main、Hypha | 正規のJungle発動とMASKING送信の両方が一度成立した証明 |
| `appearance/v1/hypha-jungle-preference.json` | Hyphaの表示設定serviceのみ | 全Hypha | 初回受信、利用者の選択、互換予約field |

配置先は既存Hypha共有rootを使う。
macOSは`~/Library/Application Support/Kirin OS/plugin_data/`、Windowsは`%LOCALAPPDATA%/Kirin OS/plugin_data/`である。
実装は既存のplatform path規則を参照し、developer/test用の明示root injectionも両製品で揃える。
Project Folder、DAW state、ネットワーク共有、OSライセンスファイルへ外観設定を保存しない。

MASKING送信記録は`schema_version`、固定`kind`、検証済み`guide_id`、`sent_at`だけを持つ。
Work ID、曲名、MASKING内容、対象帯域は含めず、最初の正常送信事実を更新で上書きしない。

OSの発動記録は`schema_version`、`kind`、`activation_id`、`activated_at`、`jungle_activated_at`、`masking_guide_id`、`masking_sent_at`、`theme`を持つ。
`kind`は`kirin_os_jungle_activation`、`theme`は`ce2226`に固定する。
`theme`は連動素材の識別子であり、通常版がCE 2226ではないことを意味しない。
`activation_id`は初回に一度発行し、`activated_at`は二条件のうち後で成立した時刻と一致させる。
利用開始日、解放までの日数、曲名、Work名、認証情報、素材pathは含めない。
この記録は外観用であり、RecordやReferenceを解放する権限には使えない。

Hyphaの保存値は`schema_version`、単調増加の`revision`、`activation_seen`、`first_activation_id`、`choice`、互換予約fieldの`notice_acknowledged`とする。
`notice_acknowledged`は既存schemaを壊さないため保持するが、通知や由来表示の発生条件には使わない。
`choice`は`unset / on / off`の三値で、UIはON/OFFの二値だけを見せる。
受信前はOFF、初回受信後の`unset`はON、明示選択後はその選択を使う。
保存された希望状態と各editorの実際の表示状態を分け、素材準備と安全な適用時点が揃うまで現在の表示を保つ。
保留中のメニューは現在の表示と適用待ちを区別し、未適用を適用済みとして見せない。
発動記録のIDが回復処理で変わっても、保存済みの選択を初期化しない。

既存の`ui-preferences.txt`にJungle項目を追記しない。
現行hover help writerが未知の項目を消す実装なので、外観設定を別ファイルにすることで旧Hyphaとの共存を保つ。

## 5. Kirin OS側の発行条件

main processが検証した有効なOS権限、実際のJungle発動状態、MASKING GuideのHyphaへのpublish成功が揃った場合だけ発行する。
MASKING成功時は最小の送信記録を先にatomic保存し、現行profileを検証後に再読してAND条件を評価する。
Jungleが後から発動した場合は保存済みの送信記録を検証して同じAND条件を評価するため、成立順序へ依存しない。
通常の発見後発動、既存profileからの起動時復元、公開access操作、検証済みowner強制ONを同じ判定に通す。
解放条件成立だけ、発見前、無権限、Sense、単なるDOM変更では発行しない。
developer専用`FORCE_JUNGLE`は隔離test root以外へ発動記録を出さない。

publisherは起動後の権限確定、profile read/patch完了、現行rootの変更、owner設定変更から呼ぶ。
`main.cjs`の既存巨大領域へ新しい処理本体を埋めず、検証済みprofileと最終状態を受け取る小さいmoduleへ分ける。
外部profile更新は既存watcherで再読するが、通知だけを根拠にしない。
旧profileの読込経路、access操作経路、復元経路を一つの検証後投影へ集約する。

最初の発動記録は利用者単位で永続化する。
以後のProject Folder変更、OSを閉じる操作、owner強制OFF、権限の変化は過去の発動事実を削除せず、Hyphaの選択も変更しない。
未発動端末への新規発行だけをその時点の有効な条件で制御する。
既に発動済みの利用者は、新OSへ更新後の最初の検証済みprofile readで移行できる。

publisherの失敗はOSの発動成功を取り消さない。
当該起動中に上限付きで再試行し、その後も次回の検証済みreadまたは起動時に修復する。
連動ファイルの失敗を、Jungle自体の失敗や音声機能の失敗として表示しない。
OS側の発動画面へ新たな成功文を出す場合は、ファイル発行成功とHypha側の実適用を混同しない。

## 6. Hypha側の共有状態と競合

各plug-in binaryは同じプロセス内で一つの外観serviceを共有する。
editorは不変な`AppearanceSnapshot`だけを受け取り、paint中にファイル、lock、画像decodeへ触れない。
PRE、POST、AU、VST3、別DAW、sandbox process間は同じ保存ファイルを介して収束する。
インスタンスごとに専用workerやファイル監視を作らない。

外観serviceは少なくとも一つの可視editorがある間だけ低頻度の読取を予約する。
最後のeditorが閉じたら監視を停止し、次のeditor生成で一度再読する。
バックグラウンドjobは一個までとし、実行中のjobへ同種要求を積み重ねない。
UIとAudio Threadは待たず、遅いfilesystemでも直前のsnapshotで描画を続ける。

設定更新は明示的な操作として処理する。
`setChoice(on/off)`と`acknowledgeNotice`を分け、排他取得後に最新ファイルを再読して必要なfieldだけ変更する。
別インスタンスのOFFを古いsnapshotのONで戻すような、全体上書きを禁止する。
同時に異なるON/OFFを選んだ場合は、保存transactionの確定順に最後の明示選択へ収束する。

atomic replaceだけでは複数writerの取りこぼしを防げないため、短い更新transactionをOSレベルの排他で直列化する。
ロック取得と再試行はバックグラウンドで行い、即時取得に失敗したらUIを待たせず再予約する。
macOSのJUCE `InterProcessLock`は`fcntl`を使っているため、同一DAW内の別binary同士まで排他できるとは仮定しない。
採用するprimitiveは公式仕様を確認し、同一processの別moduleと別processの両方で排他試験を通してから使用する。
数十行のplatform adapterへ閉じ込め、音声経路やRust FFIへ導入しない。

破損、部分書込、未知schema、読取不能は直前の検証済みsnapshotまたはbackupを保持する。
未知schemaのprimaryを古い形式で上書きせず、その版では永続変更を行わない。
初回に検証済み状態を得られない場合は通常外観とし、受信済みを捏造しない。
明示ON/OFFの保存失敗はそのHyphaでの一時変更と保存失敗を短く知らせ、全インスタンス共有や再起動後保持を成功扱いにしない。
両方の設定ファイルとbackupを手動削除した場合の履歴回復までは保証せず、設定リセットとして扱う。

## 7. 連動表示と独立切替

初回ポップアップ、由来badge、外部状態を示す常設文言は表示しない。
発動条件はKirin OSのJungle発動とMASKING Guide送信成功のANDのまま保持するが、その内部契約を計器面へ説明表示しない。

発動後だけ既存Displayメニューへ`Jungle Mode`のチェック操作を追加する。
利用者がON/OFFを選ぶとPRE/POSTと全インスタンスで共有し、Blind中だけ現在の表示を固定する。
明示操作は再生中も受け付け、保存に失敗した場合はそのセッションだけの変更であることを短く通知する。
自動適用は再生、録音、Blind、他の操作dialog中を避け、作業を妨げない時点まで保留する。
未発動時はJungleの項目、無効ボタン、解放条件を表示しない。
hover helpをOFFにしていても、既存Displayメニューから切り替えられる。

## 8. 状態遷移の合格表

| 状況 | 必須の結果 |
| --- | --- |
| OS未導入、未発動、旧OS | 質感を統一した新通常版。Jungle入口を出さず通常計測を続ける |
| 解放可能だが未発動 | Hyphaを切り替えない |
| Jungle発動済み、MASKING未送信 | 通常外観を維持し、発動記録を作らない |
| MASKING送信済み、Jungle未発動 | 通常外観を維持し、送信の最小事実だけを保持する |
| 二条件成立、Hypha未起動 | 発動記録を残し、後日の起動で受け取る |
| 発動時に複数Hyphaが表示中 | 各editorが共有snapshotを読み、安全な時点で同じ外観へ収束する |
| 再生中、録音中、Blind中 | 初回の自動変化を保留し、終了後に再判定する |
| 利用者がOFF、その後OSから再通知 | 新通常版を維持し、利用者選択を上書きしない |
| OS終了、権限変更、Project Folder変更 | 保存済みの外観選択を維持 |
| 新しいPRE/POST instance、DAW再起動 | 保存済みの共有選択を読み、DAW stateから逆に上書きしない |
| 旧Hyphaと新Hyphaの混在 | 旧版は通常表示。新しい外観設定を旧hover help writerから保護 |
| UIを開かずoffline render | 外観serviceを起動せず、通常A経路を維持 |
| 素材欠損またはdecode失敗 | 通常素材へfallback。明示選択の保存失敗だけを通知 |
| 発動記録欠損、読取失敗 | 保存済み選択を消さない。新規発動は確認できるまで待つ |
| Capture中に別instanceで切替 | 一枚の中で外観を混在させず、Capture開始時のsnapshotを使う |
| 監視jobの終了 | 音声と計測は継続。次のeditorで読み直せる |

## 9. 変更対象と分離順序

以下の新規名は責務の配置案であり、既存の同等moduleが見つかれば再利用する。

| 責務 | 対象 |
| --- | --- |
| OSの検証後投影 | `main.cjs`のprofile入口、起動後権限確定、owner設定入口、MASKING publish成功後。`src/utils/hyphaJungleProjection.cjs` |
| OSの判定共通化 | `src/App.jsx`、`src/utils/jungleOwnerOverride.cjs`、`src/hooks/useJungleDiscovery.js`周辺。既存発動条件を保持して共通の純粋判定へ寄せる |
| OSのpathと回復 | `src/utils/platformPaths.cjs`、既存atomic writer、profile lifecycle試験。発動記録専用schema fixtureを追加 |
| Hyphaの契約とservice | 新規`appearance/AppearanceContract.*`、`AppearanceService.*`、`AppearanceStorage.*`、小さいplatform排他adapter |
| editorの配線 | `PluginEditor.h`、`PluginEditor.cpp`、`PluginEditorObservatory.cpp`。新規`PluginEditorAppearance.cpp`へcallbackとlifecycleをまとめる |
| メニュー | `PluginEditorInformation.cpp`、`PluginEditorMenu.cpp`、Reference内のInformation入口。共通menu builderとactionを使用 |
| 外観snapshot | `HyphaObservatoryView.*`、`HyphaObservatoryWorld.*`、`HyphaHybridVuPainter.*`、`HyphaObservatoryCapture.cpp`、`PluginEditorCapture.cpp` |
| 共通仕上げ | 新規`HyphaSurfaceMaterial.*`へ素材tokenとframe描画を分離。`HyphaTheme.h`、`HyphaUiContract.h`、文字契約を参照し、意味色は保持 |
| 通常版panelと操作 | `HyphaObservatoryMetrics.cpp`、`HyphaObservatoryButton.cpp`、`HyphaObservatoryViewFooter.cpp`、`HyphaInformationButton.h`。共通仕上げを既存geometry内へ適用 |
| Referenceとpopup | `HyphaReferenceComponent.cpp`、`HyphaReferenceVisuals.cpp`、`HyphaReferenceMetricPainter.cpp`、`HyphaReferenceControls.cpp`、`HyphaReferenceSelectorLookAndFeel.h`、`HyphaReferenceAccessPanel.h`、`HyphaTooltipLookAndFeel.h`、既存Pair menuのLookAndFeel |
| 解析面との境界 | `HyphaSpectrumChromePainter.*`、`HyphaSpectrumMagnitudeChrome.h`、`HyphaAttackComponent.cpp`、`HyphaSpacePainter.cpp`等の枠と操作描画。測定painterとspecimenの意味を変更しない |
| Jungle差分と素材 | 新規`HyphaJungleMaterialPainter.*`と専用素材。通常版の共通仕上げを使い、第二の独立したUI実装を作らない。既存chassis正本は保持 |
| build一覧 | `juce_shell/CMakeLists.txt`のPRE/POST、native test、preview、BinaryData一覧 |
| 検証 | nativeの新規appearance suite、`HybridVuContractTest.cpp`、`ObservatoryViewContractTest.cpp`、Capture、Information、OS publisherとmigration試験 |
| 文書 | README、Hypha visual system、製品表示契約、不変条件、OS Jungle lifecycle文書、両repoのprotocol参照 |

`PluginEditor.cpp`は500行、`HyphaHybridVuPainter.cpp`は479行、`HyphaObservatoryView.cpp`は472行である。
対象責務を先に500行以下のmoduleへ分けてから機能を配線する。
既存巨大sourceを増やさず、減少時は同じcommitでline-budget baselineを下げる。
OSの`main.cjs`と`src/App.jsx`にも新しい責務を埋め込まない。
通常A経路、DSP、Rust measurement、FFI ABIは変更不要な構造とする。

## 10. 軽量性の予算

以下は未測定の実装予算であり、達成済みの数値ではない。
予算を超えた場合は装飾素材とcache構造を見直し、計測更新頻度や精度を下げて合わせない。
比較元は変更前の現行版に固定し、新通常版とJungle版をそれぞれ同じ比較元から測る。
以下は通常版刷新とJungle追加を合わせた予算であり、二工程へ別々に与えて積み増さない。

| 項目 | 合格条件 |
| --- | --- |
| Audio Thread | 外観用の処理、allocation、lock、I/Oがゼロ。追加latencyは0 samples |
| 解析 | FFT、測定worker、測定要求の追加ゼロ |
| 常時動作 | 装飾用animation timerと独立repaintゼロ。信号の既存更新は従来どおり |
| 連動読取 | 可視editorがある各binary/processにつき最大1回/秒。同時job一個、変更なしならJSON再parseなし |
| 非表示 | 可視editorがゼロなら周期読取と外観job予約ゼロ |
| 反映時間 | 正常なローカルFSで共有設定保存後2秒以内。再生等による自動適用保留は別に測る |
| 素材decode | 初回Jungle適用前に共有cacheへdecodeし、そのcacheの存続中は再利用。paint中decodeと毎frameの拡縮cache生成をしない |
| 通常外観 | 既存のnative枠描画の置換と共有素材の再利用を基本にする。未発動時はJungle素材をdecodeしない |
| 追加描画時間 | 900×600の変更前の現行版比で平均+0.5 ms/frame以内、backing scale 2では+1.0 ms/frame以内。既存各surfaceの絶対予算も維持 |
| 追加メモリ | decode済み共通素材は24 MiB/binary以内。現在寸法の追加cacheは16 MiB/可視editor以内。全5サイズのcacheを常駐させない |
| 追加CPU | 同条件の変更前の現行版比で、一画面時+0.2 percentage point以内、最大二画面時+0.5以内。1 logical coreを100%として測る |

通常版のpanelごとに高解像度bitmapを持たず、既存の描画passで短いnative輪郭と面を描く。
新しい静的仕上げcacheのkeyは寸法、backing scale、素材revision、外観状態に限り、LUFSやbalanceの更新で再生成しない。
現行`Backdrop::drawDomainBed`はenergyとdirectionの変化でrasterを作り直すため、新しい静的仕上げをそこへ混在させない。
既存の実測応答を持つ世界層は保持し、今回の外観追加を理由にその動きやPRESENCE値を変えない。
Jungle素材は共有sourceから必要な一寸法だけをcacheし、resizeでは最新要求だけを処理する。
OFFでも再ONのため共有decode結果を保持できるが、最後のeditorが閉じたら追加画像cacheを解放する。
decode失敗はcacheし、定期読取のたびに再試行しない。
素材revisionの変更または利用者の明示再試行でのみ準備をやり直す。
OS→Hyphaの外観状態を毎音声blockや既存PRE共有メモリへ載せない。
一画面、二画面、多数の閉じたinstance、停止中、10分連続再生で差分を測る。

## 11. 実装工程と検証

| 工程 | 作業 | 出口 |
| --- | --- | --- |
| P0 | 現行commitの描画と性能baseline、保護maskを採取。VUを基準に新通常版のLEVEL 600、Compact 300、Reference 900とVUの比較を用意。保存契約と排他probeを先に確かめる | 通常版の具体的な仕上げをDaisukeが確認。5サイズの構図と保存境界が成立する |
| P1 | 共通素材とframeを先行分離し、新通常版を全対象へ適用。OS publisher、MASKINGとのAND条件、既発動移行、Hypha共有serviceも分離実装 | 通常版だけで全画面の品位と機能が成立。二条件の順序逆転、OFF保持、破損、同時更新が対象試験でpass |
| P2 | 仕上がった通常版と同じ構図からJungle差分を作り、VU、共通surface、Captureへ接続 | 新通常版とJungleを全5サイズで比較。同じ計器の生命感が増し、測定の読みやすさは維持 |
| P3 | 連動表示を追加せず、既存Displayメニューの独立ON/OFF、保留条件、保存失敗表示を配線 | 即時切替、Blind保護、共有保存、再起動保持がpass |
| P4 | 現行版、新通常版、Jungleの比較を同じfixtureと条件へ集約 | 本計画の状態表と合算の性能予算を全件満たす |
| P5 | macOS/Windowsの実ホストでOS発動から独立切替まで確認 | 各製品のcommit/build ID、起動順、ON/OFF、再起動、5サイズの証跡を記録 |

開発中は変更した責務の試験だけを実行する。
最終候補の全体gateは既存CIまたは標準scriptに一度集約し、同じsuiteを別コマンドで重ねない。
失敗や追加変更があれば、その影響箇所だけを再検証する。
OS側の全体baselineも対象試験が安定した後の一回とし、機能ごとに広域testを反復しない。

native試験ではPRE/POST、AU/VST3共通shell、5基準サイズ、density境界、日英、Retina、Windows 100/125/150/200% DPIを含める。
画面比較は変更前の現行版、新通常版、Jungleの三つを区別する。
新通常版の意図した構造pixel変更は許可領域として記録し、画面全体のpixel不変を要求しない。
測定線、数値、axis、状態色とhit領域は保護maskで比較し、文字の背景contrastも確認する。
Jungleとの差分を素材の許可領域へ限定し、OFFで新通常版の同一snapshotへ戻ることを確認する。
共通部の確認だけで終わらず、popup、tooltip、Referenceのselectorと接続案内、Captureまで仕上げの取り残しを確認する。
メニューだけでなく、外部analysis bodyを合成したLEVEL、TIME各面、FREQ、SPACE、Reference、Captureを確認する。
fixtureはActive、Inactive、Bypassed、Guide有無、Pair欠損、素材欠損を含める。

競合試験はPRE/POST別module、AU/VST3混在、別processのON/OFF、closeとOFF、owner異常終了、時計変更を扱う。
保存試験はprimary/backup各欠損、破損、未知schema、read-only、再起動、旧OS/旧Hypha混在を扱う。
OS側は発見、公開access、owner override、起動時復元、root変更、失敗後再試行を一つの発行結果へ照合する。

Rustを変更しない限り、新たなFFI検証を増やさない。
実装上Rust/FFI変更が必要になった場合は責務の置き場を再検討し、実際にFFIを変更するならrepo規約どおりignored parityとpairing_candidatesを実測件数で全件実行する。
既存B-812の全体testにある旧文字列依存のxtask失敗二件は開始時baselineに記録し、新機能のpassと混同しない。

## 12. 実装着手と完了の境界

今回の到達点はHypha通常版の共通surface、Kirin OSの二条件publisher、Hyphaの共有外観service、全5サイズのJungle差分実装と対象native検証である。
連動ポップアップは採用しない。実ホストと配布はまだ変更していない。
既存のdirtyなJUCE submodule、別件handoff、既存build directoryの内容は変更対象に含めない。
Notionへの書込みも行わない。

実装完了には通常版全体の質感統一、両製品の互換契約、全5サイズの描画、独立切替、合算の性能予算、macOS/Windowsの実ホスト証跡を揃える。
未実行を次工程へ送っただけでは完了扱いにしない。
Windows操作前にはOS側`docs/windows_validation_remote_access.md`を読む。
公開する場合は別途既存release runbookに従い、HyphaのLS PKG、macOS無料ZIPとGitHub Releaseと英日HP、同じ版の署名済みWindows installerを揃える。
release build、公証、配置を行うセッションのLSパッケージ準備も省略しない。

次の着手点はP4の合算性能確認とP5の実ホスト検証である。
通常版の共通surface、P2のJungle差分、Displayメニューの独立切替を固定し、同一fixtureで性能予算と復元を確認する。
追加の外観生成や全体testを大量に繰り返さず、最終候補で必要なgateを一度だけ実行する。
