# Reference Preset別準備 — B-800 / OS W-3025

## 変更範囲

OS worktree: `/Users/nishiodaisuke/.codex/worktrees/reference-check-version-blind-boundary`。

起動時はactive Presetだけを準備する。別Presetを選んだときは要求されたWork snapshotを準備し、既存の準備済みPresetはimmutable archiveと依存graphが検証できれば維持する。未選択音源の欠損や未準備で、使うPresetの準備を止めない。

これはPreset単位の分離であり、選択Preset内の全enabled Candidate待ちはまだ残る。候補単位の即試聴、CPU削減率、全体のReference完成は主張しない。

| 入力／境界 | 実装 |
| --- | --- |
| 正本 | Work state／Preset／applicationは変更しない。Global templateを暗黙更新しない |
| 重い準備 | OSの既存専用worker。Hyphaへ観測・全音源decodeを移さない |
| 保存／公開 | v4 Manifestにpending metadataを追加。前回の設定receiptが変わったgraphは保持しない |
| 表示／操作 | 既存Preset menuを使用。Globalの旧revisionは`· WORK`として残し、新revisionと取り違えない |
| 遷移 | pending選択→既存typed request→OS再確認・準備→typed ack→準備完了。音声はAのまま |
| 失敗出口 | 明示操作は既存source選択／retry。内部の再利用失敗はpending化。既存Manifestを消さない |
| 役割／platform | POST consumer、共通JUCE実装。PRE／POST測定・Record・FFI・Audio Threadは変更なし。Windows実動は未検証 |

## データ契約

- `kirin_hypha_reference_manifest` v4は従来8 fields＋`pending_presets`のみ。最大256 KiB。v3読込は維持し、8 fields／64 KiBのまま。
- pendingは`name`、`source_template_artifact`、`source_preset_artifact`のみ。ready＋pending合計128件。Preset ID／snapshot revisionは一意、templateとsnapshotは同ID・別revision。activeはどちらかの正確なsnapshotへbindする。
- pendingの音源・測定・ready状態を捏造しない。準備済みprojectionの中へplaceholder Candidateを入れない。
- Globalに同じtemplate revisionが無いWork設定はmenu内部で`work:<preset UUID>`を使う。wire request／recovery／正本にはprefixを出さない。
- Work設定の要求時はmanifest revisionとcapabilityを検証し、OS側で現在のsnapshot receiptとtemplate→snapshot applicationを再確認する。Globalが更新・削除されていても保存済みの設定だけを呼び出す。
- 保持した未選択graphはartifactを検証するが、音源fileの再hash／再decodeは行わない。Hyphaは選択時に通常のsource検証を行う。
- v4非対応Hyphaとの互換性はない。OS producerと対応Hyphaを組にして配布する。今回は公開・配置しない。

## 確認した事実

- OSのlazy準備7件: 初回2 Preset中1件のsource解決、二つ目選択後に累計2件。再起動相当のcache再利用でdecode回数は増えない。
- 未選択file不在でも別Presetを準備。選択file不在では公開を拒否して前Manifestを保持。旧依存artifact破損はpending化し、mutable projection破損はimmutable archiveから保持。unknown target／stale receiptは準備前に拒否。
- Manifest parser／schema／IPC／pending Inboxと既存publication境界を対象確認。旧Global template再読込・Work write・無関係な音源inventoryなしでrendererから要求できる。
- Hypha `--lazy-presets-only`: pending読込、異なるGlobal／Work revisionのmenu identity、失敗→retry→準備完了、選択表示、A維持。通常CI suiteにも同じ試験を登録。
- `--workspace-only`: 既存の通常B／Blind、一覧更新時の継続、source／gain変更時の停止を確認。
- 実WAV→OS専用worker→v4 publication→Hypha本番parser: 5 Checkが受理。48 kHz stereo 10秒、100 time bins／64 bands、9,618 bytes。I −14.00848 LUFS、TP −13.96591 dBTP。
- Debug runtime test targetとPOST editorをビルド。最初の新規test fixtureはconstructor引数が旧Controller用だったため、現行V2の2引数へ修正。製品APIを変更して通していない。
- source line budgetと差分空白チェックpass。Rust／FFI未変更のためcargo全体／clippy／ignored parity、全体suiteの反復、release／install、実DAW、Windows実動はskip。

## 残る作業

1. 選択Preset内のB／Candidate単位の先行準備と表示契約。未準備Candidateを隠さずに扱う。
2. 多数Presetのreceipt／projection再検証負荷、host-rate試聴cacheのOS所有化。
3. 同曲Version追加時の候補更新、PSR／chroma、実曲corpusでの照合・観測精度。
4. 実データ・各サイズの目視、macOS／WindowsのDAW実動。今回menuの構造と選択状態を検証したのであり、目視済みとはしていない。

新しい画面・panelは追加していない。Check設定PresetとVersion Blindは引き続き別作業。未実施項目をPhase 2へ送る判断はしていない。
