# Installed-payload checks shared by the lifecycle, the upgrade matrix and their fixtures.
# Functions return only structured records; native tool output goes to the host log.

$HyphaAppUninstallKey = '{B3301A1C-4782-4E36-9B4D-89CC6C5B701E}_is1'

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
  if ($Expected -eq "signed" -and ($null -eq $record.timestamp_thumbprint -or $null -eq $record.signer_thumbprint)) {
    throw "Signed ${Role} has no signer or timestamp evidence: $FilePath"
  }
  return $record
}

function Resolve-AaxWraptool([string]$Requested) {
  $candidates = @(
    $Requested,
    (Join-Path $env:ProgramFiles "PACEAntiPiracy\Eden\Fusion\Current\bin\wraptool.exe"),
    (Join-Path $env:ProgramFiles "PACEAntiPiracy\Eden\Fusion\Versions\6\bin\wraptool.exe")
  ) | Where-Object { $_ -ne "" }
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw "PACE wraptool.exe is required to verify an AAX installer"
}

# PACE prints diagnostics on stdout. Capture them and write them to the host so they can never
# join the signature records returned through the pipeline; the exit code is the result.
# stderr is left on the host stream untouched.
function Invoke-HyphaPaceVerification([string]$Wraptool, [string]$Role, [string]$Binary) {
  $output = @(& $Wraptool verify --localonly --in $Binary)
  $exitCode = $LASTEXITCODE
  foreach ($line in $output) { Write-Host "[pace $Role] $line" }
  if ($exitCode -ne 0) {
    throw "PACE verification failed for installed $Role AAX binary (exit $exitCode)"
  }
}

function Wait-PathState([string]$PathValue, [bool]$Present, [string]$Label) {
  for ($attempt = 0; $attempt -lt 60; $attempt += 1) {
    if ((Test-Path -LiteralPath $PathValue) -eq $Present) { return }
    Start-Sleep -Milliseconds 500
  }
  throw "Timed out waiting for ${Label}: $PathValue"
}

function Resolve-HyphaUninstaller([string]$Root) {
  $uninstallers = @(Get-ChildItem -LiteralPath $Root -File -Filter "unins*.exe" -ErrorAction Stop)
  if ($uninstallers.Count -ne 1) {
    throw "Expected exactly one Kirin Hypha uninstaller, found $($uninstallers.Count)"
  }
  return $uninstallers[0].FullName
}

# Registrations of this AppId only; other products and diagnostics with similar names are ignored.
function Find-HyphaUninstallEntries {
  $roots = @(
    @{ scope = "current-user"; path = "Registry::HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall" },
    @{ scope = "all-users"; path = "Registry::HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Uninstall" },
    @{ scope = "all-users-32"; path = "Registry::HKEY_LOCAL_MACHINE\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall" }
  )
  $entries = @()
  foreach ($root in $roots) {
    $key = Join-Path $root.path $HyphaAppUninstallKey
    if (!(Test-Path -LiteralPath $key)) { continue }
    $item = Get-ItemProperty -LiteralPath $key
    $entries += [pscustomobject]@{
      scope = $root.scope
      key = $key
      display_version = [string]$item.DisplayVersion
      uninstall_string = [string]$item.UninstallString
    }
  }
  return $entries
}

function Assert-Payload([string]$InstalledRoot, [string]$SourceRoot, [string]$Signing) {
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

function Assert-AaxPayload([string]$InstalledRoot, [string]$SourceRoot, [string]$Wraptool) {
  $records = @()
  foreach ($role in @("PRE", "POST")) {
    $bundleName = "Kirin Hypha ${role}.aaxplugin"
    $relativeBinary = "Contents\x64\$bundleName"
    $sourceBinary = Resolve-RequiredFile (Join-Path (Join-Path $SourceRoot $bundleName) $relativeBinary) "source $role AAX binary"
    $installedBinary = Resolve-RequiredFile (Join-Path (Join-Path $InstalledRoot $bundleName) $relativeBinary) "installed $role AAX binary"
    $sourceHash = Get-Sha256 $sourceBinary
    $installedHash = Get-Sha256 $installedBinary
    if ($sourceHash -ne $installedHash) {
      throw "$role installed AAX hash mismatch: $installedHash != $sourceHash"
    }
    $records += Get-SignatureRecord "installed $role AAX binary" $installedBinary "signed"
    Invoke-HyphaPaceVerification $Wraptool $role $installedBinary
  }
  return $records
}
