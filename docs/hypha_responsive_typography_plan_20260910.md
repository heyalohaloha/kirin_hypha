# Hyphaの全画面表示を統一する実装計画

作成日：2026-09-10。
調査基準：`c369e8f7f4ec34ff516961d7d0d54cdb16450564`（B-794）。
状態：計画作成済み、製品コードの実装は未着手。

文字の役割、サイズへの追従、余白、情報の配置、収まらない場合の処理を共通契約にまとめる。
各画面はその契約から表示を選び、新しい画面にも同じ制約が働く構造を作る。
主値、補助値、軸、説明文には役割に応じた大小を残す。
同じ役割の読みやすさと、画面を移った時の操作の一貫性を完了基準にする。

## 1. 調査の根拠と適用範囲

既存コードでは、次の決定が分散している。

| 決定 | 現状の根拠 | 計画での対処 |
| --- | --- | --- |
| 表示密度 | `HyphaObservatoryLevelStrips.cpp`の`currentPreset()`、`HyphaObservatoryCapture.cpp`の`capturePreset()`、各Componentの幅や高さ判定 | 用途を指定した共通の表示コンテキストを入口で生成する |
| フォント高 | `HyphaTypography.cpp`で11未満を11へ補正し、Painterではそれ未満の値も指定 | 読みやすさの下限を共通契約へ移し、指定と解決後の値を一致させる |
| 拡大率 | `HyphaSpectrumUiContract.h`の連続倍率、`HyphaAttackUiContract.h`の幅別倍率、Referenceの詳細表示分岐、Blindの高さ別分岐 | 文字用の共通スケールを用意し、グラフの座標計算から独立させる |
| 配置と余白 | Headerには密度別の寸法がある一方、各Painterが行高やpaddingを個別指定 | 文字高と一緒に行高、間隔、整列規則を解決する |
| 収まり方 | `drawFittedText`、Labelの設定、`drawTabularText`で動作が異なる | 役割ごとに折返し、省略、完全表示を定義する |
| 検証 | 個別の寸法、文字幅、画像差分、状態遷移はあるが、全画面の役割対応表がない | 同じ製品描画を使う全画面一覧と役割単位の検証を追加する |

前回レビューの文字サイズ表は、主にコード上の指定値を比較したものだった。
LEVELの主値には残り高さによる制限があり、Referenceの通常比較値にも領域高さによる独自倍率がある。
11という下限もフォント生成時の値であり、その後の文字圧縮や描画領域からの欠けまで防ぐものではない。
したがって、移行前の記録には指定値だけでなく、解決後のフォント、実描画領域、圧縮の有無を含める。

既存の`reference_preview_contract.png`は製品Componentを使ったReferenceのプレビューであり、実DAWの全状態を確認した記録ではない。
公開画像も撮影条件とbuildが異なるため、最終合否には同一候補から生成した画像と実ホストの表示を使う。

対象は出荷JUCE shellのPREとPOST、macOS AUとVST3、Windows VST3である。
Reference preview rendererとCaptureは同じ描画部品を利用するため、移行対象へ含める。
旧Rust editorの刷新、測定定義の変更、新機能の追加はこの計画に含めない。
通常音声、Blindの秘匿と通常復帰、VUのCLEAR、RecordとKeepの契約は既存のまま維持する。

## 2. 共通契約の責任分担

次の境界を設ける。
ファイル名は実装時の配置案であり、表の責務を既存の巨大ファイルへ集約しない。

| 境界 | 入力と責務 | 配置案 |
| --- | --- | --- |
| 表示コンテキスト | editorの論理寸法、経験系統、表示密度、出力用途を保持する | `HyphaPresentationContext.h` |
| 文字と間隔の契約 | 役割別のフォント高、行高、余白、整列、桁配置、overflow方針を解決する | `HyphaTypographyContract.h`、`HyphaLayoutTokens.h` |
| 画面別の構成 | 主値と補助値の配置、表示する情報、利用する文字役割を定義する | `HyphaSurfacePresentation.h`と責務別の小モジュール |
| JUCE描画への変換 | 書体の選択、文字幅の計測、折返し、描画、LabelやLookAndFeelへの適用を担う | `HyphaTextStyle.h/.cpp`、既存`HyphaTypography.cpp` |
| 製品画面と検証 | 同じコンテキストと描画結果を使用し、検証時だけレイアウト情報を記録する | 各Component、Painter、専用のUI検証入口 |

