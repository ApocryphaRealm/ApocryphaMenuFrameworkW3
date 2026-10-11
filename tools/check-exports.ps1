# check-exports.ps1 - the framework's exported names must not disappear (1.0.5, plan row 60; Skyrim AMF's export check).
# Consumer mods find AMF's functions by name (GetProcAddress on ApocryphaMenuFramework.asi: the AMF_* API, the SKSE Menu
# Framework-compatible exports in Compat.cpp and the cimgui ig* functions). A name that vanishes from a build breaks every
# mod that calls it, with nothing at build time to say so. tools\exports.txt holds every name a released build exports;
# this script fails when one of them is missing from the built .asi. New exports are fine - add them with -Update.
#   powershell -NoProfile -File tools\check-exports.ps1            check the release build (exit 1 on a missing name)
#   powershell -NoProfile -File tools\check-exports.ps1 -Update    write tools\exports.txt from the built .asi
# dumpbin is found through vswhere, the way the build finds the compiler - never a fixed path.
param(
    [string]$Asi = '',
    [switch]$Update
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Asi) { $Asi = Join-Path $repo 'build\windows\x64\releasedbg\ApocryphaMenuFramework.asi' }
$list = Join-Path $PSScriptRoot 'exports.txt'
if (-not (Test-Path -LiteralPath $Asi)) { Write-Output "check-exports: $Asi is missing - xmake build first"; exit 1 }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { Write-Output 'check-exports: vswhere.exe not found (Visual Studio Installer)'; exit 1 }
$dumpbin = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' |
    Select-Object -First 1
if (-not $dumpbin -or -not (Test-Path -LiteralPath $dumpbin)) { Write-Output 'check-exports: dumpbin.exe not found through vswhere'; exit 1 }

# "    ordinal hint RVA      name" rows of dumpbin /exports
$names = New-Object System.Collections.Generic.List[string]
foreach ($line in (& $dumpbin /nologo /exports $Asi)) {
    if ($line -match '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]{8}\s+(\S+)') { $names.Add($Matches[1]) }
}
if ($names.Count -eq 0) { Write-Output "check-exports: dumpbin listed no exports in $Asi"; exit 1 }

if ($Update) {
    $header = @(
        '# Every name ApocryphaMenuFramework.asi exports (tools\check-exports.ps1 -Update, from dumpbin /exports).',
        '# check-exports.ps1 fails when one of these is missing from a build; tools\package.ps1 runs it before packaging.'
    )
    $sorted = $names | Sort-Object -Unique
    [System.IO.File]::WriteAllLines($list, [string[]]($header + $sorted), (New-Object System.Text.UTF8Encoding($false)))
    Write-Output ("check-exports: wrote {0} names to {1}" -f $sorted.Count, $list)
    exit 0
}

if (-not (Test-Path -LiteralPath $list)) { Write-Output "check-exports: $list is missing - run with -Update once"; exit 1 }
$want = Get-Content -LiteralPath $list | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { $_.Trim() }
$have = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($n in $names) { [void]$have.Add($n) }
$missing = @($want | Where-Object { -not $have.Contains($_) })
if ($missing.Count -gt 0) {
    Write-Output ("check-exports: FAIL - {0} exported name(s) missing from {1}:" -f $missing.Count, $Asi)
    $missing | ForEach-Object { Write-Output "  $_" }
    exit 1
}
$new = @($names | Where-Object { $want -notcontains $_ })
Write-Output ("check-exports: ok - all {0} names in exports.txt are exported ({1} export(s) not in the list yet{2})" -f $want.Count, $new.Count,
    $(if ($new.Count) { ': ' + ($new -join ', ') + ' - add them with -Update' } else { '' }))
exit 0
