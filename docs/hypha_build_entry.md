# Hyphaの統合ビルド入口

## 検証用プラグインをすぐ作る

toolchainを用意した作業checkoutで、日常のGUI / DSP確認用なら次の1コマンド。
MacはPRE/POST × AU/VST3 Universalの4本、WindowsはPRE/POST × VST3 x64の2本を作る。
AAX SDK、署名認証、iLok USB / Cloud、公証、CI、公開操作は不要。
公開CIと未署名診断buildはRust stableを使う。正式配布producerは、同梱するruntime通知に
対応する固定版を製造前とfreeze時に照合する。診断buildの成功を正式配布のruntime証明としない。

```bash
node scripts/build_hypha.mjs --without-aax
```

これはAAXを**明示的に外すローカル検証専用**の選択肢。通常の全format buildは既定のまま維持する。
AAXも含めた検証は、後述の`--sdk PATH --license-confirmed`の1コマンドで6本／4本を作る。
全format版と出力/cacheを分離するため、既存のAAX成果物や正規署名物を上書きしない。
再確認だけなら`node scripts/build_hypha.mjs --without-aax --verify-only`で、再buildしない。
配置・DAW起動はこのscriptで自動実行しない。**通常のPro Tools用AAX検証だけはPACE署名が必要**であり、
未署名AAXを使える診断hostと区別する。詳細は[署名入口](aax_build_signing_entry.md)。

## 公開までの入口との違い

**ビルドからHPアップまで**は同じscriptの`--release` modeを使う。
[全工程の入口](hypha_release_entry.md)で、署名・公証、3チャネル配布物、受入gate、英日HP反映と
公開物再取得までを一連の作業として扱う。本書のbuild-only modeは診断用に残す。

