# Persist lifecycle facts only after verify-installer has checked hashes and installation behavior.
function Write-HyphaInstallerQualification {
  param($manifestData, [string]$manifestPath, [string]$Signing, $records, [bool]$withAax,
    [AllowNull()]$previousInstallerPath, [string]$previousVersion)
  $manifestData.signing.status = if ($Signing -eq "signed") { "valid" } else { "verified_unsigned_ci_candidate" }
  $manifestData.signing | Add-Member -NotePropertyName verification -NotePropertyValue ([ordered]@{
    method = "Get-AuthenticodeSignature after repeat install"
    targets = $records
  }) -Force
  $manifestData.ci_validation.status = "passed"
  $manifestData.ci_validation | Add-Member -NotePropertyName verified_at -NotePropertyValue ((Get-Date).ToUniversalTime().ToString("o")) -Force
  $facts = @(
    $(if ($withAax) { "all-users silent install passed" } else { "per-user silent install passed" }),
    $(if ($null -ne $previousInstallerPath) { "upgrade from $previousVersion passed" }),
    "same-version repeat install passed",
    "installed PRE/POST hashes match packaged payload",
    $(if ($withAax) { "installed PRE/POST AAX passed Authenticode and PACE verification" }),
    "silent uninstall removed both owned bundles",
    "unrelated VST3 sentinel survived uninstall",
    $(if ($withAax) { "unrelated AAX sentinel survived uninstall" }),
    "uninstall registry entry was removed"
  ) | Where-Object { $null -ne $_ }
  $manifestData.ci_validation | Add-Member -NotePropertyName facts -NotePropertyValue $facts -Force
  if ($null -ne $previousInstallerPath) {
    $manifestData.ci_validation | Add-Member -NotePropertyName prior_public_upgrade -NotePropertyValue ([ordered]@{
      installer = [System.IO.Path]::GetFileName($previousInstallerPath)
      version = $previousVersion
      status = "passed"
    }) -Force
  }
  $manifestData.distribution.public_ready = (
    $Signing -eq "signed" -and $manifestData.external_validation.status -eq "complete"
  )
  $manifestData | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}
