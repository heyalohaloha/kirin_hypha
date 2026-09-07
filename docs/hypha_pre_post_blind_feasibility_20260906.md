# Hypha PRE/POST Blind A/B 実現性と採否の提案

作成日: 2026-09-06
調査基準: B-707 / `1bfcaac`
状態: 2026-09-06、推奨方式と安全条件での着手を承認済み。検証前の機能有効化や公開承認ではない。
追加決定: 2026-09-07、pairは利用者が候補から明示選択したexact PREを正本とする。名前は任意の表示ラベルで、host固有IDは補助診断に限定する。
対象: PRE/POST 比較。SPACE と ATTACK の計画から独立した提案。
利用範囲: 2MIX と TRACK/STEM。2026-09-06 の追加指示を反映。
再点検: 2026-09-06、B-708 の作業木を照合。§11 の安全条件を追加し、実装仕様の凍結前であることを明示。
共通の採否表と最新版の導線は [両計画の再点検と更新案内](hypha_plans_review_and_update_path_20260906.md) を参照。
最新の承認内容と「既存 2 枠内、Blind は同時 1 枠」の制約は [実装承認記録](hypha_implementation_approval_20260906.md) を正本とする。
実装と試験の進捗は [PRE/POST Blind の実装状況](hypha_local_blind_runtime_progress_20260906.md) を参照。
以下は計画時点の調査記録であり、製品機能の完成を示すものではない。
現在の完成条件は [統合実装計画](hypha_integrated_implementation_plan_20260907.md) を参照する。

## 1. 結論

技術的には可能であり、条件を限定した実証へ進む価値がある。
推奨するのは、利用者が指定した同一区間の PRE と POST を取得し、不変な試聴コピーを音量合わせして切り替える方式である。
2MIX だけでなく、mono / stereo の TRACK と STEM も製品対象に含める。
2MIX だけの検証をもって本計画の完了とせず、TRACK/STEM を承認なく後段へ送らない。
常時リアルタイムの PRE 音声転送や、再生音へ追従する Auto Gain を最初から採用することは勧めない。

Hypha が測った処理前後の差を、利用者自身の耳で確認できる点に製品上の価値がある。
ただし、これは設計上の評価であり、利用頻度や購入意向を実測した結論ではない。
音が変わったという測定事実と、どちらを好むかという利用者の判断を結び、Hypha 自身は優劣を判定しない。

現時点で正式搭載や「軽い機能」との判断はできない。
既存の Reference Blind に再利用候補はあるが、PRE/POST の同時取得、対応する時刻の証明、試聴経路の契約は新たに必要になる。
実装開始前に、R-12 と Reference 契約が許す範囲を明文化する。

## 2. 何を比較する機能か

この提案が比較するのは「PRE の挿入位置で取得した音」と「POST の挿入位置で取得した音」である。
任意のプラグインを自動でバイパスした結果や、DAW 全体の設定変更前後を再現する機能ではない。
チェイン途中に分岐、加算、サイドチェインがある場合、その結果を含む挿入位置間の比較になる。
ペアが成立していることだけでは、DAW のルーティングやチェイン構成まで証明できない。

用語を次のように分離する。

- 通常 A 経路: POST に届いた現在の DAW 入力を、そのまま出力する経路。
- POST 試聴コピー: 同一区間から取得し、試聴中は内容を変更しない処理後の音。
- PRE 試聴コピー: その区間に対応する処理前の音。
- 表示 1 / 2: Trial ごとにランダムに割り当て、回答まで PRE/POST を隠す表示。

内部の基準音 A を POST コピー、比較音 B を PRE コピーとする案なら、既存 Reference の「現在の POST が基準」という意味と揃えられる。
表示 1 / 2 のランダム化と、内部 A/B の音量処理は混同しない。

## 3. 実現方式の比較

| 方式 | 得られる体験 | 主な負担と制約 | 採否の提案 |
| --- | --- | --- | --- |
| PRE 音声を常時 POST へ転送 | 再生中の調整を、その場で比較できる | インスタンス間 PCM 通信、PDC、処理順、欠落、動的遅延への対応が必要 | 先に採用しない。継続検討するかを別途判断 |
| 同じ再生パスを取得し、固定コピーを試聴 | 同一演奏区間を繰り返し比較できる | 取得待ちと一時メモリが必要。調整後は再取得が必要 | 実証の推奨方式 |
| DAW で前後を書き出して比較 | 永続ファイルで比較条件を確認しやすい | 書き出しと対応付けの操作が増え、非決定的な処理では別パス差も入る | 製品価値の事前検証に使える対照手順 |

