param(
  [Parameter(Mandatory = $true)]
  [string]$Installer,

  [Parameter(Mandatory = $true)]
  [string]$PayloadDir,

  [Parameter(Mandatory = $true)]
  [ValidateSet("unsigned", "signed")]
  [string]$Signing,

  [Parameter(Mandatory = $true)]
  [string]$Manifest,

  [string]$AaxWraptool = "",

  # Directory produced by download_previous_installers.ps1; required for signed candidates.
  [string]$PreviousInstallerDir = "",

  [string]$UpgradeFixture = ""
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot/installer_payload.ps1"
. "$PSScriptRoot/installer_environment.ps1"
. "$PSScriptRoot/installer_upgrade.ps1"
. "$PSScriptRoot/installer_qualification.ps1"

$installerPath = Resolve-RequiredFile $Installer "installer"
$payloadPath = Resolve-RequiredDirectory $PayloadDir "installer payload"
$manifestPath = Resolve-RequiredFile $Manifest "installer manifest"
$manifestData = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$withAax = [bool]$manifestData.distribution.aax_included
$version = [string]$manifestData.product.version
if ([string]::IsNullOrWhiteSpace($UpgradeFixture)) {
  $UpgradeFixture = Join-Path $PSScriptRoot "fixtures/previous-public-installers.json"
}
$previousDir = $null
if (![string]::IsNullOrWhiteSpace($PreviousInstallerDir)) {
  $previousDir = Resolve-RequiredDirectory $PreviousInstallerDir "previous public installers"
} elseif ($Signing -eq "signed") {
  throw "Signed release verification requires -PreviousInstallerDir with the pinned previous public installers"
}
$vst3Root = if ($withAax) {
  Join-Path $env:CommonProgramFiles "VST3"
} else {
  Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"
}
$uninstallRoot = if ($withAax) {
  Join-Path $env:ProgramFiles "Kirin Mastering\Kirin Hypha"
} else {
  Join-Path $env:LOCALAPPDATA "Programs\Kirin Mastering\Kirin Hypha"
}
$cleanScope = if ($withAax) { "all-users" } else { "current-user" }
function Verify-InstalledLegalDelivery {
  if ($null -eq $manifestData.legalDelivery) {
    if ($Signing -eq 'signed') { throw 'Signed installer legal delivery evidence is missing' }
    return
  }
  $legalRoot = Join-Path $uninstallRoot 'Legal'
  $actual = Get-Content (Join-Path $legalRoot 'legal-delivery.json') -Raw | ConvertFrom-Json
  if ($actual.commit -ne $manifestData.source.commit -or
      $actual.sourceSha256 -ne $manifestData.legalDelivery.sourceSha256) {
    throw 'Installed Corresponding Source identity differs from installer'
  }
  foreach ($entry in $manifestData.legalDelivery.files) {
    $file = Join-Path $legalRoot $entry.path
    if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Sha256 $file) -ne $entry.sha256) {
      throw "Installed notice/license/source pointer differs: $($entry.path)"
    }
  }
}
$aaxRoot = Join-Path $env:CommonProgramFiles "Avid\Audio\Plug-Ins"
$preBundle = Join-Path $vst3Root "Kirin Hypha PRE.vst3"
$postBundle = Join-Path $vst3Root "Kirin Hypha POST.vst3"
$preAaxBundle = Join-Path $aaxRoot "Kirin Hypha PRE.aaxplugin"
$postAaxBundle = Join-Path $aaxRoot "Kirin Hypha POST.aaxplugin"
$resolvedWraptool = if ($withAax) { Resolve-AaxWraptool $AaxWraptool } else { "" }
$payloadHashes = Get-HyphaPayloadHashes $payloadPath $withAax

Assert-HyphaInstallerEnvironment | Out-Null

# Phase 1: fresh install on a clean machine, same-version reinstall and uninstall.
New-Item -ItemType Directory -Force -Path $vst3Root | Out-Null
$sentinel = Join-Path $vst3Root ("kirin-hypha-preserve-" + [Guid]::NewGuid().ToString("N") + ".txt")
Set-Content -LiteralPath $sentinel -Value "unrelated VST3 sentinel" -Encoding utf8
$aaxSentinel = $null
if ($withAax) {
  New-Item -ItemType Directory -Force -Path $aaxRoot | Out-Null
  $aaxSentinel = Join-Path $aaxRoot ("kirin-hypha-preserve-" + [Guid]::NewGuid().ToString("N") + ".txt")
  Set-Content -LiteralPath $aaxSentinel -Value "unrelated AAX sentinel" -Encoding utf8
}
$records = @()
$uninstallerPath = $null

