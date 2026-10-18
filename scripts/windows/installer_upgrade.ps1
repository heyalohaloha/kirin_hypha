# Upgrade and scope-migration matrix. Each scenario starts from a verified clean machine, installs
# the pinned previous public installers in their own scopes (never moved aside by the test), then
# lets this installer upgrade or migrate. Pass means every VST3/AAX location a host scans holds
# exactly one PRE and one POST, with this payload's hashes, and one registration remains.
# Requires installer_payload.ps1 and installer_environment.ps1 to be dot-sourced first.

function Get-HyphaPluginRoots {
  return @(
    [pscustomobject]@{ format = "VST3"; scope = "current-user"; token = "%LOCALAPPDATA%\Programs\Common\VST3"
      path = (Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"); suffix = ".vst3"; binary = "Contents\x86_64-win" },
    [pscustomobject]@{ format = "VST3"; scope = "all-users"; token = "%CommonProgramFiles%\VST3"
      path = (Join-Path $env:CommonProgramFiles "VST3"); suffix = ".vst3"; binary = "Contents\x86_64-win" },
    [pscustomobject]@{ format = "AAX"; scope = "all-users"; token = "%CommonProgramFiles%\Avid\Audio\Plug-Ins"
      path = (Join-Path $env:CommonProgramFiles "Avid\Audio\Plug-Ins"); suffix = ".aaxplugin"; binary = "Contents\x64" }
  )
}

# Every PRE/POST bundle a host could load, at any depth below the scanned roots.
function Get-HyphaPluginResolution {
  $found = @()
  foreach ($root in Get-HyphaPluginRoots) {
    if (!(Test-Path -LiteralPath $root.path -PathType Container)) { continue }
    foreach ($role in @("PRE", "POST")) {
      $name = "Kirin Hypha $role$($root.suffix)"
      $bundles = @(Get-ChildItem -LiteralPath $root.path -Directory -Recurse -Filter $name -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq $name })
      foreach ($bundle in $bundles) {
        $binary = Join-Path $bundle.FullName (Join-Path $root.binary $name)
        $relative = $bundle.FullName.Substring($root.path.Length).TrimStart('\')
        $found += [pscustomobject]@{
          format = $root.format; role = $role; scope = $root.scope
          location = "$($root.token)\$relative"
          sha256 = $(if (Test-Path -LiteralPath $binary -PathType Leaf) { Get-Sha256 $binary } else { "missing" })
        }
      }
    }
  }
  return $found
}

function Get-HyphaPayloadHashes([string]$PayloadDir, [bool]$withAax) {
  $hashes = @{}
  foreach ($role in @("PRE", "POST")) {
    $hashes["VST3/$role"] = Get-Sha256 (Resolve-RequiredFile (Join-Path $PayloadDir "Kirin Hypha $role.vst3\Contents\x86_64-win\Kirin Hypha $role.vst3") "payload $role VST3")
    if ($withAax) {
      $hashes["AAX/$role"] = Get-Sha256 (Resolve-RequiredFile (Join-Path $PayloadDir "Kirin Hypha $role.aaxplugin\Contents\x64\Kirin Hypha $role.aaxplugin") "payload $role AAX")
    }
  }
  return $hashes
}

# The exact set a host would see: one bundle per owned format/role, in the expected scope.
function Assert-HyphaResolvedPayload($Hashes, [string]$Scope, [string]$Label) {
  $resolution = @(Get-HyphaPluginResolution)
  foreach ($key in $Hashes.Keys) {
    $format, $role = $key.Split("/")
    $rows = @($resolution | Where-Object { $_.format -eq $format -and $_.role -eq $role })
    $expectedScope = if ($format -eq "AAX") { "all-users" } else { $Scope }
    if ($rows.Count -ne 1 -or $rows[0].scope -ne $expectedScope -or $rows[0].sha256 -ne $Hashes[$key]) {
      $seen = ($rows | ForEach-Object { "$($_.location)=$($_.sha256)" }) -join "; "
      throw "${Label}: hosts would not load exactly this $format $role ($expectedScope): $seen"
    }
  }
  return $resolution
}

function Assert-HyphaRegistration([string]$Scope, [string]$Version, [string]$Label) {
  $entries = @(Find-HyphaUninstallEntries)
  $registryScope = if ($Scope -eq "current-user") { "current-user" } else { "all-users" }
  if ($entries.Count -ne 1 -or $entries[0].scope -ne $registryScope -or $entries[0].display_version -ne $Version) {
    $seen = ($entries | ForEach-Object { "$($_.scope)=$($_.display_version)" }) -join "; "
    throw "${Label}: expected one $registryScope registration of $Version, found: $seen"
  }
  return $entries[0]
}

function Invoke-HyphaSetup([string]$Path, [string]$Scope, [string]$LogPath) {
  $arguments = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/LOG=`"$LogPath`"")
  if ($Scope -eq "all-users") { $arguments += "/ALLUSERS" }
  elseif ($Scope -eq "current-user") { $arguments += "/CURRENTUSER" }
  elseif ($Scope -ne "default") { throw "Unknown installer scope request: $Scope" }
  $process = Start-Process -FilePath $Path -ArgumentList $arguments -Wait -PassThru
  if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
    Get-Content -LiteralPath $LogPath | Where-Object { $_ -match 'Kirin Hypha:' } | ForEach-Object { Write-Host "[setup] $_" }
  }
  return $process.ExitCode
}

