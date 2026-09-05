# Reference runtime v2 実装ハンドオフ

Status: implementation started in Kirin OS / Hypha runtime awaits schema fixtures

Date: 2026-09-05

Consumer baseline: B-700 / `65b02b4`

Canonical cross-product contract: `kirin_sense_lens/docs/reference_product_contract_20260905.md`

## 1. 目的

現行の一つのBを読むReference runtimeを、Kirin OSのPreset、Check、Candidateを自動で受け取るruntimeへ置き換える。

Kirin OSでPresetを選んだ後に`Open in Hypha`を要求しない。

HyphaはDAW内でPreset、Check、第1候補、第2候補、A/Bを短い操作で呼び出せるようにする。

Daisukeの2026-09-05判断により、日本語UIへ`Candidate`を表示しない。上部ラベルは「比較する曲」、先頭2件は「第1候補」「第2候補」、追加は「候補曲を追加」、3件目以後は「その他」とする。内部schema／code上の`candidate`は変更しない。

Aのbit identity、0 samples latency、Audio ThreadのRT safetyを維持する。

## 2. 現行資産

次の実装は残して拡張する。

- `ReferenceAuditionRepository`
- `ReferenceAuditionProtocol`
- `ReferenceAuditionController`
- `ReferenceAuditionLease`
- `ReferenceAudioPages`
- `ReferenceBlindSession`
- callback receiptによる可聴source確認
- malformed、expired、mismatched receiptのfail-closed処理
- startup、restore、invalidated時のA復帰

`Domain::reference`は現行正本なので、新しいObservation targetを追加しない。

## 3. 変更する契約

v1の`Preparation`一件を、v2の`Manifest`へ置き換える。

Manifestは次を持つ。

```text
format = kirin_hypha_reference_manifest
version = 2.0
work_id
revision
source_state_artifact
active_preset
preset_artifacts
```

Closed top-levelは上記7項目だけとする。

`revision`はWork単位で初回1、次の公開ごとに正確に1増える。

Hyphaは現在保持するrevision以下を採用せず、Kirin OSも古い生成処理の後着を公開しない。

`source_state_artifact`は`relative_path`、`sha256`、`bytes`の3項目だけを持ち、`reference/states/<sha256>.v1.json`へ完全一致する。

`active_preset`は`null`または`preset_id`と`revision_id`だけを持ち、非null時は`preset_artifacts`内の一件へ完全一致する。

Presetが一件でもあるManifestでは`active_preset`を必須とし、Hyphaの初期選択だけを示す。

Hypha内の一時的なPreset切替はKirin OSのactive Presetを書き換えない。

`preset_artifacts[]`は`preset_id`、`revision_id`、`relative_path`、`sha256`、`bytes`の5項目だけを持つ最大128件の順序付き配列とする。

各pathは`plugin_data/reference/v2/presets/<work_id>/<preset_id>.json`へ完全一致する。

ManifestはPreset本文、表示名、Check、Candidate、設定値、生成時刻を持たない。

Preset projectionは、Daisukeの2026-09-05判断に基づき`plugin_data/reference/v2/presets/<work_id>/<preset_id>.json`から個別に読む。

Hyphaが使うPreset projectionは、Workへ適用されたimmutable revisionの表示名と順序付きCheckを持つ。

個別Preset projectionのclosed top-levelは`format`、`version`、`work_id`、`source_preset_artifact`、`name`、`checks`の6項目だけとする。

`source_preset_artifact`は、Work stateが指すimmutable Preset snapshotの`preset_id`、`revision_id`、`relative_path`、`sha256`、`bytes`を保持する。

`origin`、`purpose`、現在選択、表示配置、音源path、測定値、生成時刻をtop-levelへ複製しない。

Hypha用Check projectionは`check_id`、`label`、`mode`、`view_bindings`、`comparison_mode`、`candidates`、`profile_bindings`の7項目だけを持つ。

Kirin OS正本で`enabled: true`のCheckだけを元の配列順で0〜64件投影し、`enabled`、順位、現在選択、測定値を渡さない。

投影対象が0件の場合もPreset projection自体は有効とする。

HyphaはAを維持し、「Kirin OSでCheckを有効にする」操作から同じWorkのReferenceへ移動できるようにする。

Hypha用Candidate projectionは`candidate_id`、`display_name`、`source_kind`、`source_identity`、`source_artifact`、`cues`、`default_cue_id`の7項目だけを持つ。

`display_name`はKirin OSが現在のWorks VersionまたはCatalog Trackから作るcanonical snapshotとする。

永続Candidateの`note`、順位、現在選択、測定値、絶対path、Blind可否をCandidate projectionへ複製しない。

通常試聴メモとBlind回答はHistoryまたはListening Trialへ渡し、Candidate noteと混在させない。

Blind可否はTrial開始時に同曲かつ別Version／別contentであることを再検証する。

Candidate projectionの`source_artifact`は`relative_path`、`sha256`、`bytes`の3項目だけを持つ。

`relative_path`は`plugin_data/reference/v2/sources/<sha256>.json`へ完全一致させ、`bytes`は1〜65,536とする。

Hyphaは本文を読む前にbytesとSHA-256を検証し、同じhashの既存fileはbyte完全一致の場合だけ再利用する。

Candidate ID、表示名、source identity、絶対path、有効期限を参照側へ重複しない。

Source artifact本文のclosed top-levelは`format`、`version`、`source_kind`、`source_identity`、`file`、`audio`、`measurement`、`alignment`の8項目だけとする。

`format = kirin_hypha_reference_source`、`version = 2.0`とする。

表示名とCandidate IDを含めず、Candidate projectionの`source_kind`および`source_identity`へ完全一致させる。

Source artifactへ`verified_at`または`expires_at`を設けず、Hyphaは使用時にfile revisionとfull-file SHA-256を再検証する。

時刻だけを理由に利用不能へせず、Kirin OSが閉じていても検証済みfileを利用できる。

Source artifactの`file`は`absolute_path`と`revision`だけを持つ。

`revision`は`device_id`、`file_id`、`size_bytes`、`mtime_ns`、`ctime_ns`の5項目だけを持ち、すべて非負の10進文字列として保持する。

`absolute_path`はNULを含まない最大4,096 UTF-8 bytesの絶対pathとし、Hyphaは通常fileであることとsymbolic link不使用を確認する。

file名とfolderを別fieldへ複製せず、path変更、同名差替え、上書きをrevisionおよび`source_identity.sha256_file`の再検証で検出する。

Source artifactの`audio`は`sample_rate_hz`、`channels`、`total_sample_frames`の3項目だけを持つ。

`sample_rate_hz`は8,000〜768,000、`channels`は1または2、`total_sample_frames`は1以上のsafe integerとする。

秒数を保存せず、表示時に総sample数とsample rateから算出する。

Candidate Cueは同じsample rateを持ち、`end_sample`が総sample数以下でなければならない。

DAWとsourceのsample rateが異なる場合は、再生開始前の一度の利用者承認へ接続する。

Source artifactの`measurement`は`summary`と`detail_artifact`の2項目だけを持ち、各項目は`null`を許可する。

`summary`は音量合わせと即時表示に必要な少数の測定事実、`detail_artifact`はCheckに応じた比較描画用projectionへの参照とする。

詳細測定が欠損しても`original`試聴を止めず、該当する描画だけに測定データ欠損の事実とMEASUREへの復旧動線を示す。

詳細データを64 KiB上限のSource artifactへ埋め込まない。

`measurement.summary`は`measured_at`、`loudness_standard`、`lufs_i`、`max_true_peak_dbtp`、`lra_lu`、`psr_mean_db`、`crest_factor_db`、`stereo_width_pct`の8項目だけを持つ。

`loudness_standard = itu_r_bs_1770`とし、`measured_at`はcanonical UTCとする。

測定できなかった数値は`null`とし、summaryが非nullの場合は6つの数値事実のうち最低1件を必須とする。

LUFS-I欠損時は`loudness_match`、True Peak欠損時は`peak_match`を適用せず、Aと`original`試聴を維持する。

PLRは`max_true_peak_dbtp - lufs_i`から表示時に算出し、保存しない。

`psr_mean_db`と`crest_factor_db`を別の測定事実として保持し、一方から他方を推定しない。

