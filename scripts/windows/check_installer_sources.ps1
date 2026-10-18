# Fast pre-build gate: parse every Windows PowerShell script, run the installer record and per-user
# cleanup fixtures and compile both installer variants from inert payload folders. Nothing is
# installed or signed.
$ErrorActionPreference = "Stop"
foreach ($script in Get-ChildItem -LiteralPath $PSScriptRoot -Filter "*.ps1") {
  $errors = $null
  [System.Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref]$null, [ref]$errors) | Out-Null
  if ($errors.Count -gt 0) {
    throw "$($script.Name):$($errors[0].Extent.StartLineNumber): $($errors[0].Message)"
  }
}

pwsh -NoProfile -File (Join-Path $PSScriptRoot "installer_payload_fixture.ps1")
if ($LASTEXITCODE -ne 0) { throw "Installer payload record fixture failed" }
# The recipe must embed the current per-user cleanup script, which then runs on Windows PowerShell 5.1.
node (Join-Path $PSScriptRoot "installer_user_cleanup.mjs")
if ($LASTEXITCODE -ne 0) { throw "The recipe's embedded per-user cleanup script is stale" }
pwsh -NoProfile -File (Join-Path $PSScriptRoot "installer_user_cleanup_fixture.ps1")
if ($LASTEXITCODE -ne 0) { throw "Per-user cleanup fixture failed" }

$iscc = $env:ISCC_PATH
if ([string]::IsNullOrWhiteSpace($iscc) -or !(Test-Path -LiteralPath $iscc -PathType Leaf)) {
  throw "ISCC_PATH must name the verified Inno Setup compiler"
}
$root = Join-Path ([System.IO.Path]::GetTempPath()) ("kirin-hypha-recipe-" + [Guid]::NewGuid().ToString("N"))
try {
  $bundles = @{}
  foreach ($name in @("Kirin Hypha PRE.vst3", "Kirin Hypha POST.vst3", "Kirin Hypha PRE.aaxplugin", "Kirin Hypha POST.aaxplugin")) {
    $bundles[$name] = Join-Path $root "payload\$name"
    New-Item -ItemType Directory -Force -Path (Join-Path $bundles[$name] "Contents") | Out-Null
    Set-Content -LiteralPath (Join-Path $bundles[$name] "Contents\inert.txt") -Value "inert" -Encoding ascii
  }
  $recipe = Join-Path $PSScriptRoot "kirin-hypha-installer.iss"
  foreach ($variant in @("vst3", "aax")) {
    $output = Join-Path $root "out-$variant"
    $arguments = @("/Q", "/DAppVersion=0.0.1", "/DOutputDir=$output",
      "/DPreBundle=$($bundles['Kirin Hypha PRE.vst3'])", "/DPostBundle=$($bundles['Kirin Hypha POST.vst3'])")
    if ($variant -eq "aax") {
      $arguments += @("/DWithAax=1", "/DPreAaxBundle=$($bundles['Kirin Hypha PRE.aaxplugin'])",
        "/DPostAaxBundle=$($bundles['Kirin Hypha POST.aaxplugin'])")
    }
    & $iscc @arguments $recipe
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup could not compile the $variant installer recipe" }
    if (@(Get-ChildItem -LiteralPath $output -Filter "*.exe").Count -ne 1) { throw "No $variant installer was compiled" }
  }
  Write-Host "[hypha-installer-sources] scripts parsed; fixture passed; both recipes compiled"
}
finally {
  Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
}
