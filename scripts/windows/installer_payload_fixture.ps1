# Credential-free fixture for the installed-payload record stream. It never signs, installs or
# contacts a provider: a PACE stub prints several stdout lines, and every "binary" is a copy of a
# file that already carries an embedded, timestamped Authenticode signature on the runner.
$ErrorActionPreference = "Stop"
. "$PSScriptRoot/installer_payload.ps1"
. "$PSScriptRoot/installer_qualification.ps1"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$root = Join-Path ([System.IO.Path]::GetTempPath()) ("kirin-hypha-payload-fixture-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $root | Out-Null
$script:passed = 0

function Reject([string]$Label, [scriptblock]$Action, [string]$Pattern) {
  $accepted = $false
  try { & $Action | Out-Null; $accepted = $true }
  catch {
    if ($_.Exception.Message -notmatch $Pattern) { throw "$Label was rejected for another reason: $($_.Exception.Message)" }
  }
  if ($accepted) { throw "$Label was accepted" }
  $script:passed += 1
}

function Find-SignedSample {
  $candidates = @((Join-Path $PSHOME "pwsh.exe"), (Get-Command node -ErrorAction SilentlyContinue).Source) | Where-Object { $_ }
  foreach ($candidate in $candidates) {
    $signature = Get-AuthenticodeSignature -FilePath $candidate
    if ([string]$signature.Status -eq "Valid" -and [string]$signature.SignatureType -eq "Authenticode" -and
        $null -ne $signature.TimeStamperCertificate) { return $candidate }
  }
  throw "No embedded, timestamped Authenticode sample is available on this runner"
}

function New-PaceStub([string]$Name, [int]$ExitCode, [int]$Lines) {
  $stub = Join-Path $root $Name
  $body = @("@echo off")
  for ($line = 1; $line -le $Lines; $line += 1) { $body += "echo PACE wraptool fixture line $line for %4" }
  $body += "exit /b $ExitCode"
  Set-Content -LiteralPath $stub -Value $body -Encoding ascii
  return $stub
}

function Set-AaxTree([string]$Tree, [string]$Sample) {
  foreach ($role in @("PRE", "POST")) {
    $binary = Join-Path $Tree "Kirin Hypha $role.aaxplugin\Contents\x64\Kirin Hypha $role.aaxplugin"
    New-Item -ItemType Directory -Force -Path (Split-Path $binary) | Out-Null
    Copy-Item -LiteralPath $Sample -Destination $binary -Force
  }
}

try {
  $sample = Find-SignedSample
  $source = Join-Path $root "source"
  $installed = Join-Path $root "installed"
  Set-AaxTree $source $sample
  Set-AaxTree $installed $sample
  $paceOk = New-PaceStub "pace-ok.cmd" 0 18
  $paceFail = New-PaceStub "pace-fail.cmd" 3 4

  # Control: the former uncaptured call shape leaks every stub line into the returned array.
  $leaked = @(& { Get-SignatureRecord "installed PRE AAX binary" (Join-Path $installed "Kirin Hypha PRE.aaxplugin\Contents\x64\Kirin Hypha PRE.aaxplugin") "signed"; & $paceOk verify --localonly --in "fixture" })
  if (@($leaked | Where-Object { $_ -is [string] }).Count -ne 18) { throw "PACE stub control did not reproduce the stdout leak" }
  $script:passed += 1

  $aax = @(Assert-AaxPayload $installed $source $paceOk)
  if ($aax.Count -ne 2 -or @($aax | Where-Object { $_ -isnot [System.Collections.IDictionary] }).Count -ne 0 -or
      ($aax | ForEach-Object { $_.role }) -join "|" -cne "installed PRE AAX binary|installed POST AAX binary") {
    throw "Assert-AaxPayload must return exactly the two structured AAX records"
  }
  foreach ($record in $aax) {
    if ($record.status -cne "Valid" -or !$record.timestamp_thumbprint -or $record.sha256 -cne (Get-Sha256 $sample)) {
      throw "AAX record lost its signature, timestamp or hash evidence"
    }
  }
  $script:passed += 1

  Reject "non-zero PACE exit" { Assert-AaxPayload $installed $source $paceFail } "PACE verification failed for installed PRE AAX binary \(exit 3\)"
  $post = Join-Path $installed "Kirin Hypha POST.aaxplugin"
  Rename-Item -LiteralPath $post -NewName "Kirin Hypha POST.moved"
  Reject "missing POST role" { Assert-AaxPayload $installed $source $paceOk } "Missing installed POST AAX binary"
  Rename-Item -LiteralPath (Join-Path $installed "Kirin Hypha POST.moved") -NewName "Kirin Hypha POST.aaxplugin"
  $preBinary = Join-Path $installed "Kirin Hypha PRE.aaxplugin\Contents\x64\Kirin Hypha PRE.aaxplugin"
  Add-Content -LiteralPath $preBinary -Value "changed" -Encoding ascii
  Reject "installed hash differs from payload" { Assert-AaxPayload $installed $source $paceOk } "PRE installed AAX hash mismatch"
  $unsigned = Join-Path $root "unsigned.bin"
  Set-Content -LiteralPath $unsigned -Value "not a signed binary" -Encoding ascii
  Copy-Item -LiteralPath $unsigned -Destination $preBinary -Force
  Copy-Item -LiteralPath $unsigned -Destination (Join-Path $source "Kirin Hypha PRE.aaxplugin\Contents\x64\Kirin Hypha PRE.aaxplugin") -Force
  Reject "unsigned AAX payload" { Assert-AaxPayload $installed $source $paceOk } "Invalid Authenticode signature for installed PRE AAX binary"
  Set-AaxTree $source $sample
  Set-AaxTree $installed $sample

  # The complete six-record set written through the real qualification writer.
  $six = @(Get-SignatureRecord "installer" $sample "signed") +
    @(foreach ($role in @("PRE", "POST")) { Get-SignatureRecord "installed $role VST3 binary" $sample "signed" }) +
    @(Assert-AaxPayload $installed $source $paceOk) +
    @(Get-SignatureRecord "installed uninstaller" $sample "signed")
  # The tracked fixture's bytes and required scenarios, as Invoke-HyphaUpgradeMatrix reports them.
  $upgradeFixture = Join-Path $repoRoot "scripts/windows/fixtures/previous-public-installers.json"
  $required = @((Get-Content -LiteralPath $upgradeFixture -Raw | ConvertFrom-Json).required_for_release)
  $matrix = [ordered]@{ fixture_sha256 = Get-Sha256 $upgradeFixture
    scenarios = @($required | ForEach-Object { [ordered]@{ id = $_; status = "passed" } }) }
  $manifestFile = Join-Path $root "manifest.json"
  $base = [ordered]@{ product = [ordered]@{ version = "0.0.0" }; signing = [ordered]@{ status = "pending" }
    ci_validation = [ordered]@{ status = "pending" }; external_validation = [ordered]@{ status = "pending" }
    distribution = [ordered]@{ aax_included = $true; public_ready = $false } }
  $fresh = { ($base | ConvertTo-Json -Depth 6) | ConvertFrom-Json }
  Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" $six $true $matrix
  $written = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
  $targets = @($written.signing.verification.targets)
  if ($targets.Count -ne 6 -or @($targets | Where-Object { $_ -is [string] }).Count -ne 0) {
    throw "Qualified manifest must hold six structured signature records and no strings"
  }
  Push-Location $repoRoot
  try {
    # The release-set reader must accept exactly what this writer produced: six signature records
    # and the previous-release upgrade matrix bound to the tracked fixture bytes.
    & node --input-type=module -e "import fs from 'node:fs'; import { requireValidSigningTargets, requirePreviousReleaseUpgrades } from './scripts/ls_release/build_kirin_hypha_release_set.mjs'; const manifest = JSON.parse(fs.readFileSync(process.argv[1], 'utf8').replace(/^\uFEFF/, '')); requireValidSigningTargets(manifest, { requireAax: true }); requirePreviousReleaseUpgrades(manifest);" $manifestFile
    if ($LASTEXITCODE -ne 0) { throw "The release-set validator rejected the qualified manifest" }
  } finally { Pop-Location }
  $script:passed += 1

  $duplicate = @($six[0..4]) + @($six[0])
  Reject "duplicate role" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" $duplicate $true $matrix } "Expected one signature record for installer, found 2"
  Reject "tool text beside records" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" (@($six) + @("PACE line")) $true $matrix } "Expected 6 signature records, found 7"
  Reject "tool text replacing a record" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" (@($six[0..4]) + @("PACE line")) $true $matrix } "structured records"
  $noStamp = [ordered]@{}; foreach ($key in $six[5].Keys) { if ($key -ne "timestamp_thumbprint") { $noStamp[$key] = $six[5][$key] } }
  Reject "missing timestamp" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" (@($six[0..4]) + @($noStamp)) $true $matrix } "timestamp_thumbprint"
  $badHash = [ordered]@{}; foreach ($key in $six[5].Keys) { $badHash[$key] = $six[5][$key] }; $badHash.sha256 = "not-a-hash"
  Reject "invalid hash" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" (@($six[0..4]) + @($badHash)) $true $matrix } "has no SHA-256"
  Reject "missing AAX role" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" (@($six[0..2]) + @($six[5])) $true $matrix } "Expected 6 signature records, found 4"
  Reject "signed without upgrade matrix" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" $six $true $null } "upgrade matrix"
  Reject "failed upgrade scenario" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" $six $true ([ordered]@{ fixture_sha256 = $matrix.fixture_sha256; scenarios = @([ordered]@{ id = "x"; status = "failed" }) }) } "did not pass"
  Reject "upgrade matrix without fixture hash" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" $six $true ([ordered]@{ scenarios = $matrix.scenarios }) } "fixture SHA-256"
  Reject "scenario list instead of a matrix" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" $six $true @($matrix.scenarios) } "fixture SHA-256"
  Reject "upgrade matrix without scenarios" { Write-HyphaInstallerQualification (& $fresh) $manifestFile "signed" $six $true ([ordered]@{ fixture_sha256 = $matrix.fixture_sha256; scenarios = @() }) } "ran no scenarios"
  Write-Host "[hypha-payload-fixture] $($script:passed) checks passed"
}
finally {
  Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