`measurement.detail_artifact`は`null`または`relative_path`、`sha256`、`bytes`の3項目だけを持つ。

`relative_path`は`plugin_data/reference/v2/measurements/<sha256>.json`へ完全一致させ、`bytes`は1〜2,097,152とする。

Kirin OSは既存のWorkまたはCatalog測定からHypha専用のbounded projectionを生成し、元の測定fileや絶対pathを直接渡さない。

HyphaはSHA-256とbytesを確認してから読み、欠損または破損時は比較描画だけを外して試聴を維持する。

同じ測定内容のdetail artifactはCandidateおよびPresetをまたいで再利用できる。

詳細測定artifact本文のclosed top-levelは`format`、`version`、`source_content`、`audio`、`views`の5項目だけとする。

`format = kirin_hypha_reference_measurement`、`version = 2.0`とする。

`source_content`は`sha256_file`と`sha256_pcm`だけを持ち、Source artifactのidentityへ完全一致させる。

`audio`はSource artifactと同じ`sample_rate_hz`、`channels`、`total_sample_frames`へ完全一致させる。

`views`は`waveform`、`spectrum`、`loudness`、`dynamics`、`transient`、`stereo`の6項目を固定で持ち、各項目は`null`を許可する。

`spectrum_full`と`spectrum_low`は同じ`spectrum`測定を表示範囲だけ変えて使用し、データを二重保存しない。

source ID、path、表示名、良否判定を詳細測定artifactへ含めない。

`views.waveform`は`start_sample`、`frames_per_bin`、`bin_count`、`sample_peak_millidbfs`、`rms_millidbfs`の5項目だけを持つ。

`start_sample = 0`とし、全曲を最大4,096 binで覆う。

2つの値配列は`[channel][bin]`とし、channel数、bin数、全曲coverageを`audio`へ完全一致させる。

値は−300,000〜+24,000の整数milli-dBFSとし、完全無音と下限未満は−300,000へ固定する。

各binでRMSをsample peak以下とし、先頭無音binを切り落とさない。

`views.spectrum`は`band_centers_hz`、`p10_millidbfs`、`median_millidbfs`、`p90_millidbfs`の4項目だけを持つ。

12〜256個の厳密な昇順bandをNyquist以下へ置き、各bandで`p10 ≤ median ≤ p90`を必須とする。

`views.loudness`は`start_sample`、`hop_samples`、`lufs_m_millilu`、`lufs_s_millilu`、`views.dynamics`は`start_sample`、`hop_samples`、`psr_millidb`、`crest_millidb`だけを持つ。

`views.transient`は`start_sample`、`hop_samples`、`onset_strength_q15`、`views.stereo`は`start_sample`、`hop_samples`、`correlation_milli`、`width_basis_points`だけを持つ。

各timelineは`start_sample = 0`、最大8,192点で全曲を覆い、配列長を一致させる。欠損測定点は`null`とし、推定補間しない。

Stereo correlationは−1,000〜1,000、widthは0〜15,000とし、mono sourceのStereo viewは`null`にする。

Source artifactの`alignment`は`null`または`relative_path`、`sha256`、`bytes`だけを持つcontent-addressed参照とする。

pathは`plugin_data/reference/v2/alignments/<sha256>.json`、上限は524,288 bytesとする。

Alignment artifact本文は`format`、`version`、`feature_profile`、`source_content`、`audio`、`grid`、`features`の7項目だけを持つ。

`format = kirin_hypha_reference_alignment`、`version = 2.0`、`feature_profile = kirin_content_features_v1`とする。

`grid`は`start_sample = 0`、`hop_samples`、2〜2,048の`point_count`だけを持ち、先頭無音を含む全曲を覆う。

`features`はonset、sub／bass／mid／high energy、12 pitch classのchroma、loudnessだけを持ち、整数へ量子化する。

先頭無音は配列に残すが一致根拠へ算入せず、内容を持つ点が2件未満ならAlignment artifactを作らない。

単一offset、秒、小節数、confidence、良否判定をAlignment artifactへ保存しない。

Global Presetの変更を直接読まず、Kirin OSが明示的に更新したWork snapshotだけを読む。

Daisukeの2026-09-05判断により、Kirin OS正本のPreset snapshotは`format = kirin_reference_preset`、`version = 1.0`、`preset_id`、`revision_id`、`name`、`origin`、`purpose`、`checks`、`created_at`の9項目だけを持つ。`work_id`、content hash、`updated_at`、`rank`、`order`、`template_id`を重複しない。

Checkは表示名、比較方法、Candidate bindingを持つ。

Daisukeの2026-09-05判断により、Checkは`check_id`、`label`、`mode`、`view_bindings`、`comparison_mode`、`candidates`、`profile_bindings`、`enabled`の8項目だけを持ち、`mode`は`audition_only | audition_with_facts`に閉じる。Facts欠損で試聴を止めず、Hyphaは推定値や良否を補完しない。

Daisukeの2026-09-05判断により、Kirin OS正本の各Checkは`check_id`、`label`、`mode`、`view_bindings`、`comparison_mode`、`candidates`、`profile_bindings`、`enabled`の8項目だけを持つ。Hypha projectionは必要な表示・再生fieldだけを派生し、現在選択中のCandidate、測定結果、順序fieldを正本Checkへ書き戻さない。

Daisukeの2026-09-05判断により、保存済みPresetの`checks[]`は1〜64件、`check_id`は一意、配列順を表示順の正本とする。Hyphaは空Presetを受け取らず、並べ替え時も`rank`や`order`を派生して正本へ書き戻さない。

Daisukeの2026-09-05判断により、Preset `name`はCheck `label`と同じNFC正規化済み、前後空白なし、1〜80 Unicode文字の一行stringとする。Hyphaは非canonicalな保存値を修復せずprojectionを拒否する。

Daisukeの2026-09-05判断により、Check `label`はNFC正規化済み、前後空白なし、1〜80 Unicode文字の一行stringとする。改行、tab、C0／C1制御文字、Unicode line／paragraph separator、不正surrogateを受け付けない。Hyphaは非canonicalな保存値を修復せずprojectionを拒否し、表示上のellipsisで保存値を変えない。

Daisukeの2026-09-05判断により、`view_bindings`は`waveform | spectrum_full | spectrum_low | loudness | dynamics | transient | stereo`から重複なしで最大3件を選ぶ順序付き配列とする。`audition_only`は0〜3件、`audition_with_facts`は1〜3件を必須とし、先頭を主表示、残りを補助表示としてReferenceの同じ画面へ出す。FREQ／TIME／SPACE等へ画面遷移させず、既存分析描画はprojectionとして再利用して再生経路を結合しない。

Daisukeの2026-09-05判断により、表示配置はPreset／Checkから分離したpresentation preferenceとし、`layout_mode = auto | main | equal`の3値だけを扱う。日本語UIは「自動」「主表示を大きく」「均等」とする。`auto`は画面幅と表示件数に応じた再配置、`main`は先頭viewを大きくした全件表示、`equal`は同じ優先度の全件表示とする。狭い画面の縦積みはruntimeが自動適用し、`stack`を保存値にしない。

主表示／補助表示の順番は`view_bindings[]`だけから導く。Kirin OS ReferenceのHypha PreviewとHypha本体は同じpresentation preferenceを読み、配置変更でPreset revision、Check本文、測定事実、Blind条件を書き換えない。

presentation preferenceはWork単位の`reference/presentation.v1.json`へatomic replaceし、closed top-levelを`format`、`version`、`work_id`、`layout_mode`、`updated_at`の5項目だけにする。Preset artifact、state artifact、History、Listening Trialへhashや本文を追加しない。Hyphaは欠損、破損、Work不一致を無言で`auto`へfallbackし、明示した保存失敗だけを通知する。

Preset変更時は、新Presetにも同じstable `check_id`の有効Checkがある場合だけ選択を維持し、なければ`checks[]`の先頭の有効Checkへ移る。表示名では照合しない。HyphaはPreset確定と同じUI更新で新しい`view_bindings[]`と`layout_mode`を描画し、旧測定値を即座に外す。新しい事実が未準備なら「測定データを準備中」とし、A再生を維持したままB source検証と独立して準備する。

