# Persist lifecycle facts only after verify-installer has checked hashes and installation behavior.

function Get-HyphaSignatureRoles([bool]$withAax) {
  return @(
    "installer",
    "installed PRE VST3 binary",
    "installed POST VST3 binary"
  ) + $(if ($withAax) { @("installed PRE AAX binary", "installed POST AAX binary") } else { @() }) + @(
    "installed uninstaller"
  )
}

# Exactly one structured record per signed surface: no tool text, duplicates or missing roles.
function Assert-HyphaSignatureRecords($records, [bool]$withAax, [string]$Signing) {
  $items = @($records)
  $roles = Get-HyphaSignatureRoles $withAax
  if ($items.Count -ne $roles.Count) {
    throw "Expected $($roles.Count) signature records, found $($items.Count)"
  }
  foreach ($item in $items) {
    if ($item -isnot [System.Collections.IDictionary]) {
      $type = if ($null -eq $item) { "null" } else { $item.GetType().FullName }
      throw "Signature records must be structured records, not $type"
    }
  }
  foreach ($role in $roles) {
    $found = @($items | Where-Object { $_.role -ceq $role })
    if ($found.Count -ne 1) { throw "Expected one signature record for $role, found $($found.Count)" }
    $record = $found[0]
    if ([string]$record.sha256 -notmatch '^[0-9a-f]{64}$') { throw "Signature record for $role has no SHA-256" }
    # AAX binaries are always PACE + Authenticode signed before packaging (Assert-AaxPayload), so an
    # unsigned rehearsal installer carries signed AAX records and unsigned records for the rest.
    $signedRole = $Signing -eq "signed" -or $role -like "installed * AAX binary"
    $expectedStatus = if ($signedRole) { "Valid" } else { "NotSigned" }
    if ($record.status -cne $expectedStatus) { throw "Signature record for $role is $($record.status), not $expectedStatus" }
    if ($signedRole) {
      foreach ($field in @("signer_subject", "signer_thumbprint", "timestamp_subject", "timestamp_thumbprint")) {
        if ([string]::IsNullOrWhiteSpace([string]$record[$field])) { throw "Signature record for $role has no $field" }
      }
    }
  }
}

function Write-HyphaInstallerQualification {
  param($manifestData, [string]$manifestPath, [string]$Signing, $records, [bool]$withAax,
    [AllowNull()]$upgradeMatrix)
  Assert-HyphaSignatureRecords $records $withAax $Signing
  # The matrix carries the SHA-256 of the fixture it ran; the release reader compares it with the
  # tracked fixture, so a result can never be reused for a different set of previous installers.
  $scenarios = @()
  if ($null -ne $upgradeMatrix) {
    if ($upgradeMatrix -isnot [System.Collections.IDictionary] -or [string]$upgradeMatrix.fixture_sha256 -cnotmatch '^[0-9a-f]{64}$') {
      throw "Upgrade matrix has no fixture SHA-256"
    }
    $scenarios = @($upgradeMatrix.scenarios | Where-Object { $null -ne $_ })
    if ($scenarios.Count -eq 0) { throw "Upgrade matrix ran no scenarios" }
  }
  if ($Signing -eq "signed" -and $scenarios.Count -eq 0) {
    throw "Signed installer qualification requires the previous public release upgrade matrix"
  }
  foreach ($scenario in $scenarios) {
    if ($scenario.status -cne "passed") { throw "Upgrade scenario $($scenario.id) did not pass" }
  }
  $manifestData.signing.status = if ($Signing -eq "signed") { "valid" } else { "verified_unsigned_ci_candidate" }
  $manifestData.signing | Add-Member -NotePropertyName verification -NotePropertyValue ([ordered]@{
    method = "Get-AuthenticodeSignature after repeat install"
    targets = @($records)
  }) -Force
  $manifestData.ci_validation.status = "passed"
  $manifestData.ci_validation | Add-Member -NotePropertyName verified_at -NotePropertyValue ((Get-Date).ToUniversalTime().ToString("o")) -Force
  $facts = @(
    $(if ($withAax) { "all-users silent install passed" } else { "per-user silent install passed" }),
    "same-version repeat install passed",
    "installed PRE/POST hashes match packaged payload",
    $(if ($withAax) { "installed PRE/POST AAX passed Authenticode and PACE verification" }),
    "silent uninstall removed both owned bundles",
    "unrelated VST3 sentinel survived uninstall",
    $(if ($withAax) { "unrelated AAX sentinel survived uninstall" }),
    "uninstall registry entry was removed"
  ) + @($scenarios | ForEach-Object { "upgrade scenario $($_.id) passed" }) | Where-Object { $null -ne $_ }
  $manifestData.ci_validation | Add-Member -NotePropertyName facts -NotePropertyValue @($facts) -Force
  if ($scenarios.Count -gt 0) {
    $manifestData.ci_validation | Add-Member -NotePropertyName prior_public_upgrade -NotePropertyValue ([ordered]@{
      status = "passed"
      fixture_sha256 = [string]$upgradeMatrix.fixture_sha256
      method = "previous public installers in their own scopes, then this installer; resolved VST3/AAX paths and hashes"
      scenarios = @($scenarios)
    }) -Force
  }
  $manifestData.distribution.public_ready = (
    $Signing -eq "signed" -and $manifestData.external_validation.status -eq "complete"
  )
  $manifestData | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}
