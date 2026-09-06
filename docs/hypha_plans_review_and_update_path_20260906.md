# Hypha 両計画の再点検と最新版への導線

作成日: 2026-09-06
調査基準: B-708 / `4e0364f`
状態: 計画の改訂。製品実装、ネットワーク機能、公開の承認ではない。
対象: SPACE / ATTACK、PRE/POST Blind A/B、共通の更新案内。

## 1. 再点検の判定

SPACE / ATTACK と PRE/POST Blind の方向性は維持してよい。
前者は測定値の意味と適用素材を限定し、後者は利用者自身の聴取判断を補助するため、Hypha の計測と判断尊重の原則につながる。
ただし、当初の文書は研究と実現性評価の計画であり、そのまま製品コードを完成させられる凍結仕様ではなかった。

今回、コードで確認できる制約と、組み合わせた場合の設計上の問題を区別して追記した。
特に、SPACE の無音入力、SPACE の absolute 固定、Blind の下流 Record、取得時と試聴時の loop の違いは、実装後のレビューを待たずに計画へ反映する必要があった。
最新版の導線は測定と独立した共通機能として追加し、自動通知とは別の採否単位にした。

「レビュー0件」は目標として扱うが、将来のコードや未実施の実機試験に対して保証しない。
完了条件は、承認済み要求に対応する検証証拠があり、対象の未解決レビュー指摘が 0 件で、skip を pass と数えていないことである。
新しい指摘が出た場合は該当要求と回帰テストへ戻し、承認なく対象外や後工程へ移して件数を減らさない。

改訂した計画:

- [SPACE / ATTACK 計画](hypha_space_attack_plan_20260906.md): SA-01〜SA-07 を追加。
- [PRE/POST Blind 計画](hypha_pre_post_blind_feasibility_20260906.md): BL-01〜BL-08 を追加。

## 2. 見つかった不足と反映先

以下は計画上の不足であり、既存製品の不具合が実機で再現したという報告ではない。
「コード事実」と「設計上の推論」を分けて読む。

| ID | 根拠と不足 | 今回の補強 | 実装前の状態 |
| --- | --- | --- | --- |
| RV-01 | コード事実: SPACE の capabilities は domain 全体を absolute 固定 | FIELD を維持し、DECAY の差分、操作、Capture を subview 単位に分離 | SA-01 の承認と実装が必要 |
| RV-02 | コード事実: Record 外の無音は通常 sample push から外れる | ゼロ、tail、drop、callback 停止を区別する連続窓の入口 | SA-02 の設計と試験が必要 |
| RV-03 | 計画不足: 共通起点の権限と DECAY の区間選択が未固定 | PRE event 起点案、丸め、区間選択、重なり分類、収支を追加 | SA-02 / SA-03 の実験と定義承認が必要 |
| RV-04 | コード事実: 解析 mode と lease は排他的で DECAY route はない | 一つの owner 内で候補生成と減衰計算を行う構成 | SA-01 / SA-07 の予算固定が必要 |
| RV-05 | 設計上の推論: TRACK の試聴音は下流の別ペアへ届く | 同じ participant scope 内の試聴と Keep / All Keep の排他、計測 provenance | BL-04 の契約承認と実証が必要 |
| RV-06 | 計画不足: 取得 loop の禁止と試聴 loop の使用条件が混在 | 取得世代と再生世代、Cue 境界、block split を分離 | BL-02 の実行可能な状態表が必要 |
| RV-07 | コード事実: 既存 gain は連続 27 block を要求し、中断後に A 減衰を保持する実装もある | 短い TRACK の policy と、通常復帰／減衰復帰待ちを別契約に分離 | BL-03 と R-12 の承認が必要 |
| RV-08 | 計画不足: PCM 回収、他の試聴、別ウィンドウ、保存失敗の順序が未固定 | 単一試聴所有者、非 RT 回収、非開示対象、保存と出力復帰の分離 | BL-04〜BL-07 のテストが必要 |
| RV-09 | 計画不足: 新旧 PRE/POST、Capture、ABI の互換表がない | capability 不一致時は新機能を開始せず、通常計測は既存互換範囲で維持 | SA-06 / BL-07 の matrix が必要 |
| RV-10 | コード事実: 小型 footer では version が表示されない場合がある | 全サイズ共通の情報メニューと更新入口 | UP-01 の設計承認後に実装可能 |
| RV-11 | コード事実: AU の network client 宣言なし、curl と内蔵 browser を無効化 | 手動の外部 browser 導線と自動検知を分ける | 自動検知は追加承認が必要 |
| RV-12 | 公開状態とローカル事実: 公開版も開発作業木も SemVer は 1.1.49 | 公開 version と開発 build identity を分離し、B 番号で新旧を判定しない | UP-02 の build identity 設計が必要 |

