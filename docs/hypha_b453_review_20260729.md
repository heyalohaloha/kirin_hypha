# B-453レビュー再評価とB-454対応記録

- 対象コミット：`a09aa34`（B-453）
- 元レビュー：`6965197` の同名文書
- 再評価日：2026-07-29
- 対応番号：B-454

## 結論

B-453が直した実行ファイル名の選択は正しい。

元レビューのうち、回帰テストが`verify_deployment`を通っていないこと、バンドル定義と検証処理が重複していたこと、`CFBundleExecutable`を確認していなかったこと、実インストール名のVST3をpluginvalへ渡していなかったことは正しい指摘である。

一方、`notarize.rs`を「現行で同じ障害が残る箇所」とした評価は正確ではない。
notarizeが扱うJUCEビルド出力では、外側のbundle名と内部実行ファイル名がどちらも`Kirin Hypha PRE`または`Kirin Hypha POST`で一致していた。
したがって、これはリネーム済みbundleを将来扱った場合に現れる潜在的な仮定であり、B-453と同時に発生していた障害ではない。

B-454では、個別箇所へ条件分岐を足す方法を採らず、macOS出荷バンドルの定義と実行ファイル解決を共通化した。

## 指摘ごとの判定

| 元レビューの指摘 | 判定 | 再評価 |
|---|---|---|
| B-453の追加テストは修正行を保護しない | 正しい | fake bundleを作らず、`verify_deployment`も通していなかった |
| 名前、CID、表示名、実行ファイル名が複数箇所に重複 | 正しい | Rust 3経路とJavaScriptに独立定義があった |
| `Bundle::name`の説明が実態と異なる | 正しい | VST3の外側の名前と内部実行ファイル名を同一視しやすい構造だった |
| `CFBundleExecutable`が未検証 | 正しい | bundleと実行ファイルを結ぶ実データを読んでいなかった |
| リネーム後のVST3をpluginvalへ渡していない | 正しい | 従来はJUCEビルド出力名だけを検証していた |
| notarizeにも現行の同一障害が残る | 一部修正 | 現行入力では名前が一致するため障害は発生しないが、仮定の重複は解消対象だった |
| 1.1.34と1.1.35の配置物は未検証 | 時点依存 | 1.1.35の現在のシステム配置物は2026-07-29に再検証済み。1.1.34は履歴上の証跡不足として残る |
| ソース文字列の存在確認では挙動を保証できない | 正しい | 実ファイルを使う契約テストへ置き換える必要があった |

## macOSで外側のVST3名を変えられる根拠

Appleは`CFBundleExecutable`を、loadable bundleが動的に読み込むbinaryの名前として定義している。
Appleの説明には、利用者がappまたはbundleのディレクトリ名を変更した場合にも、macOSがこのキーを使って実行ファイルを特定すると明記されている。

SteinbergはmacOS VST3を標準的なmacOS bundleとして定義し、`Contents/Info.plist`と`Contents/MacOS/`内のuniversal binaryを構成要素としている。
同じページにある「bundleフォルダとDLLを同名にする」という制約はWindows節の記述であり、macOS節の制約ではない。

以上から、macOSでは外側を`PRE Kirin Hypha.vst3`とし、`CFBundleExecutable`が`Kirin Hypha PRE`を指す現在の構成に仕様上の矛盾はないと判断できる。
これは公式資料を組み合わせた判断であり、全DAWの挙動を資料だけで保証するものではない。
B-454では、実インストール名を再現したPREとPOSTの両方をpluginvalへ渡し、現在のバンドルが実際に読み込まれることも確認した。

参照資料：

- [Apple: CFBundleExecutable](https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundleexecutable)
- [Steinberg: Plug-in Format Structure](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BFormat.html)
- [Steinberg: Plug-in Locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html)

## B-454の構造

macOSの4バンドルは`config/hypha_macos_ship_bundles.json`だけで定義する。
各定義はrole、format、ビルド元、インストール先、archive内の配置先、`CFBundleExecutable`の期待値、表示名、VST3 CID、旧配置名を持つ。

Rust側の共通検証はInfo.plistから`CFBundleExecutable`を読み、manifestの期待値との一致と実行ファイルの実在を確認する。
その後、AU名、VST3の`CFBundleName`と`CFBundleDisplayName`、moduleinfoのCIDとName、binary内の表示名を確認する。

コピー後の検証はファイルサイズだけでは判定しない。
元と配置先の実行ファイルを`CFBundleExecutable`から個別に解決し、全byteを比較する。
同一サイズで内容が異なるbinaryも拒否する。