表示コンテキストはeditorのrootで一度決め、Header、body、Footer、子画面へ明示的に渡す。
body幅やカードの高さから子画面がeditorの密度を再判定する処理をなくす。
子領域の寸法は、その領域内での配置や折返しを計算するために使う。

コンテキストと解決済みstyleはinstanceごとに所有する。
共有LookAndFeelへ「現在のサイズ」を書き込む設計は採用しない。
異なるサイズのPREとPOSTを同時に開いた場合も、一方のリサイズが他方の文字に影響しないようにする。

既存の5プリセットを文字スケールの基準点にし、有効な中間寸法も同じ関数で解決する。
同じ構成が継続する範囲では文字と間隔を連続的に変化させ、情報の追加やナビゲーションの折畳みは共通の密度境界で行う。
CompactとObservatoryという二系統の表示思想、900×600で履歴や軸の面積を増やす方針を保持する。
OSやホストのDPI倍率とeditorの論理寸法は別に扱い、文字を二重に拡大しない。

Captureは出力用途を指定したコンテキストから論理レイアウトを作り、最後に画像倍率を一度だけ適用する。
横長、正方形、縦長の出力には既存のCapture構成を使い、3:2のeditor条件を強制しない。
PopupMenuとTooltipにも用途を与え、別windowの可読性をeditor倍率だけで決めない。

## 3. 文字階層とレイアウトの規則

画面に出る文字を、利用者が読む目的で分類する。
実装では、この分類を**文字役割**として指定する。

| 文字役割 | 対象 | 共通に管理すること |
| --- | --- | --- |
| `shellTitle`、`navigation` | PRE HYPHA、domainと下位tab | サイズ、位置、選択状態、操作領域 |
| `primaryValue`、`secondaryValue` | 即読する主値、補助の数値 | 大小関係、桁セル、符号、小数点、単位との間隔 |
| `metricLabel`、`unit` | 測定名と単位 | 数値との対応、整列、行高 |
| `axis`、`legend`、`readout` | 目盛り、凡例、hoverの値 | 最小可読性、グラフとの間隔、情報の優先順位 |
| `sectionTitle`、`body`、`status` | ReferenceやBlindの説明と状態 | 見出しと本文の大小、行間、折返し |
| `action`、`selector` | 開始、復帰、CLEAR、各選択操作 | 意味に応じた強調、文字と操作領域の余白 |
| `menu`、`tooltip`、`captureMetadata` | 補足と画像の付帯情報 | 出力用途に応じた読みやすさと表示領域 |

LEVELの主値、グラフに添える現在値、VUの目盛りは同じ大きさにそろえない。
必要な差は「主値を中心に読む面」「グラフと共に読む面」「操作と説明を読む面」「VUの計器面」という少数の構成に定義し、共通の役割表に紐付ける。
画面名ごとに任意の倍率を足す例外表は作らない。
新しい構成が必要になった場合は、読み方の違いと全サイズでの比較画像を同じ変更へ含める。

文字サイズの数値表は、移行前の同条件レンダーと実書体の計測から作る。
対象の全画面を見た上で基準値を決め、5サイズでの役割表を共通契約から生成する。
既存の指定値をそのまま名前付き定数へ置き換えるだけでは移行完了にしない。
現在のフォント高11（JUCEの論理座標上の高さ）を移行中の最低基準にし、役割に応じて基準を引き上げる。

行高、ラベルと数値の間隔、単位の位置、panel内の余白も文字役割と同時に解決する。
同じ構成内では整列位置をそろえ、画面遷移だけで主操作の位置や見出しの強さが変わらないようにする。
データ座標、周波数軸、波形、VUの針などの計算は各可視化に残し、文字の周囲に必要な領域を共通レイアウトから受け取る。
背景素材とPRESENCEの調整値は維持し、文字のための負空間を確保する。

収まらない場合の処理は、文字役割に明記する。

