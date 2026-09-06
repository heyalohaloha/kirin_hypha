# DRUM B案の実装と軽量化検証

## 現在の状態

2026-09-06にDaisukeが選択したB案「菌糸の扇」を、JUCEのDRUM表示へ実装した。
Windows検証機で単体描画の60条件が合格したが、macOSの最大Retina表示には性能基準を超える2条件が残る。
**軽量化全体、DAW上の実音同期、配布版への反映は未完了**である。
この変更は検証候補であり、公開可能なリリースではない。

検証用のconsole executableだけをビルドした。
DAWの既存PRE/POST、曲、チャンネル、オーディオ機器、インストーラーは変更していない。
このセッションではWindowsのGUIを操作する利用可能な経路がなく、SSH経由の単体描画テストと画像確認までを行った。
RustDeskの認証、接続制限、常設サービスは変更していない。

## B案の表示契約

**扇**は四つの観測量を形へ写す図形であり、実測波形そのものでも、減衰時間を示す図でもない。
下部の右を空間的な前方として `FRONT >`、左を `REAR` と表示する。
上部の6秒履歴の横軸だけが時間を表す。
以前の下部にあった `−100..+30 ms` 表記は、扇の横幅を時間と誤認させるため外した。
観測窓そのものは変更していない。

| 観測量 | 形の対応 | 色 | 変えない意味 |
| --- | --- | --- | --- |
| STRENGTH | 根元の厚み | 既存のgold | 30 ms attack RMS |
| BRIGHTNESS | 扇の開き | 既存のice blue | 100 ms Sharpness、acum |
| TEXTURE | 連結した枝の成長と密度 | 既存のcopper | edge、Crest、plateauの既存複合量 |
| TRANSIENT | 右への張り出し | 既存のteal | 直前との局所contrast。長さは秒数ではない |

色相、物理閾値、特徴量の算出式、数値の有効桁は変更しない。
発光開始は既存の固定閾値とsmooth-stepに従い、各成分の発光量はゼロから連続する。
一成分だけ有効なときに、無効な別成分まで発光させない。
曲内最大値やpercentileによる正規化は加えない。

PREは薄い無彩色の輪郭、POSTは既存の固有色とする。
四量に共通の絶対scaleを使い、数値の差分は常に `POST−PRE` を保つ。
PREがない場合はPOSTの絶対量を描き、PREを複製しない。
PREの説明は見出しへ置き、左をPRE、右をPOSTという位置関係にはしない。

## 繊細な変化と無音時の挙動

新しい音声解析は追加しない。
既存の10 ms RMS包絡から20 ms間隔の8点を読み、隣接差分を7本の曲線の内部制御点へ写す。
連続する包絡binの間だけ表示用に補間し、欠測、世代違い、sample rate違い、channel数の変更をまたいで動きを作らない。
根元の厚み、扇の開き、枝の量、前方への到達点は観測量から決まり、揺れで数値の意味を変えない。

PAIRの曲率変化はPREとPOSTへ共通に適用する。
同じ測定値に対し、装飾の揺れだけで差分らしい二重輪郭が生じることを防ぐ。
LIVEかつ信号がある場合だけ包絡に追従し、LOCKした過去イベントは動かさない。
停止または無音のLIVE表示では、描画領域を黒に戻す。
独立した発振、巡回光、固定420 msの走査、無音時の呼吸は使わない。

この挙動の自動テストは合格しているが、DAWの音と画面の体感同期は未検証である。
10 Hzの観測受け渡しと既存の100 ms表示時刻補間は残っているため、同期遅延をゼロとは言わない。

## 描画負荷を抑える構造