Daisukeの2026-09-05判断により、`comparison_mode`は通常A/B試聴専用の`original | loudness_match | peak_match`に閉じる。Blind開始時はこのfieldを参照せず、Kirin OSが発行したStart artifactの`conditions.gain_match`だけを使用する。Preset projectionへBlind用固定値を追加しない。

通常比較ではA／Bと設定済みの描画を表示する。Blind中は1／2の切替、transport、回答、note、操作不能・再生失敗の必要な事実だけを残し、`view_bindings`の描画、比較数値、凡例、音源名、色分け、Gain情報を全面非表示にする。tooltip、accessibility label、logにもidentityを混ぜない。Blind終了後は保存済み`view_bindings`を変更せず、元の順序の描画へ戻す。通常比較で測定事実が欠けても試聴を止めず、短い欠損事実だけを表示する。直接の測定事実がないvocal balance等は推定描画を作らず`audition_only`で提供する。

Candidate bindingはsource receiptのcontent hashと、sample位置を正本とするCueを持つ。

Daisukeの2026-09-05判断により、永続Candidate Cueは`cue_id`、`label`、`sample_rate_hz`、`start_sample`、`end_sample`、`loop_enabled`の6項目だけを持つ。範囲はsource時間軸のsample frameによる非負の半開区間で、`end_sample`も必須とする。Candidateは1〜4件を持ち、`default_cue_id`は配列内のCueを必ず指す。Cue未設定時はKirin OSが全曲Cueを作る。

親Candidateのdual hashをCueへ重複しない。Hyphaはsource receiptとのsample rate／総frame境界を再生前に検証し、負のhost timeline位置はListening Trial Cueだけで扱う。

Daisukeの2026-09-05判断により、Candidateのclosed top-levelは`candidate_id`、`source_kind`、`source_identity`、`cues`、`default_cue_id`、`note`の6項目だけとする。`source_kind`は`work_version | catalog_track`に閉じる。DAW再生音はHypha runtimeの一時的なAとしてのみ扱い、Preset projectionのCandidateには保存しない。

Daisukeの2026-09-05判断により、`source_identity`はWork Versionでは`work_id / recording_id / version_id / sha256_file / sha256_pcm`、Catalog Trackでは`catalog_reference_id / sha256_file / sha256_pcm`だけを持つ。Hyphaはpath、表示名、曲長、測定値をidentityとして受理せず、content-addressed Source artifactから現在pathを使い、両hashを照合する。

第1候補、第2候補等は`candidates[]`の配列順から取り、順位、絶対path、測定結果、現在選択状態、Blind可否をCandidate projectionへ加えない。表示名だけは`display_name` snapshotとして投影し、永続Candidateへ書き戻さない。Blind可否はTrial開始時のsame-recording／different-revision gateが決める。

Daisukeの2026-09-05判断により、`candidates[]`は最大16件とする。Global／Factory templateと無効Checkは0件を許可するが、Workへ適用された有効Checkは1件以上を必須とする。同一Check内のCandidate IDとsource identityは一意で、同じsourceの複数区間は一Candidateの`cues[]`から選ぶ。Hyphaは先頭2件を第1候補／第2候補として常時表示し、3件目以後を「その他」から選べるようにする。

Daisukeの2026-09-05判断により、Candidate `note`は`null`またはcanonical LFの1〜4000文字に閉じる。Hyphaはnoteをsource identity、判定、推奨へ使用せず、そのまま表示する。非canonicalな保存済みnoteをruntimeで修復しない。

Daisukeの2026-09-05判断により、Profile bindingは`profile_artifact`と`weight_basis_points`だけを持つ。artifactは`profile_id`、`revision_id`、`relative_path`、`sha256`、`bytes`のimmutable receiptで、0〜3件、非空時の整数weight合計は10,000とする。HyphaはProfile本文や測定値をPreset projectionから受けず、検証済みProfile projectionだけを表示に使う。Blind中はProfileを描画しない。

Daisukeの2026-09-05判断に基づき、CheckとCandidateの表示順はPreset projectionの配列順から取る。

`rank`または`order` fieldを受け付けない。

Hyphaは`start_sample`と`end_sample`をsource sample rateからhost timelineへ投影する。

Preset projection内の秒値を正本として受け付けない。

Blindを許可するCandidateは、Kirin OSが発行したexact A bindingも持つ。Trial開始時の関係は固定文字列`same_recording_different_revision`へ再検証し、旧称`different_version`を受け付けない。

Preset projectionのCandidateは安定IDとSHA-256だけを持つ。

Daisukeの2026-09-05判断に基づき、absolute pathとfile revisionはcontent-addressed Source artifactだけに置き、使用時に再検証する。

HyphaはPreset projection内のpath fieldを受け付けない。

unknown field、重複ID、循環参照、orphan receiptはrejectする。

Manifest全体は64 KiBを超えない。

Preset projection一件は2 MiBを超えない。

Kirin OS正本のimmutable Preset snapshotはDaisukeの2026-09-05判断により、RFC 8785 JCS UTF-8 bytesで最大8 MiBとする。Hyphaは読取前にreceiptのbytes／SHA-256と8 MiB上限を検証し、正本全体をそのままruntime projectionへ複製しない。active Checkに必要な情報だけを2 MiB projection上限内へ展開する。

Daisukeの2026-09-05判断により、Global User Preset registryはKirin OSローカル領域の`reference/templates/index.v1.json`を正本とし、`format`、`version`、`revision`、`preset_artifacts`、`updated_at`のexact 5 fieldsだけを持つ。`preset_artifacts[]`は最大128件のcurrent `origin: user` template receiptを配列順で保持する。Factory Presetを含めず、Hyphaもregistryを直接読まない。Hyphaが受け取るのはKirin OSが選択・検証してWorkへ固定したsnapshotのruntime projectionだけである。

同registryはRFC 8785 JCS UTF-8 bytesで最大64 KiBとし、Kirin OSが保存前と読込時にfail closedで検証する。Hyphaへregistryの読込責務や上限処理を移さない。

Daisukeの2026-09-05判断により、Factory Presetを利用者Presetとして複製する場合はKirin OSが新しい`preset_id`と`revision_id`を発行し、`origin: user`とする。未編集のFactoryをWorkへ適用する場合だけFactoryのID、`origin: factory`、content hashを維持する。Hyphaは受領したWork snapshotのidentityを再発行または書き換えない。

同じFactory複製処理で、Kirin OSは全`check_id`、`candidate_id`、`cue_id`を新規発行し、`default_cue_id`を対応する新Cueへ付け替える。Work、Recording、Version、Catalog、音声hash、Profile artifactの外部identityは維持する。Hyphaは複製前後のowned IDを同一視せず、受領したWork snapshotのIDだけを使う。

Source receiptは64 KiBを超えない。

Profile projectionは最大128 KiBとし、音声sourceとして扱わない。

Profileは最大3件をCheckへbindingでき、正規化済みweightと中立な分布だけを表示へ渡す。

## 4. Model分離

`ReferenceAuditionModel.h`へ全責務を足さない。

次のowned sourceへ分ける。

| Source | 責務 |
| --- | --- |
| `ReferenceManifestModel.h` | immutable manifest value |
| `ReferenceManifestProtocol.cpp` | exact JSON validation |
| `ReferencePresetSelection.h` | active preset/check/candidate state |
| `ReferenceSourceCache.cpp` | 最大3 sourceの非RT cache |
| `ReferenceAuditionController.cpp` | lifecycleとatomic command |
| `ReferenceBlindSession.cpp` | same-song Blind state |

新規owned sourceは500行以下にする。

現行499行のControllerへ新責務を直書きしない。

## 5. 非RT準備

Manifest read、path解決、full-file hash、decode、reader open、page fillは非RT threadで行う。

active CheckのCandidate 1と2、次のCheckのCandidate 1をhot setにする。

一つのsourceは現行6 page方式を使う。

総page memoryは64 MiB以下にする。

cache evictionはAudio Threadが参照していないslotだけに行う。

同一sourceの同時準備は一つにまとめる。

新revisionが届いたら古い準備完了をcommitしない。

## 6. Audio Thread契約

Audio Threadへ渡すcommandは固定容量にする。

commandはA、B slot index、gain、source generation、request tokenだけを持つ。

Audio Threadはallocation、lock、file I/O、JSON parse、hash、decode、log出力を行わない。

B pageが不足した場合はAを出力する。