- 測定値、符号、単位、開始や通常復帰に必要な操作は、許容される最大桁を確保して完全表示する。
- 説明文と重要な状態表示は、行高を確保して折り返す。
- 利用者が付けた長い名前は、既存の全文確認手段と対応する省略表示を使う。
- Compactで折り畳める補助情報は、既存の製品契約に沿って配置を切り替える。
- フォントの横圧縮や、値が変わるたびに文字サイズを縮める処理は使わない。

収まりの失敗は開発用レイアウト診断へ記録し、製品UIへ内部エラーを追加しない。
BlindのPRE/POST割当は、Reveal前に画面、Tooltip、accessibility、全文表示のいずれにも流さない。

## 4. 移行対象の一覧

実装開始時に、以下の単位で役割、状態、サイズ、描画入口を一覧化する。
動的に表示する画面と、現在は使われていない旧描画を呼出し元まで確認し、除外には理由を残す。

| 対象 | 主な既存ファイル | 確認する表示 |
| --- | --- | --- |
| 共通shell | `PluginEditor*`、`HyphaObservatoryView*`、`HyphaObservatoryLevelStrips.cpp`、`HyphaObservatoryContract.h` | Header、Pair名、Guide、domain、下位tab、Footer、待機と通知 |
| LEVEL | `HyphaObservatoryMetrics.cpp`、`HyphaObservatoryLevelStrips.cpp`、`HyphaCaptureHistoryPainter.cpp` | 絶対値、差分、2MIX、TRACK/STEM、Historyとchannel strip |
| HISTORYとRun Summary | `HyphaTimeHistoryPainter.cpp`、`HyphaRunSummary.cpp` | 軸、凡例、時刻、現在値、停止後の一覧 |
| ATTACK | `HyphaAttackComponent.cpp`、`HyphaAttackUiContract.h`、`HyphaAttackMetricPainter.cpp`、`HyphaAttackDetailView.cpp` | DRUM、選択event、説明、未成立時 |
| SHARPとLIVE | `HyphaPerceptual*`、`HyphaAbsolute*` | 絶対値と差分、単位、凡例、停止と欠測 |
| FREQとPSB | `HyphaSpectrum*`、`HyphaPsbPainter.cpp`、`HyphaGuideFrequencyOverlay.cpp` | channel、MARK、Focus Trail、hover、Guide、PSB |
| SPACE | `HyphaSpacePainter.cpp` | field、balance、correlation、mono、warming |
| Reference | `HyphaReferenceComponent.cpp`、`HyphaReferenceVisuals.cpp`、`HyphaReferenceSelectorLookAndFeel.h` | 選択、通常比較、複数のchart、Reference Blind、復帰 |
| Reference接続案内 | `HyphaReferenceAccessPanel.h` | 未所有、所有済み、未接続、再確認 |
| Local PRE/POST Blind | `HyphaLocalBlindComponent.cpp`、`PluginEditorLocalBlind.cpp` | 取得、準備、減衰承認、Source、回答、Reveal、中断、通常復帰 |
| Hybrid VU | `HyphaHybridVuPainter.cpp`、`HyphaObservatoryViewLayout.cpp` | 計器の構図、目盛り、三値、VUとCLEAR、手動とRecord |
| 共通操作と補足 | `HyphaWidgets*`、`PostControls*`、`HyphaTooltipLookAndFeel.h`、`PluginEditorInformation.cpp` | ボタン、名前入力、選択menu、情報menu、Tooltip、keyboard focus |
| 画像出力 | `HyphaObservatoryCapture.cpp`、関連Capture描画、`tools/ReferencePreviewRenderer.cpp` | 出力寸法、付帯情報、実画面との役割共有 |

共通の書体入口は`HyphaTheme.h`と`HyphaTypography.cpp`を整理する。
`HyphaUiContract.h`と`HyphaSpectrumUiContract.h`の文字定数は新契約へ移し、幾何や測定に必要な定義を残す。
`HyphaObservatoryPresentation.h`とサイズ契約は、新しい表示コンテキストと重複する判定を統合する。