固定コピー方式でも PRE から POST への音声受け渡し自体は必要である。
違いは、取得後に非 RT 側で受け渡しと検証を完了し、試聴 callback が通信相手の到着を待たなくてよい点にある。
既存の測定値 JSON を読むだけでは、試聴用 PCM は得られない。

リアルタイム方式が必ず追加遅延を生むとは断定しない。
チェイン遅延が既知で、必要な PRE 履歴が既に到着していれば整列できる場合はある。
しかし、VST3 の presentation latency は任意の通知で、0 が未知を意味する場合もあるため、全ホストでの成立や 0 samples 維持を一般化できない。
これは SDK 仕様からの設計上の判断であり、Windows 実機を今回確認した結果ではない。
[Steinberg: Audio Presentation Latency](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Change%2BHistory/3.1.0/IAudioPresentationLatency.html)

## 4. 既存資産と、新規に必要なもの

調査時点のコードには次の資産がある。
存在を確認したことと、今回の用途で実機検証が完了していることは別である。

| 既存資産 | 確認した事実 | PRE/POST 比較への適用条件 |
| --- | --- | --- |
| `ReferenceRuntimeACapture` | POST 入力を RT 側の事前確保キューへコピーし、非 RT 側で短命の音声を組み立てる | PRE 側への取得経路、共通の取得範囲、容量管理が必要 |
| `ReferenceRuntimeV2Blind` | A/B を固定 PCM として保持し、ランダム割当と callback receipt を扱う | source 種別と Trial の同一性契約を新設する |
| `analyze_reference_gain` | 対応した 400 ms block の音量差から固定 Gain Match 用の事実を計算する | 新しい対応区間での精度と負荷を検証する |
| `RecordSpool` | Record 計測用 PCM を非 RT worker が一時ファイルへ書く | 試聴用の公開音源ではない。Record の寿命や所有権を流用して変更しない |
| PRE/POST pair claim と Record clock | exact な所有権、取得世代、host 時刻や presentation latency を扱う | PCM の各範囲まで対応が成立することを別途証明する |

現行 Reference の Blind は、Hypha 側 A=`daw_revision`、B=`work_version` に限定される。
同じ Work と Recording の異なる revision が前提で、同一 Cue PCM の組も拒否する。
PRE コピーを架空の `work_version` として登録したり、この検証を外したりして対応しない。
新しい比較関係と source 契約を定義し、既存 Reference の閉じた schema を黙って拡張しない。

現行の一時取得は 4 秒を基準とし、先頭の非活動 sample を飛ばす処理がある。
Reference の対応位置探索にも 20 ms envelope を使う処理がある。
これらをそのまま PRE/POST の sample 単位整列へ転用しない。
処理によって先頭の活動判定が異なると、比較したい時間差を自動 trim で消したり、別区間を対応させたりする危険がある。

## 5. Auto Gain は常時追従させない

比較には音量差を抑える仕組みが必要だが、再生中に動き続ける AGC は不要である。
取得した同一区間を非 RT 側で一度解析し、Trial 中は一定の gain を適用すればよい。
処理によるダイナミクスの違いは残し、コンプレッサーや limiter で差を消さない。

再利用候補は、既存の `kirin_aligned_active_blocks_v1` である。
400 ms、75% overlap の両側に有効音がある対応 block を使い、最長の連続区間から音量差の中央値を求める。
現行実装は最低 27 block、すなわち 3 秒の連続した有効範囲を要求する。
短い Cue を同じ音の loop 反復で水増しせず、無音や不足時は Blind を開始しない。

ただし、疎な drum、単発効果音、短い vocal phrase には、この連続 3 秒条件が適さない可能性がある。
TRACK 対応を、既存 Gain Match が通る素材だけで検証済みとしない。
連続した PCM の中にある正当な無音と、取得に失敗した欠落を区別し、短い event と tail に適した固定 Gain policy の要否を検証する。
必要なら用途ごとに明示された別 policy を定義し、同じ名前のまま最低条件を緩めない。
音量合わせが成立しない素材を、未補正のまま公平な Blind として開始しない。