source generationが一致しない場合はAを出力する。

offline renderでは常にAを出力する。

可聴source表示はcallback receipt後だけ更新する。

## 7. UI契約

tab labelは`REF`から`REFERENCE`へ変更する。

内部component IDとdomain IDは`reference`を維持する。

POSTのdomain順は`REFERENCE / LEVEL / TIME / FREQ / SPACE`とする。

Kirin OS entitlementがある新規POST instanceはREFERENCEを表示する。

entitlementが無い新規POST instanceはLEVELを表示し、REFERENCEはdisabled表示にする。

既存projectは保存済みdomain preferenceを復元する。

REFERENCEを表示しても可聴sourceはAのままにする。

PREにはREFERENCE domainとB再生を追加しない。

compact layoutは次の順で表示する。

1. Preset
2. Check
3. Candidate 1 / 2
4. A / B
5. Blind Compare

3番目以後のCandidateは一つのmenuから選ぶ。

path、hash、receipt、internal IDは表示しない。

準備中はAを維持して`PREPARING REFERENCE`を表示する。

sourceが変わった場合はBを無効にし、`SOURCE CHANGED`を表示する。

plugin open、project restore、reconnect、Candidate変更ではAに戻る。

5 sizeすべてでlabel truncation、overlap、keyboard focus、tooltip targetを確認する。

Manifestにある全Presetはすぐ一覧表示する。

active Presetと直近二つのPresetに含まれるsourceを、非RT threadで順番にfull-file検証する。

それ以外は選択時に自動検証する。

revisionが変わらない間は検証結果を再利用する。

active Preset以外の音声全体はRAMへdecodeしない。

別Presetの選択はreader openとpage warm-upを自動で始め、準備専用ボタンを要求しない。

### 7.1 CE2226の画面契約

Hypha REFERENCEは、Kirin OSの設定基盤からDAW内へ現れる菌糸の先端としてCE2226の世界観を画面全体へ明確に出す。

Jungle Modeの有効状態には依存しない。

既存の暗いmycelium backdrop、understory、琥珀色と青緑色のnode、暖色の数値、Reference bridgeの絡み合う線を継承する。

Kirin OS通常モードのHypha Previewも同じvisual familyを限定された窓として使い、Jungle ModeではReference画面全体が同じfamilyへ連続する。

CE2226の表現は背景、surface、接続関係、静的な有機形状へ置く。

Preset、Check、比較する曲、A/B、transport、必要な状態表示の可読性を下げる前景装飾、連続animation、点滅、無目的な発光は追加しない。

compactでは操作階層を優先し、画面サイズが増えた時だけ既存observatory density規則に従って世界表現を増やす。

Blind Compare中は非情報的な静的背景だけを残し、A/Bを推測できる色、線の強弱、数値、波形、音源名、Gain情報、Profile、比較凡例を描画しない。

世界設定を説明するcopyや機能名の物語上のrenameは追加しない。

### 7.2 Work bindingと逆方向event

Hypha POSTは、自分のexact `work_id`と同じManifestだけを読む。

複数POSTが存在してもruntime instance pickerを表示しない。

Work bindingが無いPOSTはAを維持し、Reference Bを無効にする。

Hyphaで行ったPreset、Check、Candidate、Cue、A/B、Revealの操作は、Kirin OS-owned Presetを書き換えず、bounded event fileとして出力する。

Eventはmanifest revision、Work ID、runtime instance ID、event ID、操作、時刻、任意の既存`run_id`、stable ID、当時の表示名snapshotを持つ。

Daisukeの2026-09-05判断により、Hypha runtime eventのclosed top-level envelopeは`format`、`version`、`event_id`、`runtime_instance_id`、`host_process_id`、`work_id`、`manifest_revision`、`event_type`、`occurred_at_ms`、`run_id`、`preset_artifact`、`display_snapshot`、`note`、`payload`の14項目だけを持つ。

`format = kirin_hypha_reference_event`、`version = 1.0`とする。`payload`は`event_type`ごとのclosed objectであり、History `details`を無検証で通す汎用objectにはしない。

Hyphaは`recorded_at`、`previous_event_sha256`、History segment、History transaction、次state pointerをemitしない。Kirin OSがruntime authority、Manifest revision、Preset artifact、表示snapshot、event固有payloadを検証した後にこれらを確定する。

Daisukeの2026-09-05判断に基づき、表示名snapshotはHistory表示専用とし、identity判定には使わない。

Kirin OSは一致するeventだけをidempotentにHistoryへ取り込む。

Kirin OSが取り込んだeventは月単位のappend-only JSONL segmentへ保存し、月の変更やrotateで過去segmentを削除しない。

Eventは一件16 KiB、runtime instanceあたり最大256件とする。

Queue上限時は古いbackground eventから削除し、明示的なRevealまたはnote eventを無言で失わない。

Queueがexplicit eventだけで満杯の場合はevent保存を失敗させ、GUIへ`HISTORY NOT SAVED`を返す。

試聴中の音声は止めない。

Hyphaがメモ修正を受け付ける場合も、既存eventを変更せず`note_revised` eventを新規出力する。

## 8. Blind Compare

BlindはUIのvisible判定だけで許可しない。

Controllerのcommand作成時とAudio ThreadのB選択条件でも再検証する。

必要条件はKirin OS正本の`REF-BLIND-001`と一致させる。

特に、同じ`work_id`と`recording_id`、異なる`version_id`とfile SHA-256を要求する。

live Aが保存済みfile Versionではない場合は、Kirin OSのexact DAW revision bindingを要求し、Trial開始時にBのWork Version identityと`same_recording_different_revision`を再検証する。

Hyphaはfolder、title、duration、測定値からsame songを推測しない。

Catalogと別WorkはBlindへ入れない。

Reveal前はsource identityをpaint、tooltip、accessibility、log、acknowledgementへ出さない。

Blind中はProfile overlayも表示しない。

条件喪失時はRevealせずsessionを破棄してAへ戻す。

## 9. Gain Match

Daisukeの2026-09-05判断により、Bのpositive gainとheadroom不足時の承認済みA減衰を許可する。

既定`a_fixed`はAを0 dBで維持し、Bだけを必要量減衰または増幅する。

True Peak ceilingは`max(-1.000 dBTP, A Cue True Peak, B Cue True Peak)`とし、元のA/Bが持つ最大exposureを超えるpositive gainを適用しない。

完全matchできない場合も通常A/Bは止めない。Blindだけを未開始にし、「Blind用にAを下げる」の1操作を提示する。

利用者が承認した`lower_a_approved`ではpositive loudness deltaの全量をAの減衰へ適用し、Bは0 dBにする。Partial attenuationやA/Bへの分配は行わない。

limiter、clipper、compressor、EQ、normalizationは使わない。

Blindは`a_fixed`または`lower_a_approved`で必要なmatchを完全に適用できる時だけ開始できる。

`lower_a_approved`はBlind開始前の承認画面で「Blind中は基準音源をN.N dB下げます」と明示する。Blind開始後はどちらを下げたかを含むGain情報を表示せず、終了操作に「元の音量へ戻る（+N.N dB）」を表示する。runtime invalidation時も再生中にAを0 dBへ急に戻さず、停止後または明示操作で復帰する。

Kirin OSの現行R-12を先に更新し、producer、Hypha protocol、controller、UI、testsを同じ契約へ揃える。

## 10. v1境界

Kirin OS／Hyphaとも未発売であるため、Reference runtime v2はclean breakとする。

`plugin_data/hypha_ab/v1`のpreparationをv2 Presetへ自動投影せず、v2 manifestが無い場合はAを維持する。

旧fileは書き換えないが、fallback sourceとしても読まない。

旧Reference Labels等のKirin OS内データ保持はOS側の別境界であり、Hypha runtime互換処理へ混在させない。

旧fileの削除は本実装で行わない。

## 11. 影響ファイル

実装開始時に最低限、次を一括監査する。

