# ATTACK Phase 2-R audio provenance報告

**日付**：2026-08-30
**実装ラベル**：B-552
**対象profile**：DRUM
**audio provenance判定**：Go
**DRUM候補freeze判定**：No-Go、正式採点未実施
**公開判定**：No-Go、ATTACKはOFF

## 1. 判定範囲

B-552は、B-550で固定した290件のDRUM development manifestを、公式E-GMD audio archive内のWAVと対応MIDIへ結び付けた。
同じarchive読み取りで全体SHA-256、ZIP64構造、全91,108個のlocal header、選択WAV、対応MIDIを検証した。

選択290件は全てRIFF WAVE、mono、44.1 kHz、PCM16だった。
manifestが固定したsource sample数と実WAVのsample数は全件で一致し、core範囲外参照は0件だった。

raw WAV、source PCM、core PCMの重複は0 groupだった。
無音または定値のsourceとcoreも0件である。
同梱MIDIはB-551 member SHA-256と全290件で一致し、MIDI note-onがaudio終端を2 msより超える件も0件だった。

このGoはaudio archive-memberとPCM provenanceに限る。
formal authorization、blind acoustic audit、source-origin context採点、candidate score、winner、fresh holdout、runtime、公開ATTACKのGoではない。

| 対象 | 判定 | 根拠 |
|---|---|---|
| 公式audio archive identity | Go | 96,422,999,145 bytes、固定SHA-256一致 |
| ZIP64とfull local layout | Go | 91,108 entry、local header 91,108件、overlap 0 |
| 選択WAV | Go | train 246件、validation 44件、全290件一致 |
| PCM意味列 | Go | source、core、maximum-contextを別domainで固定 |
| audio重複と無音 | Go | 拒否対象は0件 |
| audioとMIDIの結合 | Go | B-551 SHA不一致0件、終端超過0件 |
| formal candidate score | No-Go | authorization、blind audit、context、candidate planが未成立 |
| 2MIX | No-Go、未着手 | DRUMのaudio証拠を転用しない |
| 公開ATTACK | No-Go | runtimeとfresh holdoutを含む公開gate未実施 |

## 2. 固定した親artifact

B-552は、CLIから渡されたhashを信頼の起点にしない。
B-550 manifest、B-550 receipt、B-551 MIDI receipt、公式audio archiveのhashをコードに固定し、実bytesと意味を入力順に再検証する。

| artifact | SHA-256 |
|---|---|
| B-550 development manifest | `80ebe2961ece9833f554f98430e6617aad2496603f0c105c611dfa710938ad8c` |
| B-550 fold metadata | `51c0b30d535a2819dd17b0a49e410c378e2d436eb5f98be06caff5757be45675` |
| B-550 development receipt | `57fcd65d2b9f265796ba2142f367a89bd932e1cefd73c76f09e96d743963153a` |
| B-551 MIDI receipt | `7c923cf224f8201d0496c304cb160b0cc8859340cdb0b74c7b490b3cd6223447` |
| E-GMD MIDI-only archive | `5e70a6f4d760385a5e5ec986a2f02179d96f61181a920e592876b577a75844d3` |
| E-GMD full audio archive | `7d9a264fb4c9eabd9fec09d5f8e333192f529b1a1b845d170279a977ac436053` |
| full local-layout ledger | `6eee9c9f18e1b90355ea84c75ab5fe96f54b67ff8220a41e24bce16807ba61bf` |

full local-layout ledgerは、公式archive全体のSHA-256だけから別のZIP解釈を後付けで採用しないために固定する。
全91,108 entryについてcentral index、literal name、local-header start、data start、data end、local-header digestを順序付きでhashした。

最初の認証済みrunでledger値を得た時点では、componentを意図的に失敗扱いにした。
その値をコードへ固定し、archive検証とreceipt検証の両方がexact一致を要求する実装に変えた後のみ、componentをGoにした。

## 3. 96 GB archiveの読み取り契約

96.4 GBのarchive全体をmemoryへ複製する方式は採用しない。
検証器は一つのfile descriptorからZIP64 end record、central directory、local headerをpreflightし、対象rangeを決定する。

その後、offset 0からEOFまで一回の連続読み取りを行う。
各chunkをarchive SHA-256へ入れるのと同時に、同じchunk bytesから全local header、選択WAV 290件、対応MIDI 290件、central directoryとend recordをcaptureする。

