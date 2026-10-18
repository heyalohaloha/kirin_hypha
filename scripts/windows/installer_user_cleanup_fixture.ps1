# Credential-free fixture for the per-user cleanup that Setup runs as the user who started it.
# It runs the exact -EncodedCommand text embedded in the Inno recipe with Windows PowerShell 5.1
# against a temporary LOCALAPPDATA. Nothing is installed, signed or written to the real profile.
$ErrorActionPreference = "Stop"
. "$PSScriptRoot/installer_payload.ps1"

$recipe = Get-Content -LiteralPath (Join-Path $PSScriptRoot "kirin-hypha-installer.iss") -Raw
function Get-EmbeddedCommand([string]$Name) {
  $start = $recipe.IndexOf("function ${Name}: String;")
  if ($start -lt 0) { throw "Recipe has no $Name" }
  $end = $recipe.IndexOf("end;", $start)
  $chunks = [regex]::Matches($recipe.Substring($start, $end - $start), "'([A-Za-z0-9+/=]+)'") | ForEach-Object { $_.Groups[1].Value }
  return ($chunks -join "")
}
$commands = @{ check = Get-EmbeddedCommand "HyphaUserCleanupCheckCommand"; remove = Get-EmbeddedCommand "HyphaUserCleanupRemoveCommand" }
$powershell = Join-Path $env:SystemRoot "System32\WindowsPowerShell\v1.0\powershell.exe"
# The script also reads this AppId's real current-user registration; it must not exist here.
if (@(Find-HyphaUninstallEntries | Where-Object { $_.scope -eq "current-user" }).Count -ne 0) {
  throw "The runner already has a current-user Kirin Hypha registration; run this fixture on a clean runner"
}
$root = Join-Path ([System.IO.Path]::GetTempPath()) ("kirin-hypha-user-cleanup-" + [Guid]::NewGuid().ToString("N"))
$vst3 = Join-Path $root "Programs\Common\VST3"
$script:passed = 0

function Invoke-Cleanup([string]$Mode) {
  $saved = $env:LOCALAPPDATA
  try {
    $env:LOCALAPPDATA = $root
    & $powershell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $commands[$Mode] | Out-Null
    return $LASTEXITCODE
  }
  finally { $env:LOCALAPPDATA = $saved }
}

function Expect([string]$Label, [string]$Mode, [int]$Code) {
  $actual = Invoke-Cleanup $Mode
  if ($actual -ne $Code) { throw "${Label}: $Mode returned $actual, expected $Code" }
  $script:passed += 1
}

function New-Bundle([string]$Name) {
  $binary = Join-Path $vst3 "$Name.vst3\Contents\x86_64-win\$Name.vst3"
  New-Item -ItemType Directory -Force -Path (Split-Path $binary) | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $vst3 "$Name.vst3\Contents\Resources") | Out-Null
  Set-Content -LiteralPath $binary -Value "binary $Name" -Encoding ascii
  Set-Content -LiteralPath (Join-Path $vst3 "$Name.vst3\Contents\Resources\moduleinfo.json") -Value "{}" -Encoding ascii
  return $binary
}

# Every file below the VST3 root with its hash; links are listed, never followed.
function Get-TreeState {
  if (!(Test-Path -LiteralPath $vst3)) { return "" }
  $items = Get-ChildItem -LiteralPath $vst3 -Recurse -Force | ForEach-Object {
    $relative = $_.FullName.Substring($vst3.Length)
    if ($_.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { "$relative=link" }
    elseif ($_.PSIsContainer) { "$relative=dir" }
    else { "$relative=$(Get-Sha256 $_.FullName)" }
  }
  return (@($items) | Sort-Object) -join "|"
}

function Assert-NoStaged([string]$Label) {
  if (@(Get-ChildItem -LiteralPath $vst3 -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "*.kirin-hypha-removing" }).Count -ne 0) {
    throw "${Label}: a renamed copy was left behind"
  }
}