これは既存の独自 match policy であり、人の知覚的な等ラウドネスを常に保証するものではない。
強い EQ、低域増減、コンプレッション、極端な低音量素材では、同一区間の Integrated LUFS 差と人の聴感評価も対照にする。
音量差が好みを決めてしまう組が多ければ、固定値の計算法を再検討する。

gain の既定案は既存 Reference に合わせ、POST コピーを 0 dB の基準にして PRE コピーだけを必要量調整する。
True Peak 上限は既存の `max(-1 dBTP, A の元 True Peak, B の元 True Peak)` を候補とし、元からある最大 exposure を新たに増やさない。
これは 0 dBTP 未満の保証ではないため、既に高い peak を持つ素材も検証対象とする。
必要な gain を全量適用できなければ、不完全な音量合わせで Blind を始めない。

マスタリングで POST が大きく、PRE の crest が高い組では、PRE を持ち上げると peak 上限に当たる可能性がある。
したがって、既定方式だけで実用になる割合も測る必要がある。

2026-09-05 の Reference 正本には、利用者承認時だけ基準コピーを下げる `lower_a_approved` が既にある。
一方、Hypha の現行 R-12 は通常 A 経路を不変とし、登録済み Reference の試聴を例外としている。
PRE/POST コピーへの適用は未承認として扱い、次を契約上の判断点にする。

- 基準コピーを下げる例外を、新しい PRE/POST 試聴にも適用するか。
- 試聴中断時の急な音量復帰をどう防ぎ、通常 A 経路へいつ戻すか。
- 試聴を終えた状態と、承認済みの一時減衰が残った状態をどう区別するか。

既存 `renderInvalidatedA()` は、条件により再生中の入力へ一時減衰を維持する。
この復帰方式まで自動的に移植すると、単に「コピーだけを変える」と説明した範囲を越える。
例外の開始条件と終了条件を Hypha の正本へ反映するまでは、新用途に採用しない。
A/B 双方の normalization、共通減衰、動的 leveling は既存 Reference 契約も採用していないため、本提案でも追加しない。

## 6. 通常計測を壊さない構造

取得、準備、試聴を別の状態に分離する。
提案する状態遷移は、通常計測 → 明示取得 → 検証中 → 試聴準備完了 → 明示的な Blind 開始 → 終了、である。
準備完了だけでは出音を切り替えない。

通常計測、取得中、準備中は A の bit identity と 0 samples latency を維持する。
Audio Thread では事前確保済み領域への bounded copy と通知だけを行い、ファイル操作、解析、メモリ確保、ロック、相手待ちを行わない。
キュー不足や worker 障害ではその取得を不成立にし、DAW の再生を待たせない。

試聴時だけ POST 出力を検証済みコピーへ切り替える。
POST コピーをもう一度 PRE と POST の間のチェインへ入れず、処理後の音を二重処理しない。
元の PRE/POST 計測と Record は引き続き実入力を扱い、試聴コピーの値で上書きしない。
計測に使う入力を取得した後に試聴出力を選択する既存順序は維持する。
ただし、この順序が保護するのは試聴する POST 自身の入力であり、下流の別ペアの入力ではない。
TRACK 試聴の出力は下流の bus や 2MIX へ届くため、正本 Record の保護には §11.1 の共通排他が別に必要になる。

Reference 再生と PRE/POST Blind が同時に出力を所有しない、単一の試聴状態管理が必要である。
別の試聴、ペア変更、インスタンス破棄、SR 変更、channel 変更、再取得では同じ Trial を継続しない。
停止、非 realtime render、バイパス、復元時にコピーが勝手に再生されないことを検証する。
明示取得の失敗は短い理由を示し、未取得のまま成功表示しない。
内部の未使用機能の失敗は R-28 に従い通知しない。

試聴コピーを Record、Works、DAW project state へ自動保存しない。
短命 cache の寿命、byte 上限、削除対象を所有者と取得世代へ結ぶ。
非 RT の一時ファイルを使う場合は、原子的な公開、hash と範囲の検証、権限、破損、容量不足、クラッシュ後の所有ファイル回収も実装条件に含める。
JSON に PCM を埋め込まず、既存 Record spool を別インスタンスから勝手に読む設計にしない。

## 7. 本当の難所は時刻と試聴条件