archive SHA-256と固定値が一致するまで、captureしたpayloadを展開しない。
展開するbytesは、全体SHA-256に実際に入れたbytesと同じである。
hash後にpathを開き直し、別のarchiveからmemberを読む経路はない。

公式archiveの構造値は次のとおりである。

| 項目 | 実測値 |
|---|---:|
| archive bytes | 96,422,999,145 |
| ZIP64 central entry | 91,108 |
| central-directory offset | 96,409,831,470 |
| central-directory bytes | 13,167,577 |
| authenticated local header | 91,108 |
| overlapping payload range | 0 |
| 選択WAV非圧縮bytes | 774,062,982 |
| 選択WAV圧縮bytes | 521,284,272 |
| 対応MIDI非圧縮bytes | 932,889 |
| 対応MIDI圧縮bytes | 546,934 |

ZIP検証は、multi-disk、end-record gap、非ASCIIまたは危険な名前、exact重複、canonical重複、case-fold重複、symlink、暗号化、未対応圧縮、range overlapを拒否する。
各local headerはliteral filename、flag、圧縮方式、CRC32、圧縮size、非圧縮sizeをcentral headerと照合する。

選択payloadは個別size、合計size、展開率をcapture前に上限検査する。
Deflateはstream end、消費圧縮bytes、出力bytes、CRC32を全てexact比較し、正常streamの後ろに追加bytesを隠す入力も拒否する。

## 4. WAVとPCMの正本

WAV decoderはRIFF sizeとmember長のexact一致、`fmt `と`data`の各1個、chunk長、odd padding、block align、byte rate、data整列を検査する。
受け入れる形式はformat code 1のmono、44.1 kHz、signed integer PCM16またはPCM24である。

固定archiveの選択290件は全てPCM16だった。
公式ページの「44.1 kHz、24 bitで録音」という説明は録音過程を指し、固定した配布ZIPの各WAV containerが24 bitであることを保証しない。
B-552は「録音時24 bit」と「配布artifactの実decodeはPCM16」を区別する。

PCM16は符号付き24 bit分子へ8 bit左shiftし、PCM24は符号拡張する。
canonical PCM digestはdomain、sample rate、channel数、sample数、i32 big-endianの値列をhashする。

範囲ごとの実測は次のとおりである。

| 範囲 | sample | 時間 | zero sample | peak abs PCM24 | sum squares PCM24 |
|---|---:|---:|---:|---:|---:|
| source | 387,025,111 | 146.27分 | 14,526,592 | 5,260,032 | 10,207,068,698,698,317,824 |
| core | 136,801,333 | 51.70分 | 6,422,241 | 5,260,032 | 4,499,531,763,342,901,248 |
| maximum context | 387,025,111 | 146.27分 | 14,526,592 | 5,260,032 | 10,207,068,698,698,317,824 |

sourceとmaximum contextは現時点で同じ半開範囲`[0, actual_samples)`を持つが、別domainでhashする。
このmaximum contextは後続evaluatorが必要範囲を再検証できる証拠であり、context evaluatorの完成を意味しない。

## 5. duration、重複、無音の監査

manifest decimal由来のsource sample数とWAV dataから数えたactual sample数は、387,025,111 sampleで一致した。
差0 sampleのmemberが290件、許容差を使ったmemberが0件、最大差も0 sampleだった。
coreの合計は136,801,333 sampleである。

| 重複class | group数 | cross-split group数 |
|---|---:|---:|
| raw WAV member | 0 | 0 |
| source canonical PCM | 0 | 0 |
| core-relative canonical PCM | 0 | 0 |
| maximum-context canonical PCM観測 | 0 | 0 |

同一source PCMを異なるperformance IDで繰り返し数えない。
core digestはabsolute crop startを含めず、coreからの相対位置とcontentを固定するため、別のsource位置へ複製された同一coreも検出対象になる。

all-zeroと非0定値はsourceとcoreで別々検査した。
該当があってもreserveと差し替えず全290件をreceiptへ残し、componentを失敗にする契約である。
実測ではall-zero、非0定値とも0件だった。

## 6. 対応MIDIとaudio範囲

full audio archiveには選択WAVと同じrelative nameのMIDIも入っている。
B-552はその290 memberも同じfull SHA読み取りかcaptureし、raw SHA-256をB-550 manifestとB-551 receiptへ照合した。
不一致は0件だった。

