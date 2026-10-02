# 初回LOOP: 未成立の入口と、成立後の中断を分離する

2026-10-02。既存の[LOOP設計](hypha_daw_loop_match_design_20260930.md)に対する開始境界の補足。
LOOP解除、再MATCH、専用モード、追加クリックを通常手順に加えない。

## 再現した原因

B-1139の必須CI `36979780781` で、`kirin_live_initial_loop_product` が開始待ちのまま失敗した。
診断では時計準備が8回連続の有効な根拠を取得していても、Consumerの初回admissionは閉じていた。
最初の未確認callbackが通常のtimeline失効として扱われ、PREを一度も許可する前に、
同じ明示操作での開始権まで消費していた。Windows実DAWでも開始待ちが残った。

実Windows VST3で取得した時計は、認定content-clock authority、native LOOP長288000 samples、
有効な正のpresentation latencyを保持していた。独立した全frame監査にも周回誤対応は無かったが、
この観測を比較開始・MATCH・Blindの合格へ読み替えない。native host identityの修正だけでは
開始境界の問題が解消しないことを確認した。

## 所有する状態と責務

| 状態 | 欠けたclock / stop / gapの扱い | PRE出力を許す条件 |
| --- | --- | --- |
| 新しい明示操作の未成立入口 | POSTを維持し、Consumerの途中の較正・前回位置・runとtimelineを破棄。未使用の初回admissionだけ保持 | 別の時計準備が取得した現在の完全なproofをadoptできること |
| 一度成立した比較 | 従来の失効経路。初回入口へ戻さない | 失効したproofの再adoptでは戻さない |
| 終了・restore・pair / format / lifetime変更 | 従来の権限境界で拒否 | 新しい明示操作と新しい根拠が必要 |

未成立入口の判定は`initialAdmission && !kValid`だけを対象とする。
成立済みの通常線形較正でも初回admissionは閉じるため、LOOPだけの例外的な自動復活を作らない。
ring identityの検査はこの待機判定より先に行い、外国ringを待機扱いで受け入れない。

時計準備は未確認観測を独立にfenceする。待機を保持すること自体は出力権限でもMATCHでもない。
adoptionには現在のPRE generation / 128-bit owner、現在のPOST callback、proof種別と一意なK、
現在世代で書かれたPCMが必要。その後も全block範囲、周回、PCM seqlockとコピー前後の
generation / owner-closeを検査する。clockの到着だけで音を出さない。

## 再発防止の試験

純粋な対応試験は、最初のcallbackに次の5条件を個別注入する。
continuous clock欠損、project clock欠損、停止、0 frames、callback gap。
scratchのsentinelでPCMコピーが無いことを検査し、以後のclockだけではPREを許可しない。
新しい完全なproofだけで同じ未成立入口をadoptでき、独立delay lineのPCMと一致することを検査する。
同じ障害をadoption後に再注入し、成立済みproofが初回経路から復活しないことも検査する。
この試験は修正前のConsumerでadoptionに失敗し、原因を決定的に再現した。
部分的な線形較正の後にも同じ5条件を注入し、欠落前の確認回数を持ち越さず、
欠落後の8回の新しいjoinだけで開始できることを検査する。最初の修正案に途中のstreakが
残る問題もこの試験で再現し、未成立の待機と古い候補の再利用を分離した。

実Processor / editorの初回LOOP試験には、非出荷diagnostic設定だけで最初のauthoritative clockを
1 callback欠かす制御を追加する。注入が実際に起きたことと、そのcallbackのunity POSTを
全frame bit照合する。runnerへのsleep、deadline延長、PCM許容誤差は使わない。
既存の4096-sample物理delay、状態を持つcompressor / dynamic band、20周の全frame source / gain、
固定MATCH、BLIND → Source 2 → 開示 → ENDの4クリック、実END receiptの基準は維持する。

## 軽量性と受入の境界

製品変更はConsumer開始境界の固定個数のboolean判定だけで、PCM容量・コピー回数・worker・timer、
Audio Threadのallocation / lock / I/O、host allowlist、delay boundを増やさない。
通常unity POSTの既存省略経路はConsumerを呼ばず、今回の分岐を通らない。
未確認範囲では常にPOSTを維持する安全性は変えない。

fixtureの合格と、現候補の必須CI / Mac AU・VST3 / Windows VST3・Developer AAXの実DAW受入は
別の証跡である。旧候補のgreen、PCMの無誤対応、build生成成功だけで初回LOOP完了としない。
公開配布・LS upload・HP反映の承認とrelease gateも別であり、この修正から推定しない。