MacとWindowsで使う入口は **`node scripts/build_hypha.mjs`**。
共通Rust coreとJUCE shellを同じCMake graphでビルドする。formatごとにDSPを複製しない。
READMEとAGENTS.mdから本入口へ案内する。Claude用CLAUDE.mdはAGENTS.mdをimportし、指示の別コピーを作らない。
import方式の根拠は[Claude Code公式の共有手順](https://code.claude.com/docs/en/memory#share-one-file-with-other-coding-tools)。

|実行OS|作るformat|CPU|本数|
|---|---|---|---|
|macOS|AAX / AU / VST3|全bundleがx86_64 + arm64 Universal|PRE / POSTで6本|
|Windows|AAX / VST3|x64|PRE / POSTで4本|

AUはApple platform用。Windowsのx64をUniversal Binaryと呼ばない。
現在のAvid Windows要件は64-bit Intelで、Hyphaの既存AAX SDK / 検証経路もx64。
Windows ARM64 / Arm64Xの生成・host対応は本入口の合格範囲に含めない。

## 事前条件

Node、Git、CMake、Rustと、pinned JUCE submoduleの初期化が必要。
MacではXcode toolchainとRustの`x86_64-apple-darwin` / `aarch64-apple-darwin` targetを用意する。
WindowsではVisual Studio Build Tools 2022のC++ / Windows SDK、Git Bash、Rust MSVC x64を用意する。
SDKの取得、ライセンスへの同意、toolchainのinstallを本scriptが無断で行うことはない。
既存の承認済みJUCE patchだけは、正規scriptで適用・照合してからbuildする。

## 1コマンドで作る

次のコマンド例はrepository rootから実行する。
scriptを絶対pathで呼び出した場合は、現在のdirectoryにかかわらずscript自身が属するcheckoutを使う。
SDKはrepository外へ置く。ライセンス確認は毎回明示する。

```bash
# Mac: 6本。Windowsでも同じCLIで、そのOS用4本を作る。
node scripts/build_hypha.mjs --sdk /absolute/external/aax-sdk-root --license-confirmed
```

Windowsの例（PowerShell、Visual Studio Build Tools 2022 / Git Bash / Rust MSVC / CMake / Nodeが必要）:

```powershell
node scripts/build_hypha.mjs --sdk C:\absolute\external\aax-sdk-root --license-confirmed
```

SDKの非秘密pathを`KIRIN_AAX_SDK_PATH`へ設定済みなら`--sdk`を省ける。
pathをrepositoryにhard-codeしたり、PACE / Apple / eSigner認証値を本入口へ保存したりしない。

```bash
# コマンド構成だけを確認。build / CI / signing / 機器操作は実行しない。
node scripts/build_hypha.mjs --sdk /absolute/external/aax-sdk-root --license-confirmed --dry-run

# 保存した結果のhash・CPU・version・sourceを再確認。再ビルドしない。
node scripts/build_hypha.mjs --verify-only
```

別候補を分けるときは`--build-id name`、並列数は`--jobs N`。
更新通知の鍵は`--update-public-key KEY`で明示する。既定は空で、以前のCMake cacheに鍵があっても
毎configureで空へ戻す。承認済みRSA-2048/65537の正規公開鍵だけを受け付け、全role/formatの
実binary内digestとMac plistを照合し、`hypha-build.json`へ記録する。鍵の違うmanifestは再利用しない。
`--platform windows --dry-run`でMacからWindows用計画だけを確認できる。
実ビルドは各OSで実行し、MacからWindows binaryを生成したとは報告しない。

## 出力と安全性

全formatの出力は`target/hypha-build/macos-universal/`または`target/hypha-build/windows-x64/`。
`--without-aax`の出力はそれぞれ`macos-universal-no-aax/` / `windows-x64-no-aax/`へ分離する。
各roleの`KirinHypha{PRE,POST}_artefacts/Release/<format>/`へbundleを生成する。
選択した全bundle（既定6本／4本、`--without-aax`では4本／2本）の存在、サイズ、実binaryのCPU、
versionを確認した後だけ`hypha-build.json`を出す。選択modeの異なるmanifestを再利用しない。
manifestにはsource commit / B / modified state、JUCEのcommitと実bytesを含む変更fingerprint、各binaryのhashを残す。
ビルド中にsourceが変わった場合は成功扱いにしない。

- Rust FFIはMacで各CPU一度、Windowsでx64一度。CMake configure / buildは各OS一組。
- 通常はincremental build。無条件のclean-first、重複CI、署名のやり直しは行わない。
- 同じoutputの並行操作はlockで拒否する。別checkoutや別build-idは別output。
- 既存の署名済みoutputは上書きしない。旧正規build directory、installed plug-in、公開pkgを触らない。
- 完了manifestがあるだけでは足りない。`--verify-only`はhashと現在のsourceを再照合する。

## ビルドと製品化を分ける

この入口は**未署名のローカル診断ビルド**。iLok USBやCloudの移動は不要。
AAXは未署名を明示的に許す診断hostだけで使える。通常のPro Toolsでのloadは保証しない。
JUCEのMac ad-hoc signatureが付く場合も、Developer ID / PACE署名済みとは扱わない。

署名・公証・installer / pkg / zip・実host受入・公開は`--release` mode内の独立gate。
この出力を署名／配布factoryへ任意に差し込まない。承認済みのclean-source経路を使う。
正本は[署名入口](aax_build_signing_entry.md)と[3チャネルRunbook](ls_release/kirin_hypha_ls_runbook.md)。
既存の`build_juce_universal.sh`、`build_aax_universal.sh`、`build_aax_windows.ps1`の
署名・配布契約を維持する。producer変更後はprivate factoryのscript hash allowlistを再レビューし、
承認済みのexact sourceへ揃えてから正式署名する。

## 根拠と自動試験

- [Apple Audio Unit](https://developer.apple.com/documentation/audiotoolbox/incorporating-audio-effects-and-instruments)
- [Avid Pro Tools要件](https://kb.avid.com/pkb/articles/en_US/Knowledge/Pro-Tools-System-Requirements)
- [Microsoft Arm64Xの別ABIと対応条件](https://learn.microsoft.com/en-us/windows/arm/arm64x-pe)
- [CMake CLI](https://cmake.org/cmake/help/latest/manual/cmake.1.html)

SDK・認証・機器に依存しない自動試験:

```bash
node --test scripts/build_hypha.test.mjs
```

この試験は軽量source gateにも含める。fixture成功と実ビルド、署名、実host受入は別証跡とする。