function Invoke-HyphaRegisteredUninstall($Entry, [string]$Label) {
  $uninstaller = $Entry.uninstall_string.Trim().Trim('"')
  $allowed = @((Join-Path $env:LOCALAPPDATA "Programs\Kirin Mastering\Kirin Hypha"), (Join-Path $env:ProgramFiles "Kirin Mastering\Kirin Hypha"))
  if ($allowed -notcontains [System.IO.Path]::GetDirectoryName($uninstaller) -or
      [System.IO.Path]::GetFileName($uninstaller) -notmatch '^unins\d{3}\.exe$') {
    throw "${Label}: registration points outside the Hypha install folders: $uninstaller"
  }
  $process = Start-Process -FilePath $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") -Wait -PassThru
  if ($process.ExitCode -ne 0) { throw "${Label}: uninstaller exit code $($process.ExitCode)" }
  for ($attempt = 0; $attempt -lt 60 -and (Test-Path -LiteralPath $Entry.key); $attempt += 1) { Start-Sleep -Milliseconds 500 }
  if (Test-Path -LiteralPath $Entry.key) { throw "${Label}: uninstaller left its registration" }
}

# Owned-state teardown, valid only because the scenario began on a verified clean machine.
function Remove-HyphaScenarioState {
  foreach ($entry in @(Find-HyphaUninstallEntries)) {
    try { Invoke-HyphaRegisteredUninstall $entry "teardown" } catch { Write-Host "[hypha-upgrade] teardown: $($_.Exception.Message)" }
  }
  foreach ($owned in Get-HyphaInstallationPaths) {
    if (Test-Path -LiteralPath $owned) { Remove-Item -LiteralPath $owned -Recurse -Force }
  }
  foreach ($entry in @(Find-HyphaUninstallEntries)) { Remove-Item -LiteralPath $entry.key -Recurse -Force }
  Assert-HyphaInstallerEnvironment | Out-Null
}

# Unrelated files that every scenario must leave untouched: other products, diagnostics whose
# names also begin with "Kirin Hypha", the per-user settings/records Hypha and Kirin OS write, and
# DAW projects.
function New-HyphaPreservedSet {
  $id = [Guid]::NewGuid().ToString("N")
  $files = @()
  foreach ($root in Get-HyphaPluginRoots) {
    $files += Join-Path $root.path "kirin-hypha-preserve-$id.txt"
    $diagnostic = if ($root.format -eq "AAX") { "Kirin Hypha Diagnostic QA-$id.aaxplugin" } else { "Kirin Hypha PDC Validation Delay 4096 QA-$id.vst3" }
    $files += Join-Path $root.path "$diagnostic\Contents\fixture.txt"
  }
  $files += Join-Path $env:APPDATA "Kirin\Kirin Hypha\installer-qa-$id.txt"
  $files += Join-Path $env:APPDATA "Kirin Hypha\UpdateCheck\v1\installer-qa-$id.txt"
  $files += Join-Path $env:LOCALAPPDATA "Kirin OS\plugin_data\installer-qa-$id.txt"
  $files += Join-Path $env:APPDATA "Kirin OS\installer-qa-$id.txt"
  # DAW projects that use both roles, as a user would keep them in Documents.
  $projects = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "Kirin Hypha installer QA-$id"
  $files += @("Studio Pro\Kirin Hypha QA.song", "Pro Tools\Kirin Hypha QA.ptx", "REAPER\Kirin Hypha QA.RPP") | ForEach-Object { Join-Path $projects $_ }
  $created = @()
  foreach ($file in $files) {
    $directory = [System.IO.Path]::GetDirectoryName($file)
    $missing = @()
    for ($dir = $directory; $dir -and !(Test-Path -LiteralPath $dir); $dir = [System.IO.Path]::GetDirectoryName($dir)) { $missing += $dir }
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    Set-Content -LiteralPath $file -Value "preserve $id" -Encoding utf8
    $created += [pscustomobject]@{ file = $file; directories = $missing; sha256 = Get-Sha256 $file }
  }
  return $created
}