- `juce_shell/src/reference_audition/ReferenceAuditionModel.h`
- `juce_shell/src/reference_audition/ReferenceAuditionProtocol.cpp`
- `juce_shell/src/reference_audition/ReferenceAuditionRepository.cpp`
- `juce_shell/src/reference_audition/ReferenceAuditionController.cpp`
- `juce_shell/src/reference_audition/ReferenceAuditionLease.cpp`
- `juce_shell/src/reference_audition/ReferenceAudioPages.cpp`
- `juce_shell/src/reference_audition/ReferenceBlindSession.cpp`
- `juce_shell/src/HyphaReferenceComponent.cpp`
- `juce_shell/src/PluginEditorReference.cpp`
- `juce_shell/src/PluginProcessorGuideTransport.cpp`
- `crates/kirin_hypha_ffi/src/reference_audition_ffi.rs`
- `juce_shell/CMakeLists.txt`
- Reference runtime、component、page cache tests

Reference変更で通常のPRE/POST measurement、Record、Guide、CAPTUREの契約を変更しない。

## 12. 必須test

### Protocol

- exact fields、unknown fields、size caps、count caps
- duplicate ID、配列順、orphan receipt
- path traversal、symlink、hash mismatch、revision rollback
- POSIXとWindows path
- clean break後の旧v1 route非到達
- Preset projection hash、missing projection、ManifestとのWork mismatch
- runtime eventのidempotency、queue cap、foreign revision reject
- A binding、`same_recording_different_revision`再検証、stale DAW revision

### Cache

- three-source hot set
- cache eviction while one slot is in use
- page boundary、loop boundary、end-of-file
- unsupported rate/channel
- source generation race
- 64 MiB cap

### Runtime

- open、restore、reconnect、close、invalidatedでA
- B is user initiated
- offline render is A
- missing page is A
- callback receipt gates UI
- no allocation、lock、I/O in process callback
- measurement A remains bit identical and 0 samples latency

### Blind

- gate全条件のtruth table
- 各条件を一つずつ欠落させたreject
- Catalog reject
- other Work reject
- same file duplicate reject
- hidden identityのpaint、tooltip、accessibility、log、receipt scan
- reveal、end、invalidation後のA

### UI

- five sizes
- long Preset、Check、Candidate名
- keyboard focus order
- reduced motion
- PREではReferenceを表示せず、POSTだけがentitlementを評価する
- 新規POSTのREFERENCE初期表示と既存domain preference復元
- `REFERENCE` labelの全入口一致

## 13. 検証順

1. Protocol fixtureを先に固定する。
2. Modelとrepositoryを実装する。
3. Source cacheとcontrollerを実装する。
4. RT contract testとaudio transparencyを通す。
5. Blind gateを実装してidentity leak testを通す。
6. UIを接続して5 sizeを確認する。
7. macOSとWindows CIを通す。
8. Studio One実機でrealtime、restore、offline export、複数POSTを確認する。
9. 対象検証が安定してから全baselineを一度実行する。
10. 初回正式レビュー0件を確認する。

## 14. 実装開始条件

Kirin OSのReference pointer方式はDaisukeの2026-09-05判断で採用済みである。

Work内のPreset snapshotは`reference/presets/<preset_id>/<revision_id>.v1.json`にimmutable fileとして保存する方式が、Daisukeの2026-09-05判断で採用済みである。

`reference/states/<state_sha256>.v1.json`はcontent-addressed immutable artifactであり、active Presetと順序付きPreset／History artifact参照だけを持つindexとする。Check、Candidate、Cue、source binding、Preset本文は持たない。

Kirin OSは新state artifactを完全保存してから`work.json`の最小pointerをatomicに切り替え、旧stateを自動削除しない。

永続Reference artifactのcontent hashはRFC 8785 JCSでcanonicalizeしたBOMなし・末尾改行なしのexact UTF-8 bytesへSHA-256を適用する。`bytes`も同じbytesの長さとする。

state artifactの読込・保存上限は`1,048,576 bytes`とし、超過時に削除やtruncateを行わない。

state `revision`は初回`1`、意味内容が変わった正常commitごとに`+1`とし、no-opとpointer未接続orphanでは進めない。

stateのPreset参照は`preset_id`、`revision_id`、`relative_path`、`sha256`、`bytes`だけ、History参照は`relative_path`、`sha256`、`bytes`だけを持つ。表示名、時刻、event数、本文は参照へ複製しない。

Preset snapshot本文は自己参照になるcontent hashを持たず、親stateのPreset artifact参照だけがsnapshot全体のSHA-256を持つ。

Preset snapshotはcanonical UTCの`created_at`だけを持ち、編集は新しい`revision_id`のimmutable file追加として表す。`updated_at`は持たない。

stateのPreset配列は各`preset_id`の現在revisionを一件だけ持ち、配列順をPreset表示順の正本とする。旧Preset fileと旧stateは保持する。

Kirin OS History eventは、当時の`preset_id`、`revision_id`、`relative_path`、`sha256`、`bytes`を一つの`preset_artifact` receiptとして保持する。Hypha event取込時も、Kirin OSが検証済みprojectionからこのreceiptを確定する。

Kirin OSが月次JSONLへ取り込む際は、次state artifactをcontent-addressed pathへ先に保存してfsyncし、そのreceiptを持つper-event write-ahead transactionをfsyncしてから、segment追記とwork pointer切替を冪等に完了させる。Hyphaは次state artifact、transaction、segment、work pointerを直接書かない。

Kirin OSのHistory transactionは`format`、`version`、`event`、`segment_transition`、`state_transition`の5項目だけを持つJCS artifactとし、`reference/transactions/history/<event_id>.v1.json`へ一時保存する。HyphaはWALを作成・更新せず、同じevent IDとpayloadを再送するだけにする。Kirin OSはpointer切替完了後にWALを削除する。

History eventのJCS＋LFは最大256 KiB、Kirin OS transactionのJCSは最大512 KiBとする。Hyphaは上限超過時にnoteや表示snapshotを切り詰めず、event receipt生成を失敗させる。Kirin OSもsegment読込とtransaction読込で同じ上限を再検証する。

Hyphaの明示操作が失敗した場合は、その操作位置にinlineで「問題の場所」「現在の事実」「直接復旧する主操作」「安全な代替出口」「保持される状態」を表示する。

Toastだけで終了せず、主操作から接続確認、Work選択、音源再選択、再試行を直接実行できるようにする。

Schema、hash、receipt、内部path、event ID、run IDは通常表示に出さず、必要な診断情報だけ「詳しい状況を見る」の先へ分離する。

Reference projectionを受け取れない場合はKirin OSのReferenceへ移動できる出口を示し、Hyphaへ接続できない場合はKirin OS内で聴ける出口を示す。

再生準備またはBlind条件の失敗では基本の曲へ戻し、DAW再生、基本の曲の音量、元ファイル、編集中の設定を変更しない。

Blind中は失敗表示にも音源名、割り当て、比較表示を含めない。

利用者が保存または履歴記録の結果を期待する操作は沈黙させず、再試行可能なpending状態を元の操作位置へ残す。

Daisukeの2026-09-05判断により、Kirin OSから生存中のHypha runtimeへ選択中のWorkとRecordingを渡す短命な権限receiptは`plugin_data/reference/v2/a_bindings/<runtime_instance_id>.json`へruntimeごとの最新一件をatomic replaceする。

Closed top-levelは`format`、`version`、`binding_id`、`runtime_instance_id`、`host_process_id`、`work_id`、`recording_id`、`issued_at_ms`、`lease_expires_at_ms`の9項目だけとする。

`format = kirin_hypha_reference_a_binding`、`version = 1.0`とし、最大8 KiB、BOMなしUTF-8 JSONとする。同じruntime instance、host process、Work、Recordingのlease更新ではbinding IDを維持し、どれかが変わればrotationする。

Hyphaは自分のruntime instance、host process、Workと一致し、現在時刻がlease内にあるreceiptだけを受け取る。A bindingはDAW音声を登録済みVersionとみなさず、Version ID、DAW revision ID、PCM hash、表示名、pathを含めない。

Daisukeの2026-09-05判断により、Kirin OSの復旧箇所へ移動する操作は`plugin_data/reference/v2/recovery_requests/<runtime_instance_id>.json`へruntimeごとの最新一件をatomic replaceする。

Closed top-levelは`format`、`version`、`request_id`、`runtime_instance_id`、`host_process_id`、`work_id`、`destination`、`context`、`requested_at_ms`の9項目だけとする。

`format = kirin_hypha_reference_recovery_request`、`version = 1.0`とし、requestは最大16 KiB、BOMなしUTF-8 JSONとする。

