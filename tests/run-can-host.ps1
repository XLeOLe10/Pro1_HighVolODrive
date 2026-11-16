param(
    [string]$Compiler = 'D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe'
)
$ErrorActionPreference = 'Stop'
$canRepo = Split-Path -Parent $PSScriptRoot
$canOldPath = $env:PATH
Push-Location -LiteralPath $canRepo
try {
    $env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
    New-Item -ItemType Directory -Path 'build/can-host' -Force | Out-Null
    & $Compiler --version
    if ($LASTEXITCODE -ne 0) { throw 'HOST compiler unavailable' }
    & $Compiler -std=c99 -Wall -Wextra -Werror -pedantic -I Core/Inc `
        tests/can_link_protocol_host.c Core/Src/can_link_protocol.c `
        -o build/can-host/can_link_protocol_host.exe
    if ($LASTEXITCODE -ne 0) { throw 'CAN protocol HOST compile failed' }
    & './build/can-host/can_link_protocol_host.exe'
    if ($LASTEXITCODE -ne 0) { throw 'CAN protocol HOST test failed' }
    Write-Output 'HOST CAN protocol suite PASS'
} finally {
    $env:PATH = $canOldPath
    Pop-Location
}