function Assert-HyphaPreservedSet($Set, [string]$Label) {
  foreach ($item in $Set) {
    if (!(Test-Path -LiteralPath $item.file -PathType Leaf) -or (Get-Sha256 $item.file) -ne $item.sha256) {
      throw "${Label}: an unrelated file was changed or removed: $($item.file)"
    }
  }
}

# Newest first, so a folder shared by several files is empty when its first creator is reached.
function Remove-HyphaPreservedSet($Set) {
  $items = @($Set)
  [array]::Reverse($items)
  foreach ($item in $items) {
    Remove-Item -LiteralPath $item.file -Force -ErrorAction SilentlyContinue
    foreach ($dir in $item.directories) {
      if ((Test-Path -LiteralPath $dir) -and @(Get-ChildItem -LiteralPath $dir -Force -Recurse -File).Count -eq 0) {
        Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
      }
    }
  }
}

function Install-HyphaPreviousRelease($Installer, [string]$Scope, [string]$Path, [string]$LogPath) {
  $exitCode = Invoke-HyphaSetup $Path $Scope $LogPath
  if ($exitCode -ne 0) { throw "Previous $($Installer.version) installer exit code $exitCode" }
  $actualScope = if ($Scope -eq "default") { $Installer.default_scope } else { $Scope }
  $roots = @(Get-HyphaPluginRoots | Where-Object { $_.format -eq "VST3" -and $_.scope -eq $actualScope })
  foreach ($role in @("PRE", "POST")) {
    if (!(Test-Path -LiteralPath (Join-Path $roots[0].path "Kirin Hypha $role.vst3") -PathType Container)) {
      throw "Previous $($Installer.version) did not install $role in its $actualScope scope"
    }
  }
  return [ordered]@{ version = $Installer.version; scope = $actualScope; installer_sha256 = $Installer.sha256 }
}