try {
  $records += Get-SignatureRecord "installer" $installerPath $Signing
  $installScope = if ($withAax) { "/ALLUSERS" } else { "/CURRENTUSER" }
  foreach ($installPass in 1..2) {
    $process = Start-Process -FilePath $installerPath -ArgumentList @(
      "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", $installScope
    ) -Wait -PassThru
    if ($process.ExitCode -ne 0) {
      throw "Installer pass $installPass failed with exit code $($process.ExitCode)"
    }
    Wait-PathState $preBundle $true "installed PRE bundle"
    Wait-PathState $postBundle $true "installed POST bundle"
    $passRecords = @(Assert-Payload $vst3Root $payloadPath $Signing)
    if ($withAax) {
      Wait-PathState $preAaxBundle $true "installed PRE AAX bundle"
      Wait-PathState $postAaxBundle $true "installed POST AAX bundle"
      $passRecords += @(Assert-AaxPayload $aaxRoot $payloadPath $resolvedWraptool)
    }
    Assert-HyphaResolvedPayload $payloadHashes $cleanScope "Fresh install pass $installPass" | Out-Null
    Assert-HyphaRegistration $cleanScope $version "Fresh install pass $installPass" | Out-Null
    $uninstallerPath = Resolve-HyphaUninstaller $uninstallRoot
    Verify-InstalledLegalDelivery
    if ($installPass -eq 2) { $records += $passRecords }
  }

  $records += Get-SignatureRecord "installed uninstaller" $uninstallerPath $Signing

  $uninstall = Start-Process -FilePath $uninstallerPath -ArgumentList @(
    "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"
  ) -Wait -PassThru
  if ($uninstall.ExitCode -ne 0) {
    throw "Uninstaller failed with exit code $($uninstall.ExitCode)"
  }
  Wait-PathState $preBundle $false "PRE bundle removal"
  Wait-PathState $postBundle $false "POST bundle removal"
  if ($withAax) {
    Wait-PathState $preAaxBundle $false "PRE AAX bundle removal"
    Wait-PathState $postAaxBundle $false "POST AAX bundle removal"
  }
  if (!(Test-Path -LiteralPath $sentinel -PathType Leaf)) {
    throw "Uninstaller removed an unrelated VST3 file"
  }
  if ($withAax -and !(Test-Path -LiteralPath $aaxSentinel -PathType Leaf)) {
    throw "Uninstaller removed an unrelated AAX file"
  }
  if (@(Find-HyphaUninstallEntries).Count -ne 0) {
    throw "Uninstaller left a Kirin Hypha registry entry"
  }
} finally {
  if ($null -ne $uninstallerPath -and (Test-Path -LiteralPath $uninstallerPath -PathType Leaf)) {
    Start-Process -FilePath $uninstallerPath -ArgumentList @(
      "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"
    ) -Wait -ErrorAction SilentlyContinue | Out-Null
  }
  $ownedPaths = @($preBundle, $postBundle, $uninstallRoot)
  if ($withAax) { $ownedPaths += @($preAaxBundle, $postAaxBundle) }
  foreach ($ownedPath in $ownedPaths) {
    if (Test-Path -LiteralPath $ownedPath) {
      Remove-Item -LiteralPath $ownedPath -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
  Remove-Item -LiteralPath $sentinel -Force -ErrorAction SilentlyContinue
  if ($null -ne $aaxSentinel) {
    Remove-Item -LiteralPath $aaxSentinel -Force -ErrorAction SilentlyContinue
  }
}

# Phase 2: updates from the previous public releases as users installed them, kept separate
# from the fresh-install phase and starting again from a verified clean machine.
$upgradeMatrix = $null
if ($null -ne $previousDir) {
  $upgradeMatrix = Invoke-HyphaUpgradeMatrix $UpgradeFixture $previousDir $installerPath $payloadPath $version $withAax $Signing
}

$expectedInstallerHash = [string]$manifestData.installer.sha256
if ((Get-Sha256 $installerPath) -ne $expectedInstallerHash) {
  throw "Installer no longer matches its build manifest"
}
Write-HyphaInstallerQualification $manifestData $manifestPath $Signing $records $withAax $upgradeMatrix
Write-Host "[hypha-installer] fresh install, repeat install, uninstall and upgrade verification passed"