確認箇所:

- [SPACE capabilities](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/HyphaObservationPageContract.h)
- [無音判定と取得入口](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/PluginProcessor.cpp:39)
- [optional ingress](/Users/nishiodaisuke/Dev/kirin_hypha/crates/kirin_hypha_ffi/src/lib.rs:2809)
- [mode の排他](/Users/nishiodaisuke/Dev/kirin_hypha/crates/kirin_measure/src/spectrum_exchange_control.rs)
- [POST 出力の選択](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/PluginProcessor.cpp:506)
- [Keep 入口](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/PluginProcessor.cpp:632)
- [Blind の出力と中断時 gain](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/reference_audition/ReferenceRuntimeV2BlindRealtime.cpp)
- [版表示の幅条件](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/src/HyphaObservatoryViewFooter.cpp:79)
- [AU と network の設定](/Users/nishiodaisuke/Dev/kirin_hypha/juce_shell/CMakeLists.txt:184)

## 3. 実装者の推測を残さない共通ゲート

### 3.1 仕様凍結に必要な記録

SA、BL、UP の各要求について、担当、承認状態、入力 fixture、期待値、許容誤差、変更するファイル、対応テスト名、証拠ファイルを一行で結ぶ台帳を作る。
閾値、容量、timeout、最小試聴量、保存先の未決定を実装中に補わない。
研究結果で決める値は、決定に必要な実験と採否基準を先に書く。
必要な人の注釈や聴取、両 OS の時間、Reference 契約担当が揃うまで、それを要する gate は未完了とする。

無関係な機能を一つの待ち状態に束ねない。
表示整理、ATTACK 2MIX、SPACE、Blind、手動更新導線は別の採否単位とする。
ただし共通 Audio Thread、Record、試聴所有権、ABI に退行があれば、影響する全機能の統合を止める。
既存巨大ファイルを変更する場合は対象責務の先行抽出を行い、新規 owned source は 500 行以内にする。

### 3.2 同じ commit で閉じる検証

最小試験 matrix は、PRE と POST、新旧組合せ、単体とペア、2MIX と TRACK/STEM、mono と stereo、通常と異常、表示と保存を含む。
44.1 / 48 / 96 / 192 kHz に加え、製品が受理する端点の SR と非対応値を確認する。
可変 block、最小 block、最大 block、範囲が block 内で終わる場合、seek、loop、再起動、editor の開閉を試験する。
macOS の出荷対象 AU と VST3、Windows VST3 を同一 source commit で検証する。
Windows は現在の操作停止と他作業の占有調整が解消した後にのみ操作する。

画面は 300×200 から 900×600、自由リサイズ、100 / 125 / 150 / 200% と monitor 間の DPI 移動を確認する。
font floor 11 logical px は下限であって読解の合格証拠ではない。
英語と日本語、長い pair 名、鍵盤操作、focus、hover help、accessibility、Capture を同じ能力表から検証する。
実際に描かれた pixel と、操作可能な hit target の双方を確認する。

Rust test と Clippy、FFI を変更した場合の ignored parity と pairing、出荷 source gate、native UI、実ホスト音声試験を揃える。
ignored 件数は実行時に列挙し、従来の 20+5 件を無条件に最新件数と仮定しない。
B-705 の残り検証は [既存の検証記録](hypha_windows_observability_fix_20260906.md) に従い、今回の文書チェックで代替しない。

### 3.3 レビューを閉じる条件

実装後は、要求台帳から実装とテストを確認する順と、差分から要求外の副作用を探す順の両方で点検する。
レビューを担当する人または別の評価者を実装開始時に定め、今回の自己点検を独立レビュー済みと呼ばない。
指摘が出た場合は原因と回帰テストを一緒に直し、修正対象の gate と共有部分の gate を再実行する。
未解決指摘 0、必須 gate の skip 0、未承認の仕様変更 0 が揃ってから、公開候補として承認を求める。

## 4. 現在確認できた公開版と導線