### 7.1 同一区間の証明

同じ wall-clock 時刻や、同じ表示秒数だけでは対応とみなさない。
exact pair claim に加え、両インスタンスの識別子、取得 run、連続した epoch、native sample 範囲、sample rate、channel layout、PCM hash、確認できた遅延の根拠を束ねる。
PRE/POST 双方が準備完了を返してから取得範囲を確定し、片側だけの早期開始は採用しない。
長いチェイン遅延や tail を含む対応範囲を取得できなければ、その比較は不成立にする。

PDC の補正を一度だけ適用し、既に補正された時刻へ二重に offset を足さない。
transport jump、loop の別周回、停止再開、動的 PDC 変更、片側の欠落を継ぎ合わせない。
これは取得時の連続性条件である。
取得後の試聴では、同じ承認済み範囲を DAW が正確に loop する動作と、範囲外への seek を区別する。
正確な loop 一周ごとに取得失敗として Trial を捨てる設計にはせず、再生時の世代と取得 PCM の世代を分離する。
host 時刻があっても、block 内 loop 境界の通知精度まで仮定しない。
[Steinberg: ProcessContext](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)

解析で補う場合も、曖昧な相関だけで整列済みにしない。
既知の純遅延と、EQ の位相、リバーブの tail、transient の変化を区別し、後者を time warp や補正処理で消さない。
強い音色変化や反復音で位置が一意に定まらなければ、通常 A を維持する。

### 7.2 2MIX と TRACK/STEM の試聴条件

2MIX と TRACK/STEM は同じ取得と試聴の基盤を使い、別バイナリや常駐エンジンを追加しない。
ただし、比較対象と音量合わせの意味は次のように分ける。

| 使用場所 | 比較するもの | 音量合わせの基準 | 必須条件 |
| --- | --- | --- | --- |
| 2MIX | 最終チェインの処理前後 | 2MIX の POST 位置 | 後段の処理や automation の条件を固定する |
| TRACK 単体 | 対象トラックの処理前後 | 対象 TRACK の POST 位置 | mono / stereo、短い音、無音、tail を検証する |
| TRACK の mix 内試聴 | ほかのトラックと混ぜたときの対象処理の違い | 対象 TRACK の POST 位置 | 固定コピーを残りの live mix と同一時刻で再生する |
| STEM | subgroup の処理前後 | 対象 STEM の POST 位置 | 下流 bus、外部 send、sidechain を含む比較範囲を確認する |

トラック単体で聴く場合の solo や monitoring は、利用者が DAW 側で設定する。
Hypha が solo 状態や routing 全体を確認できるとは主張しない。
mono を勝手に stereo 化せず、channel 数を維持したまま取得、音量合わせ、再生する。
2MIX / TRACK / STEM の区分を、音の種類やプラグイン名から推測しない。

mix 内では、対象トラックのコピーだけを自由な位置から loop させない。
DAW の同じ取得範囲へ同期し、ほかのトラック、automation、sidechain と対応する時刻を使う。
DAW が範囲外へ移動した場合はその Trial を開示せず中断し、通常 A へ戻る方針を基本にする。
承認済み減衰がある場合の復帰方法は §5 の契約判断に従う。
局所的な時間合わせが、ほかのトラックとの位相関係を壊さないことも検証する。

下流の bus compressor や master limiter が、PRE/POST の差に応じて異なる動作をすることはあり得る。
それも mix 内で聴く結果に含まれるが、対象トラックで音量を合わせたことを「mix 全体が等ラウドネス」とは説明しない。
全体の音量まで一致させるために、master gain やほかのトラックを Hypha が操作しない。
試聴開始前に、音量合わせの基準が対象の POST 位置であることを分かるようにする。

POST より前で分岐した send の音は、POST 出力の切替では変わらない。
その構成をトラック全経路の処理前後比較と呼ばず、「この PRE/POST 挿入位置間の比較」とする。
parallel compression、wet/dry、external sidechain、feedback を試験に含め、保証できない routing は制約を明示する。
PRE の時点で既に混ざっている音源を分離する機能も追加しない。

取得済みの対象トラックと、後から編集されたほかのトラックを混ぜると、取得時の mix とは異なる条件になる。
Hypha が DAW 全体の編集を検知できるとは主張せず、変更後は新しい比較として再取得する運用を明示する。
検知可能な pair、sample rate、channel、transport 世代の変更は自動で失効させる。

