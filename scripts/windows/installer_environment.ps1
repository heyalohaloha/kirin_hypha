# Read-only preflight shared by the signing entry and actual lifecycle verification.
function Find-HyphaInstallRegistrations {
  $roots = @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')
  return @($roots | ForEach-Object {
    if (Test-Path -LiteralPath $_) {
      Get-ChildItem -LiteralPath $_ | ForEach-Object { Get-ItemProperty -LiteralPath $_.PSPath } |
        Where-Object { $_.DisplayName -like 'Kirin Hypha*' }
    }
  })
}

function Get-HyphaInstallationPaths {
  $paths = @()
  foreach ($root in @((Join-Path $env:LOCALAPPDATA 'Programs/Common/VST3'), (Join-Path $env:CommonProgramFiles 'VST3'))) {
    foreach ($role in @('PRE','POST')) { $paths += Join-Path $root "Kirin Hypha $role.vst3" }
  }
  foreach ($role in @('PRE','POST')) { $paths += Join-Path $env:CommonProgramFiles "Avid/Audio/Plug-Ins/Kirin Hypha $role.aaxplugin" }
  $paths += Join-Path $env:LOCALAPPDATA 'Programs/Kirin Mastering/Kirin Hypha'
  $paths += Join-Path $env:ProgramFiles 'Kirin Mastering/Kirin Hypha'
  return $paths
}

function Assert-HyphaInstallerEnvironment {
  param([string[]]$Paths = (Get-HyphaInstallationPaths),
        [object[]]$Registrations = @(Find-HyphaInstallRegistrations))
  foreach ($file in $Paths) {
    if (Test-Path -LiteralPath $file) { throw 'Existing Hypha installation: use a clean isolated lifecycle runner; preserve this installation' }
  }
  if ($Registrations.Count) { throw 'Existing Hypha installer registration: use a clean isolated lifecycle runner' }
  return [ordered]@{ schema = 'hypha-installer-environment-v1'; existingHypha = $false; checkedPathCount = $Paths.Count; checkedRegistryCount = $Registrations.Count }
}

if ($MyInvocation.InvocationName -ne '.') {
  $ErrorActionPreference = 'Stop'
  Assert-HyphaInstallerEnvironment | ConvertTo-Json
}
