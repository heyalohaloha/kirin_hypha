# 未署名候補を手で通す

署名の前に同じ source の未署名候補で普通の操作を一度通す。公開 CI の待ち時間に実施できる。
この記録は署名済み候補の正式受入を代用しない。採用 source を直したら影響範囲を通し直す。
DAW の起動、挿入、操作、保存、再起動、書き出しはすべて担当者が手で行う。

## 準備

[build-only 入口](../hypha_build_entry.md)で未署名候補を作る。AAX は外部 SDK と開発用 host の条件を確認する。
未署名確認のために license の移動を要求しない。既存の公開版、session、設定を保全し、
同じ ID の複数候補を同居させず、読み込んだ path と候補 hash を run identity に一度記録する。
Windows は同じ source の未署名 factory artifact を使う。

## host ごとの通し

1. POST を先に挿し、PRE が無い表示を確認する。PRE を挿して組を選ぶ。
2. 普通の音を再生し、表示、音の経路、Keep → Stop → Record を確認する。
3. bypass と解除、停止と再開、seek を試す。直した時計・ループ・PDC の部分は fixRows に従って深く見る。
4. editor の開閉と resize を一度行い、session を保存する。DAW を終了し、別 process で開き直して組と値を確認する。
5. 同じ区間を Hypha 無し／通常計測ありで書き出す。比較試聴は終了し、gain、dither、normalization、
   fade、tail、sample rate、channel、開始位置、長さを両方で揃える。非周期的な信号を含める。
   書き出しは float64 を推奨する。float32 への丸めだけでは double の下位 bit の後退を捕まえられない。
6. Record を閉じ、writer が終了してから選んだ record directory を収集する。出力先は source の外に新設する。

```bash
# PCM16/24/32、IEEE float32/64、対応する WAVE extensible を decoded sample ごとに比較。
# gain、整列、trim、許容差は一切適用しない。出力の保存先は新しい束の中を選ぶ。
python3 scripts/acceptance/wav_null.py /exports/without.wav /exports/with.wav > /evidence/null.json
node scripts/acceptance/collect_records.mjs /closed/record_sessions /evidence/records
```

WAV 比較の exit 0 は同じ rate・channel・frame 数で全 sample の差が 0、非有限値が 0。
exit 1 は差または非有限値、exit 2 は不正／非対応／異なる geometry。差がある場合の latency は未確定なので
`latencySamples: null` とする。差が無い場合だけ固定した frame 位置での `latencySamples: 0` を報告する。
無音だけの一致は host latency の証明にならない。RF64 と圧縮 WAV は対象外で、通常 RIFF WAV へ書き出す。
JSON の hash は比較した WAV 自体に結ぶ。record manifest は全収集 file の相対 path・bytes・SHA-256 を保持する。
収集中の変更、symlink、既存の収集先は拒否する。収集だけで Record の整合性を PASS としない。

Logic は Cycle → Keep → 周回 → Stop と停止再開、REAPER は loop と PDC、
Windows の停止型 bypass、AAX は読み込んだ path と host 固有の時計を担当者が確認する。
共通の流れは代表 host に寄せ、もう一方の OS は固有部分を確認する。

## 受入と記録の型

- 一行一束。複数行が同じ証拠 file を指してよい。identity（候補、OS、locale、host、path）は run の始めに一度。
- 人が hash を固める境目は候補、凍結、packages、最終公開物の四点。途中の実 file 照合は coordinator に任せる。
- 独立 review は host の通しの最後に一度。ケースごとの待ちを作らない。
- 二分進まなければ、操作、表示、状態、再開条件を一行に記録して次へ。調査は確認済み FAIL に絞る。
- 未実施を PASS にしない。全行の穴埋めや coverage 件数を完了条件にしない。
  本人の延期判断は凍結後に正式な owner-decision へ結び、定量的安全条件を緩めない。

正式受入の依頼には [雛形](acceptance_request_template.md)を使う。
