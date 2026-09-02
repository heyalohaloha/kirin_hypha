param(
  [Parameter(Mandatory = $true)]
  [string]$Installer,

  [Parameter(Mandatory = $true)]
  [string]$PayloadDir,

  [Parameter(Mandatory = $true)]
  [ValidateSet("unsigned", "signed")]
  [string]$Signing,

  [Parameter(Mandatory = $true)]
  [string]$Manifest
)

$ErrorActionPreference = "Stop"

function Resolve-RequiredFile([string]$Value, [string]$Label) {
  $resolved = Resolve-Path -LiteralPath $Value -ErrorAction SilentlyContinue
  if ($null -eq $resolved -or !(Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
    throw "Missing ${Label}: $Value"
  }
  return $resolved.Path
}

function Resolve-RequiredDirectory([string]$Value, [string]$Label) {
  $resolved = Resolve-Path -LiteralPath $Value -ErrorAction SilentlyContinue
  if ($null -eq $resolved -or !(Test-Path -LiteralPath $resolved.Path -PathType Container)) {
    throw "Missing ${Label}: $Value"
  }
  return $resolved.Path
}

function Get-Sha256([string]$FilePath) {
  return (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash.ToLower()
}

function Get-SignatureRecord([string]$Role, [string]$FilePath, [string]$Expected) {
  $signature = Get-AuthenticodeSignature -FilePath $FilePath
  $actual = [string]$signature.Status
  if ($Expected -eq "signed" -and $actual -ne "Valid") {
    throw "Invalid Authenticode signature for ${Role}: $actual ($FilePath)"
  }
  if ($Expected -eq "unsigned" -and $actual -ne "NotSigned") {
    throw "Unsigned CI candidate unexpectedly has signature status $actual for ${Role}: $FilePath"
  }
  $record = [ordered]@{
    role = $Role
    file_name = [System.IO.Path]::GetFileName($FilePath)
    status = $actual
    sha256 = Get-Sha256 $FilePath
  }
  if ($null -ne $signature.SignerCertificate) {
    $record.signer_subject = $signature.SignerCertificate.Subject
    $record.signer_thumbprint = $signature.SignerCertificate.Thumbprint
  }
  if ($null -ne $signature.TimeStamperCertificate) {
    $record.timestamp_subject = $signature.TimeStamperCertificate.Subject
    $record.timestamp_thumbprint = $signature.TimeStamperCertificate.Thumbprint
  }
  return $record
}

function Find-HyphaUninstallEntries {
  $roots = @(
    "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall",
    "Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Uninstall",
    "Registry::HKEY_LOCAL_MACHINE\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"
  )
  $matches = @()
  foreach ($root in $roots) {
    if (!(Test-Path -LiteralPath $root)) { continue }
    $matches += @(Get-ItemProperty -Path "$root\*" -ErrorAction SilentlyContinue | Where-Object {
      $_.DisplayName -eq "Kirin Hypha"
    })
  }
  return $matches
}

function Wait-PathState([string]$PathValue, [bool]$Present, [string]$Label) {
  for ($attempt = 0; $attempt -lt 60; $attempt += 1) {
    if ((Test-Path -LiteralPath $PathValue) -eq $Present) { return }
    Start-Sleep -Milliseconds 500
  }
  throw "Timed out waiting for ${Label}: $PathValue"
}

function Assert-Payload([string]$InstalledRoot, [string]$SourceRoot) {
  $records = @()
  foreach ($role in @("PRE", "POST")) {
    $bundleName = "Kirin Hypha ${role}.vst3"
    $relativeBinary = "Contents\x86_64-win\$bundleName"
    $sourceBinary = Resolve-RequiredFile (Join-Path (Join-Path $SourceRoot $bundleName) $relativeBinary) "source $role binary"
    $installedBinary = Resolve-RequiredFile (Join-Path (Join-Path $InstalledRoot $bundleName) $relativeBinary) "installed $role binary"
    $sourceHash = Get-Sha256 $sourceBinary
    $installedHash = Get-Sha256 $installedBinary
    if ($sourceHash -ne $installedHash) {
      throw "$role installed payload hash mismatch: $installedHash != $sourceHash"
    }
    $records += Get-SignatureRecord "installed $role VST3 binary" $installedBinary $Signing
  }
  return $records
}

$installerPath = Resolve-RequiredFile $Installer "installer"
$payloadPath = Resolve-RequiredDirectory $PayloadDir "installer payload"
$manifestPath = Resolve-RequiredFile $Manifest "installer manifest"
$userVst3 = Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"
$uninstallRoot = Join-Path $env:LOCALAPPDATA "Programs\Kirin Mastering\Kirin Hypha"
$preBundle = Join-Path $userVst3 "Kirin Hypha PRE.vst3"
$postBundle = Join-Path $userVst3 "Kirin Hypha POST.vst3"

if ((Test-Path -LiteralPath $preBundle) -or (Test-Path -LiteralPath $postBundle) -or
    (Test-Path -LiteralPath $uninstallRoot) -or @(Find-HyphaUninstallEntries).Count -ne 0) {
  throw "Refusing installer verification because Kirin Hypha is already installed for this runner"
}

New-Item -ItemType Directory -Force -Path $userVst3 | Out-Null
$sentinel = Join-Path $userVst3 ("kirin-hypha-preserve-" + [Guid]::NewGuid().ToString("N") + ".txt")
Set-Content -LiteralPath $sentinel -Value "unrelated VST3 sentinel" -Encoding utf8
$records = @()
$uninstallerPath = $null

try {
  $records += Get-SignatureRecord "installer" $installerPath $Signing
  foreach ($installPass in 1..2) {
    $process = Start-Process -FilePath $installerPath -ArgumentList @(
      "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER"
    ) -Wait -PassThru
    if ($process.ExitCode -ne 0) {
      throw "Installer pass $installPass failed with exit code $($process.ExitCode)"
    }
    Wait-PathState $preBundle $true "installed PRE bundle"
    Wait-PathState $postBundle $true "installed POST bundle"
    $passRecords = @(Assert-Payload $userVst3 $payloadPath)
    if ($installPass -eq 2) { $records += $passRecords }
  }

  $uninstallers = @(Get-ChildItem -LiteralPath $uninstallRoot -File -Filter "unins*.exe" -ErrorAction Stop)
  if ($uninstallers.Count -ne 1) {
    throw "Expected exactly one Kirin Hypha uninstaller, found $($uninstallers.Count)"
  }
  $uninstallerPath = $uninstallers[0].FullName
  $records += Get-SignatureRecord "installed uninstaller" $uninstallerPath $Signing
  if (@(Find-HyphaUninstallEntries).Count -ne 1) {
    throw "Expected exactly one Kirin Hypha uninstall registry entry after install"
  }

  $uninstall = Start-Process -FilePath $uninstallerPath -ArgumentList @(
    "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"
  ) -Wait -PassThru
  if ($uninstall.ExitCode -ne 0) {
    throw "Uninstaller failed with exit code $($uninstall.ExitCode)"
  }
  Wait-PathState $preBundle $false "PRE bundle removal"
  Wait-PathState $postBundle $false "POST bundle removal"
  if (!(Test-Path -LiteralPath $sentinel -PathType Leaf)) {
    throw "Uninstaller removed an unrelated VST3 file"
  }
  if (@(Find-HyphaUninstallEntries).Count -ne 0) {
    throw "Uninstaller left a Kirin Hypha registry entry"
  }

  $manifestData = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
  $expectedInstallerHash = [string]$manifestData.installer.sha256
  if ((Get-Sha256 $installerPath) -ne $expectedInstallerHash) {
    throw "Installer no longer matches its build manifest"
  }
  $manifestData.signing.status = if ($Signing -eq "signed") { "valid" } else { "verified_unsigned_ci_candidate" }
  $manifestData.signing | Add-Member -NotePropertyName verification -NotePropertyValue ([ordered]@{
    method = "Get-AuthenticodeSignature after repeat install"
    targets = $records
  }) -Force
  $manifestData.ci_validation.status = "passed"
  $manifestData.ci_validation | Add-Member -NotePropertyName verified_at -NotePropertyValue ((Get-Date).ToUniversalTime().ToString("o")) -Force
  $manifestData.ci_validation | Add-Member -NotePropertyName facts -NotePropertyValue @(
    "per-user silent install passed",
    "same-version repeat install passed",
    "installed PRE/POST hashes match packaged payload",
    "silent uninstall removed both owned bundles",
    "unrelated VST3 sentinel survived uninstall",
    "uninstall registry entry was removed"
  ) -Force
  $manifestData.distribution.public_ready = (
    $Signing -eq "signed" -and $manifestData.external_validation.status -eq "complete"
  )
  $manifestData | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
  Write-Host "[hypha-installer] repeat install/uninstall verification passed"
} finally {
  if ($null -ne $uninstallerPath -and (Test-Path -LiteralPath $uninstallerPath -PathType Leaf)) {
    Start-Process -FilePath $uninstallerPath -ArgumentList @(
      "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"
    ) -Wait -ErrorAction SilentlyContinue | Out-Null
  }
  foreach ($ownedPath in @($preBundle, $postBundle, $uninstallRoot)) {
    if (Test-Path -LiteralPath $ownedPath) {
      Remove-Item -LiteralPath $ownedPath -Recurse -Force -ErrorAction SilentlyContinue
    }
  }
  Remove-Item -LiteralPath $sentinel -Force -ErrorAction SilentlyContinue
}