function Assert-Gone([string]$Label) {
  foreach ($role in @("PRE", "POST")) {
    if (Test-Path -LiteralPath (Join-Path $vst3 "Kirin Hypha $role.vst3")) { throw "${Label}: $role remains" }
  }
  Assert-NoStaged $Label
}

try {
  Expect "no VST3 folder" "check" 0
  Expect "no VST3 folder" "remove" 0

  New-Bundle "Kirin Hypha PDC Validation Delay 4096" | Out-Null
  New-Bundle "Other Vendor PRE" | Out-Null
  Set-Content -LiteralPath (Join-Path $vst3 "Kirin Hypha PRE.vst3.txt") -Value "unrelated" -Encoding ascii
  $unrelated = Get-TreeState
  Expect "only unrelated plug-ins" "check" 0
  Expect "only unrelated plug-ins" "remove" 0
  if ((Get-TreeState) -cne $unrelated) { throw "only unrelated plug-ins: something changed" }

  New-Bundle "Kirin Hypha PRE" | Out-Null
  New-Bundle "Kirin Hypha POST" | Out-Null
  Expect "both roles" "check" 10
  Expect "both roles" "remove" 0
  Assert-Gone "both roles"
  if ((Get-TreeState) -cne $unrelated) { throw "both roles: an unrelated item changed" }

  foreach ($held in @("PRE", "POST")) {
    $binaries = @{ PRE = New-Bundle "Kirin Hypha PRE"; POST = New-Bundle "Kirin Hypha POST" }
    $before = Get-TreeState
    $lock = [System.IO.File]::Open($binaries[$held], [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
    try {
      Expect "$held in use" "remove" 20
      Assert-NoStaged "$held in use"
    }
    finally { $lock.Dispose() }
    if ((Get-TreeState) -cne $before) { throw "$held in use: the older copy was not left intact" }
    Expect "$held released" "remove" 0
    Assert-Gone "$held released"
  }

  # A copy renamed by an interrupted earlier removal is found and removed.
  New-Bundle "Kirin Hypha POST" | Out-Null
  Rename-Item -LiteralPath (Join-Path $vst3 "Kirin Hypha POST.vst3") -NewName "Kirin Hypha POST.vst3.kirin-hypha-removing"
  Expect "interrupted removal" "check" 10
  Expect "interrupted removal" "remove" 0
  Assert-Gone "interrupted removal"

  # A single-file module carrying a bundle name is removed too.
  Set-Content -LiteralPath (Join-Path $vst3 "Kirin Hypha PRE.vst3") -Value "single-file module" -Encoding ascii
  Expect "single-file module" "check" 10
  Expect "single-file module" "remove" 0
  Assert-Gone "single-file module"

  # A link inside a bundle is removed as a link; its target outside the bundle survives.
  $outside = Join-Path $root "outside"
  New-Item -ItemType Directory -Force -Path $outside | Out-Null
  Set-Content -LiteralPath (Join-Path $outside "keep.txt") -Value "keep" -Encoding ascii
  $keep = Get-Sha256 (Join-Path $outside "keep.txt")
  New-Bundle "Kirin Hypha PRE" | Out-Null
  New-Item -ItemType Junction -Path (Join-Path $vst3 "Kirin Hypha PRE.vst3\Contents\Resources\linked") -Target $outside | Out-Null
  Expect "link inside a bundle" "remove" 0
  Assert-Gone "link inside a bundle"
  if (!(Test-Path -LiteralPath (Join-Path $outside "keep.txt")) -or (Get-Sha256 (Join-Path $outside "keep.txt")) -ne $keep) {
    throw "link inside a bundle: the link target was changed"
  }
  if ((Get-TreeState) -cne $unrelated) { throw "final: an unrelated item changed" }
  Write-Host "[hypha-user-cleanup-fixture] $($script:passed) checks passed"
}
finally {
  if (Test-Path -LiteralPath $root) { & cmd.exe /d /c rd /s /q $root | Out-Null }
}
