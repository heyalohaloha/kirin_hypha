# Runs as the Windows user who started Setup (Inno ExecAsOriginalUser, Windows PowerShell 5.1),
# so it sees that user's profile and registry even when Setup was elevated with another
# administrator account, and it never gains rights that user lacks. It touches only that user's
# exact older current-user PRE/POST bundles and this AppId's current-user registration.
# The installer embeds this file as -EncodedCommand with "$Mode = 'check'" or "$Mode = 'remove'"
# prepended (scripts/windows/installer_user_cleanup.mjs); keep it ASCII.
# Exit codes. check: 0 none, 10 found. remove: 0 removed or none, 20 in use (nothing changed),
# 21 a bundle could not be removed after removal started, 22 the registration remained.
# Any other code means the copy could not be checked or removed.
$ErrorActionPreference = 'Stop'
try {
  $root = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
  $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{B3301A1C-4782-4E36-9B4D-89CC6C5B701E}_is1'
  $cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
  $roles = @('PRE', 'POST')
  $suffix = '.kirin-hypha-removing'
  function Get-HyphaUserBundle([string]$Role) { return (Join-Path $root ('Kirin Hypha ' + $Role + '.vst3')) }
  function Get-HyphaUserStaged([string]$Role) { return ((Get-HyphaUserBundle $Role) + $suffix) }
  # rd does not follow junctions or symbolic links inside the tree; it removes the link itself.
  # Windows PowerShell turns native stderr into errors, so the call runs with Continue. A single-file
  # module with a bundle name is a plain file and is deleted directly.
  function Remove-HyphaUserTree([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
      Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
    }
    elseif (Test-Path -LiteralPath $Path) {
      $ErrorActionPreference = 'Continue'
      & $cmd /d /c rd /s /q $Path 2>&1 | Out-Null
      $ErrorActionPreference = 'Stop'
    }
    return (-not (Test-Path -LiteralPath $Path))
  }
  # A binary left in a renamed folder would still be found by hosts that scan for .vst3 files.
  function Hide-HyphaUserBinary([string]$Role) {
    $binary = Join-Path (Get-HyphaUserStaged $Role) ('Contents\x86_64-win\Kirin Hypha ' + $Role + '.vst3')
    if (Test-Path -LiteralPath $binary) {
      Rename-Item -LiteralPath $binary -NewName ('Kirin Hypha ' + $Role + '.vst3' + $suffix) -ErrorAction SilentlyContinue
    }
  }

  $found = @($roles | Where-Object { (Test-Path -LiteralPath (Get-HyphaUserBundle $_)) -or (Test-Path -LiteralPath (Get-HyphaUserStaged $_)) })
  $registered = Test-Path -LiteralPath $key
  if ($Mode -eq 'check') {
    if ($found.Count -gt 0 -or $registered) { exit 10 }
    exit 0
  }
  if ($Mode -ne 'remove') { exit 31 }

  # A copy left by an interrupted earlier removal goes first; if it is still held, stop here.
  foreach ($role in $roles) {
    if (-not (Remove-HyphaUserTree (Get-HyphaUserStaged $role))) { Hide-HyphaUserBinary $role; exit 20 }
  }
  # Rename both bundles inside the same folder before deleting anything. A folder that holds an
  # open file cannot be renamed, so an in-use copy stops here with both bundles unchanged.
  $renamed = @()
  foreach ($role in $roles) {
    $bundle = Get-HyphaUserBundle $role
    if (-not (Test-Path -LiteralPath $bundle)) { continue }
    try {
      Rename-Item -LiteralPath $bundle -NewName ('Kirin Hypha ' + $role + '.vst3' + $suffix)
      $renamed += $role
    }
    catch {
      foreach ($done in $renamed) {
        try { Rename-Item -LiteralPath (Get-HyphaUserStaged $done) -NewName ('Kirin Hypha ' + $done + '.vst3') }
        catch { Hide-HyphaUserBinary $done; exit 21 }
      }
      exit 20
    }
  }
  $left = @($renamed | Where-Object { -not (Remove-HyphaUserTree (Get-HyphaUserStaged $_)) })
  if ($left.Count -gt 0) {
    foreach ($role in $left) { Hide-HyphaUserBinary $role }
    exit 21
  }
  try { if (Test-Path -LiteralPath $key) { Remove-Item -LiteralPath $key -Recurse -Force } }
  catch { exit 22 }
  if (Test-Path -LiteralPath $key) { exit 22 }
  exit 0
}
catch {
  exit 30
}
