# DAWループ再生と固定MATCH保持 — 設計と実装状況

2026-09-30。利用者が指定した主用途は「DAWのループ再生に合わせ、MATCHを保ってPRE／POSTを聴き比べる」。
中断理由・復帰案内・本文配置は`5dbea7ce`（B-1108）に保存した。
B-1106の成立性試験で見つけた整数周回ずれに対し、以下の既知K継続を実装した。
現在の実装条件は「既知KによるLOOP継続の実装範囲」とINV-LC22を正本とする。
直下は利用者の実行指示に沿って進めている再計画であり、未実装の初回LOOPを対応済みと認定するものではない。
下段の成立性調査と旧構造の説明は実装前の記録として保持する。
製品候補の実機認定、公開リリース、通常版の配置は未実施。別識別の非出荷診断による実機観測とは区別する。
最初からLOOP中の初回K取得を完了したとは扱わない。

## 2026年10月1日追記：比較前の時計準備

利用者が指定した主用途は音色・ダイナミクスの比較であり、コンプとdynamic EQを含む。
毎周の素材生成・ランダムなアレンジを評価する機能は今回の受入範囲に含めない。
この範囲指定を、処理の内部状態・微小ノイズ・通常の変調が動くたびに時刻根拠を失効させる許可へ読み替えない。
新しい時計準備はPCMの一致や相関を条件にせず、処理の音色変化と時計の連続性を分離する。

通常再生で比較を開く前から確認できた線形時計を保存し、その後に停止せずLOOPへ移った場合は、
LOOP中に初めてBLIND／LISTENを押しても同じKを検証して引き継ぐ構造を実装した。
MATCH済みであることは前提にせず、BLINDを直接押した後の固定MATCHは従来の音声窓で準備する。
LOOP解除・再MATCH・LOOP専用モード・利用者の追加確認はこの経路へ加えない。

PREが常時公開するのは128 bytesの固定時計metadataだけで、PCM履歴・demand・gain・選択・出力許可は持たない。
既存のPCM容量は524288 framesのまま、音声コピーは明示比較のdemand後だけ行う。
POSTの準備leaseはdemandを所有せず、終了しても実比較の音声コピーを止めない。
既存service timerを待機中250 ms、明示比較・復帰・他の未完了処理中50 msで再利用し、新timer／workerは作らない。
新layoutはversion 5と専用名で旧版を分離する。実processor全体の負荷受入は、局所metadataの小ささとは別に測る。
PREの時計判定はcallbackごとに一度だけ行い、その結果を明示比較時のPCM公開にも共有する。

根拠はpair／rate、PRE領域の128-bit lifetime、PRE時計世代、POSTの当該callback、restore ticketへ束縛する。
初回の読み取り競合やPOST先行callbackでは、同じ未成立の入口を保ったまま新しいPRE PCMを待つ。
古い音声窓を時計metadataだけで出力可能へ昇格させず、書込み世代・全block範囲・PCM seqlockを検査する。
PCM seqlockとは別に公開される時計世代と領域lifetimeはコピー前後で照合し、コピー中の変更とPRE終了も拒否する。
時計metadataの一時的な書込み競合だけでは、すでに成立したPCM比較へ新たな中断条件を加えない。
完成した／壊れたproofは初回入口へ戻さず、失効したBLINDを準備根拠で自動復活させない。

ローカルの実PRE／POST、Rust C ABI、pair、共有ring、実editorで「通常再生→LOOP→後から直接BLIND」を確認した。
4096 samplesの物理遅延と、毎周状態をresetしないコンプ／level-dependent bandのモデルを使用した。
S-1実ファイルに毎周同じ広帯域成分とレベル変化を重ね、単一周波数だけでなく全frameのsource／gainを検査した。
このfixtureを相関offset monitorや未申告の第三者plugin遅延変更の認定へ流用しない。
最終の広帯域fixtureは20周の安定出力、453632 stereo framesを物理delay lineの期待値と全frame照合した。
両Sourceの実content offsetも決定可能で0 samplesだった。通常Aはbit identical／0 samplesを確認した。
比較の操作はBLIND、Source 2、開示、ENDの4クリック。pair選択とDAWのLOOP設定はこの数に含めない。
これは検証用host fixtureの合格であり、各社pluginやMac／Windows実DAWの現候補認定ではない。
Windows x64 Releaseでも同じportable契約を`/W4 /WX`で検証した。遅延0／64／4096 samplesと
LOOP長6000／24000／192000 samplesの9条件、14301 full-frame blocksは独立した物理delay lineと一致した。
no-demandのPCM未変更、領域終了・世代／lifetime変更・欠けた時計・stop／seek／range／tempo変更の拒否も含む。
この合格はWindows共有領域と共通時刻判定の範囲であり、Windows DAW・署名AAX製品の合格ではない。

先行する通常再生が無い最初からのLOOP、停止後LOOPの再取得、hostの全周project clampは未完了のまま。
LOOPだけの反復位置からKを生成したり、停止前の根拠を持ち越したりしない。
初回LOOPを後期へ送ったり、AU／AAXを除外したりする判断も行っていない。

### 時計準備候補のローカル検証と負荷判定

Macの機能回帰41件はpass。コピー後の世代確認を加えた後、影響する33件を再実行してpassし、
上記の広帯域・動的処理・content offsetを強化した製品fixtureもpassした。
Windowsの共通時刻判定は同じ最終sourceでpass。これらを未実施の実DAW認定へ流用しない。
時計準備の先行検証ではRust workspaceは2185 pass／0 fail／41 ignored、source契約161件とclippyもpass。
FFI sourceは未変更。未実施のignored parity／pairingを通常workspaceの合格へ含めない。

Full processorの基準はHEAD B-1121、候補は未commitの時計準備を含む現tree。
同一の測定器、Mac x86_64 Release、同じRust FFIで、64／128／256／512 samplesの
通常A／PRE LOOP／同じMATCHのBLIND LOOPを各1200 callbacks検証した。
測定器の表示用status問い合わせはAudio Threadから除き、message threadの状態確認と
独立した全frameのPRE／gain照合へ分けた。基準側にも同じ測定器を適用し、source hashを照合した。
この変更は測定器だけで、製品出力や数値基準を変えていない。

全条件で音声、0 sample、C++／System allocatorの確保・解放0、deadline超過0はpass。
追加時間の事前上限は中央値max(1 µs, 基準の10%)、p99 max(2 µs, 基準の20%)のまま。
最終比較は12条件中3条件でfailしたため、軽量性の受入は保留する。

| 512 samplesの未達条件 | 基準 → 候補 µs | 追加／上限 µs |
| --- | --- | --- |
| 通常A 中央値 | 15.349 → 18.311 | 2.962／1.5349 |
| PRE LOOP p99 | 87.174 → 105.844 | 18.670／17.4348 |
| BLIND PRE LOOP p99 | 79.624 → 99.012 | 19.388／15.9248 |

時計判定の共有化前後とCPU／wall診断には変動があり、製品処理と計測環境の寄与は未確定。
局所metadataが小さいこと、deadline内であること、別測定がpassすることを、追加負荷基準の代用にしない。
測定器修正前の失敗結果も保全し、合格するまでの無根拠なmatrix再実行は行わない。
次は未達条件の処理内訳と環境変動を分離してから、必要な再測定範囲を決める。
既存のRust Phase Dの負荷下失敗も、今回の時計準備で構造的に解消したとは扱わない。
この負荷測定時点では、製品配置・署名・公証・GitHub CI・公開は未実施だった。

### 厳格レビューで見つかった共有領域と検証の欠落

同じpair identityのPREを複製すると、稼働中の共有領域を再初期化できる経路を再現した。
領域lifetimeと時計世代の検査だけでは、二つの書き手が同じ領域へ書き込む事態を防げない。
PREは領域作成の前に非RT側で一意な所有権を取得し、領域終了後にだけ解放する構造へ変更した。
Macは本人だけが開ける通常ファイルの非ブロックflock、Windowsは読取り側が保持しない専用の名前付きsectionを使う。
プロセスの異常終了でもOSが所有権を解放する。旧POSTが保持する音声領域は終了扱いにし、再初期化しない。
Windowsの既存の4枠とPCM容量は維持し、Audio Threadへロック・syscall・pollは追加しない。
MacのPOSIX共有memory descriptorへのflockはENOTSUPだったため、その方式は採用しなかった。
MacとWindowsのportable試験で、同一pairの重複拒否、別pairの独立動作、通常終了後の再取得、
C++ destructorを実行しない異常終了後の再取得と旧readerの非再初期化を確認した。

時計準備のportable試験と実processorの動的処理fixtureは、従来のrelease sourceの明示一覧から漏れていた。
Macの必須source gateとWindows CIのbuild／test選択へ両方を加え、CTest実件数と静的契約で欠落を検出する。
Source切替のfixtureでは、監査終了時に最後のaudio writerが退場した後だけ件数を確定するようにした。
非出荷の時計trace試験は、ReleaseのNDEBUGで判定が消えるassertを使っていた。
Releaseでも必ず判定する失敗経路へ置き換え、最適化・NDEBUG有効の実行と解析器の正常系／異常系を確認した。
LEVELは完成したPOST packetでも、PRE-chain側のwriterがbusyだと旧pairのchainを保持できることを再現した。
PRE bindingの変更は完成packetとfallbackの両方で旧chainだけを破棄し、POSTの絶対値・履歴は保持する。
新しい境界試験も五sizeの描画契約へ含め、単なる取得競合では画面を消さない規則と両立させる。
これらの修正を、初回LOOPの完成や、上記の追加負荷超過を解消した証拠にはしない。

レビュー後の必須source gateはnative 55件、通常Rust、静的契約162件、clippyがpassした。
ignored suiteの実一覧はparity 20件／pairing_candidates 6件で、両方の全件を実行してpassした。
後から追加したLEVEL binding境界の修正も、描画・実editor・TIME履歴・analysis demandの関連4件を再実行してpassした。
これらはソースとfixtureの確認であり、現候補の実DAW・署名AU／VST3／AAX・公開配布の認定ではない。

## 2026年10月1日の再計画案

推奨は、通常再生で時刻対応を確定してからLOOPへ入る経路を先に完成させ、初回LOOPからの開始を
独立した成立性ゲートで判定すること。利用者の目的はどちらも少ない操作で比較することであり、
LOOP解除や再MATCHを通常の手順として要求しない。AU／Windows AAXを対象外にしたり、
初回LOOPを利用者の承認なしで後期工程へ送ったりしない。

### 先行する経路と初回LOOPを分ける理由

通常再生から停止せずLOOPへ入る場合、すでに確かめたPRE／POSTの時刻差Kを引き継げる条件がある。
最初からLOOPの場合は、同じ曲位置に複数周回の音があり、Kを新しく一意に取得する必要がある。
これが先行経路を推す理由であり、「MATCH値が残っていればPREを出してよい」という意味ではない。

| 経路 | 保持するもの | PRE出力の条件と残る検証 |
| --- | --- | --- |
| 通常再生でMATCH後、停止せずLOOPへ移行 | 承認gain・ceiling・選択。時刻根拠も連続する場合に限り保持 | 正常wrapの全frameが対応し、100周で再MATCHも中断も不要であること |
| MATCH後、停止してLOOPを設定し再生 | 同じpair／format内の承認gain・ceiling・記名選択 | 停止前Kを無条件で再利用しない。新しい時刻根拠が取得できれば音量再測定なしで復帰すること |
| 比較前の通常再生から停止せずLOOPへ入り、LOOP中に初めて比較開始 | 準備した時計根拠だけ。gainや出力許可は持ち越さない | 上記の時計準備で当該callbackと新しいPCMを検証し、追加操作なしで固定MATCHを準備する |
| 先行する通常再生の無い最初からLOOP | 未取得のMATCHや時刻根拠を捏造しない | 現在の正しい周回を独立に特定し、その後に既存のMATCH測定を行えること |

