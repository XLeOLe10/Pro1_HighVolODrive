param(
    [string]$Compiler = 'D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe'
)
$ErrorActionPreference = 'Stop'
$commandRepo = Split-Path -Parent $PSScriptRoot
$commandOldPath = $env:PATH
Push-Location -LiteralPath $commandRepo
try {
    $env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
    New-Item -ItemType Directory -Path 'build/command-host' -Force | Out-Null
    & $Compiler --version
    if ($LASTEXITCODE -ne 0) { throw 'HOST compiler unavailable' }
    & $Compiler -std=c99 -Wall -Wextra -Werror -pedantic -I Core/Inc `
        tests/axis_command_host_tests.c Core/Src/axis_command_core.c `
        -o build/command-host/axis_command_host_tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Command core HOST compile failed' }
    & './build/command-host/axis_command_host_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Command core HOST test failed' }
    & $Compiler -std=c99 -Wall -Wextra -Werror -I tests/host_stubs -I Core/Inc -I Core/math `
        tests/open_foc_command_host.c Core/Src/open_foc.c Core/Src/axis_command_core.c `
        Core/math/Xmath.c -o build/command-host/open_foc_command_host.exe
    if ($LASTEXITCODE -ne 0) { throw 'FOC integration HOST compile failed' }
    & './build/command-host/open_foc_command_host.exe'
    if ($LASTEXITCODE -ne 0) { throw 'FOC integration HOST test failed' }
    Write-Output 'HOST command and FOC integration suites PASS'
} finally {
    $env:PATH = $commandOldPath
    Pop-Location
}