`destination`は`reference | work_binding | candidate_source | candidate_measurement | diagnostics`に閉じ、自由なpath、URL、commandを受け付けない。

`context`は`preset_id`、`check_id`、`candidate_id`の3項目だけを持つ。

比較する曲の再選択と再測定では3 IDをすべて渡し、Kirin OSが表示名から対象を推測しないようにする。

Work接続だけは`work_id`と3 IDを`null`にし、未接続状態から復旧できるようにする。

RequestはHistoryへ保存せず、Reference正本またはPreset projectionを書き換えない。

書込失敗時はAとDAW再生を維持し、同じinline表示へ再試行を残す。

Daisukeの2026-09-05判断により、Kirin OSの処理結果は`plugin_data/reference/v2/recovery_acknowledgements/<runtime_instance_id>.json`へruntimeごとの最新一件をatomic replaceして返す。

Closed top-levelは`format`、`version`、`request_id`、`runtime_instance_id`、`host_process_id`、`outcome`、`handled_at_ms`の7項目だけとする。

`format = kirin_hypha_reference_recovery_acknowledgement`、`version = 1.0`とし、最大8 KiB、BOMなしUTF-8 JSONとする。

`outcome`は`exact_opened | safe_fallback_opened | rejected`に閉じる。

HyphaはRequest ID、runtime instance、host processの完全一致と、request以後の`handled_at_ms`を確認してから受理する。

`safe_fallback_opened`はCandidateの再選択または再測定requestにだけ許可し、Kirin OSが同じWorkのReference入口を開いた事実として扱う。

一致しないacknowledgementは背景処理として無言で読み飛ばす。

`rejected`または応答不在では成功表示にせず、元のinline出口、再試行、Aを維持する。

History `preset_artifact`は操作対象へ固定する。通常A/BとBlindの開始／完了は開始時に使用した同じ5項目receipt、`note_revised`は訂正対象の元eventと同じreceiptをemitし、途中でKirin OSの有効Presetが変わっても追随させない。Kirin OSはIDだけでなくpath、hash、bytesを含む5項目完全一致で検証する。

Kirin OS Historyの一recordはRFC 8785 JCS bytes＋LF一byteとし、直前record全体のSHA-256を`previous_event_sha256`へ保持する。Chainは月次segmentを跨いで継続する。

History event IDは各Work内で一意とする。Hyphaは同じruntime actionを再送するとき`event_id`とpayloadを変更しない。Kirin OSは`(work_id, event_id)`が既存recordと同一JCS内容なら再追記せず成功応答し、同じIDで内容が異なる再送は競合として拒否する。別Workのevent IDとは照合しない。

月次History segmentはKirin OSの`recorded_at`をUTCで評価し、各月の最初を`YYYY-MM.jsonl`、上限後を`YYYY-MM-02.jsonl`以後とする。Hyphaの`occurred_at_ms`が過去月でも過去segmentへ差し込まず、取込時の現行segmentへ追記する。Hyphaはsegment名や配列順を指定しない。

Kirin OSはsegment年月の後戻り、月内連番の欠番、末尾以外への追記を拒否する。PC時刻が前月へ逆行した場合、Hypha receiptを破棄または時刻補正せず再送可能なpending状態に保つ。利用者へは内部segment情報を露出せず、コンピュータの日付と時刻を確認する短い出口だけを表示する。

Kirin OS History eventは固定envelopeと`event_type`別closed `details`を使い、`origin=hypha`の`occurred_at`はHypha receiptの`occurred_at_ms`から変換し、`recorded_at`はKirin OS取込時刻とする。

`recorded_at >= occurred_at`を必須とし、同時刻だけを許可する。Hyphaは`occurred_at_ms`を発生時の値として保持し、Kirin OSは逆転時刻を補正してHistoryへ混入させない。表示順と訂正chainはtimestamp sortではなくJSONL追記順と`previous_event_sha256`を正本とする。

History eventの識別子はDaisukeの2026-09-05判断により`format = kirin_reference_history_event`、`version = 1.0`へ固定する。Hypha runtime receiptをKirin OSがこの固定14項目envelopeへ変換し、9種類の`event_type`に対応しない`details`は取り込まない。

Referenceが発行する`event_id`と非`null`の`run_id`は小文字UUIDv4、`work_id`は既存WorksのUUID形式をそのまま使う。History所有者、開始／完了event、note訂正元、Blind Start artifactの`work_id`を完全一致させる一方、通常A/Bで参照する別Work Versionのsource `work_id`はHistory所有者と同一である必要はない。

`run_id`は比較セッションだけに使う。通常A/Bの開始／完了は同じ非`null` UUIDv4をemitし、Blindの開始／完了はartifactの`trial_id`と同じ値をemitする。Preset操作と`note_revised`はKirin OS History上で`run_id: null`とし、Hyphaが任意の追跡IDを混ぜない。

History envelopeの`note`へ本文を保存できるのは`audition_completed`と`note_revised`だけとし、他7 event typeでは必ず`null`をemitする。Blind初回回答メモはCompleted artifactの`answer.note`だけを正本とし、`blind_compare_completed` receiptへ複製しない。訂正時だけ`note_revised.note`へ新しい本文または削除を表す`null`を渡す。

Historyの`display_snapshot`は`preset_name`、`checks`、`candidates`、`cues`の固定4項目だけを持つ。`checks[]`は`check_id`と`label`、`candidates[]`は`candidate_id`と`display_name`、`cues[]`は`cue_id`と`label`だけを持ち、該当しない配列も空配列として残す。Hyphaはruntime projectionから当該操作に必要なsnapshotだけをevent receiptへ渡し、path、測定値、hash、順位、現在選択状態を混ぜない。

Preset操作eventはPreset名だけ、通常A/B開始は選択したCheck／Candidate／Cueを各1件、完了は開始Checkと確認済みB切替に現れたCandidate／Cueを初回切替順で渡す。Blind開始は検証済みStart artifactのB Work Version／Cueへ一致する各1件だけを渡し、Aの基本曲をCandidateへ混ぜない。Blind完了は対応する開始event、note訂正は元eventのsnapshotを完全コピーする。Kirin OSは自由入力として信用せず、検証済みprojection、artifact、既存eventから再構成して完全一致を確認する。

`candidates[].display_name`はNFC正規化済み、前後空白なし、一行、制御文字なしの1〜160 Unicode文字とする。Kirin OSが160文字を超える現在名をsnapshot化する場合は、元データを変更せず先頭159文字と`…`へ自動短縮する。Hyphaは受領したsnapshotを再短縮または修復しない。

History v1は9 event typeに閉じ、Hyphaがemitするのは`audition_started`、`audition_completed`、`blind_compare_started`、`blind_compare_completed`、`note_revised`に必要なruntime receiptだけとする。Kirin OSが検証後にHistory envelopeへ変換する。

`preset_revision_created.details`はKirin OS専用で、`previous_preset_artifact`の1項目だけを持つ。初回revisionは`null`、更新はevent envelopeの現在receiptと同じ`preset_id`かつ異なる`revision_id`／content hashの直前receiptとする。Hyphaはこのeventをemitしない。

`preset_activated.details`もKirin OS専用で、切替前の`previous_preset_artifact`だけを持ち、初回は`null`とする。同じimmutable Presetの再選択、project restore、plugin reconnectではeventを作らず、Hyphaもこのeventをemitしない。

`preset_reordered.details`もKirin OS専用で、0始まりの`from_index`と`to_index`だけを持つ。移動対象はevent envelopeのPreset receiptから特定し、配列範囲外とno-opを拒否する。Hyphaはこのeventをemitしない。

`preset_removed.details`もKirin OS専用で、`previous_index`と`active_preset_artifact_after`だけを持つ。最後のPresetを外した場合だけ`null`、別Presetが残る場合は削除後の有効Preset receiptを必須とし、削除に伴う自動選択を別の`preset_activated` eventへ重複しない。Hyphaはこのeventをemitしない。

`audition_started.details`は`surface`、`check_id`、`candidate_id`、`cue_id`、`comparison_mode`の5項目だけを持つ。`origin: hypha`は`surface: hypha`だけをemitし、Check、Candidate、Cueの所属と比較方法をruntime projectionへ照合する。時刻、path、source identity、表示名をdetailsへ重複しない。

