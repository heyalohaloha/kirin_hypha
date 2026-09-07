# B1 ホスト実機観測と取得失敗の診断

更新日: 2026-09-07。B-740。

ローカルPRE/POST BlindのB1は未成立である。
Windows Studio Proの保存済み検証曲を開き、PRE/POSTの診断表示を確認した。
ホスト識別情報と入力presentation latencyが取得できず、既知遅延を使う同期検証の前提が不足している。
この結果を、Studio ProのAPIが未対応だという断定や、対応形式を減らす根拠にはしない。

## 検証対象

実ホストで使用したのは、既に一時配置されていたB-726の最適化Debug版である。
起動前後の両VST3のSHA-256が一致した。
現行候補の配布版、B-740の診断追加版をDAWに読み込んだ結果ではない。

| 対象 | bytes | SHA-256 |
| --- | ---: | --- |
| PRE | 31,251,968 | `2D4564FB57964D392EE9A0C9EB19272FA4ADA9583815CABC6307BA0FCE827BD3` |
| POST | 35,204,608 | `C8C083DF9C9A0F3A9990F3E7E5E593B5DA74FEB0F37AB953677C87E4F734715E` |

今回使用したHostContext、診断表示、host clock、presentation clock patchの4ファイルは、
B-726からB-739まで変更がないことをGit差分で確認した。
他の処理の変更やビルド条件まで同一だとする証明ではない。

## 観測値

曲設定44.1 kHzとデバイス48 kHzの相違を告げる起動時警告が出た。
設定を変更せず警告を閉じた後、callback側では48 kHz・stereoと表示された。

| 対象と状態 | callback番号 | sample位置 | clock source | 入力presentation | 出力presentation | identity |
| --- | ---: | ---: | ---: | --- | --- | --- |
| POST・停止 | 30,820 | 8,246,431 | 1 | 未通知 | 0（意味未確定） | unavailable |
| POST・再生 | 38,937 | 10,469,215 | 1 | 未通知 | 0（意味未確定） | unavailable |
| PRE・再生 | 62,983 | 10,565,599 | 1 | 未通知 | 96 samples | unavailable |

各画面でnominal blockは528 frames、host revisionは7、document/channel hashはabsentだった。
nominal blockは`getBlockSize()`の値であり、実callbackごとのframe数ではない。
採取時刻が異なるため、表のPRE/POST位置を引き算してPDCを求めない。
PREの96 samplesも、共通の内容時刻や他トラックとの同期の証明には使わない。
停止中の最終callback表示は鮮度の保証ではない。

実機では日本語の更新情報メニューが四角い代替字形になった。
現行コードも共通PairMenuLookAndFeelで`monoFont()`を使い、日本語用の`nativeTextFont()`を選択しない。
U工程の全メニュー確認へ追加する。
OneDriveの容量100%通知も表示されたが、アカウントや同期設定は変更していない。

## 今回の実装

従来の`unavailable`は、provider、host application、host名、document ID、active document ID、
channel IDのどの取得段階で失敗したかを残さなかった。
`HostContextFacts`に失敗箇所を追加し、Debugの明示的な診断メニューにだけ表示する。
inactive documentの不一致と読取り中のrevision変更も区別する。
失敗時に部分的なIDをすべて消す規則は維持し、この診断値で開始許可を発行しない。
Audio Thread、音声出力、Record、C ABIの処理は変更していない。

## 検証

- macOSの対象native試験: pass、1 CTest、0.25秒。
- Windowsの隔離した対象native試験: pass、1 CTest、0.86秒。
- 欠落した各取得段階、不正なID、非active document、競合する通知、復旧時の診断解除を確認。
  既存の通知寿命と一貫したclock snapshotの試験も同じ実行に含む。
- macOSのPRE/POST共通shellのDebugビルド: pass。
- Rustのnative host境界に関する対象契約: 1 pass、135 filtered。
- `cargo clippy -p xtask --locked --all-targets --no-deps -- -D warnings`: pass。
- source行数制限と`git diff --check`: pass。

Rustの対象試験は実行ファイル起動前に待機した。
採取したsampleでは`_dyld_start`のみで、その後、追加の再実行や設定変更なしに試験が開始してpassした。
試験本体のhangとは扱わない。
全体suiteとFFIのignored suiteは再実行していない。
FFI Rust sourceは変更しておらず、B-737/738の全体検証と今回の対象検証を区別する。

画面、起動前後のinventory、復旧結果、ログとhash台帳は、ローカルの
`Downloads/Hypha_B1_Host_Evidence_20260907/`へ保存した。
検証曲は保存せず閉じ、元のsongファイルのhash一致、Studio Pro終了、今回作った一時タスクの削除を確認した。
常設の操作タスク、プラグイン配置、DAWのデバイス設定は変更していない。

## 次に閉じる条件

1. この診断変更を含む識別可能な候補で実ホストを再観測し、失敗したAPI段階を特定する。
   今回のnative試験は、この実ホスト再観測を代替しない。
2. 検証済みparticipant scopeとexact PRE ownerの成立根拠を確定する。
   機能が欠ける場合は必要な構造を再検討し、PIDや保存UUIDで穴埋めしない。
3. 既知遅延、共通区間、他トラックとの同期を検証し、整列後の残差0 sampleを実測する。
   macOS VST3/AUも形式ごとに同じ成立条件を満たす必要がある。
4. B1成立後にB2の開始排他と取得barrierを接続する。
   B1の未成立中もU/M工程は進められるが、Blind開始機能を有効にしない。

LSアップ用: skip。HPアップ用: macOS skip、Windows skip。
今回はビルド済み配布物の配置、署名、notarize、公開を実施していない。
