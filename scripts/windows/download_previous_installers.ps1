# Fetch the previous public Windows installers named in the tracked fixture and accept them only
# when bytes, SHA-256 and Authenticode match. The pin, not a sidecar from the same release, decides.
param(
  [string]$Fixture = (Join-Path $PSScriptRoot "fixtures/previous-public-installers.json"),
  [Parameter(Mandatory = $true)]
  [string]$OutDir
)

$ErrorActionPreference = "Stop"
$data = Get-Content -LiteralPath $Fixture -Raw | ConvertFrom-Json
if ($data.schema -cne "kirin-hypha-previous-windows-installers-v1") { throw "Unsupported previous installer fixture" }
if (Test-Path -LiteralPath $OutDir) { throw "Previous installer directory already exists; preserve it for inspection: $OutDir" }
New-Item -ItemType Directory -Path $OutDir | Out-Null
foreach ($entry in $data.installers) {
  if ($entry.version -notmatch '^\d+\.\d+\.\d+$' -or
      $entry.file_name -cne "Kirin-Hypha-$($entry.version)-Windows-x64-Setup.exe" -or
      $entry.sha256 -notmatch '^[0-9a-f]{64}$') {
    throw "Invalid previous installer entry: $($entry.file_name)"
  }
  $target = Join-Path $OutDir $entry.file_name
  $uri = "$($data.release_download_base)/v$($entry.version)/$($entry.file_name)"
  Invoke-WebRequest -Uri $uri -OutFile $target
  $item = Get-Item -LiteralPath $target
  $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLower()
  if ($item.Length -ne [long]$entry.bytes -or $hash -cne $entry.sha256) {
    throw "Previous public installer differs from its pin: $($entry.file_name) ($($item.Length) bytes, $hash)"
  }
  $signature = Get-AuthenticodeSignature -LiteralPath $target
  if ([string]$signature.Status -ne "Valid") {
    throw "Previous public installer Authenticode is $($signature.Status): $($entry.file_name)"
  }
  Write-Host "[hypha-previous] $($entry.file_name) $hash"
}