新規owned sourceは500行以内とする。
既存の500行超テストへ手を入れる場合は、今回必要な責務を先行コミットで分離し、baselineを下げる。
500行に達している`PluginEditor.cpp`も、表示コンテキストの配線を責務別ファイルへ置いて肥大化を防ぐ。

## 5. 再発を検出する仕組み

製品の描画側には、役割とコンテキストを受け取る型付きAPIを公開する。
任意の数値を受け取るフォント生成は変換層の内部に置き、呼出し側が解決済みstyleの高さを変更できないようにする。
Label、TextEditor、Button、ComboBox、PopupMenuには同じ契約を使うadapterを設ける。

このAPIと合わせて、`juce_shell/src`、製品描画を使う`juce_shell/tools`、関連テストを対象にした軽量な検査をCIへ追加する。
新しいファイルと未追跡ファイルも列挙し、移行済みファイルだけを検査する方式にはしない。
検査対象には直接のFont生成、`setFont`、Font高の変更、独自の文字描画やfitted-text処理を含める。
通常の描画側にFontを返さず、文字を設定するAPIと低水準の文字描画APIは許可したadapter内へ限定する。
検査は既存のNode環境で動くsource検査を追加し、コメントや文字列を区別して複数行のAPI参照も調べる。
引数が数値か変数かによらず境界外の低水準API参照を検出することで、倍率を変数へ隠すだけの迂回を防ぐ。
検査器の対応構文と限界は記録し、見逃しが判明した構文は失敗fixtureへ追加する。

フォント生成を許可するファイルは変換層に限定し、テストやpreview rendererも製品画面と同じ役割APIを使う。
テスト専用の書体診断などに例外が必要な場合は、最小範囲、用途、理由を記録する。
移行中の旧指定は明示した台帳で減少だけを許可し、移行完了時に製品描画側の台帳を空にする。
画面ID、遷移先、文字構成、検証fixtureの対応は共通の画面一覧で管理し、製品の画面選択とテスト列挙から参照する。
未登録IDへの既定styleによる代替を設けず、新しい画面に必要な対応が欠けた場合はbuildまたは対象検査で失敗させる。

検査器自身には、旧APIの再導入、複数行の数値指定、独自の縮小、未登録画面が失敗する例を用意する。
共通定数が存在するだけのテストではなく、製品の描画経路がその契約を使用していることを確認する。

## 6. 実装順序と成果物

以下は一つの移行を進める内部工程であり、特定画面だけを先に完成扱いで出荷しない。

| 工程 | 作業 | 次へ進む条件 |
| --- | --- | --- |
| P0 現状の固定 | 同一commit、同一fixture、同一書体で全画面の5サイズを一巡して記録する。実描画情報を採取し、対象一覧を確定する | 呼出し元、文字役割、旧指定、状態別の欠けを追跡できる |
| P1 共通構成の決定 | 文字階層、間隔、overflow、密度境界、用途別の構成を定義する。代表面で全サイズを比較して数値表を決める | LEVEL、グラフ、Reference、Blind、VUの読み方の違いを説明でき、最小と最大で成立する |
| P2 基盤と検査の導入 | コンテキスト、型付きstyle、文字計測と描画adapter、旧指定台帳、CI検査を導入する | 親子の密度一致、直接指定の検出、instance間の独立性を対象試験で確認する |
| P3 全対象の移行 | 共通shellと操作部品から各機能、画像出力へ同じ契約を適用し、旧指定を除去する | 全対象が移行済みで、製品描画側の旧指定がゼロになる |
| P4 全画面の整合確認 | 役割表と比較画像を生成し、サイズ変更、画面遷移、長文、欠測、状態変化を確認する | 下記の完了条件を満たし、残る実ホスト項目を明示できる |
| P5 最終候補の検証 | 一つの候補で必須の全体gateと各formatの実ホスト確認を行う | 同一候補の結果と未解決項目を記録し、配布工程へ渡せる |

比較画像は「同じ画面を5サイズで並べる一覧」と「同じサイズで全画面を並べる一覧」の両方を作る。
縮小サムネイルには確認用の実寸画像を付ける。
開発用一覧にはbuild、書体、論理寸法、出力倍率、fixture、状態を記録し、実機確認と区別する。
製品へ新しい設定画面やフォント調整UIは追加しない。