function Invoke-HyphaUpgradeScenario($Scenario, $Fixture, [string]$PreviousDir, [string]$Installer,
    [string]$Version, $Hashes, [bool]$withAax, [string]$LogDir) {
  Assert-HyphaInstallerEnvironment | Out-Null
  $preserved = New-HyphaPreservedSet
  $lock = $null
  try {
    $previous = @()
    $step = 0
    foreach ($request in $Scenario.previous) {
      $entry = @($Fixture.installers | Where-Object { $_.version -eq $request.version })
      if ($entry.Count -ne 1) { throw "Scenario $($Scenario.id) names an unpinned version $($request.version)" }
      $path = Resolve-RequiredFile (Join-Path $PreviousDir $entry[0].file_name) "previous $($request.version) installer"
      if ((Get-Sha256 $path) -ne $entry[0].sha256) { throw "Previous installer changed after download: $path" }
      $step += 1
      $previous += Install-HyphaPreviousRelease $entry[0] $request.scope $path (Join-Path $LogDir "$($Scenario.id)-previous-$step.log")
    }
    $before = @(Get-HyphaPluginResolution)
    # The elevated Setup must neither run nor delete files in the user-writable per-user install folder.
    $userUninstallDir = Join-Path $env:LOCALAPPDATA "Programs\Kirin Mastering\Kirin Hypha"
    $userUninstaller = @(if (Test-Path -LiteralPath $userUninstallDir -PathType Container) {
      Get-ChildItem -LiteralPath $userUninstallDir -File | ForEach-Object { [pscustomobject]@{ file = $_.FullName; sha256 = Get-Sha256 $_.FullName } }
    })
    $result = [ordered]@{ id = $Scenario.id; previous = $previous; new_scope = $Scenario.new_scope
      before = @($before | ForEach-Object { "$($_.location)=$($_.sha256)" }) }
    $newScope = if ($withAax -or $Scenario.new_scope -eq "all-users") { "all-users" } else { "current-user" }

    if ($Scenario.expect -eq "refused") {
      $exitCode = Invoke-HyphaSetup $Installer $Scenario.new_scope (Join-Path $LogDir "$($Scenario.id)-new.log")
      if ($exitCode -ne 7) { throw "Scenario $($Scenario.id): expected Setup to refuse with exit code 7, got $exitCode" }
      $after = @(Get-HyphaPluginResolution | ForEach-Object { "$($_.location)=$($_.sha256)" })
      if ((@($result.before | Sort-Object) -join "|") -ne (@($after | Sort-Object) -join "|")) {
        throw "Scenario $($Scenario.id): a refused Setup changed installed bundles"
      }
      Assert-HyphaRegistration "all-users" $Scenario.previous[-1].version "Scenario $($Scenario.id) refused" | Out-Null
      $result.refused_exit_code = $exitCode
    }
    else {
      if ($Scenario.expect -eq "refused-while-locked-then-replaced") {
        # Hold one role's binary as a DAW would. Setup must stop with the older copy untouched:
        # both bundles byte for byte, its registration, and nothing new for all users.
        $userVst3 = Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"
        $heldRole = [string]$Scenario.lock_role
        if ($heldRole -cnotin @("PRE", "POST")) { throw "Scenario $($Scenario.id) names no lock_role" }
        $olderCopy = @(Get-ChildItem -LiteralPath $userVst3 -Recurse -Force -File | Where-Object { $_.FullName -match '\\Kirin Hypha (PRE|POST)\.vst3\\' } |
          ForEach-Object { [pscustomobject]@{ file = $_.FullName; sha256 = Get-Sha256 $_.FullName } })
        if ($olderCopy.Count -eq 0) { throw "Scenario $($Scenario.id): no older current-user copy to hold" }
        $locked = Join-Path $userVst3 "Kirin Hypha $heldRole.vst3\Contents\x86_64-win\Kirin Hypha $heldRole.vst3"
        $lock = [System.IO.File]::Open($locked, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
        $exitCode = Invoke-HyphaSetup $Installer $Scenario.new_scope (Join-Path $LogDir "$($Scenario.id)-locked.log")
        # Release the lock before reading: hashing the held file would itself fail.
        $lock.Dispose(); $lock = $null
        if ($exitCode -ne 7) { throw "Scenario $($Scenario.id): a locked current-user copy must stop Setup with exit code 7, got $exitCode" }
        if (@(Get-HyphaPluginResolution | Where-Object { $_.scope -eq "all-users" -and $_.format -eq "VST3" }).Count -ne 0) {
          throw "Scenario $($Scenario.id): Setup installed the all-users copy while the current-user copy remained"
        }
        Assert-HyphaPreservedSet $olderCopy "Scenario $($Scenario.id) refused (older current-user copy left intact)"
        if (@(Get-ChildItem -LiteralPath $userVst3 -Force | Where-Object { $_.Name -like "*.kirin-hypha-removing" }).Count -ne 0) {
          throw "Scenario $($Scenario.id): a refused migration left a renamed copy"
        }
        if (@(Find-HyphaUninstallEntries | Where-Object { $_.scope -eq "current-user" }).Count -ne 1) {
          throw "Scenario $($Scenario.id): a refused migration removed the current-user registration"
        }
        $result.locked_role = $heldRole
        $result.locked_exit_code = $exitCode
      }
      foreach ($pass in 1..2) {
        $exitCode = Invoke-HyphaSetup $Installer $Scenario.new_scope (Join-Path $LogDir "$($Scenario.id)-new-$pass.log")
        if ($exitCode -ne 0) { throw "Scenario $($Scenario.id) pass ${pass}: Setup exit code $exitCode" }
        $resolved = Assert-HyphaResolvedPayload $Hashes $newScope "Scenario $($Scenario.id) pass $pass"
        $registration = Assert-HyphaRegistration $newScope $Version "Scenario $($Scenario.id) pass $pass"
        Assert-HyphaPreservedSet $preserved "Scenario $($Scenario.id) pass $pass"
        Assert-HyphaPreservedSet $userUninstaller "Scenario $($Scenario.id) pass $pass (old per-user uninstaller files, never run or removed)"
        $userVst3Root = Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"
        if ((Test-Path -LiteralPath $userVst3Root) -and @(Get-ChildItem -LiteralPath $userVst3Root -Force | Where-Object { $_.Name -like "*.kirin-hypha-removing" }).Count -ne 0) {
          throw "Scenario $($Scenario.id) pass ${pass}: the migration left a renamed copy"
        }
      }
      $result.resolved = @($resolved | ForEach-Object { [ordered]@{ format = $_.format; role = $_.role; location = $_.location; sha256 = $_.sha256 } })
      $result.registration = [ordered]@{ scope = $registration.scope; display_version = $registration.display_version }
      Invoke-HyphaRegisteredUninstall $registration "Scenario $($Scenario.id) uninstall"
      $left = @(Get-HyphaPluginResolution | Where-Object { $Hashes.ContainsKey("$($_.format)/$($_.role)") })
      if ($left.Count -ne 0 -or @(Find-HyphaUninstallEntries).Count -ne 0) {
        throw "Scenario $($Scenario.id): uninstall left owned bundles or a registration"
      }
    }
    Assert-HyphaPreservedSet $preserved "Scenario $($Scenario.id)"
    $result.status = "passed"
    return $result
  }
  finally {
    if ($null -ne $lock) { $lock.Dispose() }
    Remove-HyphaScenarioState
    Remove-HyphaPreservedSet $preserved
  }
}

# Returns the fixture bytes' SHA-256 with the scenario results; the release reader binds both.
function Invoke-HyphaUpgradeMatrix([string]$FixturePath, [string]$PreviousDir, [string]$Installer,
    [string]$PayloadDir, [string]$Version, [bool]$withAax, [string]$Signing) {
  $fixtureBytes = [System.IO.File]::ReadAllBytes((Resolve-RequiredFile $FixturePath "previous installer fixture"))
  $fixtureSha256 = [System.Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($fixtureBytes)).ToLower()
  $fixture = [System.Text.Encoding]::UTF8.GetString($fixtureBytes) | ConvertFrom-Json
  if ($fixture.schema -cne "kirin-hypha-previous-windows-installers-v1") { throw "Unsupported previous installer fixture" }
  foreach ($entry in $fixture.installers) {
    # A release must be newer than every pinned public installer. Unsigned CI may still carry the
    # latest public version number before the release bump; its payload is still this build.
    $previousVersion = [version]$entry.version
    if ($previousVersion -gt [version]$Version -or ($Signing -eq "signed" -and $previousVersion -ge [version]$Version)) {
      throw "Previous installer $($entry.version) is not older than $Version"
    }
  }
  $hashes = Get-HyphaPayloadHashes $PayloadDir $withAax
  $logDir = Join-Path ([System.IO.Path]::GetTempPath()) ("kirin-hypha-upgrade-" + [Guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Path $logDir | Out-Null
  $results = @()
  try {
    foreach ($scenario in $fixture.scenarios) {
      if ($scenario.vst3_only_installer -and $withAax) { continue }
      Write-Host "[hypha-upgrade] scenario $($scenario.id)"
      $results += Invoke-HyphaUpgradeScenario $scenario $fixture $PreviousDir $Installer $Version $hashes $withAax $logDir
    }
  }
  finally { Remove-Item -LiteralPath $logDir -Recurse -Force -ErrorAction SilentlyContinue }
  $missing = @($fixture.required_for_release | Where-Object { $_ -notin @($results | ForEach-Object { $_.id }) })
  if ($missing.Count -gt 0) { throw "Required upgrade scenarios did not run: $($missing -join ', ')" }
  return [ordered]@{ fixture_sha256 = $fixtureSha256; scenarios = @($results) }
}