同梱MIDIをB-551と同じ整数時刻parserで解析し、最初と最後のnote-onを得た。
最後note-onとactual audio終端は次の整数式で比較した。

`last_note_micros * 44,100 <= actual_samples * 1,000,000 + 2,000 * 44,100`

終端超過、B-551 raw SHA不一致、caller flagと再計算の不一致は全て0件だった。
これにより、固定WAVと固定MIDIが同じ配布archive内の対応素材であることも確認した。

## 7. receiptの再現性

full local-layout ledgerをsource pinしたコードで二回実行し、両receiptは899,526 bytesでbyte単位に一致した。
receipt SHA-256は`788faee0f60c6173aac48b9681d98314bcdbb949a0e41d9c78b6a185f3056233`である。

receiptは親hash、archive構造、全290 memberのWAV、PCM、MIDI証拠、aggregate、重複、無音、隔離assertion、未解決blockerをrank順に記録する。
時刻、hostname、絶対path、candidate scoreを含めない。

`component_verified` と `full_layout_ledger_source_pinned` はtrueである。
一方、`overall_formal_authorization`、`formal_scoring_allowed`、`context_evaluator_ready`、`winner_allowed`、`selection_replacement_allowed`は全てfalseのままである。

## 8. 隔離assertionの意味

B-552はarchive全体96,422,999,145 bytesを不透明な圧縮bytesとして読み、hashとZIP構造検査へ使った。
したがって「E-GMD testを含むarchive bytesへ一切触れていない」とは主張しない。

decompressしてWAV decoderまたはMIDI parserへ渡したのは、manifestで固定したtrain 246件とvalidation 44件のWAVと対応MIDIだけである。
unselected payloadとtest payloadのdecompressまたはsemantic decodeは0件だった。
fresh holdout、2MIX、candidate scoreも開始していない。

これらは実行経路の`operational_assertions_not_evidence`である。
FormalAuthorizationはこのbooleanを非アクセスの証明として信頼しない。

## 9. 残るNo-Go条件

audio provenanceが成立しても、MIDI compoundが可聴attackの公開正解として十分かは確定しない。
同じ固定audio coreを二人のannotatorが候補出力なしで聞き、MIDI proxyのPrecisionとRecallを別々に検証する必要がある。

formal scorerは引き続きfilesystem入力前に停止する。
次のblockerが残るためである。

- development、fold、MIDI、audio receiptをsource commitへ結ぶFormalAuthorization semantic verifierが未実装である。
- blind proxy auditが実データで未実施である。
- 順序付きcandidate planが未固定である。
- source sample 0をframe originにするcontext evaluatorが未実装である。
- sealed candidate setの完走receiptが未実装である。
- leave-one-drummer-outとleave-one-session-outの実resultがない。
- test隔離事故後のfresh holdoutはguardian裁定または新しい未接触dataを必要とする。

次工程を次の順序へ固定する。

1. 固定audio coreと二人のblind annotationを同一hashで結ぶ。
2. MIDI proxyのPrecisionとRecallをannotatorごとに検証する。
3. 全候補から必要な左右contextを導出し、source-origin evaluatorを実装する。
4. 親receipt、blind audit、candidate planをsource commitの認可hashへ結ぶ。
5. sealed candidate setを五fold、LODO、LOSO、runtimeで一度だけ評価する。
6. guardian裁定または新しいholdoutを確定し、fresh評価へ進む。

工程5までDRUM candidate freezeはNo-Goである。
工程6とPhase 3以降のruntime、macOS、Windows gateが完了するまでATTACKは公開No-Go、OFFを維持する。

2MIXは同じATTACK画面の別profileだが、DRUMのaudio receipt、annotation、threshold、definition hash、Goを転用しない。
2MIX用dataとaudio-only annotationは独立に作る。

## 10. 参照

- [Expanded Groove MIDI Dataset](https://magenta.withgoogle.com/datasets/e-gmd)
- [zip 8.6.0 API documentation](https://docs.rs/zip/8.6.0/zip/)
- [flate2 1.1.9 API documentation](https://docs.rs/flate2/1.1.9/flate2/)
- `docs/transient_delta_phase2_formal_development_gate_report_20260830.md`
- `docs/transient_delta_phase2_midi_provenance_report_20260830.md`
- `docs/transient_delta_phase2_recovery_plan_20260830.md`