- 固定PNGを実行時に分割して描く処理と、多重のぼかし相当の再描画を外した。
- 下部は7本の主線と最大28本の連結した枝に制限した。小さな履歴標本では3本の主線と3本の枝を使い、読めない細部の重ね描きを増やさない。
- 色の奥行きは連続した5点のグラデーションで作る。グラデーション自体は削除していない。
- 上部のRMS流線はPOSTが5本、重ね表示のPREが3本とし、同じ幅の線を一度の描画へまとめた。細い上部流線の継ぎ目はbevel、下部は曲線と丸い終端を保つ。
- 上部の頂点整理は表示座標で最大0.05 logical px以内とし、10 msの正本データ、ピーク、数値、Recordを変更しない。
- 履歴の図形を再利用する。入力値、寸法、DPI、比較モード、図形の詳細度をすべてcache keyへ含める。
- 下部は、四つの特徴量が完全一致し、曲率変化による制御点の移動が0.05物理px以内の場合だけ前の画像を再利用する。枝を含む全制御点の変位係数は描画高さの0.487倍以下であり、これを0.5倍として保守的に境界を求めた。数値と特徴量由来の厚み、開き、枝の成長量、前方の到達点は丸めない。
- キャッシュはcomponentごとに512件、画像pixel payloadは最大8 MiB。二段のPRE 240件とPOST 240件を同時に保持できる。2個のcomponentならpixel payload上限は16 MiBで、オブジェクト管理用メモリは別途少量必要となる。
- 生成不能、DPI範囲外、寸法範囲外では同じvector描画へ戻す。キャッシュを測定データの保存先にはしない。
- `setSnapshot` は意味のある全フィールドを比較する。更新がなければ再コピーと再描画を省き、途中のevent detail修正や遅れて届いたPREは更新として扱う。paddingと未使用容量は比較しない。

これらはGUI側だけの変更である。
Audio Thread、計測worker、IPC、PRE/POSTの通常音声経路に新しい処理を加えていない。
旧PNGの読み込みと描画は止めたが、共通CMakeへの別作業の変更と衝突しないよう、埋め込みassetの削除は行っていない。

## 単体描画テストの条件と結果

Windowsは検証機のMSVC、macOSはClangで、同じ表示コードをRelWithDebInfoへビルドした。
利用したJUCEは作業ツリーの7.0.12で、既存のローカルpatchを含む。
既存のplugin build directoryとの競合を避け、専用console test targetを使った。
この単体targetには有償fontを埋め込まず、既存のinstalled/fallback font選択を使用した。
配布binaryの埋め込みfontでの文字配置確認は、DAW実動検証に含める。

| 条件 | 内容 |
| --- | --- |
| 表示サイズ | 製品の100%、125%、150%、200%、300%相当の5種類 |
| DPI倍率 | 1、1.25、2 |
| 比較表示 | PRE/POST二段と重ね表示 |
| イベント数 | 31、最大240 |
| データ | PREとPOSTで異なる四量、600点の包絡。各frameで包絡、時刻、途中のdetailを更新 |
| 測定対象 | snapshot受け渡し、時刻反映、component全体の描画。DAWと音声処理は含まない |
| 予算 | 更新frameの5回中央値12 ms以下、最大24 ms以下、初回80 ms以下 |

この予算は今回の設計上の採用条件であり、どの機械でも保証する製品仕様値ではない。
固定した同じ画像を描き続ける測定だけで合否を決めない。
二つのcomponentを同時に開いたDAW全体の予算は、別途実機で確認する。

| 最終記録 | Windows | macOS |
| --- | ---: | ---: |
| 条件数 | 60 | 60 |
| 各条件の中央値の最大 | 9.3581 ms | 12.2536 ms |
| 更新frameの最大 | 15.1250 ms | 19.2401 ms |
| 初回の最大 | 32.1917 ms | 87.2066 ms |
| 合否 | 60条件PASS | 58条件PASS、2条件FAIL |

macOSの不合格は300%相当、DPI 2、240件の二段および重ね表示である。
二段は初回81.9349 ms、重ね表示は初回87.2066 msと中央値12.2536 msが基準を超えた。
基準を緩めて合格にせず、未完了条件として残す。

旧描画の同検証機での単体測定は、DPI 1、31件の二段が446.448 ms、重ね表示が362.267 msだった。
旧fixtureと今回の60条件は入力値と更新方法が異なるため、この二つの数値から厳密な倍率やDAW全体のCPU削減率は算出しない。
旧測定の詳細は [DRUM描画の診断](hypha_drum_render_diagnosis_20260906.md) を参照する。