2026-09-06 12:33 JST に、英日 HP の HTML と GitHub の公開 API を直接取得して照合した。
GitHub の公開 latest は `v1.1.49`、draft と prerelease は false、公開日時は 2026-09-02 12:48:38 UTC だった。
[公式リリース v1.1.49](https://github.com/heyalohaloha/kirin_hypha/releases/tag/v1.1.49)

| 入口 | 確認したリンク先 |
| --- | --- |
| [英語 HP](https://kirinmastering.com/hypha) | macOS Universal ZIP と Windows x64 Setup EXE がともに v1.1.49 |
| [日本語 HP](https://kirinmastering.com/ja/hypha) | 同じ二つの v1.1.49 配布物 |
| GitHub Release | macOS PKG、macOS ZIP、Windows Setup EXE と検証用 sidecar が存在 |

3 つの配布物の HEAD は HTTP 200 を返した。
これはリンク到達の確認であり、今回再ダウンロードして署名や installer の実動作を再検証したという意味ではない。
Lemon Squeezy の管理画面や購入者の入手経路は今回確認していない。
検索経由で取得した HP には旧 v1.1.47 のリンクも返ったため、上表にはライブ HTML の直接取得結果だけを使った。

ローカルの開発 source は公開後の変更を含むが、Cargo の version は同じ 1.1.49 のままである。
そのため、version が一致することだけで「公式の最新版を使用中」とは証明できない。
この計画にある SPACE、ATTACK 2MIX、PRE/POST Blind が公開版へ含まれているとも案内しない。

## 5. 先に設けられる手動の更新入口

### 5.1 配置と表示

推奨する入口は、PRE と POST の共通 shell メニューに置く「Hypha 情報」である。
その画面に現在ロードしている version、role、format、platform、確認できる build identity と、「更新情報とダウンロード」を表示する。
小型画面の footer へ長い文を足さず、全サイズで同じ入口に到達できるようにする。
OS entitlement や pair の有無で更新入口を無効にしない。

「更新情報とダウンロード」は明示操作で公式 HP を外部 browser に開く。
日本語は `https://kirinmastering.com/ja/hypha`、英語は `https://kirinmastering.com/hypha` を使う案とし、言語は既存 UI 設定に従う。
併せて「変更内容を見る」から GitHub の公開 release 一覧へ移動できるようにする。
GitHub の一覧を最高 SemVer や全チャネル準備完了の自動判定に使わず、利用者が読む公式情報として開く。

Hypha は既に外部 browser を開く処理を持つ。
JUCE の API も OS の既定 browser を起動する方式であり、この手動入口のために内蔵 browser や HTTP client を追加する必要はない。
ただし AU sandbox での実動作は出荷形態で確認する。
[JUCE URL の公式仕様](https://docs.juce.com/master/classjuce_1_1URL.html)

ボタンだけを設けた段階では「最新版があります」と表示しない。
外部ページを開いた要求の成功を、更新版の確認、ダウンロード完了、インストール成功と扱わない。
browser 起動に失敗した場合は短い失敗表示と公式 URL をコピーする操作を用意する。
URL は固定 allowlist の HTTPS とし、pair 名、音声、Work、license、instance ID を query に付けない。

Blind 中はこの入口から browser を開かず、試聴終了後に操作できることを示す。
通常の計測中は利用者の明示操作だけで開き、version の到着を理由とする強制 popup、点滅、全面 overlay を作らない。

### 5.2 Web 側で揃える内容

HP の二言語に、公開 version、公開日、変更内容、対応 OS、PRE/POST 同梱、適切な配布物、更新手順を揃える。
配布物を選ぶ前に、その版へ実際に含まれる機能を確認できるようにする。
今回の生 HTML 照合はリンクの版を確認しただけで、専用の更新説明欄が完成していることの確認ではない。

| 利用者の選択 | 案内するもの | 避けること |
| --- | --- | --- |
| macOS 無料配布 | 現行 HP の signed / notarized Universal ZIP と対応する導入手順 | 無断で ZIP チャネルを廃止しない |
| macOS Installer | 公式 release または既存購入者の正当な入手先にある signed / notarized PKG | 更新のために再購入が必要だと誤認させない |
| Windows | PRE/POST 同梱の Authenticode 署名済み Setup EXE | 診断用 VST3 ZIP を主導線にしない |
| 非対応 OS / architecture | 対応条件と利用可能な公式情報 | 非対応 installer を「更新できます」と勧めない |

更新手順は、作業の保存、DAW 終了、公式配布物の導入、DAW 再起動、必要時の rescan、ロードした PRE と POST の版確認までを一続きにする。
メモリ内の旧 plugin とディスク上の新 plugin を混同しない。
PRE と POST の片側だけを更新した場合、新機能の capability 不一致を示し、同版の両方を導入する入口へ戻す。
版確認だけのために音声 callback、Record、pair を変更しない。
読み込まれている plugin の置換、DAW の強制終了、権限昇格、再起動を Hypha 自身が実行する auto updater は含めない。

旧 user-level bundle と新 system-level bundle の重複、Windows の current-user と all-users の重複も更新検証に含める。
利用者向け手順は出荷 installer と整合させ、セキュリティ機能の無効化や無条件の quarantine 除去を標準手順にしない。

## 6. 自動の最新版通知を採用する場合

### 6.1 追加承認が必要な理由

現行 Hypha は `JUCE_USE_CURL=0`、`JUCE_WEB_BROWSER=0` で、AU の network client 宣言も持たない。
したがって、ネットワークへ自動接続する更新チェッカーを「単なる UI 追加」として入れない。
network / privacy 方針、AU sandbox、取得元、利用者設定、障害時の振る舞いを別の契約として承認する。

採用する場合の推奨は、初期値 off の任意設定と、利用者の明示的な確認操作である。
自動確認は非 RT worker で最大 24 時間に一度、同じ端末内の複数 instance で要求をまとめる案とする。
待ち時間上限 3 秒、応答上限 16 KiB、起動失敗時の無制限 retry なしを初期予算案とし、AU と Windows の実測後に固定する。
この設定が off、ネット接続なし、取得失敗でも測定、pair、Record、承認済み試聴は利用できる。
通常の update-check 失敗は R-28 に従い通知せず、明示的な確認操作の失敗だけを短く通知する。
外部接続先には IP address など通常の通信情報が伝わることを privacy 説明から省略しない。

### 6.2 更新判定の正本

新しい通知用 release manifest を採用する場合は、公開側の release 手順が所有する。
endpoint と署名鍵は未設置であり、本計画で存在を仮定しない。
GitHub の latest は公開 release を取得する API だが、LS、両 HP、署名と実機検証の完了を証明する API ではない。
[GitHub Releases API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)

manifest は product、schema version、stable channel、SemVer、source commit、publish / expiry 時刻、単調な publication sequence、対応 OS / architecture / format、PRE/POST capability、公式 release ページ、各チャネルの準備証拠への参照を持つ案とする。
全チャネルが同じ release commit で公開準備を終えた後にのみ、通知対象を進める。
署名済みの短い manifest、固定 HTTPS origin と path、bytes 上限、型、期限、sequence、署名鍵を検証し、受信文字列を shell command、HTML、任意 URL として扱わない。
署名検証は通知情報の真正性の検査であり、installer のコード署名検証の代わりにはならない。

version 比較は SemVer と channel に従う。
`1.1.9 < 1.1.10` を文字列比較せず、build metadata や B 番号を公開版の新旧判定に使わない。
同じ SemVer で commit が違う開発 build は、「開発版」または「公式配布版との一致は未確認」とし、通常の最新版判定と分ける。
[SemVer 2.0.0](https://semver.org/)

署名済み情報を cache する場合も、有効期限と確認日時を保持する。
期限切れ、不正な時刻、schema 不一致、破損、HTTP error を「最新版です」に変換しない。
過去 sequence の再送を新しい通知にせず、公開取り消しは新 sequence の署名済み状態で扱う。
取り消しを自動 downgrade の許可にせず、必要な公式案内への明示操作だけに留める。

### 6.3 状態と動線

| 確認状態 | 表示 | 利用者の動線 |
| --- | --- | --- |
| 未確認 / 自動確認 off | 使用中の version と「更新情報とダウンロード」 | 手動で公式 HP を開く |
| 対応する新しい stable 版が検証済み | 非点滅の「更新あり」表示 | 現在版、新版、確認日時、変更内容、対応配布物へ |
| 公開 version と一致 | 「公開版と同じバージョン」 | build 一致が未確認ならその範囲も示す |
| 使用中が開発版 / 公開版より先 | 開発版の事実 | 強制 downgrade や誤った「更新あり」を出さない |
| 期限切れ / 失敗 | 最後の確認日時か未確認 | 明示操作で再確認。自動失敗は静かに扱う |
| 未対応 OS / 必須 artifact 不足 | 更新可能と表示しない | 公式の対応条件へ |

更新通知を一度閉じた状態は端末側に保存し、DAW project state へ混ぜない。
同じ version の通知を PRE/POST の全ウィンドウで繰り返さず、次の公開版や利用者の再確認を別の機会とする。
Blind 中は取得結果を内部に保持しても UI を変化させず、終了後の通常画面でだけ提示する。

## 7. 更新導線の受入条件

| 要求 ID | 実装する範囲 | 最低限の検証 |
| --- | --- | --- |
| UP-01 | PRE/POST 共通の情報メニューと手動入口 | 全サイズ、pair 無し、OS license 無し、keyboard、accessibility、browser 起動失敗、Blind 中 |
| UP-02 | ロードした版と公開版の区別 | 同版別 build、開発版、`1.1.9` と `1.1.10`、片側だけ更新、再起動前後 |
| UP-03 | 公開ページと配布物 | 英日 HP、stable release、macOS ZIP / PKG、Windows EXE、404、欠けた sidecar、対応 OS |
| UP-04 | 導入後の確認 | PRE/POST 両方の同版読込、user/system 重複、同版再 install、旧公開版 upgrade、uninstall |
| UP-05 | 任意の自動確認 | off 時の通信なし、AU sandbox、複数 instance、cancel、timeout、応答上限、worker 停滞 |
| UP-06 | 通知の真正性と期限 | schema、署名、origin、expiry、sequence、withdrawal、改竄、cache、clock rollback |
| UP-07 | 非侵入性 | RT / Record / 計測への影響なし、popup なし、無音と Blind で図や識別情報を出さない |
| UP-08 | 公開側の整合 | 同一 commit の 3 チャネルと英日ページが揃うまで通知を公開しない |

UP-01〜UP-04 の手動導線は、自動確認の承認や新しい計測機能の完成を待たず、独立した変更として着手を提案できる。
UP-07 の非侵入性と UP-08 の公開整合性は手動導線にも適用し、通知 manifest に関する部分だけを自動確認の追加条件とする。
今回の「設けられる」は設計上の判断であり、既に button を配置したという意味ではない。
UP-05〜UP-06 と通知 manifest を使う自動通知は、network と公開 manifest の契約承認を要する。
手動入口だけを実装した状態で、自動通知の条件まで完了と数えない。

## 8. 承認事項と申し送り

採否を求める単位は、SPACE / ATTACK の研究と定義凍結、Blind の安全条件の実証、手動の更新入口、自動通知の 4 つに分ける。
TRACK/STEM を Blind の対象から外す提案はしていない。
手動更新入口を先に実装するなら、全サイズ共通の情報メニューと、HP の二言語の更新説明を同じ変更範囲として承認する。
HP が別 repository にあることを理由に説明部分を抜かず、担当と成果物を明示する。

**Handoff 案**

- To: Hypha 製品契約担当と Kirin OS Reference 契約担当
- From: Codex 2026-09-06
- What: SA / BL の定義凍結、試聴と Record の排他、R-12 の復帰状態、短い TRACK の gain 方針を裁定する。
- Why: 現行 Reference 契約の流用だけでは PRE/POST 試聴と下流の正本保護を保証できない。
- Next: 各 ID に承認者、実験、合否、対応 fixture を割り当て、製品実装開始の条件を確定する。
- Ref: 本書 §2〜§3 と両計画の追加 gate。

**Handoff 案**

- To: Hypha 配布担当と HP 担当
- From: Codex 2026-09-06
- What: 共通情報メニューから入る英日更新説明と、3 チャネルの同版リンク確認を準備する。
- Why: 単に最新 release を開くだけでは、適切な配布物と導入後の版確認まで案内できない。
- Next: UP-01〜UP-04 を承認し、公開 version、変更内容、対応 OS、PRE/POST の更新手順を二言語で揃える。自動 manifest は別承認とする。
- Ref: 本書 §4〜§7、[配布 runbook](ls_release/kirin_hypha_ls_runbook.md)。

Handoff は未送信の計画であり、別タスクの作成、Notion 書込み、HP 公開変更は行っていない。
今回は文書改訂と読み取り検証だけで、コード変更、build、install、release、Windows 操作は行っていない。
日本語技術文書の規範を使い、確認済み事実と未承認案、コードの存在と実機での成立を分離した。