次の経路が同じ定義と検証を使う。

- system install
- notarize前検証
- manual zip作成
- Lemon Squeezy用pkg作成
- macOS pluginval

通常のinstallでは、利用者領域の削除やsudo実行より先に、一時領域へ正確なインストール配置を作る。
その一時配置について、binary一致、universal、署名、notarization、表示契約を確認してから実配置へ進む。

`install --release --verify-only`も追加した。
このモードはsystem配置を変更せず、ビルド元と現在の配置物を比較する。

pluginvalはJUCEビルド出力を直接開かない。
隔離したHOMEとTMPDIRの下へ`PRE Kirin Hypha.vst3`と`POST Kirin Hypha.vst3`を作り、共通検証を通した後、その2本を開く。

pkgのpreinstall削除対象もmanifestから作る。
manifestのinstall先と旧install先は、承認済みのComponentsまたはVST3ディレクトリ直下でなければ読み込みを拒否する。

## 回帰テスト

fake VST3 bundleを実際に作るテストで、次の境界を固定した。

- 外側が`PRE Kirin Hypha.vst3`でも、`CFBundleExecutable`が指す`Kirin Hypha PRE`を解決できる
- `CFBundleExecutable`が期待値と異なる場合は、ファイル探索前に拒否する
- `CFBundleExecutable`が指すファイルが存在しない場合は拒否する
- 元と配置先のbinaryが同一サイズでも内容が異なる場合は拒否する
- manifestのinstall先が承認済みプラグインフォルダ外へ出る場合は、RustとNodeの両方で拒否する

従来の「各ソースに特定文字列が書かれていること」を同期手段にしたテストは廃止した。
現在の静的テストは、各出荷経路が共通検証を呼ぶことだけを配線契約として確認する。

## 2026-07-29の実物検証

| 対象 | 結果 |
|---|---|
| JUCE source AU/VST3 4本 | `CFBundleExecutable`、実行ファイル、表示名、CIDを確認し、4本ともpass |
| system配置 AU/VST3 4本 | sourceと実行ファイルがbyte単位で一致し、universal、署名、notarization、表示契約がpass |
| system配置 AU binary | PRE、POSTとも24,882,128 bytes |
| system配置 VST3 binary | PRE、POSTとも25,468,480 bytes |
| role-first一時配置のpluginval | strictness 5でPRE、POSTともSUCCESS |
| Steinberg VST3 validator | 実行ファイルが未指定のためpluginval内ではskip |
| unsigned pkg smoke | 37,327,425 bytes。4バンドルのpayload、preinstall構文、12本の限定削除行を確認 |
| Node release metadata | 9件pass |
| xtask | 既存113件pass。README旧文言を要求した1件を修正後、対象テストpass |
| FFI ignored gate | parity 20件、pairing candidates 5件が単一スレッドでpass |
| clippy | `kirin_measure`、`kirin_hypha_ffi`、`xtask`がwarning 0 |

全体ゲートはREADMEの説明更新に追従していない静的テスト1件で一度停止した。
利用量を抑える方針に従い、全体ゲートは再実行していない。
失敗した1件だけを修正して再実行し、停止位置より後ろのFFI ignored 25件とclippyを個別に実行した。

## 影響範囲

B-454は出荷定義、検証、install、packaging、pluginvalの開発経路だけを変更する。
計測engine、Audio Thread、Measure Thread、IO Thread、FFI、GUI、保存schema、pairing、Record処理には変更を加えていない。

実物検証ではsystem配置を読み取っただけで、AUまたはVST3の再配置、再署名、再notarizeは行っていない。
Kirin OSのデータ作成、削除、pair変更も行っていない。
pluginvalのHOMEとTMPDIRはリポジトリ内の隔離領域へ向けた。

## 履歴上の残件

`e258929`（B-400）から`a09aa34`（B-453）まで、installは配置後のVST3検証で失敗する状態だった。
配置処理自体は検証より先に完了するため、この履歴だけから「配布binaryが壊れていた」とは判断できない。
確定しているのは、当時のinstall完了を示す後段検証の証跡が不足していることである。

現在の1.1.35配置物は再検証済みである。
1.1.34は過去artifactを改めて取得して監査しない限り、履歴上の証跡不足を解消できない。
これは現在の1.1.35へ追加修正を要求する不具合ではなく、旧release artifactの監査要否として扱う。
