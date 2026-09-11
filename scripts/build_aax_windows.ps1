param(
  [Parameter(Mandatory = $true)]
  [string]$Sdk,

  [switch]$LicenseConfirmed,

  [string]$KimeraFont = "",

  [switch]$KimeraLicenseConfirmed,

  [string]$BuildDir = "build-aax-windows"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

if (!$LicenseConfirmed) {
  throw "-LicenseConfirmed is required for the external AAX SDK"
}
$sdkPath = (Resolve-Path -LiteralPath $Sdk -ErrorAction Stop).Path
if (!(Test-Path -LiteralPath (Join-Path $sdkPath "Interfaces\ACF") -PathType Container)) {
  throw "AAX SDK must contain Interfaces\ACF"
}
$repoPrefix = $repoRoot.TrimEnd("\") + "\"
if ($sdkPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
  throw "AAX SDK must remain outside the repository"
}

$kimeraPath = ""
if (![string]::IsNullOrWhiteSpace($KimeraFont)) {
  if (!$KimeraLicenseConfirmed) {
    throw "-KimeraLicenseConfirmed is required when -KimeraFont is supplied"
  }
  $kimeraPath = (Resolve-Path -LiteralPath $KimeraFont -ErrorAction Stop).Path
  if ($kimeraPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "licensed Kimera font must remain outside the repository"
  }
} elseif ($KimeraLicenseConfirmed) {
  throw "-KimeraFont is required when -KimeraLicenseConfirmed is supplied"
}

function Invoke-Checked([string]$Label, [scriptblock]$Command) {
  Write-Host "[aax-windows] $Label"
  & $Command
  if ($LASTEXITCODE -ne 0) {
    throw "$Label failed with exit code $LASTEXITCODE"
  }
}

Invoke-Checked "build kirin_hypha_ffi x64 Release" {
  cargo build --release -p kirin_hypha_ffi --locked
}
Invoke-Checked "apply tracked JUCE patches" {
  bash scripts/apply_juce_patches.sh
}
Invoke-Checked "verify tracked JUCE patches" {
  bash scripts/verify_juce_patch_state.sh
}

$identityJson = @(& node scripts/ls_release/release_source_identity.mjs --require-clean 2>&1)
if ($LASTEXITCODE -ne 0) {
  $identityJson | ForEach-Object { Write-Host $_ }
  throw "release source identity verification failed"
}
$sourceIdentity = ($identityJson -join [Environment]::NewLine) | ConvertFrom-Json

$buildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$ffiLibrary = Join-Path $repoRoot "target\release\kirin_hypha_ffi.lib"
$cmakeArguments = @(
  "-S", "juce_shell",
  "-B", $buildPath,
  "-DKIRIN_FFI_LIB=$ffiLibrary",
  "-DKIRIN_HYPHA_AAX_SDK_PATH=$sdkPath",
  "-DKIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=ON",
  "-DKIRIN_HYPHA_REQUIRE_AAX=ON"
)
if ($kimeraPath -ne "") {
  $cmakeArguments += @(
    "-DKIRIN_HYPHA_KIMERA_FONT_FILE=$kimeraPath",
    "-DKIRIN_HYPHA_KIMERA_APP_LICENSE_CONFIRMED=ON",
    "-DKIRIN_HYPHA_REQUIRE_KIMERA=ON"
  )
}
Invoke-Checked "configure external-SDK AAX build" {
  & cmake @cmakeArguments
}
Invoke-Checked "build PRE and POST AAX" {
  cmake --build $buildPath --config Release `
    --target KirinHyphaPRE_AAX KirinHyphaPOST_AAX --parallel 2
}

$versionSource = Get-Content -LiteralPath "crates\hypha_pre\Cargo.toml" -Raw
$versionMatch = [regex]::Match($versionSource, '(?m)^version\s*=\s*"([^"]+)"')
if (!$versionMatch.Success) { throw "Hypha version not found" }
$version = $versionMatch.Groups[1].Value

foreach ($role in @("PRE", "POST")) {
  $binary = Join-Path $buildPath "KirinHypha${role}_artefacts\Release\AAX\Kirin Hypha ${role}.aaxplugin\Contents\x64\Kirin Hypha ${role}.aaxplugin"
  if (!(Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "$role AAX binary is missing: $binary"
  }
  $item = Get-Item -LiteralPath $binary
  if ($item.Length -le 0) { throw "$role AAX binary is empty" }
  if ($item.VersionInfo.FileVersion -ne $version -or $item.VersionInfo.ProductVersion -ne $version) {
    throw "$role AAX version is $($item.VersionInfo.FileVersion)/$($item.VersionInfo.ProductVersion), expected $version"
  }
  Write-Host "[aax-windows] $role AAX ready for PACE signing: $($item.Length) bytes"
}

Invoke-Checked "write exact Windows AAX build provenance" {
  node scripts/windows/windows-aax-provenance.mjs write-build `
    --artifact-dir $buildPath `
    --version $version `
    --source-commit $sourceIdentity.commit `
    --b-number $sourceIdentity.bNumber `
    --source-state $sourceIdentity.sourceState
}