Hypha は他プラグインの bypass、DAW の routing、fader、solo を自動変更しない。
これらを自動制御しないと実用にならない場合は、境界を広げて継続せず採否を再検討する。

### 7.3 Blind が成立する条件

Trial 中は PRE/POST 名、色、測定値、差分、Gain、波形、tooltips、accessibility の識別情報を隠す。
別に開いた PRE/POST ウィンドウやメーターにも適用範囲を定める。
DAW や他プラグインの画面までは制御できないため、環境全体を保証する二重盲検とは呼ばない。

表示の選択状態は、callback がその刺激を出力した receipt の後に変える。
これは POST の出力確認であり、スピーカーから耳に届いたことや、下流で mute されていないことの証明ではない。
両刺激の必要な試聴確認がない状態で回答を完了させない。

切替クリック、無音長、再開位置、反応時間が割当の手掛かりにならないことも確認する。
既存 Reference の切替契約は `callback_boundary_no_crossfade` である。
クリック対策として crossfade を無断で足さず、必要なら新用途の切替方針として承認し、混合区間を評価対象や出力 receipt と区別する。

回答は好みと、判断できなかった事実を記録する。
「好みがない」と「差が分からない」は区別し、Hypha が処理の勝敗を表示しない。
Blind A/B の好みの比較を、ABX の識別検定や音質改善の証明とは呼ばない。
厳密な主観評価では順序の無作為化や対照条件も必要になるが、本機能が ITU-R BS.1116 準拠であるとは主張しない。
[ITU-R BS.1116-3: 実験設計と主観評価の方法](https://www.itu.int/rec/R-REC-BS.1116-3-201502-I/en)

## 8. 重さをどう判断するか

float32 の PRE/POST 2 音源を保持する生 PCM の容量は、`秒数 × sample rate × channels × 4 bytes × 2` となる。
次は stereo の計算値で、既定秒数や全体 RAM 上限を決めた表ではない。

| 取得時間 | 48 kHz | 192 kHz |
| --- | ---: | ---: |
| 4 秒 | 2.93 MiB | 11.72 MiB |
| 10 秒 | 7.32 MiB | 29.30 MiB |
| 30 秒 | 21.97 MiB | 87.89 MiB |

キュー、整列用の余白、解析領域、公開 snapshot、コピーの重複、一時ファイルは別に必要になる。
既存 `RuntimeACapture` も、構築時の queue 約 2 MiB と capture 領域 16 MiB を持つため、短い取得時間だけから消費量を判断できない。
全 PRE/POST にこの構成を常時追加しない。
通常未使用時の追加 PCM buffer を持たず、非 RT 側で準備した上限付き領域を、明示取得したペアへ渡す案を評価する。

固定 gain の適用は sample ごとの乗算で済むが、解析、コピー、取得待ち、他 worker との競合は残る。
既存 Gain Match は block ごとに meter を構築するため、再利用時も非 RT 処理時間と peak RAM を測る。
既存の 2 つの解析所有枠を増やさず、追加解析の優先順位と競合時の扱いを決める。
試聴を始めるために、通常計測、Record、音声 callback の余裕を失ってはならない。

## 9. 実証と正式搭載の判断条件

実証実装を行うなら、次の順番を提案する。
現時点では、以下の作業開始も未承認である。

1. R-12、source 契約、固定取得方式、Gain Match の例外、ライセンス区分を決める。
2. 既存の書き出しを使う対照試聴で、処理前後の比較が実際の判断に役立つか確認する。
3. UI を作り込む前に、同一パスの両側取得、整列、固定試聴、失敗時の通常 A 維持を検証する。
4. 2MIX と TRACK/STEM の実プロジェクトで取得待ち、音量合わせの不成立率、再取得の負担、利用意向を確かめる。
5. 技術条件と体験の両方が成立した場合だけ正式搭載へ進む。

少なくとも次の検証を省略しない。

| 対象 | 判定に必要な証拠 |
| --- | --- |
| 通常経路 | 未使用、取得中、準備中、通常復帰完了後で入出力 bit identical、追加 latency 0 samples。承認済み減衰の復帰待ちは別状態として検証 |
| RT 安全性 | allocation / lock / I/O が Audio Thread にないこと。小 block、高 SR、worker 停滞時の callback 分布と deadline 超過を計測 |
| 対応時刻 | 既知の純遅延で残差 0 sample。block サイズ変更、loop、seek、動的 PDC で誤った組を受理しない |
| PCM 完全性 | 全範囲の連続性、hash、両側の取得完了、有限値。片側欠落や古い世代を拒否 |
| Gain | 純 gain 差の既知信号で補正誤差を測る。候補目標は 0.1 dB 以内。実音源の知覚的一致とは分ける |
| Blind | 同一 PCM の対照、既知差、ランダム割当、切替手掛かり、情報漏れ、出力 receipt、未試聴回答を検証 |
| 障害 | allocation 失敗、取得 timeout、容量不足、破損、worker 再起動、PRE 消失、複数ペア、復元、offline export |
| 通常機能との分離 | Reference 同時開始を拒否し、測定、Record、差分、Capture、既存表示を回帰検証 |
| TRACK/STEM | 単体と mix 内の双方で、mono / stereo、drum、bass、vocal、持続音、疎な音、短い phrase、tail を検証 |
| mix 内同期 | 残りのトラック、DAW loop、automation、phase、send、parallel bus、sidechain、下流の非線形処理を検証 |
| 多数配置 | 複数 TRACK に PRE/POST があっても明示した exact pair だけを取得し、同時試聴の競合を拒否。未使用ペアの負荷と取得予算を計測 |
| ホスト | macOS と Windows の Studio One/Studio Pro の実機で、2MIX stereo と TRACK/STEM mono / stereo を確認。Windows は操作再開の調整後に行う |

同一 PCM の対照は、試聴機構自体が差を作らないことを確認するために必要である。
既存 Reference の「同一内容は拒否」を変更せず、新しい PRE/POST 比較で無変化の組をどう扱うかを別に定義する。
公開 UI の正常状態と、内部検証の対照条件を混同しない。

CPU、peak RAM、取得秒数、準備待ち、不成立率、複数 instance 上限の数値は実測後に固定する。
UI は対象ペア、試聴準備、1 / 2、回答、復帰操作の読みやすさを共通基準にし、100%、125%、150%、200% と別ウィンドウでの情報漏れも検証する。
「追加 CPU は小さい」「Review 0 件」を事前の合格事実にしない。
既存の未完了検証も新機能の検証で置き換えず、公開判定では両方を解消する。
公開時には通常の macOS と Windows の全配布チャネルの条件を満たす。

## 10. 採否に先立つ判断事項

推奨は「限定した実証へ進む」であり、「すぐ正式搭載する」ではない。
特に次の 3 点を先に判断する。

1. 調整を即座に反映する live 比較ではなく、再取得を伴う固定コピー方式で目的を満たすか。
2. 一時取得した PRE/POST コピーの明示試聴と、必要時の承認済み基準コピー減衰を Hypha の境界に含めるか。
3. Hypha 単体で提供するか、既存 Reference と同じ Kirin OS entitlement に含めるか。

単体プラグインとしての価値を狙うなら、ローカル PRE/POST 比較は単体提供を検討する理由がある。
ただし、現行 Reference の entitlement や Work binding を外す権限にはならない。
ローカル比較に必要な identity と、Kirin OS の Work 履歴への任意連携を分離して契約化する。
GPLv3 の Hypha 内の再利用に留め、Kirin OS の非公開コードを共有しない。

既存 SPACE / ATTACK 計画との優先順位は変更していない。
TRACK/STEM 対応は本計画の対象として確定し、その具体的な安全条件と実装採否が未決定である。
常時ライブ転送、DAW 全体の routing 自動制御、書き出し処理への介入まで必要なら、本提案の境界を越えるため改めて採否を決める。

## 11. 再点検で追加した安全条件と仕様凍結

### 11.1 下流を含む Record と試聴の排他

トラックの POST が PRE コピーを出力すれば、下流の 2MIX の PRE/POST はその音を入力として計測する。
これは routing から生じる影響で、対象 POST 内で計測を先に行うだけでは防げない。
本提案の当初版にある「正本を変更しない」を、DAW 内の全計測値が影響を受けないという保証へ広げない。

推奨する保護単位は、routing を推測しない、同一 DAW の確認済み Hypha participant scope 全体である。
その scope 内では、Reference を含む試聴出力の所有者を一つにし、Record の準備中、armed、記録中、finalize 中との同時開始を許可しない。
対象ペア以外の Keep と All Keep も、共通の予約処理で相互排他にする。
既に動いている Record や試聴を勝手に停止し、新しい操作を通してはならない。
明示操作を断った理由と、利用者が先に終了させる対象を示す。

開始前の一度の状態確認だけでは race が残るため、participant の確認、試聴 lease の確定、Record 予約が同じ排他契約に従う必要がある。
両操作を同時要求するテストでは、一方だけが成立し、他方は出力も Record 作成も開始しないことを確認する。
新しい protocol を理解しない旧 participant、期限切れ lease、確認できない別 process 構成を、排他確認済みとして扱わない。
複数 DAW、別 project、同名 bus を混同しない scope 定義を契約担当と固定する。
安全な scope を構成できない場合は Blind を開始せず、既存の通常計測を維持する。

試聴中の live meter が下流入力の変化を読むことはあり得る。
試聴期間の source provenance を持たせ、通常の作業結果として自動 Capture や比較集計へ混入させない設計を決める。
既存 Meter Session の RESET や過去の Record 書換えで帳尻を合わせない。
混在を隔離する変更が既存メーター契約を越える場合も、R-12 とともに採否判断へ戻す。

試聴後の下流 compressor や reverb の内部状態まで、Hypha は巻き戻せない。
通常 A に戻ったことと、チェイン全体が取得前の状態に戻ったことを同一視しない。
正本 Record の再開は通常復帰の明示確認を経て行い、下流の残留音を含む pre-roll の必要性も実プロジェクトで検証する。
この制約を説明できないまま「試聴はセッションに一切影響しない」と公開しない。

### 11.2 出力復帰と callback 境界

次の表は、基準コピーを減衰しない通常案の状態契約である。
承認済み減衰を採用する場合は、別の復帰待ち状態を R-12 に合意してから表を拡張する。

| 状態または操作 | 出力とデータの扱い |
| --- | --- |
| 準備中、取得失敗 | 通常 A のみ。Trial の割当や成功結果を作らない |
| 準備完了 | 通常 A のまま。開始操作が必要 |
| Blind 中 | 同一時計の固定コピーのみ。実出力 receipt の確認後に選択表示を変更 |
| 正確な DAW loop | 検証済み Cue 内の対応位置を再生し、同じ割当を維持 |
| 範囲外 seek、停止、pair 変更 | 割当を開示せず中断。通常 A への復帰と Trial 失効を分けて記録 |
| offline render、host bypass、復元 | 最初の該当 callback から試聴コピーを出さない。再開時に自動で Blind へ戻らない |
| editor を閉じる | 初期案では試聴を終了する。画面のないままコピーや一時 gain を残さない |
| 回答と Reveal | 両刺激の必要な出力確認と回答の成立後にのみ割当を表示。終了しても元 PCM は不変 |

Cue 終端が block の途中に来る場合を必須テストにする。
block 全体を無条件に modulo 再生したり、足りない部分へ前回の buffer を残したりしない。
正確な split ができる場合のコピー範囲、通常復帰部分、receipt の frame 数を定義し、判断不能ならコピーを出す前にその block を通常 A へ戻す。
loop、最初の再生、最後の再生、素早い連打で無音長やクリックが刺激の割当と結び付かないことを確認する。

固定コピーの再生も DAW の callback が来る範囲でしか動作できない。
DAW 停止中に独立再生する機能や、DAW を自動で再生開始させる機能は含めない。
PRE/POST の整列だけでなく、TRACK と残りの mix の対応時刻を synthetic impulse と実音源の両方で確認する。

### 11.3 所有権と情報非開示

非 RT worker が PCM を準備してから、世代付きの immutable 所有権を Audio Thread へ公開する。
取消、再取得、editor 破棄、worker 再起動中に callback が参照している PCM を解放しない。
Audio Thread で最後の参照が外れたときに巨大 buffer の destructor が走らないよう、非 RT 回収の境界も設計する。
キュー上限、取得 timeout、最大 seconds と bytes は sample rate と participant 数を含めた計算表にする。

Blind の非開示対象には、新しい SPACE と ATTACK、PRE の別ウィンドウ、履歴、Capture、copy-to-clipboard、通常ログを含める。
更新バッジや外部ブラウザへの誘導も Trial 中は出さず、試聴を中断させる新しい overlay を作らない。
ローカルの診断 artifact を含めた検査と、通常の UI で隠す対象を分け、DAW や外部 debugger まで情報隠蔽できるとは主張しない。
結果保存に失敗した場合は、回答が保存されたように見せず、音声の通常復帰を保存成功待ちにしない。

### 11.4 製品実装へ渡す凍結表

| 要求 ID | 実装前に固定するもの | 合格証拠 |
| --- | --- | --- |
| BL-01 | ローカル source identity、scope、pair、取得と試聴それぞれの世代 | 別 project、旧版、途中切替、重複通知で混線しない |
| BL-02 | 取得開始 barrier、範囲、時刻写像、PDC 根拠、再生 loop | 既知遅延の 0 sample 残差、mix 内同期、block 終端試験 |
| BL-03 | 短い TRACK を含む固定 Gain policy と headroom、復帰方針 | 純 gain 既知差、無音、疎な音、強い EQ、clip 境界、人による確認 |
| BL-04 | 試聴 lease と Keep / All Keep の共通排他 | 同時要求、nested pairs、下流 Record、復元、期限切れ、通常復帰 |
| BL-05 | PCM の上限、所有権、cancel、非 RT 回収 | allocation 失敗、timeout、破損、worker 停滞、SR 変更で A が継続 |
| BL-06 | 切替、最小試聴量、回答、Reveal、非開示対象 | 両刺激 receipt、同一 PCM 対照、連打、初回刺激、Capture と accessibility |
| BL-07 | 単体か OS entitlement か、保存先と寿命、旧版互換 | 権限 truth table、offline、Work 不在、旧 PRE、新 POST、結果保存失敗 |
| BL-08 | 2MIX と TRACK/STEM の体験と性能予算 | 両 OS、mono / stereo、短い音、mix 内、複数配置、画面倍率別の実機確認 |

未確定値を実装者が推測で埋めず、採否担当、実験入力、期待値、変更する契約、対応テストを台帳へ記録する。
現時点では BL-03 と BL-04 を含む仕様の承認と実証が未完了であり、正式な製品実装の開始条件は満たしていない。
計画に検証項目を書いたことを、音声安全性や Review 0 件の達成証拠にはしない。

## 12. 調査範囲と根拠

今回はコードと契約の読解、一次資料の照合、容量計算、文書の作成だけを行った。
音声実装、Windows 操作、build、install、release は行っていない。
日本語技術文書の規範に沿い、確認済みの事実、計算値、未承認の設計案を分けた。

ローカル根拠:

- [Hypha 製品契約](hypha_meter_product_contract_20260831.md)
- [Reference runtime v2 handoff](reference_runtime_v2_handoff_20260905.md)
- [Reference 横断契約](/Users/nishiodaisuke/Dev/kirin_sense_lens/docs/reference_product_contract_20260905.md): §8、§9、Listening Trial の source matrix と Hypha runtime 契約。
- [POST の計測と出力切替](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/PluginProcessor.cpp:506)
- [一時音声取得](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/reference_audition/ReferenceRuntimeACapture.cpp)
- [Blind 準備](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/reference_audition/ReferenceRuntimeV2Blind.cpp)
- [Gain 方針とランダム割当](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/reference_audition/ReferenceRuntimeV2BlindState.cpp)
- [Blind 出力と中断時減衰](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/reference_audition/ReferenceRuntimeV2BlindRealtime.cpp)
- [Gain Match 解析](/Users/nishiodaisuke/Dev/kirin_hypha/crates/kirin_measure/src/reference_gain.rs)
- [Record の一時 PCM](/Users/nishiodaisuke/Dev/kirin_hypha/crates/kirin_measure/src/record_spool.rs)
- [PRE 所有権の公開](/Users/nishiodaisuke/Dev/kirin_hypha/crates/kirin_measure/src/io_thread_post_pair_claim.rs)
- [既存の検証結果と未完了事項](hypha_windows_observability_fix_20260906.md)
- [独立した SPACE / ATTACK 計画](hypha_space_attack_plan_20260906.md)

外部根拠は本文の該当箇所にある Steinberg と ITU の一次資料だけを使用した。
市場シェア、競合優位性、知覚的一致率、CPU 使用率の実測は今回行っていない。