Windows Pro Toolsでは既存試験時に再生中のLOOP操作が無効だった。
したがって「後からLOOPだけにする」案でも、停止再開後の再取得問題は残る。
比較前の時計準備を実装しても、先行する線形観測の無い初回LOOPの反例は解消していない。
二つの入口を分けることは調査・実装順の改善であって、全形式の完成を保証する回避策ではない。

### 公式資料から確認した時計の違い

2026-10-01に以下の一次資料と使用中wrapper／SDKを読み直した。Web仕様は一般契約、
実測は特定host／version／構成の観測であり、両者を混ぜて万能な時計として扱わない。

- VST3の`continousTimeSamples`はLOOPを折り返さないproject時計だがoptionalであり、valid flagを要する。
  cycle境界でblockを分割する義務もない。この仕様だけで全hostのPDC後の音声位置まで証明しない。
  [Steinberg ProcessContext](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)。
- 必要なprocess contextは宣言する必要がある。現在のJUCE VST3 wrapperは連続時計を要求済みなので、
  単に要求flagを足す修正ではない。
  [Steinberg Process Context Requirements](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Change%2BHistory/3.7.0/IProcessContextRequirements.html)。
- AUのrender sample timeとhost transportのtimeline位置は別の値。
  `mHostTime`もvalid flag付きの機械時計であり、音の周回IDではない。現在のwrapperはhost timeを
  取得済みで、Studio Proの欠損はその取得コードが無いことでは説明できない。
  [Apple AudioTimeStamp](https://developer.apple.com/documentation/coreaudiotypes/audiotimestamp)、
  [Apple HostCallback_GetTransportState](https://developer.apple.com/documentation/audiotoolbox/hostcallback_gettransportstate)。
- AUとVST3のpresentation latencyは、そのノードの入力まで／出力からの遅延をhostが通知する情報。
  **0は遅延なしと不明の両方を表し得る**。通知された0も、自動的に既知のPDC=0へ昇格させない。
  [Apple contextPresentationLatency](https://developer.apple.com/documentation/audiotoolbox/auaudiounitbus/contextpresentationlatency)、
  [Steinberg IAudioPresentationLatency](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IAudioPresentationLatency.html)。
- AAXのnative sample locationはNative処理buffer先頭の位置。TODは直ちに読むengine時計であり、
  別APIの`AddClock`はalgorithm contextへhostが渡すrunning counterで、停止中も進む。
  [Avid AAX_ITransport](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00086.html)、
  [Avid AAX_IController](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00066.html)、
  [Avid AddClock](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00065.html)。
  公開Web資料はSDK 2.1.1であり、現在の2.9.0との差を外部SDKの同名headerでも照合した。
  2.9.0にはsample quantumごとの進行と、Known IssuesのPT-282946（live／non-live間の2-buffer差）もある。
  これは今回の検証機で同問題を再現したという意味ではない。`AddClock`をTODやcontent時計と同一視しない。
- JUCE共通`PositionInfo`はhost／形式によって欠ける値を持つ。boolや値のfallbackで出所を消すと、
  不明を既知OFFや共有時計と誤認する。
  [JUCE PositionInfo](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html)。

AUの`InputSamplesInOutput`も公式SDKで確認したが、AUからhostへ入力出力の対応を伝えるcallbackであり、
下流のHyphaが他社plugin全体の対応を問い合わせるAPIではない。これを不足情報の取得経路として採用しない。
SDK本体・認証情報はrepoへコピーしない。ベンダーへの問い合わせは今回の調査に含めていない。

### 時計取得と対応判定を分離する構造

再計画時の`LiveCompareClock.h`はVST3 continuousとAU renderを同じ`hostContinuous`へまとめていた。
実装では出所を分離し、出所が変わった最初のblockを無効にして旧runとKを引き継がないようにした。
AAXの製品経路は引き続きinstance別frame counterを使う。Consumerの初回較正は非LOOPのproject対応に依存し、
既知KのLOOP継続もproject／PPQで裏付ける。初回Kを差し込むだけでは、実測された長遅延の全周project clampを解決しない。

再設計ではraw時計の種類・有効性・単位・起点・失効条件を保持し、形式別の小さなadapterが
「このblockの音をどのPRE区間に対応付けられるか」という検証済み根拠だけを共通Consumerへ渡す。
SDK fieldが存在すること、同じ数字、同じPPQ、到着順、host名だけでは出力許可にしない。
raw情報→正規化→対応根拠→ring全範囲検査→rendererの順に責務を限定し、汎用event busは作らない。

MATCHの承認値、現在の対応根拠、利用者の選択、BLIND試行、ENDの音声復帰leaseは別に維持する。
停止・seek・gapは時刻根拠と測定窓を失効させるが、同じpair／formatの記名比較の承認値を理由なく消さない。
再取得は時刻確認であり、自動再MATCHではない。pair変更・format変更・state restoreへ旧承認を移植しない。
失効したBLINDを自動復活させず、POST代替を匿名Sourceの聴取として記録しない。

### 次に行う成立性検証

新しい製品側の時計方式を実装する前に、非出荷probeと既存の独立PCM oracleで次の三候補を判定する。
各候補にはpositive controlと、同じ見かけの位置でも正解が1周異なるnegative controlを用意する。

1. VST3は共有の連続時計が実PCMに対応する条件を確定する。既存のMac K=0観測を出発点にするが、
   Windows、途中追加、PDC状態変化、sample rate／block scheduleを別々に検査し、K=0を形式名で決めない。
2. AAXは未観測の`AddClock`をalgorithm contextの診断欄へ取得する。TOD／native位置も別欄に残す。
   sample quantumの単位・分割callbackでの進行、live／non-liveの差、停止再開、遅延0／4096／LOOP長以上を
   固有IDと照合する。共通counterでも音とのoffsetが一意でなければ不採用。2 buffersを一律に足し引きしない。
3. AUはinstance別render原点とtransport変更通知を保存し、host timeとpresentation通知の有無・更新時点を確認する。
   同一形式とVST3→AUを分け、両側が再生開始を観測した条件と、片側の途中追加・復元・sleepを対照にする。
   クリック前の根拠保存はこの検証で寿命が確定した場合だけ採用し、LOOPだけの履歴からKを生成しない。

最初の最小試験は、停止前からprobeを置いて初回LOOP→停止再開、およびLOOP中のPOST追加。
LOOP長に整列しないblockと、delayがLOOP長未満／同値／超過する条件を分ける。
既存probeの先頭／末尾IDだけの診断から、非出荷observerの全frame照合へ拡張してblock内部の誤りも検出する。
診断の事前確保領域と非RT exportは製品へ持ち込まない。通常の音源を変更する診断は承認済みの使い捨てSongだけに限定する。

各候補の合格には、一意なPCM対応、欠損／旧run／反証時の拒否、適用寿命、起点を観測できなかった場合の動作が必要。
数学的に同一の観測から異なる正解を区別できない場合は、その候補の不成立を確定し、待機を延ばして再試行しない。
同じ仮説の試行は最大2回とし、再試行には修正または新しい証拠を明記する。
成立しない形式は除外も完成扱いもしない。足りないhost保証と代替案の契約変更・負荷・操作数を提示し、
当該形式の新出力方式や後期送りは利用者の判断を得る。重い常時相関や録り直しを暗黙の代案にしない。

### 実装と受入の順序

1. 既存のEND、LEVEL publication、Capture A文字切れ、既知K継続の差分を保全し、対応条件と未実施試験を整理する。
   調査用TOD patchは出荷JUCE patch stackへ混ぜない。既存の別作業差分を巻き込まない。
2. 通常再生からのLOOP継続を先行して実processorで完成させる。gain revision／ceiling／選択を保持し、
   正常100周でPREが実際に出力されることと、MATCH済み→BLINDの継続を検査する。
3. 並べて形式別の成立性検証を行い、合格した時計根拠だけを共通の対応判定へ統合する。
   停止再開後の時刻再取得と初回LOOPを別試験にし、先行経路の合格を全入口の合格へ読み替えない。
4. 固定MATCH・新規測定窓・中断理由・復帰可否を共通projectionへ渡し、記名比較とBLINDのUIを揃える。
   上がる音量は終了前に示し、停止中ENDは1クリックで画面を閉じ、実音の復帰待ちを通常画面で示す。
5. exact candidateのMac Studio Pro VST3／AU、Windows Studio Pro VST3／Pro Tools AAXを確認する。
   Mac Pro Tools AAXも別gateとし、WindowsやDeveloper hostの成功を流用しない。
   Developer未署名probe、署名済み通常host、製品PCM受入、配布署名をそれぞれ区別する。

UIの完了条件は、認定正常LOOPで追加クリック0、周回ごとの確認0、LOOP解除／再MATCH要求0。
開示と終了の主動線を維持し、新たな回答収集やLOOP専用モードを増やさない。
待機は取得可能な根拠が有限時間で揃う場合だけ表示し、確認不能は現在のPOST出力と具体的な理由を表示する。
記名比較では承認値とPRE選択を保持して、根拠が再取得できた最初のblockから復帰する。
BLINDは反証・代替出力で理由を保持して中断する。正常wrapでの意図しない中断は合格にしない。
文字切れ・LEVELのblank／チラつき・REF共存・英日全サイズも実画面とUI fixtureで確認する。

### 軽量性と完了判定

製品のPCM容量は据え置き、時計判定はblock単位の固定サイズ・回数上限付きとする。
前段の根拠保存が必要でもPCM常時コピーは行わず、既存のmetadata増分上限64 KiB／ringを超えない。
新worker／timer／RT alloc・lock・I/O・常時logを追加せず、新owned sourceは500行以下にする。
Kirin OSは既存の分析・Reference準備・表示データを担当できるが、欠けた周回情報を推定してRTの出力許可にはしない。
Kirin OSとの往復や停止でHyphaの通常A経路が止まる設計にせず、GPL／プロプライエタリの分離も維持する。

64／128／256／512 framesでfull processorの中央値・p99、alloc／free、deadline超過を同条件で比較する。
通常Aはbit identicalかつ0 samples、実DAWの低bufferでdropoutなしを要求する。
設計上限とは別に、各clock adapter導入前に許容追加時間を数値で記録し、局所rendererの旧結果だけで低負荷と認定しない。

全frameの位置／周回／gain照合、100周、長遅延・容量超過、PDC変更、seek、gap、情報欠損、停止再開、
途中追加、保存復元、END競合、Offline Bounceを受入matrixへ入れる。全拒否で誤出力0を達成しても正常条件の合格ではない。
Rust test／clippy、native／RT source契約、line budgetを維持し、FFI変更時はignored parity／pairingも全件実行する。
実装は現在の作業branchから責務単位の連続commitにし、無関係なtreeの削除・branch整理・公開はこの再計画で行わない。
CI前に共通予算ルールと同一candidateの既存runを確認し、必要matrixとrequired checksを省略せず重複を避ける。
公開する場合は同一commitのmacOS／Windows／AAX証跡と既定の全配布gateを満たす。

再計画段階で確認した公式契約と既存実測を、新しい時計方式の実機合格に読み替えない。
以下に、実行指示後の先行LOOP継続の検証とAAX `AddClock`の独立PCM照合を別々に記録する。

## 2026年10月1日の実装と検証結果

通常MATCHから停止せずLOOPへ入る実processor試験を強化し、記名PRE 100周と同じMATCHのBLIND 100周で
全frameの位置・周回・gainを確認した。初回LOOPの取得は未完了であり、この先行経路の合格で代用しない。
AAX `AddClock`も実機で観測したが、単独で初回LOOPの音声位置を決める候補としては採用しない。

### 製品側の時計出所と先行LOOP試験

`LiveCompareClock.h`はVST3 continuous、AU render、instance別frame counterを区別する。
数値が偶然連続していても出所が変わった最初のblockは無効とし、既存Publisher／Consumerのrunとproofを失効させる。
欠けたAU／VST3値を有効なcounterへ黙って置き換えない。AAX AddClock／TODの診断拡張は製品に入れていない。
固定サイズの状態だけを追加し、製品PCM容量、RT alloc／lock／I/O、新worker／timerは増やしていない。
full processorの64／128／256／512 framesで通常A・記名PRE LOOP・同じMATCHの匿名PRE LOOPを各1200 callbacks測った。
音声の全frame照合、通常Aのbit identical／0 samples、callback内のC++／C／Rust System heap操作0、deadline超過0はpass。
ただし変更前との追加時間は12条件中3条件で事前上限を超えた。後述の追計測を含め、軽量性の最終受入は保留する。

200周試験は実PRE／POST processor、Rust C ABI、pair、共有ring、editorを通す。
間に4096 samplesの実遅延を置き、毎frame異なるstereo信号の期待値を物理delay lineから計算する。
期待値は製品K、ring読取り先、PPQから作らない。POST退避をPRE成功として数えず、gainとceilingを保持する。
通常wrapの記名100周に続き、同じMATCHの匿名100周、Source切替、開示、END、減衰保持、offlineを検査した。
これはfixtureでの受入であり、Mac／Windows実DAWの製品候補認定ではない。

### Windows AAX AddClockの実測

Windows Pro Tools Developer 2026.4、48 kHz stereo、実callback 1024 frames、LOOP長131072 samplesで観測した。
入力は承認済みの使い捨てSessionの非出荷Identity Sourceで、最大-42.14 dBFSの固有IDを全stereo frameへ出す。
sourceとobserverは明示Start後の最大65536行を事前確保領域へ公開し、非RTで不変prefixをexportする。
毎frameのIDを逆変換して連続性を調べるため、正しい両端に挟まれた別周回・無音・NaNも成功にならない。
大きなPOST blockが複数のPRE callbackをまたぐ場合も、途中のPRE観測欠損を全frame確認へ昇格させない。
この検査と保存領域は診断専用で、Hypha製品には追加しない。

| 構成 | 再生blockと全frame確認 | 確認した実音に対するAddClock差 |
| --- | --- | --- |
| SourceとObserver隣接、初回LOOPと停止再開 | 1541／1541 blocks、1577984 frames | 全blockで0 sample |
| Source → Observer → 実遅延4096 → Observer、初回LOOPと停止再開 | 1534／1542 blocks、1570816 frames | 確認済み1534 blocksで4096 samples |
| 同じ遅延後へ再生中にObserver追加 | 598／598 blocks、612352 frames | 全blockで4096 samples |

遅延構成の未確認8 blocksは、各再生開始の最初の4 blocks。全1024 framesで固有IDが復号できず、成功から除外した。
それ以後と途中追加ではblock内部ID異常0、確認済み範囲のAddClock差の矛盾0だった。
各runでAddClockはsample quantumに沿って進み、native位置は周回で折り返した。
input／output presentation latencyは通知されなかった。TOD差は別欄に保持し、AddClockへ代入していない。

この実測は共通の処理counterとしての使用可能性を示すが、PDC後の音声位置を直接示す契約の証明ではない。
遅延ありでは、同じ実音に対して4096 samplesの補正が別途必要だった。形式名によるK=0の投入を禁止する。
さらに、共有engine counter・折り返すnative／PPQ・LOOP設定がすべて同じでも、実遅延0と1周分で
正しいPCMが異なるモデルを4500 blocksで確認した。線形区間では同じmodelがK=4096を正しく取得する。
これは数学的な反例であり、未実施のAAX長遅延／live対non-live実機試験の代わりではない。
追加のcontent／PDC根拠なしにAddClockを初回LOOPの出力許可へ接続することはしない。

未署名・別plugin IDのDeveloper診断であり、通常Pro Toolsの署名済みAAX、Mac AAX、VST3／AUの認定ではない。
通常Hypha・音声routeを変更せず、信号を停止してDeveloper hostを終了し、診断3 bundleをhash確認して
元の検証directoryへ復元可能な形で退避した。SDKと診断wrapper patchは製品JUCE stackに混ぜていない。

### Full processorの負荷検査

非出荷`KirinLiveCompareProcessorBenchmark`は実PRE／POST、Rust FFI、pair、ring、message／measure／IO serviceを
通す。editor paintや実DAWのdropoutを含む試験ではない。LOOP長24000 samplesは256／512 framesに整列しない。
各サイズの通常A・記名PRE・匿名PREを1200 callbacks測り、後二者は実際のPRE出力を全frame照合する。
ランダムな匿名mappingで基準版と候補版が違うrendererを測らないよう、両方とも実際のPRE側を選ぶ。

比較元はHEAD B-1121のtracked sourceをignored directoryへ展開したもの。新branch／worktreeは作らず、
同じ試験・承認済みJUCE patch stack・変更していないRust static FFI・x86_64 Releaseのflagsを使用した。
製品に常時計測・解析worker・heap hookを追加していない。macOSのheap hookは独立した試験dylibだけにlinkする。
実Rust FFIのcreate／destroyで確保287／解放269を観測するpositive controlを置き、実callbackの最初と状態遷移も
含めて観測heap操作0を確認する。probeのattach／detach失敗は合格にならない。
Windowsの同じfixtureはC++ new／deleteだけの動的観測であり、macOSのSystem allocator coverageを流用しない。
これはVM／独自allocatorやlock／I/Oの全捕捉ではない。RT source契約と実DAW検査は別に維持する。

追加時間の事前上限はpair中央値でmax(1 µs, baselineの10%)、pair p99でmax(2 µs, baselineの20%)。
全12条件でp99上限とdeadline超過0はpassしたが、中央値では次の3条件が超過した。

| 条件 | 基準版 → 候補版のpair中央値 | 追加時間／上限 |
| --- | --- | --- |
| 128 frames、記名PRE LOOP | 8.802 → 14.339 µs | 5.537／1 µs |
| 128 frames、匿名PRE LOOP | 7.668 → 11.022 µs | 3.354／1 µs |
| 512 frames、記名PRE LOOP | 17.786 → 21.033 µs | 3.247／1.7786 µs |

超過した128／512 framesだけ、製品処理外の公開`CLOCK_THREAD_CPUTIME_ID`取得を加えてCPU時間とwall時間を
別々に診断した。128 framesの記名／匿名のCPU pair中央値は基準7.521／7.315 → 候補7.177／7.265 µs、
512 framesは基準21.202／24.541 → 候補23.111／22.884 µsだった。この追計測の差は同じ事前上限内だが、
診断の時計取得自体の負荷を含み、先の無instrumentation結果と混ぜない。製品の負荷増加か、実行環境・
時間帯による変動かはまだ確定していない。追計測のpassで先のfailを消さず、同じ失敗の無根拠な反復もしない。
実DAWの低buffer／dropout検査、追加時間の原因切り分けと最終受入を残す。

### 合格済み検査と残工程

- Rust workspace 2181 pass／0 fail／41 ignored、clippy owned警告0。FFI source未変更で、ignoredをPASSに数えない。
  最終再検証の途中で`parity_phase_d_metrics_ffi_vs_direct`が1回、完成したFFI値を公開できずfailした。
  overflowは0。package単独、失敗したworkspaceと同じbinaryの単独、他buildを伴わないworkspace再確認ではpass。
  計測許容差・試験timeout・製品FFIは変更していない。発生条件は未確定で、再確認passを再発防止の完了と呼ばない。
- nativeのEND／中断／停止再開／保存復元／MATCH／PIN／offset／AAX group等26件、200周とclock provenance 2件、
  editor／表示contract 6件がpass。engine counter反例の追加後も関連2件を再実行してpassした。
- 診断native testはMacとWindows各2件、Node analyzerは旧形式・中間sample異常・途中観測欠損を含む8件がpass。
  新source 500行制限、29件の既存ratchet、出荷JUCEの承認済み10 patches照合、diff checkもpass。
- heap probeのattach／detach失敗を拒否する最終版でも4サイズとclock provenanceの5件を再実行しpass。
  full processorの音声／heap／deadlineと、変更前に対する追加時間gateは別で、後者はfail3／12を残す。

初回LOOPと停止後LOOPの時刻再取得、各形式の根拠adapter、full processor追加時間の原因切り分けと受入、exact候補のMac／Windows
製品実DAWと署名済みAAX認定は未完了。承認gainの保持と現在の出力許可を混同して完了にしない。
FFI Phase Dの一度の未公開失敗も、原因切り分けを申し送る。
長時間待機、任意の整数周回補正、重い常時相関でこの不足を隠さない。新出力方式、初回LOOPの後期送り、
AU／Windows AAXの除外は未承認であり、利用者判断なしに行わない。commit／push／CI／配布gateも未実施。

詳細のsource／binary／CSV hash、初期失敗の原因、終了後の状態はローカル検証記録
`target/validation/loop-plan-execution-20261001.md`に残す。公開した実機受入証跡とは扱わない。

## 既知KによるLOOP継続の実装範囲

- `LoopContext`と`LoopTimeline`で両側のblock境界を確認する。一定tempo、同じ範囲、PPQとsample位置の移動が
  整合するLOOPだけを認め、Kは非LOOPで較正した値を維持する。新規timer／worker／PCM複製は追加しない。
- PREは線形runの原点と一つのLOOP anchorを固定サイズで公開する。各blockで連続性を検査するため、
  通常較正の過去descriptor探索をO(1)のanchor照合に置き換えた。共有layout／名前はversion 3に分離した。
- 既知Kの読取り先をLOOP情報で裏付ける。PPQから読取り先を作らない。POST位置clampは有限の確認待ちとして
  POSTへ退避し、期限切れや反証でKを失効する。BLINDは1 blockの退避でも中断する。
- 全区間を確認済みの履歴だけがMATCH／AUTO／offsetに使える。proof・PRE run・上書きをcopy前後で検査し、
  MATCH案にもproofを持たせる。短LOOPの実連続再生時間は貯められるが、PINの単一project範囲とは分離する。
- 正常wrapでgainを変えない。失効後は数値を保持し`HELD`と再確認案内を出す。
  対応を確認できない初回LOOPは、確認不能と現在のPOST出力、終了方法を明示する。
  LOOP解除を正常な準備手順にせず、自動で準備が進むとも表示しない。初回取得の実装完了を意味しない。
  失効したBLINDはEND後の再開始を案内する。

## 2026-10-01: 実DAWのAU境界観測と未完了の初回取得

Studio Pro 8.1.2、48 kHz / stereo / 2048 frames、診断PRE VST3 → 既知のdelay → 診断POST AUで、
両側のnative sample、連続時計、PPQ、loop range、presentation latency、PCM先頭／末尾bitを採取した。
通常版Hypha・元Song・音声device/routesは変更しない独立した診断であり、修正版製品の実DAW合格ではない。

- 4096-sample delay、4秒LOOPでは、折り返しのPRE位置`512, 2560, 4608`に対し、
  AU native位置は`-3583, -1535, 512`、PPQは`0, 0, 0.021333…`だった。
  **sample位置はclampされておらず、PPQだけが先頭で止まる**。負数の1 sample差はAU wrapperの変換に由来する。
- 既知Kで算出した対応先の音楽座標と、native sampleが正確に1周前を指す観測が既存の丸め許容内で整合した場合、
  PPQ clampをK失効と誤認しない。補正するのはtimeline検証用の座標だけで、PCM索引は`POST clock - K`のまま。
  初回K、sample位置までclampされた場合、余分な1周、矛盾するnative位置／PPQには適用しない。
- 実測368 callbacksの時計列を独立した周回別PCMで再実行し、旧352 → 新358 acceptedを確認。
  初回の10較正blocks以外は全て対応し、K変更・誤周回出力は0。矛盾7種は拒否する。
  既知K matrixはnative-before-loopを追加して750条件×100周、誤出力0、RT alloc/free 0。
  この追加モデルで100%出力を要求するのはdelay < loop長。長遅延を継続可能と誤認定しない。
- 8192-sample delay / 6000-sample LOOPも実測した。238 callbacks、80境界で連続時計の欠損0。
  AUの内容とnative位置の差は5999または11999 samples。1周分だけを認める規則では全区間を継続できない。
  WAVは毎周同じため、この実測だけで周回の正しさを証明しない。周回別PCMの独立fixtureを引き続き必要とする。
- VST3のhost nanosecondsは存在するがAUでは欠損。AU wrapperはhost timeのvalid flagを既に転送しており、
  単なる取得実装漏れではない。隣接・delayなしでは両側presentation latencyが欠損し、既知0ではなかった。
  delayありではPRE output=4096/8192、POST output=0が観測できたが、これだけで任意のclock起点や周回を認定しない。

**初回LOOPから一意なKを取得する構造は未完了。** 既知Kの継続修正とは分けて追跡する。
候補は比較開始前からの小さな時計根拠の保持だが、両側の同じ再生区間を識別する根拠、最初のcallbackを
見逃した場合、loopより長いdelay、欠損presentation、plugin追加／復元／gapの境界を先に定義する。
到着の新しさ、同じPPQ、停止→再生の回数だけでKを決めない。常時PCMコピーや解析workerの増設もしない。
利用者へLOOP解除・再MATCHを要求して本来の受入条件を達成した扱いにしない。

今回の追加経路はring容量／共有layout／timer／workerを変えない。ASan/UBSanでも境界試験を通した。
同一Macの`-O3`局所rendererを3回比較し、通常の確認済みLOOPは64 framesで旧0.753–0.765 µs、
新0.742–0.754 µs、512 framesで旧5.062–5.067 µs、新5.005–5.060 µs（中央値）。
別途native-before-loopを独立counter＋4096 delayで駆動するbenchmark mode 5を追加し、
64/128/256/512 framesで観測区間の拒否0を要求した。中央値0.822/2.067/3.600/7.928 µs、
p99 1.762/3.662/14.283/25.357 µs。並行試験負荷があり、DAW全体のCPUや低bufferの実機認定ではない。

## 2026-10-01: 初回LOOPの根拠保持と固有診断信号

`LoopEntryEvidenceContract.h`は非出荷の成立性試験。比較前の時計根拠を固定152 bytesに保持する
候補を検査した。非LOOP区間を先に観測した10条件では正しいKを保持できるが、独立counterで最初から
LOOPの場合は、事前観測を1,000 blocks増やしても一意性が得られない。6,000 / 24,000 / 192,000 samplesの
LOOPで、全clock・flags・loop metadataが同じ4,500 blocksにもかかわらず正解の周回PCMが異なる反例を確認した。
これは製品へ採用した較正器ではなく、待機時間・観測量の増加だけでは解決しないことを調べる対照である。

利用者の明示承認後、独立IDの非出荷`HyphaLoopIdentitySource`を作り、使い捨てSongだけで診断した。
入力を置き換える音源はこの診断targetだけで、通常Hyphaには入れない。各stereo frameの32-bit固有IDは
最大−42.14 dBFS、LOOP・停止再開・prepareで繰り返さず、容量上限では無音になる。解析器はclock／PPQ／
到着順を使わずIDからsourceの範囲を探し、その後にclock差を測る。先頭／末尾frameの照合であって、
全中間sampleの証明や製品受入、全hostの資格認定ではない。手順は`juce_shell/tests/host_clock_diagnostic/README.md`。

Studio Pro 8.1.2、48 kHz / stereo / 2,048 frames、6,000-sample LOOPを最初から有効にし、
合計8,192-sampleの既知delayを置いた。各形式の観測を混ぜて合格としない。

| 診断構成・操作 | 独立IDで照合できたblocks | 観測したPOST clock − 対応PRE clock |
| --- | ---: | --- |
| VST3 source → 隣接VST3 | 829 / 829 | 0 |
| VST3 source → delay → 停止中に追加したVST3 | 450 / 454 | 0。残り4はdelay初期無音 |
| VST3 source → delay → LOOP再生中に追加したVST3 | 1,144 / 1,144 | 0 |
| VST3 source → delay → AU、停止再開を含む2 run | 1,275 / 1,283 | 1,566,720 → 3,559,424。各run冒頭4は初期無音 |
| AU source → delay → AU、停止再開を含む2 run | 2,120 / 2,128 | −1,087,488 → −1,034,240。各run冒頭4は初期無音 |

照合できた範囲でclock欠損、block内のclock差不一致、ID span不一致、source範囲不在はいずれも0。
AU同士でもinstanceごとの時計原点は共通でなく、停止再開後に差が変化した。VST3の遅延後project位置は
短いLOOPの全区間で0にclampされ、project位置のwrap検出数0でも連続時計は進んでいた。
したがって、既存のloopJoinへ初回Kだけ渡す対処でも不十分である。

元CSVのhash・配置binaryのhash・Song保全先と操作終了はローカル検証記録に保存する。
ソースはB-1121上の未commit診断差分で、通常配置版・公開候補ではない。
初回LOOPの製品実装は依然未完了。次はclockの出所と適用寿命を分離し、共通content clockを認定できる条件を
明文化する。独立counterへVST3のK=0を一般化せず、AU/AAXの不足根拠を待機や再MATCHで覆わない。
製品への常時PCM解析、buffer・timer・workerの増設は行っていない。

### Windows AAX実機の再現（2026-10-01）

利用者の追加依頼により、Pro Tools Developer 2026.4.0で保存済みB-1123
（`7ad511a6556b4d4268a9155b55e9ee8192b87568`）の未署名Native PRE/POSTを診断した。
この節はB-1121上の未commit修正の受入ではなく、別候補の問題再現である。
48 kHz / stereo、Windows Audio Device / 256 samples、60秒の検証用pink noiseを使用した。
PRE/POSTのNative読込み、計測更新、手動ペアリング、通常再生中のMATCH 0.00 dBとPRE選択を確認した。

- LOOPを先に有効化し、10.240〜20.309秒を再生してからBLINDを開始すると、12秒後も
  `loopUnproven`相当の案内で進行しない。「音量を揃えています」とLOOP解除要求が同時に残った。
- 通常再生でMATCHした後、停止→LOOP有効化→再生すると、20.138秒の範囲を1周以上再生しても
  PRE WAITのまま。「保持」は残るが、PREへ復帰できたとは扱わない。
- このhostの実画面では再生中の「ループプレイバック」が無効だったため、停止を挟まない
  有効化は未検証。Studio Proの途中LOOP試験と混同しない。
- DAWの範囲終端による停止ではBLINDが中断し、停止理由・POST出力・終了後に再開する案内を表示した。
- 通常再生からの自動MATCH付きBLINDは両sourceの選択後に1回の開示で`1: PRE / 2: POST`へ進んだ。
  これはUIと再生状態の確認であり、録音した出力PCMの同一性・主観的聴取の合格とは区別する。
- END後、DAWが停止した状態では「再生すると通常音量に戻って終了します」と待機した。
  再生を2秒進めると通常画面へ戻った。停止状態だけで終了が完了したとは認定しない。

通常版Pro Toolsでの署名済みload、mono/multi-mono、Offline Bounceのbit透明性、保存復元、
遅延pluginを挟んだ周回別PCMの照合はこの実機確認では未認定。Developer hostの保存無効も区別する。
新しいCI/署名runを起動せず、既存成果物のhashを照合して再利用した。未署名診断の成功で署名・
installer・公開gateを解除しない。操作証跡と一時配置の復元はローカル検証記録を参照する。

### Windows AAXのTOD追加観測（2026-10-01）

別名の非出荷source／observer／4096-sample delayを、Pro Tools Developer 2026.4で検査した。
48 kHz / stereo、実callbackは1024 frames、10.752〜20.480秒のLOOPを初回から有効にし、
隣接とdelay後をそれぞれ停止再開込みで観測した。上のB-1123製品試験とは別binary／条件である。
SDKの`GetTODLocation`は独立した診断欄にだけ追加し、製品JUCEの承認済みpatch stackは変えていない。

- 隣接：2500 / 2500 blocksの先頭／末尾IDがsourceと対応し、native clock差は全て0。
  同じ音に付いたTOD差は0〜7 samplesに分散した。1024-frame進行に対してTODの増分は一定でなく、
  run内の連続する2498組すべてで1024と一致しなかった。
- 4096-sample delay後：1860 / 1868 blocksが対応し、各runの冒頭4 blocks、合計8は初期無音。
  対応した全blockのnative clock差は0だが、TOD差は809〜4827 samplesに分散した。
  未対応source、ID span不一致、同一block内のnative差不一致は0。native時計はLOOPで後退した。
- presentation latencyは欠損。TODをsample-exactなcontent時計やKに置き換える根拠は得られなかった。
  値を丸めたり到着時刻で最寄りの周回を選んだりせず、この候補は製品へ採用しない。

診断信号は停止し、Developer hostを終了した。三つの診断bundleはhash照合後、scan pathから
検証専用の退避directoryへ移動して復元可能な形で保持した。通常版AAXとdevice／routingは変更していない。
この観測は初回LOOPの製品受入、署名済み通常Pro Toolsの受入、全sampleの透明性認定ではない。
次の必要情報は「PREとPOSTの音に共通で、周回を折り返さず、適用寿命が分かるcontent時計」。
待機時間の増加、K=0の一般化、TOD近似、LOOP解除／再MATCH要求では不足を補わない。

## ローカル検証と残る境界

独立した周回別PCM／遅延モデルで500条件×100周を検査した。誤ったPRE出力は0。
内訳を混同しない：内容に対応したPOST位置の250条件はPRE取得率100%、有限clampの100条件は75%以上を
必要条件として通過、長遅延clampの150条件は安全退避だけを確認し継続可能とは認定しない。
17種の失効、初回LOOPの拒否、非LOOPの停止／欠損からの再取得も検査する。
LOOP単体のAudio Thread経路ではalloc/freeとも0。PCM容量は変更しない。
`sizeof(Ring)`は4,227,152→4,227,216 bytes（+64）、PCMは4,194,304 bytesのまま。
ASan／UBSanでも正常継続と失効・復帰を確認した。

局所rendererの3回比較（同じMac、Clang `-O3`、64/128/256/512 frames）では、64-frame BLINDの
中央値は旧0.590〜0.630 µs、新0.634〜0.663 µs、確認済みLOOPは0.706〜0.738 µs。
512-frameでは旧4.302〜6.745 µs、新4.590〜6.851 µs、LOOPは4.705〜6.884 µsだった。
同時ビルド等の負荷がありp99は変動した（512-frame旧23.132〜28.747 µs、新23.164〜29.727 µs、
LOOP 23.839〜34.069 µs）。追加処理をゼロ負荷とは言わず、full processor／実DAWの低buffer認定は別途必要。
再現用は`juce_shell/tests/live_compare/live_compare_recovery_benchmark.cpp`。試験内の時計読取りは製品へ入れない。

実processorの試験は0.5秒LOOPで再MATCHの実3秒窓と−6.0206 dBの測定、同じMATCHからBLINDの100周、
開示と終了を検査する。fixtureの8192-frame callbackはDAWの低buffer負荷の証拠にはしない。
通常回帰、全サイズ英日UI、局所負荷の結果は作業記録に分離する。

未完了なのは、初回LOOPでの一意なK取得、hostが全通知を整合させた見分けられないseek、
実host／formatごとの通知精度と低buffer負荷の認定。テンポ変更・範囲変更・情報欠損は継続せず、
既存のplugin遅延報告／DAW補償という前提を拡大しない。macOSとWindows、VST3／AU／AAXの実機結果を
同一視しない。これらの制限を承認ボタンで迂回する機能は追加していない。

推奨は、LOOP専用モードを増やさず、確定した音量差と現在の音声対応を分けて扱うこと。
正常と確認できた折り返しではMATCHを保持する。確認不能な音をPREとして出すことや、
毎周の自動MATCHで音量差を動かすことで継続を装わない。

## 利用者の動線

`LISTEN → MATCH → DAWでループ再生 → PRE／POST切替` を主動線とする。
すでにループしている場合も、対応を確認できる範囲ではそのままMATCHできることを受入目標に含める。
Hypha用のLOOPボタン、範囲入力、周回ごとの承認は追加しない。DAWの範囲や再生位置も操作しない。

- 同じループの折り返しで、確定済みのPRE gain・POST減衰・ceilingを変えない。
- 対応を一時確認できないときは、保持している音量のPOSTへ退避し、PRE選択とMATCH値を残す。
  確認できた最初のblockから既存の遷移でPREへ戻る。通常の折り返しに再MATCHを要求しない。
- 再MATCHは利用者の明示操作だけで行う。新しい測定と必要な承認が成功するまで前回値を維持し、
  無音・信号不足・キャンセル・古い測定結果の棄却では前回値を消さない。
- 明示的な終了は、従来どおりMATCHを解除して通常のPOST音量へ戻す。上昇量と実RT完了を示す。
  LOOP対応を理由に終了の意味を「MATCHへ戻る」へ変えない。
  終了の受理と実音の復帰完了は別に扱う。停止中も1回で比較画面を閉じ、通常画面に復帰待ちと
  残る上昇量を表示する。音声callback不在で実unityやRT receiptを作らず、新規比較は復帰完了まで待つ。
  mapping／rendererはPREのfadeとPOSTのrampに必要な間だけ保持し、非RT serviceがreceipt後に退役する。
- 「MATCH保持」は測った音量差を固定している意味であり、全周回の現在のラウドネスが常に等しいという意味ではない。

## 明示操作と自動継続の境界

利用者から、専用の明示操作を経てloopを許可する案と、追加操作なしで継続する案の正確性について確認があった。
設計上の推奨は後者を維持する。ただし自動化するのは、利用者がすでに開始した比較の、確認済みloopでの継続だけである。
DAWがloop中というだけで比較を開始せず、接続・editor open・state restoreでもPREやBLINDへ切り替えない。

専用の許可操作は対象や継続意図を明確にする利点があるが、音声の時刻対応・周回・PDCの正しさを保証しない。
利用者が範囲を選んでも、その範囲のどの周回をPOSTが処理しているかは別に確かめる必要がある。
したがって許可ボタンを追加しても内部の安全判定は同じだけ必要であり、未検証の継続を利用者承認で通す設計にはしない。

明示操作は、LISTEN／BLINDによる比較開始、PRE／POST選択、再MATCHの要求、必要な追加減衰の承認、終了と通常音量復帰に残す。
同じ比較内の正常な折り返しには、追加クリックも周回ごとの確認も要求しない。
確認が崩れたときは理由を表示して退避・中断し、「自己責任で続行」のような安全判定の迂回を用意しない。
必要なhost情報が足りない条件は、未対応・確認不能として扱う。明示モードを追加して対応済みに見せない。

## 正確性の合格条件

正確性は確認ボタンではなく、独立した期待値と実際の出力で検証する。
各周回で異なるPCMを使い、期待するPREのsample位置・周回ID・gainと出力を照合する。
正常条件だけでなく、同じ見かけの位置へ飛ぶseek、短loopより長い遅延、古いrun、欠損、PDC変更を含める。
安全判定を試験と共有して正解を作るのではなく、別の遅延モデルから期待値を生成する。

- 誤った周回・位置のPRE出力、未確認PREの遷移混入、未承認の音量上昇は試験内で0件を必須とする。
- 全部POSTへ退避させて誤出力0を達成しても合格にしない。認定正常fixtureでは、指定したSourceが
  実際に鳴ること、100周継続できること、意図しない再MATCH・試行中断がないことを確認する。
- 実hostではPRE出力時間と待機時間・回数・理由を別々に記録する。毎周の待機で比較が成立しない条件を
  「安全だから使える」と扱わず、対応条件と有限の復帰時間を示す。
- 単体fixture、実processor/editor、対象hostのexact candidateの三段階を通す。負荷・0 samples・通常Aのbit同一も別に確認する。

有限の試験で全環境の誤り0を証明したとは言わない。未知の条件では推測で進まない実装と、
検証済みhost／format／buffer／遅延条件の記録を併せて出荷判断する。

## 実装前に絡み合っていた責務（B-1106時点）

`PluginProcessorLiveCompareRealtime.cpp`はproject位置の不連続をすべて`block.afterGap`へまとめ、
`timelineGeneration`と`sessionGeneration`を進めて`matched=false`にする。
`clock.looping`は取得しているが、live比較の不連続判定には使っていない。
したがって折り返しでもMATCHの有効表示が消え、LIVE BLINDは中断する。
gainとPOST減衰の数値自体がその場でunityへ消えるわけではない。この違いを表示にも反映する。

同じ`afterGap`がConsumerのKとPOST測定履歴をリセットする。
`computeMatch`は既定で最低3秒、最大4秒の履歴を読むため、短いループでは履歴が貯まらない。
`matched=false`を止めるだけでは、短ループの初回MATCH、承認待ちの失効、BLINDの中断、
AUTOの停止、PINの範囲契約のどれも整理できない。

## 三つの責務を分ける

汎用状態管理基盤は作らず、既存のAuthority・Completion・commandを保った小さな分離にする。

| 責務 | 所有する事実 | 折り返し時の扱い |
| --- | --- | --- |
| 確定済みMATCH | PRE gain、承認POST減衰、ceiling、完全MATCHかTP LIMITか、pair／format／承認世代 | 正常折り返しでは変更しない |
| 現在の音声対応 | PRE run、連続時計、K、対象範囲の到着、欠落、設定されたloopの情報 | blockごとに判定。MATCH値だけでPREを許可しない |
| 測定窓と試行 | 同じ対応で読める履歴区間、未承認の測定結果、BLINDの割当と聴取receipt | 確認済みの折り返しだけ継続。未知の境界を結合しない |

MATCHには「なし」「固定値を使用可能」「固定値は保持しているが再確認が必要」を区別する小さな状態を持たせる。
さらにPRE WAITは現在の出力状態として別に表示する。「値がある」「比較条件が有効」「PREが鳴った」を
一つのboolへ戻さない。試聴sessionを跨ぐ永続保存や、別pairへの承認移植は追加しない。

既存generationの意味を整理し、session／承認の失効と、測定窓の区切りを別扱いにする。
ただし独立serialを多数追加するのではなく、Authority世代、gain revision、窓のproof世代を再利用する。
古いcallback・承認メニュー・worker結果は採用した世代を照合し、後のMATCHやENDを上書きできない。

## 境界ごとの動作

| 起きたこと | MATCHと出力 | LIVE BLIND |
| --- | --- | --- |
| 確認済みの同一loop折り返しで全frameが対応 | 固定値と選択を維持。Kも根拠が続く限り維持 | 同じ割当・固定gainで継続可能 |
| 同じloopだがPREの到着等を一時確認できない | 値と選択を保持しPOSTへ。理由と自動復帰を表示 | POST代替をSourceとして数えず、その試行を理由付きで中断 |
| 停止・手動seek・loop範囲変更・未知の位置飛び | 値は保持し、窓と未承認案を失効。有効MATCHと断定せず再確認を表示。記名PREの復帰は既存の対応確認規則に従う | 試行を中断。同じ割当を自動再開しない |
| PDC無効・content hold・callback空白 | MATCH保持と出力許可を分離。既存の停止／再生等の復帰手順を維持 | 既存の安全中断を維持 |
| pair／format変更・state restore | 旧MATCHを新条件の承認として使わない。保持POST減衰の安全な解除手順は維持 | 旧試行・開示・承認を復活させない |
| 明示END／RETURN | 比較とMATCHの使用を終了し、上昇量を示して実unity完了を待つ | 終了。MATCHへ暗黙には戻さない |

音声処理設定の変更をHyphaがすべて検出できるとは扱わない。
他社pluginのパラメータを監視したり、変更を推測して自動でMATCHし直したりしない。
既存の内容ずれ警告、ceiling guard、報告されたPDC条件を維持する。

## LOOPの認定と音声の対応

### 必要な情報

HostProcessClockへ、取得できるloop有効状態、loop points、PPQ、各値の有効性を固定サイズで渡す。
PREとPOSTの両側で観測し、未知の情報をfalseやゼロの既知情報に置き換えない。
各wrapperの取得結果の信頼性も区別する。現在のbool単独では、情報不在と明示OFFを区別できない。
標準JUCEで取得成否が分からないboolは、そのまま「既知OFF」へ昇格させず、loop認定の根拠なしとして扱う。
まず既存wrapperが渡す情報で成立させる。追加のnative provenanceが必要と分かった場合だけ、
対応するJUCE patch stackと時計probeの変更を影響範囲へ含め、推測で有効性を補わない。

LOOP ON、project位置の後退、連続時計が進んだという三点だけで正常折り返しとは認定しない。
LOOP ONのまま手動seekでき、AUのrender時計やAAXの自前frame時計はseekしても進むからである。
認定は、以下の証拠が揃ったhost profileと条件だけで行う。

- pair、format、PREの連続run、両側のcallback連続性、出力owner、PDC条件が維持されている。
- 同じloop設定に対する移動で、実測で確認した非整列境界・POST位置の通知パターンに収まる。
- 現在のKに対応するPRE範囲が同じ連続実行に存在し、旧周回の音へ取り替えていない。
- loop範囲変更、異常な時計差、既知の遅延変更、content holdなどの反証がない。

PPQとその瞬間のBPMだけからexactなsample境界を作らない。テンポ変化や丸めに加え、
hostが境界でcallbackを分割しない場合がある。loop情報は境界判定の補助であり、PCMの索引は連続時計とKのままにする。
境界を含む全blockを検証できないときは、未検証PREをcrossfadeに混ぜずPOSTへ退避する。

正常wrapとseekで全観測値が同じになる反例があるなら、その情報だけの判定器では区別できない。
fixtureでたまたま正常例が通ることを認定根拠にせず、追加の観測が得られない条件では継続認定しない。
この場合も固定MATCH値は失わせないが、常に待機なしでPRE／BLINDを継続できるとは約束しない。

### Kを毎周作り直さない

一意に較正済みのKと連続runが維持される正常折り返しでは、その根拠を引き継ぐ。
project時刻の後退だけでKを消して、同じproject位置の最新PREへ結び直さない。
既存ringの`run`は連続書込みの世代であり、DAWのloop周回番号ではない。この二つを混同しない。

既存G1記録では、Studio ProのPOST位置が折り返し付近で先頭に留まる間も、連続時計のKは一定だった。
このような区間は「較正候補として使えない範囲」をhost profileで限定し、偽の候補を作らない。
LOOP中という理由で候補不一致の規則M1を丸ごと無効化してはならない。
範囲を外れた不一致や期限超過は既存どおりKを失効させる。
遅延変更直後の検出限界は既存契約より都合よく拡大せず、境界付近の変更も試験する。
境界確認にはsample時計で進む有限の期限を設け、profileで確認した遅延・block条件から上限を決める。
期限不明のprofileは採用しない。実時計停止時の操作案内は既存UI周期で行い、新timerを足さない。

### 短いloopと長い遅延

loop長以上のchain遅延があると、同じproject位置に複数周回の候補が存在する。
現在の「最新の一致候補」を選ぶ方式だけでは、整数周回ずれたKを排除できない。
固定の識別PCMが各周で同じなら、この誤りはPCM一致試験でも見逃す。

初回Kの確定と、すでに確定したKの引継ぎを分ける。
loop中に初めて開始する場合は、当該hostの遅延・先行処理の境界と周回を識別できる根拠が必要。
単に探索範囲256件の中で候補が一つだったことを、全体で一意だった証拠にしない。
過去の非loop区間で得たKも、その後に停止・空白・失効があれば再利用しない。

一意性が確認できない場合は推測でPREへ進まず、確認不能な理由、現在のPOST出力、保持している設定、終了方法を示す。
範囲変更や再生操作で解消できるという認定根拠がないまま、「範囲を広げる」「LOOPを切る」「再MATCH」を要求しない。
これは全hostへの固定制限ではなく、必要な証拠がない場合のfallbackであり、無期限の準備中とも表示しない。
loopを一度解除する回避策だけで「loop中からMATCHできる」を合格扱いにしない。
具体的な認定述語とprofileの数値は、後述の成立性fixtureで確定する未解決の技術ゲートである。

## 短いloopでもMATCHの窓を貯める

正常と確認できた周回は、project座標を平坦化した偽の4秒ではなく、
PRE／POSTの実際の連続再生時間として既存の履歴へ蓄積する。
2秒loopで現行の最低3秒を測る場合も、コピーを水増しせず実際に鳴った3秒を使う。
固定回数の3周聴取やSourceごとの録り直しを求める設計にはしない。

測定可能な区間は、全frameが同じ対応根拠で読める連続区間に限定する。
履歴の終端だけでKが有効でも、窓の途中に未知の境界・K変更・PRE run変更・欠落があればその窓を使わない。
固定サイズのproof区間情報を既存履歴へ添え、copy前後の世代・run・上書きを照合する。
未知の区間を削って前後を結合したり、前周のPCMで穴埋めしたりしない。

この区分はMATCH、AUTO、中身のずれ推定が共有する。
正常loopだけで内容ずれの基準をリセットし、遅延変化の警告を隠すことはしない。
周期信号で相関が別周期へ移った可能性は、既存の判定不能規則とfixtureで確認する。

承認待ちの新MATCH案も、その窓とpair／format／出力承認の世代に結び付ける。
同じ正常loopが回っただけなら承認を破棄しない。seekや設定変更など窓の前提が変われば新案だけ棄却する。
新MATCHはPOSTを上げない。より浅いPOST減衰への変更が必要なら、現在のEND／RETURN契約に従う。

PIN／Exact 4 Sは別のnative範囲契約である。loopを跨いだMATCH窓を単一範囲artifactとしてPINへ渡さない。
project座標の連続区間を調べる既存ProjectViewは分離して維持する。

## LIVE BLINDとAUTOの扱い

主用途は記名PRE／POSTだが、同じMATCHからBLINDに入った途端に正常loopで止まる不整合も設計対象に含める。
LIVE BLINDは、認定済みloopかつ全blockの対応・固定gain・guardが成立した場合だけ継続する案とする。
匿名割当、Source選択、聴取receiptを周回ごとに作り直さず、開示で音やMATCHを変えない。
これは同一sample・同一処理結果の比較を保証する方式ではない。残響・LFO等は周回で変わり得る。

一度でも実際のPOST代替や未知の境界が必要なら、試行を理由付きで失効させる。
両Sourceを同じPOSTへ黙って置き換えたり、正常と見せたまま判定を再開したりしない。
今回の実装でINV-LC19を改定し、LOOP継続の条件をINV-LC22へ分離した。
主画面のSource 1／2、開示、終了は維持し、新しい回答収集を追加しない。

AUTOは既存の明示選択を尊重する。loopで勝手にONにせず、正常loopのみで勝手に解除もしない。
確認できた窓だけを既存周期で測り、待機中はgainを固定する。BLINDへは引き継がず停止する。
承認済みceiling、POST減衰、MATCHからの±6 dB境界は変更しない。

## 表示と音量安全

正常loopのたびにtoastや確認を出さない。固定MATCH表示を保ち、利用者はPRE／POSTを切り替えるだけにする。
待機や失敗は別作業の「最初の理由」と「現在必要な操作」の表示経路へ統合する。統合時には同じcommitで検証する。

表示文案は「PRE確認中・MATCH保持／POST出力」「再測定できませんでした・前回値を保持」など、
値が消えたのか、確認待ちなのか、何が鳴っているかが分かるものにする。
停止・再生が必要な場合と自動復帰を混同せず、未対応条件で無期限の準備中を表示しない。
英日・全サイズ・REF共存・accessibilityで同じ事実と操作を示す。

退避、折り返し、再MATCH失敗でPOSTをunityへ上げない。POST減衰は承認済み値を保持する。
PREの新gainは既存rampとceiling guardを通し、上がる量が承認の外なら勝手に適用しない。
非finite、offline、bypass、別owner、state restoreの境界をLOOPの特例で弱めない。

## 負荷を増やし過ぎない実装境界

- RTへ追加するのはblock単位の固定サイズ時計情報と、回数上限のある判定だけ。
  新しい音声走査・コピー・解析・delay・alloc/free・lock・I/O・常時logを追加しない。
- PCM ringの容量は増やさない。必要なdescriptor追加を設計しても、metadata増分はringあたり64 KiB以内を予算とする。
  これは実測値ではなく設計上限。足りない条件を無制限キューで吸収しない。
- PREからloop情報を渡すため共有layoutが変わる場合はversionを更新する。旧新版混在は明示拒否し、
  旧PREをloop対応済みとして扱わない。既存mappingの再stampやRT中の再確保はしない。
- 時計分類とMATCH承認はそれぞれ500行以下の小さなmoduleへ置く。イベントバスや巨大な状態機械は作らない。
- MATCH／AUTO／ずれ推定は既存の非RT経路と周期を使う。新workerやtimerは追加せず、状態不変時のUI更新省略を維持する。
  正常loopでAUTOが継続する分の実CPUも測る。「threadを増やさない」だけを低負荷の証拠にしない。
- 64／128／256／512 framesで、通常無試聴、記名両側、BLIND、境界失敗の前後CPU中央値・p99とRT確保回数を測る。
  前回の局所benchmarkだけでfull processorや実DAWの負荷を合格扱いにしない。

## 影響箇所と実装順

| 責務 | 主な対象 |
| --- | --- |
| host情報と境界分類 | `HostProcessClock.h`、`PluginProcessorHostClock.cpp`、`LiveCompareClock.h`、必要時の小さなTransportPolicy module |
| 周回とKの根拠 | `LiveCompareRing.h`、`LiveCompareCorrespondence.h`、`LiveCompareSharedRing.*`、PRE/POSTのRT接続 |
| MATCHと履歴 | `LiveCompareProcessorState.h`、`LiveCompareSession.h`、`LiveCompareWindows.h`、`LiveCompareMatch.*`、`PluginProcessorLiveCompare.cpp` |
| BLINDと復帰 | `PluginProcessorLiveBlind.cpp`、`LiveBlindSession.h`、`LiveCompareRecovery.h`、共通RecoveryPresentation |
| 画面とAUTO | `PluginEditorLiveCompare.cpp`、`PluginEditorLiveCompareAuto.cpp`、`PluginEditorLiveBlind.cpp`、LiveBlind／footer／英日catalog |
| 契約と試験 | live_compare unit、実processor/editor fixture、`LocalBlind.cmake`、RT source契約、README、invariants |

まず製品コードを変えず、周回別PCMの模擬hostで認定条件と反例を確定する。
次にMATCH承認と対応根拠を分離し、履歴・承認待ち・AUTO・BLIND・表示まで一つの変更範囲として実装する。
最後に対象hostのexact candidateで確認する。成立性が不足した場合は条件と理由を利用者へ返し、
勝手にWindowsやAAXを除外したり、LOOPを後期工程へ送ったりしない。
公開正本のINV-LC18〜21等は承認された仕様と実装試験が揃った時点で改定する。

## 受入試験と未解決の技術ゲート

1. 各周回で異なるIDのPCMと独立した遅延モデルを使い、Kの整数周回ずれを必ず検出する。
   0／4096 samples、loop長の直前・同値・超過、ring容量超過を分ける。固定波形の自己一致を成功条件にしない。
2. 0.5／1／2／4／8秒loop、非block整列、可変block、境界非分割、POST位置clampを含む。
   各100周でgain revision、POST目標、選択、BLIND割当が正常折り返しだけでは変わらない。
3. LOOP ON中の手動前後seek、境界と同じ行先へのseek、範囲変更、テンポ変化、情報欠損・stale情報、
   PREだけ／POSTだけのsleep、PDC変更、旧runの同一時計再利用を注入する。判別不能なら安全に退避する。
4. loop途中から初回MATCH、短loopの実3秒窓、無音・疎な打音、再MATCHの失敗・キャンセル・古い承認を検証する。
   対応不明区間を含む窓が採用されないこと、失敗時に前回値が保たれることを確認する。
5. MATCH済み→BLIND、直接BLIND、開示後の継続、折り返しとEND／再選択／restoreの競合を実processorで確認する。
   POST代替のreceipt誤計上、匿名性漏れ、急なPOST増幅、同じ無効trialの再開は0とする。
6. 通常Aのbit同一・0 samples、保持減衰、PIN/Exact 4 S、Record/FFI境界、全対象表示を回帰検証する。
   cargo test/clippy、native/source契約、行数ratchetを通し、FFI変更時はignored suiteも全件実行する。
7. Studio One／Studio Proで、実際のloopからの入口と100周のPRE／POST試聴を確認する。
   VST3／AU／AAX、macOS／Windowsは実施条件ごとに記録し、別format・過去commitの証跡を流用しない。

未解決の中心は「初回からloopしている場合のKの一意性」と「host通知だけではseekと区別できない境界」である。
これを検証前から万能な自動判定として約束しない。固定MATCHの保持という利用者契約と、
各hostでPREを出せる認定条件を分離することで、失敗しても設定を失わず、未確認の音も出さない設計にする。

## 根拠と確認範囲

- 現行ソースは基準`e02b4251`／B-1101上の未commit候補を読んだ。既存の製品コード差分は設計・成立性検証で変更していない。
- [JUCE PositionInfo公式仕様](https://docs.juce.com/master/classjuce_1_1AudioPlayHead_1_1PositionInfo.html)は、
  時刻情報がhost／formatで欠け得ること、loop pointsがoptionalであることを説明している。2026-09-30参照。
- 使用中の公式SDK header `juce_shell/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/pluginterfaces/vst/ivstprocesscontext.h`
  は、cycle境界でのblock分割が必須でないことと、連続時計・loop pointsがoptionalであることを記載している。
  公開Web版は取得できなかったため、このローカルの使用中SDKを直接確認した。
- 使用中のJUCE VST3／AU／AAX wrapperのloop情報取得を確認した。wrapperのコード存在は実hostの通知精度の証明ではない。
- [既存G1実測記録](hypha_live_chain_compare_g1_studio_pro_20260928.md)第2節・第8.6節を参照。
  限定条件の過去の実測であり、今回の候補、全buffer、長遅延、全platformへ一般化していない。
- 中断理由と復帰の別ローカル作業（未commit）とINV-LC1〜4／10／14／16／18〜21を照合した。
  本変更にその製品実装は含めない。成立性検証は次節に分け、製品のLOOP対応完了とは扱わない。

## 成立性検証で確認した制約

2026-09-30、製品のPublisher／Consumerだけを駆動する模擬hostを追加した。
processorの不連続判定・MATCH・gain・BLINDは通していない。実Studio One／Pro Toolsの再現録音でもない。
この照合器単体の結果を、製品で実際に誤音が出た証拠や、正常loopが使用可能な証拠と呼ばない。

`juce_shell/tests/live_compare/LiveCompareLoopOracle.h`は、入力PCMを独立したdelay lineへ通し、
Consumerが返したPCMと比較する。期待値をConsumerのKから逆算していない。
stereoの2値で再生通算frameを識別し、project位置が同じでも別周回を区別する。
両側のclock原点は明示したモデル仮定であり、各formatの全hostへ共通する保証ではない。

### 厳しめレビューで修正した試験の問題

- 旧seek試験は通常wrapの観測値をコピーして比較していた。独立したseek再現ではないため削除した。
  「同じ行先へのseekを区別できない」との結論は未検証へ戻す。今後はtransport操作と通知を独立に生成して確かめる。
- 旧判定モードは誤対応が存在することをassertし、将来の安全修正後に失敗する構造だった。
  誤対応件数は観測結果として扱い、正しい結果を成功にできる判定へ変更した。
- 遅延ごとにbufferが一つに固定されていた。長期surveyは遅延と全buffer scheduleを直積で検証する。
- 初回loopの反例だけで、MATCH済みからの継続まで停止必須としたのは根拠が不足していた。
  対応較正済みKからseekせずloopへ入る別fixtureを追加した。実MATCH操作の検証はまだ行っていない。
- コマンドの誤記が成功扱いになる点を修正。不明引数はexit 64。fixtureの長さ・delay・blockも範囲検査する。

oracle自体には正しいPCM、1 sampleずれ、1周ずれ、右chの最終sampleだけの誤り、NaNを与える。
正常時は一致し、すべての注入誤りを検出することを確認する。非loopの照合器positive controlは
2時計×2遅延×5bufferの20条件で、正しいPREが実際に読めることも確認する。

### 初回からループしている時の周回誤認

48 kHz、0.5秒loop、128 frames、独立counter型時計、content位置報告で、
遅延0と24,000 samplesの二つのhostを比較した。どちらもloopの途中から比較を開始する。
PRE／POST時計、loop範囲、PPQを含む1,000 callbacksの観測値は全て同じだったが、
正しく対応するPREのPCMは全て異なった。optionalなpresentation latencyは双方で欠損させている。
存在しない値を0 samplesの遅延として扱ってはならない。

| 条件 | accepted block | 誤対応block | 最初のずれ |
| --- | ---: | ---: | ---: |
| 遅延0の対照 | 992 | 0 | 0 samples |
| 遅延がloop長と同じ | 992 | 992 | 24,000 samples |

同じ1,000 blocksを毎周同じPCMにすると、遅延0と1周遅延の正解PCMが同じになり、周回差を検出できない。
この対照と周回別PCMの差は、Consumerの現在の誤対応をassertしなくても検証できる。
将来、追加の根拠を持つ照合器が対応拒否・正しく選択するようになっても、バグの存続をテストに要求しない。

### 既知Kからloopへ入る場合

非loop区間で正しいKを較正した後、現在位置から始まるloop範囲を有効にした。
有効化時のPRE／POST位置・時計が変わらず、1周後に初めて折り返すことをfixtureで確認した。
0.5秒loop、128 frames、content位置報告の4周では次の結果だった。

| 遅延 | VST3型Kの変化 | counter型Kの変化 | 各時計での誤対応 |
| --- | --- | --- | ---: |
| 4,096 samples | 0 → 0 | 374,784 → 374,784 | 0 |
| 24,001 samples | 0 → −24,000 | 394,689 → 370,689 | 554 blocks |

この条件では、正しく較正済みでも最新project一致候補がKを1周分作り直した。
初回Kの一意性とは別に、較正根拠の保持と候補更新の構造を扱う必要がある。
ただし「既知Kを常に固定すれば安全」とまでは示していない。seek・PDC変更・gapとの区別は別に必要。

### 遅延とbufferを分けた100周survey

loop長0.5／1／2／4／8秒、遅延0／4,096／loop長の1 sample前／同値／1 sample後、
VST3型・独立counter型時計、content位置・先頭clamp、64／128／256／512／可変64〜2,048 framesの
直積500ケースを各100周駆動した。実製品のBLIND／MATCH／AUTOを通す試験ではない。

照合器単体で誤対応を生じたのは100ケース、PRE取得frame比率95%未満は208ケースだった。
これら308ケースは探索基準を満たさず、残る192ケースでは誤対応0かつ取得95%以上だった。
95%は探索用の利用可能率であり、誤った音やBLIND代替を5%許可する契約ではない。
両項目は独立に集計し、将来の結果で重なりがあっても合算で誤カウントしない。

ring容量より長い524,289 samplesの遅延も5bufferで別途検証した。
64 framesは全拒否、残る4条件では誤った新しい周回をacceptedとした。
この容量超過条件の成功は全拒否であり、PRE取得率95%以上を要求しない。
128 framesの最初のずれは504,000 samplesで、584 accepted block全てが誤対応だった。
「ringから読めた」ことは、正しい周回を読めた証拠ではない。

### 再実行方法と結果の扱い

CMake targetは`KirinLiveCompareLoopFeasibilityTests`。
通常CTestは高速なoracle自己検証、metadata／PCMの対照、非loop positive control、既知Kの入口、判定の境界値を確認する。
通常の成功は試験基盤の確認であり、LOOP機能の合格ではない。

`KirinLiveCompareLoopFeasibilityTests --survey`で500ケース×100周と容量超過5ケースを実行する。
探索基準の未達または容量超過の受入れがあればexit 2、全て満たせばexit 0、不明引数はexit 64。
旧`--qualify`は廃止した。今回の未達は容量超過4条件を含む312条件で、exit 2だった。
このsurveyは製品・host・出荷の資格判定ではなく、照合器の改善箇所を調べるためのもの。

### 追加情報と次の検証範囲

AU v2の`HostCallback_GetTransportState`は汎用のtransport変更通知を返す。
現JUCE wrapperはその値を`playchanged`へ受け、Hyphaには転送していない。
Appleの使用中SDK headerで、この値が変更・不連続を表すことを確認した。
[Apple公式資料](https://developer.apple.com/documentation/audiotoolbox/hostcallback_gettransportstate)のWeb本文取得は失敗したため、
仕様の判断はローカル公式headerに基づく。seekだけを区別できるかはhost別に未検証。
VST3の[公式ProcessContext仕様](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html)
と使用中SDK headerも確認し、連続時計・loop pointsを全host必須とは扱わない。

今回の変更は試験と設計記録に限定し、RT・UI・共有ring layout・INV-LC19は変えていない。
製品へ新しい解析、buffer、timer、worker、RT確保は追加していない。
製品CPUの前後差を測ったわけではなく、LOOP実装の性能ゲートは未実施である。

次は、固定MATCHと照合根拠を分け、既知Kの引継ぎ・候補更新・履歴proofをローカルfixtureで検証する。
初回からloopする場合の一意性、seek・PDC変更との区別は独立した未解決条件として残す。
追加情報が必要なhost profileは既存の時計probeで実測する。全製品作業を実機配置待ちにするとの結論にはしない。
実DAWへの配置は別途指示を得てから。Windows／AAXを勝手に対象外へ変更せず、実証前に対応完了としない。

## 2026-10-02: 初回LOOPの製品候補

前節までの反例を残したまま、反復project位置とは別の証明を追加した。万能なhost推定ではなく、
証明を供給できるformat／hostだけをexact profileで認定する。証明が無い場合は従来どおりPOSTを出し、
「LOOPを切る」「再MATCHする」とは案内しない。

- Studio Pro 8.1.2 VST3は、周回ごとに異なる診断PCMを用いた隣接／4096-sample delayの実測で
  continuous clockがcontent位置を示したexact versionだけK=0を認定する。別versionへ一般化しない。
- AUはApple公式SDKのpresentation latency定義を使う。両側が有効でPRE側のoutput latencyが正に大きく、
  native sample折返しから測ったLOOP長の中で候補が一意な場合だけ認める。0は「無遅延または不明」なので、
  両側0や欠損からK=0を作らない。
- Pro Tools Developer 26.4.0.5 AAXはpatch 0011のDAE AddClockと、SDKに記載されたsample rate別の最大delay-compensation値を
  組み合わせる。実測LOOP長が上限より長く、modulo候補が上限内に一つだけある場合に限る。
  48 kHzでは16,383 samples以下のLOOPを短すぎるとして拒否する。AddClock単独では認定しない。
  JUCEの`File::getVersion()`が返す実値はMac／Windowsともマーケティング表記の`2026.4`ではなく
  `26.4.0.5`だったため、この完全一致だけを認定する。近接patch、製品版26.4.1.179、`2026.4`形式は
  未計測としてPOSTへ倒す。版名の表記違いで対応時計が無効になる回帰はsource contractで検出する。

PREはproof種別、clock出所、presentation latency／delay bound、native sampleで測ったLOOP長を固定サイズmetadataへ公開する。
POSTは両側のLOOP長とproofを照合し、既存の8回連続確認後に、利用者の最初のBLIND／LISTEN操作へ同じcallbackの証明を渡す。
proof変更、LOOP長不一致、停止、seek、pair／format／領域lifetime変更は世代を切り、成立済みの試行を初回入口から復活させない。
共有ringはversion 6へ上げ、旧版と混在させない。

Audio Threadへの追加はblock単位の整数比較・固定サイズatomicだけである。PCM ring容量、PCMコピー回数、worker、timer、
lock、allocation、I/O、相関処理を増やしていない。wall-clock callback間隔は補助時計が欠けるplugin-frame fallbackだけを切る。
VST3／AU／AAXのqualified host clockが連続している場合、test runnerの一時停止を音声欠落と誤認しない。

ローカル候補では、周回ごとに異なるPCMと独立delay lineに対し、VST3認定content clock、AU presentation latency、
AAX bounded engine clockの初回LOOPが全frame一致した。短いAAX LOOP、presentation proof変更、独立counterだけの初回LOOPは拒否した。
実Processor／editorでも、最初からLOOP、4096-sample delay、状態を持ち越すcompressor／dynamic band、直接BLIND、
Source 1／2、開示、ENDを4クリックで通し、通常Aはbit同一、出力latency 0を維持した。
これは実DAW／署名済み全format候補の受入証跡ではない。exact commitのMac VST3／AUとWindows AAXを別途実機確認する。

### 2026-10-02: ARM64の全frame PCM監査

B-1135のmacOS CIで残った2件のPCM不一致は、監査式`abs(actual - source * gain) <= 0`の
FMA contractionで再現した。保存済みfloatの積は丸められる一方、FMAは積を丸めずに差まで計算するため、
bit同一の実PCMに対しても非zeroの残差が出る。[Clang公式仕様](https://clang.llvm.org/docs/UsersManual.html)。
ARM64の生成assemblyで`fmsub`を確認し、Intel検証機でもfixtureだけに`-mfma`を付けると、
後からLOOP／初回LOOPの両製品試験が同じ全frame oracleで失敗した。

監査は独立delay lineの期待値をfloat PCMへ丸めてから、実PCMとbit単位で照合する共通helperへ移した。
許容誤差は設けず、1 ULP、異なるgain／音源、zeroの符号差、NaN／Infを拒否する対照試験を同じ
最適化済みtranslation unitで実行する。製品のrenderer、時計根拠、receipt、出力、安全条件は変更しない。
対照試験はvolatile入力から実行し、コンパイラの定数畳込みだけで合格させない。
同じFMA条件で修正後の2製品試験とsession試験は3/3 pass（35.70秒）。再現用の`-mfma`は通常設定に残さない。
この対照試験をCI／実DAWの現候補受入へ読み替えず、通常source gateとexact commit CI、実host検証へ進む。

### 2026-10-02: 追加負荷の対照診断（受入は保留）

B-1135製品処理と上記oracle修正だけのtreeを、B-1121と同一の測定器／Rust archive／x86_64 Releaseで
静かな直列実行により比較した。追加時間の事前上限は変えず、12条件中4条件がfailした。
64 PREのp99、128 Aの中央値／p99、128 PREのp99、256 Aの中央値が未達である。
以前の512-frameのfailも消さず、deadline内／heap操作0／別runのpassで相殺しない。

非出荷測定器へ、現在threadのQoS読取りと明示的な`--audio-qos`診断を追加した。
Apple公開pthread APIで試験threadだけをuser-interactiveへ設定し、設定後の実値を確認する。
既定のCTest、製品thread、DAW、device、global設定は変更しない。
64／128 framesの旧版／候補比較でも128の3経路で未達が残り、優先度だけを原因とは認定しなかった。

さらに実行fileのSHAが同じ候補同士を128 framesで比較すると、PRE LOOPのp99追加19.151 µsが
上限6.117 µsを超えた。製品変更がなくても同じ判定器でfailが出る対照であり、先の製品比較をPASSへ
変更する根拠ではない。時計公開／snapshot読取り／判定だけの別diagnosticは各20000 blocksを受理し、
中央値の合計は通常0.164 µs、LOOP 0.304 µsだったが、workerや実DAWを含む負荷gateの代わりにはしない。
測定の再現性と製品差の分離、実DAW低buffer／dropout検査、追加負荷の最終受入を残す。
機能のexact-commit CIと実host検証は進めるが、軽量性の認定・公開リリース完了は主張しない。

### 2026-10-02: 通常POSTの境界と、同期した実時間スレッド比較

通常POSTは、比較用の公開leaseなし、END待ちなし、匿名commandなし、実gain／目標gainがともに
正確に1の場合だけ、比較の出力処理を省略する。時計準備、周回観測、MATCH失効判定はその前に
継続する。fade／ramp lease、保持中の減衰、END、Blindは従来のfull pathを使い、同時に始まった
明示比較は次のcallbackから受ける。省略経路からPREやgainのreceiptは発行しない。
全boolean組合せ、1 ULP差、非finite gain、初回／後からのLOOPを対象試験で確認した。

同時刻の1200-block比較でも同一実行fileのp99がfailしたため、その測定で製品差を認定しなかった。
message threadのphase barrierで開始を揃え、実測windowの重なり90%以上を独立に要求する。
標本数は製品結果を読む前に6000へ固定し、全標本を保持する。既定CTestは1200のまま。
QoSだけの6000-block対照には締切超過があり、失敗証跡を保持して受入から外した。

[Apple公式のMach scheduling仕様](https://developer.apple.com/library/archive/documentation/Darwin/Conceptual/KernelProgramming/scheduler/scheduler.html)
に従い、診断threadだけを音声に近いtime-constraint schedulingへ設定し、各window前後に
実policyを読み戻す。降格はfailとし、製品thread、DAW、機器、task／global設定は変更しない。
64／128 framesの同一実行file対照6条件がpassした後、B-1121と変更候補を各6000 callbacksで一度比較した。
64／128／256／512 frames × 通常A／記名PRE LOOP／匿名PRE LOOPの12条件が、元の数値基準
（中央値max(1 µs, baseline10%)、p99 max(2 µs, baseline20%)）のままpassした。
追加中央値の最大は0.984 µs、追加p99の最大は3.472 µs、実測windowの重なりは99.6%以上。
両版の通常音声はbit同一、比較音声は丸めたfloat PCMと全frame bit照合し、RTのC++／System
確保・解放と締切超過はいずれも0だった。過去のfailを消さず、この結果の範囲をfull processorの
同条件fixture比較に限定する。実DAW低bufferや全formatの受入は引き続き別に必要である。

B-1136のARM64 CIでは初回／後からのLOOP PCM試験、REF runtimeとoffset製品試験がpassした。
残ったAAX mono group試験は、END直後のstatusを読む前に音声側が終了できる競合だった。
fixtureのcallback境界で停止を確認してからENDを押し、250 msのmessage tickだけでは音声完了を
捏造しないこと、再開後の実receiptとunity POSTのbit同一を検証する。待ち時間や合格基準は緩めない。
statusはmessage threadだけで読み、実receipt後にatomic flagを公開する。音声側はcallback前に
flagを読み、fade／receipt blockを除いた次の完全なblockからunityを照合する。
Blindの同種assertionは既にcallback停止を使い、他のlifecycle試験も実receiptを待つ構造である。

### 2026-10-02: Rust 1.99の互換wrapperとcold CI gate

B-1137のrun `36959880727`はARM64 native 56/56、ignored parity 20/20、pairing 6/6、
通常Rust／xtask試験を通過した。最後のlegacy wrapper clippyで、更新されたRust 1.99が
依存`vst3-com::vtable!`のexpression末尾のセミコロンを検出し、同時に45分のjob上限へ達した。
Rust 1.98ではpass、同じ1.99を明示したローカル検査では同じエラーを再現した。

依存のMIT snapshotを既存Cargo.lockのrevisionから保全し、macroの返すexpression末尾の
セミコロンだけを除く。警告のallow、toolchain downgrade、製品側のwarning基準変更はしない。
別crateから実macroをwarnings deniedで使い、直接trait callと同じ型／layout／3つのfunction
pointerを返す回帰試験を追加した。legacy PRE/POST clippyは1.99でもpassする。
出荷JUCE processor／Rust計測core／LOOP admission／PCM／gain／receiptには変更を加えない。

cold runnerの全gateを完走させるためjob上限だけ55分へ変更する。全56 native試験、26 ignored
試験、clippyを維持し、各製品試験のdeadlineや合格値は変えない。新commitの必須CIは別途必要で、
旧runの個別passを新候補のgreenへ読み替えない。WindowsのDeveloper実機検証も継続中であり、
unsigned AAXのloadや診断probeだけで通常Pro Tools／配布完了を主張しない。

### 2026-10-02: LOOP再取得とMATCH承認の構造的分離

Windowsの途中LOOP検証で、LOOPを有効にする前に範囲終端を越えていた場合は、正常な折返しではなく
project seekだった。PREは4100976→4096、POSTは4096880→0へ移動する一方、独立時計は進んでいた。
旧Consumerは成立後の初回取得入口を閉じ、seekでKを失効した後も、反復位置だけでは較正し直せない。
画面でPREを選び直すだけではその入口が開かず、別の準備観測に新proofがあってもPRE WAITに留まった。
較正条件を緩めるのではなく、取得入口と利用者の出力権限を分離する。

1. 通常stop、連続時計に裏付けられたproject移動、hostが通知した補償有効化を型付きの既知変更とする。
   同じpair／rate／PRE owner／出力authorityの場合だけ記名選択と固定gainを保つ。
   旧Kと測定履歴を破棄し、現在世代・owner・当該callback・新PCMに束縛された別の取得入口で確かめ直す。
2. 時計欠損、説明不能なcallback gap、owner変更、不正LOOP metadataを既知seekへ昇格させない。
   旧選択を封鎖してPOSTを出し、最初の原因と実在するPRE再選択の動線を示す。
   現在の一般的なLOOP待機案内で、保持した中断理由や次の操作を覆わない。
3. 一時的なmetadata／gain writerの競合は出力の準備不足であって、既知変更の復帰権限の消失ではない。
   競合中はPOSTを出す。coherentな原因と承認tupleを確認した後だけ復帰の権限を消費する。
   PCM runだけが変わった場合に、前の時計世代のstop原因を流用しない。
4. PAIR選択／解除はmessage timerを待たず、操作時に旧RT authorityを失効する。
   停止中にpairを変更して直ちに再生しても、古いmappingとMATCHからPREやBlindを復活させない。
5. 失効済みBlind、END、state restoreは記名自動再取得を許可しない。利用者の新しい明示BLINDは
   旧trialの復活ではなく、新しい取得要求・履歴・authorityと非RTのCSPRNG割当を使う別試行とする。
   準備が成立しないまま受理済みAPIが無期限待機になる状態を作らない。

再取得は音量の再測定ではない。正常な停止／移動後にLOOP解除や再MATCHを要求しない。
無音meterのInactiveはheartbeat停止やbypassでも起こるため、無音sleepの証明として使わない。
認定済み独立時計が保った単なる遅いcallbackと、実際の説明不能な空白を区別する。
遅延補償のON通知と未知gapが同時にあれば、既知通知で未知失効を上書きしない。

MATCHはPRE gain、POST target、ceiling、limited、保持状態、承認identityを一つのeven revisionで公開する。
全readerが同じtupleを取り、MATCH／AUTO／RT ENDの全writerが一回だけのCAS更新leaseを共有する。
途中の旧POST／新PRE混在や、旧音量の適用receiptで新MATCH／Blindを許可することを防ぐ。
現在の時刻根拠と同じrevisionの実適用receiptを確認してから完全MATCHと扱う。
Audio Threadへ待機、spin、allocation、lock、I/Oは加えず、既存ring容量とworker数を保つ。

比較lease、END、Blindがなく、実POST gainが正確に1の通常経路だけは、even revision→POST target→
acquire fence→同じrevisionでunityを確認して出力を触らず返す。このsubsetは承認tupleや適用receiptでは
なく、PRE／ceiling／承認identityを読まない。条件が成立しなければ全tupleを新たに取得し、subsetの
部分値を比較経路へ流用しない。時計準備とMATCH失効判定は省略の前に維持する。
odd writer、非unity／非finite、終了lease、保持中の減衰は従来のfull pathを通る。新しいRT状態や
workerを追加せず、決定的writer差込みとloadmaskの対照でこの通常経路の読取り境界を固定する。
これによる最新製品の追加時間の合格は、未計測のまま主張しない。

新pure契約と実processorのstop／seek／unknown／DC／odd publication／pair変更／新旧Blind対照を、
macOS release-sourceのbuild targets・選択regex・件数検査とWindows preflightへ明示登録する。
冷たいCIで新CTestが未build／未選択のまま合格することを許さない。
既存400周全frame oracle、正常Aのbit同一、RT heap操作0に加え、最終sourceのfull processor追加時間を
元の中央値／p99上限で確認する。これらの最終gateと新候補実DAW検証は、この追記時点では未完了である。

B-1142実機の認定済み範囲は別証跡として保持する。Windows VST3初回LOOPは115周／33284192 verified
frames、Developer AAX初回LOOPは157周／42791936 verified framesを独立固有PCMで確認した。
Startup／crossfade内の非grid blocksを除外記録し、全遷移bit同一や新候補の合格へ読み替えない。
AAX DeveloperのSaveが無効なのは、[Avidのdebuggable build仕様](https://learn-cdn.avid.com/AAX_SDK_2p1p1/Documentation/Doxygen/output/html/a00274.html)
に記載されたsession保存／export制限と整合する。署名済み通常Pro Toolsのsave／reopenは別受入であり、
Developerの診断成功やこの制限から合格を推定しない。

記名操作の表示も同じ因果境界へ揃える。旧中断通知のacknowledgement、操作、操作後の新故障と
現在適格性の確認を一つのmessage-thread境界で行い、新しい中断を成功通知で隠さない。
古い未観測ceilingから合法再MATCHする対照も保ち、旧履歴だけで拒否しない。非同期MATCH／AUTO
メニューは開いた比較世代を操作前と測定結果へ引き継ぎ、終了後の新しい比較へ作用させない。
同じ現行比較のAUTO適用確認待ちは未開始理由を通知する。古いメニューや新故障をその通知で
覆わず、後からAUTOへ自動移行するstate／timerは追加しない。共有UI境界の決定的純粋試験と、
実AAX拒否表示の複数tick確認、全preset・英日の新通知の幅／描画を最終gateへ含める。