## 7. 完了条件と省エネ検証

文字契約とレイアウトが守られているかは、専用の短い検証入口で確認する。
既存の`KirinUiRenderContractTests`に対象選択の仕組みがあるため、描画ソースとfixtureを再利用し、タイポグラフィだけを確認できる入口を追加する。
画像生成は通常の合否検証から選択可能にし、毎回すべての画像と性能試験を作り直さない。

完了には次の条件をすべて満たす。

- 対象一覧の全画面が共通契約を使い、各画面に任意のフォント高や横圧縮を残していない。
- 同じ役割と構成では、5プリセットと中間寸法で文字の大小関係が逆転しない。
- 密度境界の直前、境界、直後で親子の判定が一致し、意図しない急な文字変化がない。
- 有効な中間寸法のレイアウトを軽量に走査し、重なり、領域外、必須文字の欠けがない。
- 主値、符号、単位、重要な状態、通常復帰の操作は、契約上の最大桁と長文でも読める。
- 長い英数字名、日本語名、未接続、欠測、warming、disabled、focus、Guide有無でも配置が成立する。
- ページ遷移、2MIXとTRACK/STEM、絶対値と差分の切替で、共通操作の配置と文字階層が保たれる。
- 異なるサイズの複数instance、editorの閉開、保存と復元でもstyleが混線せず、保存寸法を上書きしない。
- macOSとWindowsの書体で文字幅を確認し、正式な埋込書体でも比較画像を確認する。platform間のpixel完全一致は合格条件にしない。
- CaptureとReference previewの実出力が同じ役割契約を使い、画像倍率が重複して適用されない。
- resize時と通常更新時の対象性能が既存予算内に収まる。書体解決や不変レイアウトの再計算は必要な変更時に限定する。
- 比較画像で読順、余白、主値と補助値の区別を確認する。寸法テストのpassだけで統一感を合格にしない。

P0では現状記録を一度作り、P2とP3では変更箇所に対応する検査を実行する。
共通契約を変更した場合はその全利用画面、画面固有の構成を変更した場合はその画面と隣接する遷移を確認する。
機能ごとの移行が終わるたびにworkspace全体、Recordのrealtime試験、全描画性能試験を繰り返さない。

P4の最終比較は同じfixtureから一度まとめて生成し、失敗を修正した場合だけ影響範囲を再確認する。
P5では`hypha_remaining_work_handoff_20260910.md`のC4必須gateと対応表を作り、同じチェックをscriptと手動で重複実行しない。
RustやFFIへ変更が及んだ場合は既存の必須gateを適用し、省エネを理由に未実行項目をpassとしない。

実ホストではmacOS AU、macOS VST3、Windows VST3の最終候補でリサイズと画面遷移を確認する。
macOSのRetina表示とWindowsのDPI変更で文字と操作位置が一致することも対象に含める。
Windows検証機の操作は共通Runbookに従い、Daisukeの実音確認が必要な既存項目と合わせて一巡する。
新たな全体試験を反復する条件は、失敗、共通契約の変更、または未解決の懸念がある場合に限定する。

## 8. 引き継ぎと文書の維持

本計画を完成計画C2の表示整備として扱い、C4の最終候補を固定する前に全画面の移行を完了する。
実装時には`hypha_ce2226_jungle_visual_system_20260901.md`と表示契約を現行構成へ同期する。
同文書には旧来の「四段階」と900×600の追加仕様が併存しているため、サイズと情報構成の重複記述も整理する。
実際の数値表は実行可能な共通契約から生成し、複数の文書へ手で複製しない。

変更対象は製品コード、共通契約、対象テスト、CMakeとCIの配線、表示文書を一つの移行として管理する。
レビューには役割表、全画面比較、直接指定の残数、実行済み検証、実機未確認項目を添える。
公開build、配置、公証を行う工程では、既存Runbookに従ってLS、macOS無料配布、Windows配布の三チャネルを同一versionで準備する。

今回作成したのはこの計画と再開用リンクだけである。
製品コード、配布物、テストの実行結果は更新していない。