## 正しさの検証

- 四量を個別に21段階変化させ、担当する形だけが変わることと描画境界を確認した。
- 正負および混在するPRE/POST差分、POST単体、無音、停止、LOCK、世代変更、sample rate変更、欠測を確認した。
- NaN、ゼロrate、整数時刻の下限、2^53を超える時刻、途中のdetail訂正、遅延PRE、同一snapshot、padding変更を確認した。
- キャッシュと直接描画をDPI 1と2で比較し、alphaとpremultiplied RGBの総差を5%以内で検証した。
- 両側480件を二巡して再生成が増えないこと、512件を超える更新、8 MiB上限、DPI変更、無効なDPIでのfallbackを確認した。
- 曲率の再利用境界をDPI 1、1.25、2、4で検査し、Bezier制御点の変位が0.05物理px以内であること、根元と前方が変わらないことを確認した。
- macOSとWindowsの機能テストはPASS。macOSの性能テストは上記2条件でFAIL。
- `cargo test --workspace` は1843件PASS、37件ignoreで終了コード0。ATTACKのVST接続検査を含む。ignoreされたRecord等の試験を今回実行済みとは扱わない。
- `cargo clippy --workspace --all-targets` はPASS。本体のClippy lintは0件。vendorの既知lint3件と、既存build scriptの設定通知を確認した。
- 変更したowned sourceはすべて500行以下。全体の行数検査は、別作業中の `ReferenceRuntimeV2Repository.cpp` が549行のためFAIL。このファイルには触れていない。

## 再現方法と証拠

```sh
cmake -S juce_shell/tests/attack_validation -B /tmp/hypha-attack-validation \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/hypha-attack-validation --config RelWithDebInfo \
  --target KirinAttackUiContractTests -j 4
ctest --test-dir /tmp/hypha-attack-validation -C RelWithDebInfo --output-on-failure
```

性能検査は、生成されたexecutableに環境変数 `KIRIN_ATTACK_FRAME_BUDGET=1` を付けて実行する。
通常の機能検査とは分けた明示実行であり、既存の公開CIへの追加はまだ行っていない。
Debugでは数値報告だけとなり、性能合格の根拠には使わない。
別のJUCE配置を使う場合は `HYPHA_JUCE_SOURCE` を指定する。

Windowsの専用buildは `C:\Users\hello\Dev\hypha_fan_validation_20260906\build` にある。
既存DAWのVST3とは別物であり、このexeの成功を「DAWへ配置済み」と読まない。

- Windowsの最終記録：`/tmp/hypha-fan-windows-b729-verification.log`
- macOSの性能記録：`/tmp/hypha-fan-mac-final-verification.log`
- macOSの最終機能記録：`/tmp/hypha-fan-mac-final-functional.log`
- Rust検査：`/tmp/hypha-fan-full-workspace-final.log`、`/tmp/hypha-fan-clippy.log`
- 完全な性能matrix：[検証データ](hypha_drum_fan_b_frame_evidence_20260906.json)

## 残っている作業と公開条件

1. macOSの上記2条件を、グラデーションと観測の意味を保って、中央値12 ms以下かつ初回80 ms以下にする。高密度履歴の初回生成と、最大表示の上部流線を個別に測る。
2. 同じPeachの検証projectで、停止、再生、TRACK DRUM切替、LIVEとLOCK、最大表示、音と動きの一致、製品fontでの文字配置を確認する。別の曲を新規選択しない。
3. 同時表示と画面を閉じたときの負荷を測り、他のページと音声処理への影響を確認する。今回の単体結果でPC全体のCPUを説明し切らない。
4. 本体への配置と配布を行う場合は、既存runbookのmacOS署名、notarize、LS用pkg、Windows署名installerを同じversionとcommitで揃える。

SHARPNESS単体表示、PSB、PRE/POST Blindの製品接続は、このB案の変更で完成したとは扱わない。
残件をPhase 2へ送る判断はしていない。