`audition_completed.details`は`started_event_id`、`a_confirmed_switches`、`candidate_switches`の3項目だけを持つ。各`candidate_switches[]`は`candidate_id`、`cue_id`、`confirmed_switches`だけを持ち、Audio Threadから返ったsource切替完了をCandidate／Cue別に集約する。候補側は1回以上、A側は0回以上とし、click、推定時間、`audible_frames`を通常試聴Historyへ保存しない。

`blind_compare_started.details`は`start_artifact`の1項目だけを持つ。Hyphaは`trial_id`、`relative_path`、`sha256`、`bytes`の最小receiptをemitし、phaseを`start.v1.json`へ固定する。Source、Cue、alignment、Gain Match、commitmentをHistory eventへ複製しない。

`blind_compare_completed.details`は`completed_artifact`の1項目だけを持つ。Hyphaは同じ4項目の最小receiptをemitし、phaseを`completed.v1.json`へ固定する。Kirin OSはCompleted本文のStart receiptとStart本文を検証して同じ`trial_id`へ結び、回答、Reveal、可聴callback receiptをHistoryへ複製しない。

`note_revised.details`は`target_event_id`と`supersedes_event_id`だけを持つ。新しいnoteはevent envelopeへ置き、削除は`note: null`とする。初回訂正は元event、2回目以後は直前の`note_revised`をsupersedeし、Hyphaは自分の認識する最新note以外から分岐するeventをemitしない。Kirin OSは取込時に同じWorkの最新chainへ再検証する。

Reference Blindは未発売段階のclean breakとして旧汎用Blindを置き換え、Kirin OSがReference Listening Trial 1.0として永続化する。旧Blind schemaとfallbackは維持しない。ABXは別目的のListening Protocolとして分離して残す。既定`a_fixed`は基本曲Aを0.0 dBに固定してBだけを必要量調整し、headroom不足時だけ利用者承認済み`lower_a_approved`でAを下げてBを0 dBにする。Blindはstimulus番号の割当だけをrandomizeし、A／Bのsignal処理を交換しない。

Gain Matchの測定基盤は`itu_r_bs_1770_5`、match policyは`kirin_aligned_active_blocks_v1`とする。Multi-anchorで対応するA/Bを400 ms block、75% overlapで測り、両方に有効な音がある対応blockだけのloudness差をmilli-LU整数へ量子化し、外れ値に強い中央値をA/B間の固定gain deltaにする。

GainはBlind開始前に一度だけ確定し、再生中に追従、補間、pumpingさせない。有効block不足時は通常A再生から背景準備を続け、通常A/Bを止めずBlindだけを開始しない。

このpolicyは通常のProgramme Integrated LUFS差および人の等ラウドネス試験と比較し、優位性が確認できるまで最上級の聴感一致を公開主張しない。

Blind開始には連続する有効な対応blockを最低27件要求する。400 ms blockと75% overlapで3秒を覆い、同一音声のloop反復を別blockとして水増ししない。短いCueでは前後の対応音を背景取得して測定事実を補い、聴こえるLoop範囲は変えない。

Start `conditions.gain_match`は`measurement_basis`、`match_policy`、`gain_strategy`、`paired_block_count`、`paired_loudness_delta_median_millilu`、`a_cue_true_peak_millidbtp`、`a_gain_millidb`、`b_gain_millidb`、`b_cue_true_peak_millidbtp`、`ceiling_millidbtp`の10項目だけを持つ。

`a_fixed`ではA gain=0、B gain=deltaとし、`lower_a_approved`ではpositive deltaに限ってA gain=-delta、B gain=0とする。`ceiling_millidbtp`は`max(-1000, A Cue True Peak, B Cue True Peak)`の整数完全一致とし、A/B双方の適用後True Peakを検証する。

Kirin OS実データ2 Recording／8 Versionの予備監査では、全有向32組のうち固定-1 dBTPで5組（15.6%）、固定0 dBTPでも4組（12.5%）が完全match不能だった。track-wide Integrated差とTrue Peakによる保守的代理値であり、正式なaligned Cue corpusではない。

Daisukeの2026-09-05判断により、通常A/Bはgateで拒否せず、Blindの既定`a_fixed`だけへ元のA/B exposureを超えない完全match gateを適用する。不足時は利用者の1操作承認で`lower_a_approved`へ切り替える。

Start artifactのdiscriminatorは`format = "kirin_reference_listening_trial_start"`、`version = "1.0"`の文字列完全一致とする。

Start artifactの`origin`は`kirin_os`または`hypha`の2値だけを許可し、画面名、application version、端末名を混在させない。

Start artifactの`source_kind` matrixは、Kirin OS／MEASUREではA=`work_version`、B=`work_version`、HyphaではA=`daw_revision`、B=`work_version`に固定する。

`daw_revision`はDAW projectの保存版ではなく、Hyphaのaudio callbackへ実際に届いたDAW再生音の作業revisionであり、Works Versionへ自動登録しない。Hyphaが証明する範囲はWork binding、runtime条件、transport sample位置、audio callbackへ届いた音声であり、DAW全体の編集状態ではない。

選択Cueの通常再生中にHypha input callbackからPCMを自動取得し、`daw_revision`の正本とする。利用者へ録音button、保存操作、手入力を要求しない。

取得済みAはBlind中だけimmutableな短命cacheとして再生する。既定`a_fixed`ではAを0.0 dB、BだけをGain Matchし、承認済み`lower_a_approved`ではAを必要量下げてBを0.0 dBにする。取得前でも通常A/Bは使えるが、Blindは取得完了後だけ有効にする。

取得音声をKirin OS、Works、Reference artifactへ保存しない。Hypha runtime cache終了時に破棄し、Start artifactには`daw_revision_id`と`cue_pcm_sha256`だけを保存する。

`work_version` sourceは`source_kind`、`version_id`、`sha256_file`、`sha256_pcm`、`cue_pcm_sha256`だけを持つ。`daw_revision` sourceは`source_kind`、`daw_revision_id`、`cue_pcm_sha256`だけを持つ。

`sha256_file`はencoded file全体、`sha256_pcm`はdecoded PCM全体、`cue_pcm_sha256`は今回の同一Cue範囲という異なるscopeであり、異なるscopeのhash値を相互比較しない。Hypha Blindは同じ`cue_pcm_sha256`のA/Bをrejectする。

Blind開始後にA cache、Work binding、Cue、sample rate、channel layoutまたはruntime instanceが変わった場合、そのTrialをRevealせず中断する。

Daisukeの2026-09-05判断により、Start `conditions.playback`は`engine`、`runtime_fingerprint`、`sample_rate_hz`、`channels`、`switch_policy`の5項目だけを持つ。

Hyphaは`engine = kirin_hypha_reference_v1`だけを受理する。`runtime_fingerprint`はhost process／plugin runtime／callback条件の識別事実を表示名なしで束ねたlowercase SHA-256、`switch_policy`は`callback_boundary_no_crossfade`固定とする。engine、fingerprint、sample rate、channelsの変更でBlindを無効化し、Aへ戻す。

A/Bのsource sample rateが異なる場合、現行の`sample_rate_unsupported`で終わらせない。Audio Thread外で試聴専用変換を準備し、利用者には現在の組合せについて「変換して比較」の短い1操作だけを提示する。sample rate値や変換方式は入力させず、承認前もAは通常再生できる。変換はKirin OSのReference source、Work Version、測定値、HyphaのA経路を変更しない。

DAW project先頭の意図的な空白小節、count-in、pre-rollを有効なtimelineとして扱い、自動trim、別曲判定、Cue先頭への強制補正を行わない。

Hyphaは既存のhost `transport.pos_samples()`をAのDAW timeline正本として使う。A/Bの絶対sample位置一致を要求せず、sourceごとに異なるsample範囲を保持する。

内容alignmentを自動適用し、先頭無音の長さを曲の違いscoreへ算入しない。確定できない場合だけ通常A再生中に「この位置を合わせる」を一度提示し、offset値や小節数を利用者へ入力させない。曖昧な推定のままBlindを開始しない。

単一offsetだけを正本にしない。曲頭以外の複数の内容対応点からordered alignment mapを作り、途中の挿入、削除、構成差、transport jump後も現在位置を囲む近傍anchorからB位置を解決する。

