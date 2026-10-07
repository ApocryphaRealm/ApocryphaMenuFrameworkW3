# Package Apocrypha Menu Framework (The Witcher 3) for testing:
#   1. the built framework (ApocryphaMenuFramework.asi + .pdb) and loader (dinput8.dll) must carry VERSION's number in
#      their version resource - the gate's number, never typed (rule 6)
#   2. copy dist\ to <stage>\Witcher 3 - Apocrypha Menu Framework\Apocrypha Menu Framework <version>\
#   3. bin\x64_dx12\ApocryphaMenuFramework.asi/.pdb; Root\bin\x64_dx12\dinput8.dll (1.0.1, the owner: "Repackage as
#      1.0.1" - Root Builder copies a mod's Root folder into the real game folder at launch, the one way the loader is
#      found under Mod Organizer 2, whose virtual folder is not in place when Windows loads dinput8.dll)
#   4. CHANGELOG.md beside the README, and the version stamped into the README
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
