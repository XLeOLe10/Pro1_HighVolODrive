param(
    [string]$Compiler = 'D:\FPGA\QuartusII_13.0\quartus\bin64\cygwin\bin\gcc.exe'
)
$ErrorActionPreference = 'Stop'
$canServiceRepo = Split-Path -Parent $PSScriptRoot
$canServiceOldPath = $env:PATH
Push-Location -LiteralPath $canServiceRepo
try {
    $env:PATH = (Split-Path -Parent $Compiler) + ';' + $env:PATH
    New-Item -ItemType Directory -Path 'build/can-host' -Force | Out-Null
    # GCC 4.5 warns on the valid C99 aggregate {0} idiom used by HAL callers.
    & $Compiler -std=c99 -Wall -Wextra -Werror -Wno-missing-field-initializers -pedantic -I tests/can_link_stubs -I Core/Inc `
        tests/can_link_service_host.c Core/Src/can_link.c Core/Src/can_link_protocol.c `
        -o build/can-host/can_link_service_host.exe
    if ($LASTEXITCODE -ne 0) { throw 'CAN service HOST compile failed' }
    $canScenarios = @('valid', 'lengths', 'rejects', 'busy', 'rx-fail', 'tx-fail',
        'errors', 'busoff-ir', 'busoff-psr', 'ram-error', 'bad-index', 'overwrite')
    $canScenarios += @('query-valid', 'query-status', 'query-rejects', 'query-dlc',
        'query-busy', 'query-rx-fail', 'query-tx-fail', 'query-busoff', 'no-provider')
    $canScenarios += 0..6 | ForEach-Object { 'bad-config-' + $_ }
    $canScenarios += 1..4 | ForEach-Object { 'init-fail-' + $_ }
    foreach ($canScenario in $canScenarios) {
        & './build/can-host/can_link_service_host.exe' $canScenario
        if ($LASTEXITCODE -ne 0) { throw ('CAN service HOST failed: ' + $canScenario) }
    }
    Write-Output ('CAN service HOST: ' + $canScenarios.Count + ' scenarios PASS')
} finally {
    $env:PATH = $canServiceOldPath
    Pop-Location
}
