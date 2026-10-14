# Package Apocrypha Menu Framework (The Witcher 3) for testing:
#   1. the built framework (ApocryphaMenuFramework.asi + .pdb) and loader (dinput8.dll) must carry VERSION's number in
#      their version resource - the gate's number, never typed (rule 6); and every name in tools\exports.txt must still be
#      exported (tools\check-exports.ps1, 1.0.5) - a failure packages nothing
#   2. copy dist\ to <stage>\Witcher 3 - Apocrypha Menu Framework\Apocrypha Menu Framework <version>\
#   3. bin\x64_dx12\ApocryphaMenuFramework.asi/.pdb; Root\bin\x64_dx12\dinput8.dll (1.0.1, the owner: "Repackage as
#      1.0.1" - Root Builder copies a mod's Root folder into the real game folder at launch, the one way the loader is
#      found under Mod Organizer 2, whose virtual folder is not in place when Windows loads dinput8.dll)
#   4. CHANGELOG.md beside the README, and the version stamped into the README
#   5. (1.0.6) the optional downloads beside it: "<Mod> <version> - No Installer" and "- No dinput8 (for TW3SE)"
# The stage folder comes from the project's resolver (distro-names.ps1 Resolve-PackageRoot), never a hand-joined path.
param(
    [string]$ProjectRoot = 'D:\Claude output',
    [string]$Stage = '7. current test builds',
    [string]$BuildDir = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$m = Select-String -LiteralPath (Join-Path $repo 'xmake.lua') -Pattern 'set_version\("(\d+\.\d+\.\d+)"\)' | Select-Object -First 1
if (-not $m) { throw 'no set_version in xmake.lua' }
$version = $m.Matches[0].Groups[1].Value
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\windows\x64\releasedbg' }

$asi = Join-Path $BuildDir 'ApocryphaMenuFramework.asi'
$pdb = Join-Path $BuildDir 'ApocryphaMenuFramework.pdb'
$loader = Join-Path $BuildDir 'dinput8.dll'
foreach ($f in $asi, $pdb, $loader) { if (-not (Test-Path -LiteralPath $f)) { throw "$f is missing - xmake build ApocryphaMenuFramework and AMFLoader first" } }
foreach ($f in $asi, $loader) {
    $fv = (Get-Item -LiteralPath $f).VersionInfo.FileVersion
    if ($fv -ne $version) { throw "$f says $fv, VERSION is $version - rebuild" }
}
# 1.0.5 (plan row 60): every name in tools\exports.txt must still be exported - a vanished export breaks consumer mods.
& powershell -NoProfile -File (Join-Path $PSScriptRoot 'check-exports.ps1') -Asi $asi
if ($LASTEXITCODE -ne 0) { throw 'check-exports.ps1 failed - an export in tools\exports.txt is missing from the build; nothing packaged' }

. (Join-Path $ProjectRoot '.MD\scripts\distro-names.ps1')
$root = Resolve-PackageRoot -Root (Join-Path $ProjectRoot $Stage) -ModName 'Apocrypha Menu Framework' -Game 'Witcher 3' -ProjectRoot $ProjectRoot
$pkg = Join-Path $root "Apocrypha Menu Framework $version"
if (Test-Path -LiteralPath $pkg) { throw "$pkg already exists - a version is packaged once" }
New-Item -ItemType Directory -Force -Path $pkg | Out-Null
Copy-Item -Path (Join-Path $repo 'dist\*') -Destination $pkg -Recurse
$bin = Join-Path $pkg 'bin\x64_dx12'
Copy-Item -LiteralPath $asi -Destination $bin
Copy-Item -LiteralPath $pdb -Destination $bin
$rootBin = Join-Path $pkg 'Root\bin\x64_dx12'
New-Item -ItemType Directory -Force -Path $rootBin | Out-Null
Copy-Item -LiteralPath $loader -Destination $rootBin
Copy-Item -LiteralPath (Join-Path $repo 'CHANGELOG.md') -Destination $pkg
# player settings are never in a download (User.ini, Presets\ - the README's promise)
foreach ($p in 'bin\x64_dx12\AMF\User.ini', 'bin\x64_dx12\AMF\Presets') {
    if (Test-Path -LiteralPath (Join-Path $pkg $p)) { throw "$p is in dist\ - player settings must never ship" }
}

# 1.0.6: the OPTIONAL downloads, written beside the main package from the same files before the installer step, so
# finalize-mod picks them up ("<Mod> X.Y.Z - <label>" folders, step 6b) and every version has them:
#   " - No Installer"             laid out like the game folder, the loader already in bin\x64_dx12 (RMMR, by hand)
#   " - No dinput8 (for TW3SE)"   the same with NO dinput8.dll anywhere (the owner, 2026-10-14: TW3SE starts through its
#                                 own dinput8.dll and loads the .asi itself; AMF's loader over it stops TW3SE loading -
#                                 "just have it as an additional file that says the no DLL or no dinput DLL version")
$variants = [ordered]@{ 'No Installer' = $true; 'No dinput8 (for TW3SE)' = $false }
foreach ($label in $variants.Keys) {
    $v = Join-Path $root "Apocrypha Menu Framework $version - $label"
    if (Test-Path -LiteralPath $v) { throw "$v already exists - a version is packaged once" }
    New-Item -ItemType Directory -Force -Path $v | Out-Null
    Get-ChildItem -LiteralPath $pkg | Where-Object { $_.Name -ne 'Root' } | Copy-Item -Destination $v -Recurse
    if ($variants[$label]) { Copy-Item -LiteralPath $loader -Destination (Join-Path $v 'bin\x64_dx12') }
    $vReadme = Join-Path $v 'README.txt'
    $t = [System.IO.File]::ReadAllText($vReadme)
    $t = [regex]::Replace($t, '(?m)^Version \d+\.\d+\.\d+', "Version $version", 1)
    [System.IO.File]::WriteAllText($vReadme, $t, (New-Object System.Text.UTF8Encoding($false)))
    $dlls = @(Get-ChildItem -LiteralPath $v -Recurse -Filter 'dinput8.dll')
    if ((-not $variants[$label]) -and $dlls.Count) { throw "$v holds a dinput8.dll - the TW3SE download must have none" }
    if ($variants[$label] -and $dlls.Count -ne 1) { throw "$v should hold exactly one dinput8.dll (bin\x64_dx12)" }
    Write-Output "optional $v ($($dlls.Count) dinput8.dll)"
}

# 1.0.3: the download is a FOMOD installer - one question puts the loader where the player's manager needs it
# (bin\x64_dx12 for Vortex or by hand, Root\ for Mod Organizer 2 + Root Builder, or none beside an existing ASI loader)
& python -I (Join-Path $PSScriptRoot 'fomod.py') $pkg $version
if ($LASTEXITCODE -ne 0) { throw "fomod.py failed for $pkg" }
$loaderOut = Join-Path $pkg 'loader\dinput8.dll'

$readme = Join-Path $pkg 'README.txt'
$text = [System.IO.File]::ReadAllText($readme)
$text = [regex]::Replace($text, '(?m)^Version \d+\.\d+\.\d+', "Version $version", 1)
[System.IO.File]::WriteAllText($readme, $text, (New-Object System.Text.UTF8Encoding($false)))

Write-Output "packaged $pkg"
Write-Output ("  asi    {0}" -f (Get-FileHash -LiteralPath (Join-Path $bin 'ApocryphaMenuFramework.asi') -Algorithm SHA256).Hash)
Write-Output ("  loader {0}" -f (Get-FileHash -LiteralPath $loaderOut -Algorithm SHA256).Hash)
