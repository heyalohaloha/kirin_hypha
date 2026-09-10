param(
  [string]$ArtifactDir = "build-aax-windows",

  [string]$OutputDir = "build-aax-windows-signed",

  [string]$Wraptool = "",

  [string]$SignTool = "",

  [string]$CertificateThumbprint = "",

  [string]$CustomerNumber = $env:KIRIN_AAX_PACE_CUSTOMER_NUMBER,

  [string]$CustomerName = $env:KIRIN_AAX_PACE_CUSTOMER_NAME
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $repoRoot

function Resolve-RepoPath([string]$Value) {
  if ([System.IO.Path]::IsPathRooted($Value)) {
    return [System.IO.Path]::GetFullPath($Value)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Value))
}

function Resolve-Wraptool([string]$Requested) {
  $candidates = @(
    $Requested,
    $env:KIRIN_AAX_WRAPTOOL,
    (Join-Path $env:ProgramFiles "PACEAntiPiracy\Eden\Fusion\Current\bin\wraptool.exe"),
    (Join-Path $env:ProgramFiles "PACEAntiPiracy\Eden\Fusion\Versions\6\bin\wraptool.exe")
  ) | Where-Object { ![string]::IsNullOrWhiteSpace($_) }
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }
  throw "PACE wraptool.exe is required"
}

function Write-Sanitized([object[]]$Lines, [string]$CustomerId) {
  foreach ($line in $Lines) {
    $safe = ([string]$line).Replace($CustomerId, "[REDACTED_CUSTOMER]")
    $safe = [regex]::Replace(
      $safe,
      "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+",
      "[REDACTED_ACCOUNT]"
    )
    Write-Host $safe
  }
}

if ([string]::IsNullOrWhiteSpace($CustomerNumber)) {
  throw "KIRIN_AAX_PACE_CUSTOMER_NUMBER is required"
}
if ([string]::IsNullOrWhiteSpace($CustomerName)) {
  throw "KIRIN_AAX_PACE_CUSTOMER_NAME is required"
}
if ($CertificateThumbprint -notmatch "^[0-9A-Fa-f]{40}$") {
  throw "CertificateThumbprint must be a 40-character SHA-1 thumbprint"
}

$sourceRoot = Resolve-RepoPath $ArtifactDir
$signedRoot = Resolve-RepoPath $OutputDir
if (!(Test-Path -LiteralPath $sourceRoot -PathType Container)) {
  throw "Windows AAX artifact directory is missing: $sourceRoot"
}
if ($sourceRoot -eq $signedRoot) {
  throw "OutputDir must be separate from ArtifactDir"
}
if (Test-Path -LiteralPath $signedRoot) {
  throw "OutputDir must not already exist: $signedRoot"
}

$thumbprint = $CertificateThumbprint.ToUpperInvariant()
$certificates = @(
  Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object { $_.Thumbprint -eq $thumbprint }
)
if ($certificates.Count -ne 1) {
  throw "Expected exactly one CurrentUser code-signing certificate for the requested thumbprint"
}
$certificate = $certificates[0]
if (!$certificate.HasPrivateKey) {
  throw "The selected code-signing certificate has no available private key"
}
if ($certificate.NotAfter -le (Get-Date)) {
  throw "The selected code-signing certificate is expired"
}

$wraptoolPath = Resolve-Wraptool $Wraptool
$manifest = Get-Content -LiteralPath "config\hypha_windows_aax_bundles.json" -Raw |
  ConvertFrom-Json
$records = @($manifest.bundles)
if ($records.Count -ne 2 -or
    @($records | Where-Object { $_.role -eq "PRE" }).Count -ne 1 -or
    @($records | Where-Object { $_.role -eq "POST" }).Count -ne 1) {
  throw "Windows AAX manifest must contain exactly PRE and POST"
}

foreach ($record in $records) {
  $sourceBundle = Join-Path $sourceRoot ($record.source_relative.Replace("/", "\"))
  if (!(Test-Path -LiteralPath $sourceBundle -PathType Container)) {
    throw "$($record.role) AAX bundle is missing: $sourceBundle"
  }
  $destinationBundle = Join-Path $signedRoot ($record.source_relative.Replace("/", "\"))
  $destinationParent = Split-Path -Parent $destinationBundle
  New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null
  Copy-Item -LiteralPath $sourceBundle -Destination $destinationBundle -Recurse
}

foreach ($record in $records) {
  $bundle = Join-Path $signedRoot ($record.source_relative.Replace("/", "\"))
  $binary = Join-Path $bundle ($record.binary_relative.Replace("/", "\"))
  if (!(Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "$($record.role) AAX binary is missing after staging: $binary"
  }
  $arguments = @(
    "sign",
    "--in", $binary,
    "--customernumber", $CustomerNumber,
    "--customername", $CustomerName,
    "--signid", $thumbprint,
    "--extrasigningoptions", "/fd sha256 /tr http://ts.ssl.com /td sha256"
  )
  if (![string]::IsNullOrWhiteSpace($SignTool)) {
    if (!(Test-Path -LiteralPath $SignTool -PathType Leaf)) {
      throw "signtool.exe is missing: $SignTool"
    }
    $arguments += @("--signtool", (Resolve-Path -LiteralPath $SignTool).Path)
  }
  $output = @(& $wraptoolPath @arguments 2>&1)
  $exitCode = $LASTEXITCODE
  Write-Sanitized $output $CustomerNumber
  if ($exitCode -ne 0) {
    throw "$($record.role) combined PACE and Authenticode signing failed with exit code $exitCode"
  }
  Write-Host "[aax-windows] $($record.role) combined signature applied"
}

$versionSource = Get-Content -LiteralPath "crates\hypha_pre\Cargo.toml" -Raw
$versionMatch = [regex]::Match($versionSource, '(?m)^version\s*=\s*"([^"]+)"')
if (!$versionMatch.Success) { throw "Hypha version not found" }
$env:KIRIN_AAX_WRAPTOOL = $wraptoolPath
& node scripts/windows/windows-aax-bundles.mjs `
  --artifact-dir $signedRoot `
  --version $versionMatch.Groups[1].Value
if ($LASTEXITCODE -ne 0) {
  throw "Signed Windows AAX verification failed with exit code $LASTEXITCODE"
}
Write-Host "[aax-windows] PRE/POST PACE + Authenticode verification passed"