Audio ThreadはPCMとtransport事実をbounded cache／既存timelineへ渡すだけとし、照合はworkerで行う。leading silenceを除外したonset envelope、帯域energy、chroma、既存測定timelineなど複数特徴のcoarse-to-fine照合を使い、単一特徴やfilenameに依存しない。

通常はalignment操作を要求せずPreset呼出しとA通常再生から背景準備する。自動確定できない現在Cueだけに「この位置を合わせる」を出し、sample offset、小節数、PDC値を入力させない。

Alignment mapはsource identityとCue範囲へbindし、A/B source、sample rate、channel layout、transport run変更時にstaleとして再検証する。閾値は実音源corpusでfalse alignmentを測るまで製品値として固定しない。

Start `conditions.alignment`は`algorithm = kirin_content_map_v1`、`method = automatic | assisted`、ordered `anchors[]`だけを持つ。各anchorは`a_sample`と`b_sample`だけを持つ。

2 anchor以上を必須とし、A sample列とB sample列の両方を厳密な昇順にする。単一offset、confidence、空白小節数、秒値を保存しない。`assisted`でも一度示された位置の周辺からworkerが追加anchorを確定し、単一anchorのままBlindを開始しない。

競合の先頭無音Track Align、DAW Sync、Cue／Loop、global start offsetを最低線とし、Hyphaは操作不要のmulti-anchor map、exact Work／Recording／revision identity、Cue単位fail-closed Blind、Kirin OS履歴までを一契約で接続する。

Trialの`cue`は`cue_id`、`loop_enabled`、`a`、`b`だけを持ち、A/B rangeはそれぞれ`sample_rate_hz`、`start_sample`、`end_sample`だけを持つ。範囲は`[start_sample, end_sample)`とする。

Hypha AはDAW host transport上のsigned safe integer、Bは音源file内の非負sample位置を正本とする。曲頭がAの5小節目、Bのfile先頭にあっても独立rangeで一致させ、先頭空白量を別fieldとして保存しない。

いずれも同一Work／Recordingに属し、A/Bのrevision identityと既知の音声content hashが異なる場合だけBlindを許可する。Catalog、別Work、Possible VersionはBlind sourceとして受理しない。

Start artifactの`relationship`は固定文字列`same_recording_different_revision`とする。自由記述objectや旧称`different_version`を受け付けず、Work／Recording、A/B source identity、artifact全体のhashからdomain検証する。

Reference Listening Trial 1.0は`reference/listening_trials/<trial_id>/start.v1.json`と`completed.v1.json`の2 immutable artifactとする。Hyphaはstart／completedに必要なruntime receiptをemitし、Kirin OSだけがartifactを生成する。Completedはstart artifact receiptを参照し、開始条件を複製しない。

Start artifactのclosed top-level envelopeは`format`、`version`、`trial_id`、`work_id`、`recording_id`、`created_at`、`origin`、`sources`、`relationship`、`cue`、`conditions`、`commitment`だけを持つ。`sources`は`{ a, b }`の名前付きobjectとし、A/Bを配列indexへ依存させない。`status`、`mode`、`updated_at`、表示名は重複しない。

Daisukeの2026-09-05判断により、Start `conditions`は`playback`、`alignment`、`gain_match`の3項目だけを持つ。sample rate変換承認、resampler名、変換品質名を永続conditionsへ追加せず、Cue A/Bとplaybackのsample rateから変換の有無を判定する。1操作の承認はBlind開始前のruntime gateとする。

Start `commitment`はDaisukeの2026-09-05判断により、`algorithm = sha256`、`canonicalization = rfc8785_jcs`、`domain = kirin_reference_assignment_v1`、lowercase SHA-256の`value`という4項目だけを持つ。

秘密preimageは`trial_id`、相互に異なる`stimulus_1`／`stimulus_2`の`a | b`、暗号学的乱数32 bytesのlowercase hex `nonce`である。domain UTF-8＋NUL一byte＋preimageのRFC 8785 JCS bytesをSHA-256する。Startへnonce、割当、null placeholder、重複時刻を出さず、Completedで初めて開示する。

Trial artifact receiptは`trial_id`、`relative_path`、`sha256`、`bytes`だけを持ち、phaseは参照元contextとcanonical pathで検証する。

Start artifactは最大128 KiBとし、HyphaもKirin OSも超過したstart payloadを受理しない。これは音声やHistoryの保持上限ではない。

Completed artifactは最大256 KiBとし、HyphaもKirin OSも超過したcompleted payloadを受理しない。これも音声やHistoryの保持上限ではない。

Daisukeの2026-09-05判断により、Completed artifactのclosed top-level envelopeは`format`、`version`、`trial_id`、`completed_at`、`start_artifact`、`answer`、`reveal`、`audible_receipt`の8項目だけを持つ。Work、Recording、Source、Gain Match、Alignmentを複製しない。

Daisukeの2026-09-05判断により、Completed `answer`は`selected_stimulus`と`note`だけを持つ。`selected_stimulus`は`stimulus_1 | stimulus_2`の明示回答に限定し、最後に可聴だったsourceを自動回答にしない。`note`は未入力時`null`、入力時1〜4,000文字とする。無回答のRevealではCompletedを作らず、Startだけの中断Trialとして扱う。

Daisukeの2026-09-05判断により、Completed `audible_receipt`は`basis`、`trial_id`、`runtime_fingerprint`、`first_callback_sequence`、`last_callback_sequence`、`stimulus_1`、`stimulus_2`の7項目だけを持つ。`basis = audio_callback_frames_v1`、callback sequenceはuint64非ゼロ10進文字列とし、各stimulusは`confirmed_switches`と`audible_frames`だけを持つ。両方のAudio Thread確認が正である場合だけCompletedをemitし、固定の最低秒数は課さない。

path内ID／hashと各fieldの一致はKirin OSのdomain contractで検証し、Hypha runtime projectionへ未検証値を渡さない。

Kirin OSの永続artifact時刻はUTC固定・ミリ秒3桁必須のcanonical RFC 3339 `YYYY-MM-DDTHH:mm:ss.SSSZ`とする。Hyphaの短命runtime receiptはUnix millisecondsの`*_at_ms`を使用し、Kirin OS取込境界でcanonical UTCへ変換する。

Reference固有の`preset_id`、`revision_id`、`check_id`、`candidate_id`、`cue_id`、`event_id`、`run_id`はcanonical lowercase UUID v4とし、content identityはlowercase SHA-256で別管理する。

state artifact、Manifest、Preset、Source、Measurement、Alignmentのv2 fixtureはKirin OS側のschema／domain testで固定済みである。

Hyphaにはv2 Manifest／Preset readerとSource readerを追加した。

Manifest revisionが同じ場合は直前のworkspaceをそのまま使い、新revisionのPresetが未完、改変済み、未知fieldを含む場合は新状態へ切り替えず、直前に検証済みのworkspaceを保持する。

Source readerはcontent receipt、Candidate identity、Cue sample範囲、通常file、file revision、full-file SHA-256、sample rate、channel数、総sample frameを試聴前に一括検証する。

Measurement／Alignment本文のHypha reader、Profile projectionのHypha reader、v2 workspaceから再生controllerへの接続は実装済みである。

各readerはclosed schema、content receipt、Work／Candidate／Cue binding、size limitを検証し、いずれかが成立しない新revisionへ切り替えず、直前に検証済みのworkspaceを維持する。

Profile本文とruntime projectionはKirin OS側でschema／domain fixtureが確定した。runtime Profileのclosed top-levelは`format`、`version`、`source_profile_artifact`、`name`、`source_count`、`views`の6項目であり、最大131,072 bytesとする。

Hyphaはsource一覧や生成時刻を読まず、3〜64の`source_count`と中立な`views`だけを読む。Spectrumは周波数軸、その他は0〜10,000 basis pointsの正規化位置軸を使い、各系列の`contributor_count`とp10／median／p90を同時検証する。寄与数3未満はquantile三値がすべて`null`でなければrejectする。

positive B gainは採用済みであり、Kirin OSのR-12へ反映されていることをruntime着手条件とする。

追加fixtureが必要なreaderは、対応するKirin OS schema／domain testと同じexact fields、上限、nullable規則を確認してから実装する。

現在のunrelated変更`juce_shell/JUCE`と未追跡handoff文書には触れない。
