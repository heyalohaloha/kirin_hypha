# VST3 LOOP境界: native先頭clampと遅延tailのPPQを分離する

2026-10-02。B-1140の開始境界修正後に実Windows Studio Pro 8.1.2で再現した中断への対処。
LOOP解除、再MATCH、追加確認や追加クリックを通常手順へ加えない。

## 確認した原因

48 kHz / stereo / 528 frames、PRE VST3 → 独立4096-sample delay → POST VST3、
LOOP 0..12 PPQ (native 288000 samples)、最初からLOOP ONで再生→BLINDした。
通常の最初の折り返しで「DAWの再生位置が不連続になりました」と中断した。
B-1140の4必須CIはgreenだが、この実host FAILを相殺しない。

独立した全frame identity記録では連続content clockの差は0、44周、12732848 framesに
内部frame誤り・span誤りは0。PREのgenerationも折り返しで変わっていなかった。
しかしPOSTのnative位置とPPQは、次の相異なる境界表現だった。

| POST content clock | native位置 | PPQ |
| --- | --- | --- |
| 283664 | 283664 | 11.819333333333333 |
| 284192 | 0 | -0.158666666666666 |
| 284720 | 0 | -0.136666666666666 |
| 287888 | 0 | -0.004666666666666 |
| 288416 | 416 | 0.017333333333334 |

同じcallbackのPREは288288へ進み、native位置288 / PPQ0.012で正しく折り返す。
POSTのnative位置だけが先頭へclampされ、PPQは遅延分だけ先頭前のtailを示す。
これは既知のAU境界（native負位置 / PPQ先頭clamp）とは逆であり、同一扱いできない。

旧codeへ保存CSVのnative / PPQ / clockをそのまま与えると最初のclampでtimelineが失効し、
比較が復活しないことを再現した。PCMの同一性と正しい比較開始は別の条件である。
保全CSV SHA256:

- Source: `4e55986f0ff7579ef64bcfe89a97f26e005fdd9918c229f680fe6d23456a42f2`
- Observer: `9cab1e02ccdfb7a29533ed50b3f2504226d4ebbf9bc0076f8442dd373d683d07`

CSVはprivateな検証保存領域に残し、実音源・機器情報・認証情報をrepoへ追加しない。

## 責務と許可条件

境界表現のcorroborationは`corroborateLoop`に集約し、時計準備とPCM Consumerが同じ規則を使う。
時計準備のcycle meterとtimelineにも、同じcorroborated座標を渡す。
一方だけraw clampを見てKを失効させる二重判定を作らない。

native先頭clamp / 先頭前PPQを読み替えられるのは、全て満たす場合だけ。

1. 既存のexact-host certificateが認めるVST3 content clockで、PRE / POSTのauthorityが一致する。
2. 独立時計で既に決めた読取開始はPOST content clockと同じ（K=0）。PPQでKを選ばない。
3. PREのnative sample折り返しから測った周回長があり、同じLOOP範囲・tempoを裏付ける。
4. 両側のVST3 output presentationが有効で、正の差があり、その差が1周未満。
5. native位置は当該LOOP先頭、PPQ tailは報告された遅延以内で、clockが示すtailと1 sampleの既存精度で一致する。
6. PCM Consumerの補足snapshotは現在のPCM generation / LOOP範囲と一致し、PCM seqlockも変わらない。

時計準備のanchorはPCM取込の開始より前から存在する。late LOOP→BLINDの製品試験では、
最初の要求PCMがちょうどclamp内で始まると、遅延tailはPCMのanchorより前にあることも確認した。
PCM anchorだけで座標を確認すると、正しい待機をtimeline失効と誤判定していた。
補足確認には同一generationの時計anchorを使う一方、PCM読取は独立に`runStart`より前を拒否する。
これにより証明済みKを保ってPOSTで待つが、過去の未保存PCMは一切捏造・出力しない。

読み替えるのは検証用project / PPQだけ。PCM address、K、源音、測定・Recordは変更しない。
stop、clock欠落、gap、range / tempo変更、実際のseek、他の周回、証拠の矛盾は従来どおり拒否する。
未確認区間のPOST維持と、成立済みBlindの失効・再開禁止も維持する。

## 再発防止と軽量性

純粋な試験に実測528-frame / 288000-sample / 4096-delay境界を追加する。
初回 / 途中からのLOOP、先頭0 / 86016 / 98304、非block整列の4周、独立物理delay、各frame固有の
不変identityを使う。時計準備とConsumerのKが同じままで、全frameが独立期待PCMと一致することを要求する。
既に周回していた後で最初のclamp内にPCM要求を開始する場合も含め、初回は`beforeRun`で
Kを維持し、必要frameが届いた後は全blockのPCMが一致することを要求する。
欠損、3-sample矛盾、余分な1周、未認定clock、presentation欠落・不整合等22の拒否対照も含む。

実processor / editorの既存初回 / late LOOP試験にもnative clamp / 負PPQとpresentationを加える。
初回clock欠損、動的compressor / band、固定MATCH、20周、全frame source / gain、4クリック、
END receipt、通常Aのbit同一の基準は変えない。実際にclampを通ったことも要求する。

製品にworker / timer / buffer / descriptor scanを増やさない。通常PCM blockは既存anchorの
短い成功経路を保ち、追加snapshot読取は特殊境界だけ。Audio Threadのalloc / lock / I/Oは禁止のまま。
pure fixture、保存trace replay、現candidateのCI、実Mac / Windows / AAXの受入は別々に記録する。
fixture成功だけで初回LOOP完了、公開ready、他host対応完了とはしない。
