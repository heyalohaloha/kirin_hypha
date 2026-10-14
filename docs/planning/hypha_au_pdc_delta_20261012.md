# AU の PDC と FREQ / SHARP / PSB の比較時計

## 原因と根拠

コードで確認した原因は、AU が deferred analysis に入らず、FREQ と Perceptual に
`project position + output presentation latency` を渡していたこと。この座標の一致は
同じ入力音の一致を保証しない。補償済み project position の PRE 側に後段の遅延だけが
加わると、離れた PCM を join する。変化する合成音と4096 samples の実遅延を使う
FFI 対照試験で偽差を再現する。定常音だけを正解値にしない。

公式資料で確認した区別:

- [Apple contextPresentationLatency](https://developer.apple.com/documentation/audiotoolbox/auaudiounitbus/contextpresentationlatency):
  出力から提示までの遅れを host が知らせる。0 は遅延なしと不明の双方を意味する。
  AUv2 の PresentationLatency へ bridge される。plugin 自身の処理遅延とは別の値。
- [Apple AURenderBlock](https://developer.apple.com/documentation/audiotoolbox/aurenderblock):
  render timestamp は入力を render する時刻。下流のレート変換等で device sample 時刻との
  相関が定義されない場合もある。単独で異なる insert の共通 content 座標とはみなさない。
- [Apple AUHostTransportStateBlock](https://developer.apple.com/documentation/audiotoolbox/auhosttransportstateblock):
  currentSamplePosition は host の timeline 上の sample 位置。
  [transportStateBlock](https://developer.apple.com/documentation/audiotoolbox/auaudiounit/transportstateblock)
  は AUv2 の HostCallback_GetTransportState 系に bridge される。

既存 JUCE patch は AU の host transport 位置と `mSampleTime` を別々に保存し、
render の有効flag・有限整数性と presentation latency の単位変換を検査している。
この wrapper は変更しない。これらの仕様は Logic の個々の PDC 設定での通知内容を
保証するものではない。個々のhostの二重計上は、同一callbackの両roleの通知値と
PCMを照合するまで推定に留まる。対照試験で確認した条件と実hostの通知内容は区別する。
LISTEN が開始できたという事実だけを PCM 一致の受入とはしない。

## 修正の境界

全 AUv2 host の FREQ / paired SHARP / paired PSB を、既存 LISTEN 準備と同じ callback の
PRE snapshot、owner、generation、K に束縛する。PRE は local render 座標、POST は
`render - K` を解析する。K は原点間の変換なので、AU に限り負の K も checked subtraction
で扱う。AAX の非負 K と bound、認定 VST3 の K=0 条件は維持する。

- local AU は valid な render と独立した project の連続移動を要求する。
  AU wrapper が project を render timestamp で代用した場合、比較用 projectValid を
  false にする。LISTEN も偽の線形証明を作らない。Record の時計と入力は変えない。
- AU pair は observer の linear 証明、または初回 loop の presentationLatency 証明だけ。
  後者には両側の AU 通知、正の PRE−POST 差、実測した周回、一意な対応が必要。
  linear から同一設定の loop に入るときは observer が裏付けた既知 K だけを保持する。
  contentClock / boundedEngine を AU へ借用しない。host 名による追加認定はしない。
- 失効時は同 callback で古い Δ と表示 lease を無効化する。原音 scratch の deferred
  transaction を一度だけ使い、音声出力・Watch・Record・保存 state を変更しない。
  RT に alloc / lock / I/O を追加しない。画面文言は変更しない。
- private analysis snapshot に `AudioUnitRender` 種別を追加し、Presentation / Project /
  Vst3Content / AaxEngine との混合を拒否する。旧版は未知の種別を拒否するため、異なる版の
  PRE/POST が偽差を出す fallback はない。保存した v1.1.50 state の形式は変更しない。
- 証明の取れない AU は差分を待つ。両roleのlocal FREQは、そのcallback自身の有効な
  projectまたはrender座標で観測できる。独立projectがなくてもLocalRenderとして保持し、
  PRE exchangeへ公開せずΔへ昇格させない。絶対SHARP／LIVE／absolute PSBは従来経路。
- Cycleの案内は確立済みKの有無で決める。metadataの書込み競合や折返し直後の一時的な
  evidence欠損だけではLISTEN／BLINDを止めない。各PCM blockの出力はrendererが検査し、
  Δと初回PCM取得には同callbackの有効なevidenceを引き続き要求する。

## 残る host / format の経路

| 経路 | FREQ / SHARP / PSB の Δ | 危険と扱い |
|---|---|---|
| AUv2、全 host、macOS | render−K 証明 | 証明なしは待機。実 host の PDC 前提は別受入 |
| 認定済み Studio One 6〜7／Studio Pro 8 VST3、macOS / Windows | content−K、K=0 | 認定と実 PCM の不一致は時計だけで完全検出できない |
| 認定済み REAPER VST3、macOS / Windows | 補償済みproject、K=0 | 全ての読取可能な版が対象。実測版は7.71。測定認定でありLISTENの許可ではない |
| その他の JUCE VST3、macOS / Windows、通知あり | position + output latency | 同様の二重計上による偽差が出得る。対照 fixture で条件を再現、全 host の発生実測ではない |
| その他の VST3、通知なし | FREQ は project join、SHARP / PSB Δ は不可 | 加算経路はないが project だけの FREQ も PDC content 一致の独立証明はない |
| AAX、macOS / Windows | SHARP / PSB は engine−K。FREQ は project join | wrapper は presentation を通知しないため加算経路はない。FREQには独立したPDC証明を追加していない |
| legacy nih-plug VST3 | 旧 identity / state 互換経路。出荷の JUCE optional analysis ではない | Record の presentation 情報を Δ の証明に読み替えない |
| AUv3 / standalone | 現行出荷 format ではない | AUv2 対応を認定として流用しない |

未認定 VST3には従来の座標規則が残る。continuous clockを必須にする規則とは異なり、
出力遅延の通知がないFREQもproject joinを使う。REAPERの補償済みproject認定とは区別し、
未認定hostの遅延付き挿入を挟んだΔが確認済みとは扱わない。

## content のループ端

`corroborateLoop` の認定 VST3 専用 clamp 裏付けは、正の presentation latency 差を要求する。
missing / omitted では clamp 中に証明が失効し、`note_analysis_clock_drop_from_audio` が
古い表示を捨てる。Perceptual はさらに PRE/POST 共通の future epoch を再取得する。
時計単体 fixture（48 kHz、256 frames、4096 samples delay）では最大24 block = 128 ms の
proof gap、included では0 block を確認する。製品の SHARP 停止時間とは別の計測。

認定 content 時計だけから clamp の欠けた遅延範囲を推定すると、LISTEN と共有する
裏付け規則を緩める。解析専用 observer の複製も証明を二重管理するため行わない。
製品ロジックは維持し、試験の aggregate `observed > 300` を廃止する。各 callback の
偽差ゼロ、証明失効時の即時不可視、各 wrap 後1.5秒以内の復帰を検証する。
1.5秒は既存 request lease の期間であり、次の2秒 loop まで放置する試験ではない。
各々の最低2周回を検査し、最後の周回も復帰まで確認する。平均や合計で欠落を隠さない。

## 検証入口

`analysis_clock_au_test.cpp` は delay PCM、render 原点差、通知4種、linear / 初回loop /
linear→loop、失効・異種証明拒否を共有メモリなしで検査する。
48 kHz、256 frames、4096 samples delayの非clamp fixtureでは、初回証明後の最長空白は
0 blockであり、同じ上限をassertする。初回の較正・周回観測待ちは分けて数える。
`AuCycleProofTest.h`も初回証明後0 blockを要求し、metadata書込み競合と折返しを跨いだ
LISTEN／BLINDの継続と実PCMを検査する。総受理数だけで長い欠落を見逃さない。
`analysis_product_content_test.cpp` は AU と認定 VST3、mono/stereo、通知4種で実共通 processor、
実 PRE timing mapping、FREQ / SHARP / PSB、元音の bit identity と0 latencyを検査する。
AU の独立 project 欠損も検査する。shared memory 不可の環境では代替で合格にしない。
Rust FFI / worker / file exchange は AU の独立種別、原音遅延、証明喪失と復帰、混在拒否を検査する。

```sh
cmake --build target/hypha-release-native-tests --config Release --target \
  KirinAudioUnitAnalysisClockTests KirinContentAnalysisClockTests \
  KirinContentAnalysisProductTests KirinAaxAnalysisClockTests KirinAaxAnalysisProductTests
ctest --test-dir target/hypha-release-native-tests -C Release --output-on-failure \
  -R '^kirin_(au_analysis_|content_analysis_|aax_analysis_|local_blind_pdc_validation_delay|live_compare_(timing|correspondence|loop))'
```

Logic の受入では、同じ未署名差分の PRE/POST、mono と stereo、平らな遅延付き処理で
LISTEN を始める前の三つの Δ、通常再生、初回loop、stop/play、遅延 bypass、保存再openを確認する。
証明失効中の空白と偽の数値を区別する。sample rate、両側の FFT、既存 clock probe を採取すれば
通知の二重計上と host 固有の clamp を切り分けられる。fixture と未署名 build は実機受入を代用しない。
