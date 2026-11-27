# build.ps1 - run make with the STM32CubeIDE GNU toolchain on PATH (this call only).
#
#   .\build.ps1                 # make all
#   .\build.ps1 clean all       # any make goals
#   .\build.ps1 -Demo mqtt clean all   # same as make DEMO=mqtt clean all (none|tcp_echo|mqtt|plusio|ota)
#   $env:CUBEIDE_DIR = 'D:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE'; .\build.ps1   # other install
#
# Nothing is installed and the user/system PATH is not changed.

[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('none', 'tcp_echo', 'mqtt', 'plusio', 'ota')]
    [string]$Demo = 'none',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Goals = @('all')
)

$ErrorActionPreference = 'Stop'

$cube = $env:CUBEIDE_DIR
if (-not $cube) { $cube = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE' }
$plugins = Join-Path $cube 'plugins'

$gcc  = Get-ChildItem $plugins -Directory -Filter 'com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3*win32*' |
        Select-Object -First 1
$make = Get-ChildItem $plugins -Directory -Filter 'com.st.stm32cube.ide.mcu.externaltools.make.win32*' |
        Select-Object -First 1
if (-not $gcc -or -not $make) {
    Write-Error "toolchain or make plugin not found under $plugins (set CUBEIDE_DIR)"
}

$oldPath = $env:Path
Push-Location $PSScriptRoot
try {
    $env:Path = (Join-Path $gcc.FullName 'tools\bin') + ';' + (Join-Path $make.FullName 'tools\bin') + ';' + $oldPath
    & make @Goals ('DEMO=' + $Demo)
    $rc = $LASTEXITCODE
}
finally {
    $env:Path = $oldPath
    Pop-Location
}
exit $rc
