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
        tests/can_link_service_host.c Core/Src/can_link.c Core/Src/can_link_protocol.c Core/Src/axis_command_core.c `
        -o build/can-host/can_link_service_host.exe
    if ($LASTEXITCODE -ne 0) { throw 'CAN service HOST compile failed' }
    $canScenarios = @('valid', 'lengths', 'rejects', 'busy', 'rx-fail', 'tx-fail',
        'errors', 'busoff-ir', 'busoff-psr', 'ram-error', 'bad-index', 'overwrite')
    $canScenarios += @('query-valid', 'query-status', 'query-rejects', 'query-dlc',
        'query-busy', 'query-rx-fail', 'query-tx-fail', 'query-busoff', 'no-provider')
    $canScenarios += @('stop-valid', 'stop-ordering', 'stop-tx-busy', 'stop-tx-fail',
        'stop-rejected', 'stop-invalid', 'stop-dlc', 'stop-busoff', 'stop-rx-fail',
        'stop-no-provider', 'stop-max-id')
    $canScenarios += 0..3 | ForEach-Object { 'stop-core-' + $_ }
    $canScenarios += @('target-valid', 'target-rejects', 'target-dlc', 'target-missing',
        'target-bad-provider', 'target-core', 'stop-target-retained')
    $canScenarios += 0..3 | ForEach-Object { 'target-failure-' + $_ }
    $canScenarios += 0..2 | ForEach-Object { 'boot-state-' + $_ }
    $canScenarios += @('boot-rejects', 'boot-dlc', 'boot-missing', 'boot-bad-provider', 'stop-boot-retained')
    $canScenarios += 0..3 | ForEach-Object { 'boot-failure-' + $_ }
    $canScenarios += 0..6 | ForEach-Object { 'bad-config-' + $_ }
    $canScenarios += 1..4 | ForEach-Object { 'init-fail-' + $_ }
    $canScenarios += @('time-normal','time-metadata','time-progress','time-watch','time-nonfatal','time-core','stop-time-compatible')
    $canScenarios += 0..1 | ForEach-Object { 'time-sticky-' + $_ }
    $canScenarios += 1..6 | ForEach-Object { 'time-init-fail-' + $_ }
    $canScenarios += 1..4 | ForEach-Object { 'time-bad-effect-' + $_ }
    $canScenarios += 0..2 | ForEach-Object { 'time-init-boundary-' + $_ }
    $canScenarios += 0..25 | ForEach-Object { 'time-config-' + $_ }
    $canScenarios += 0..7 | ForEach-Object { 'time-geometry-' + $_ }
    $canScenarios += 0..13 | ForEach-Object { 'time-edge-' + $_ }
    $canScenarios += 0..6 | ForEach-Object { 'time-failure-' + $_ }
    $canScenarios += @('txn-valid','txn-epoch','txn-metadata','txn-cache','txn-max-id','txn-lease','stop-txn-staging')
    $canScenarios += 0..6 | ForEach-Object { 'txn-null-' + $_ }
    $canScenarios += 0..5 | ForEach-Object { 'txn-order-' + $_ }
    $canScenarios += 0..15 | ForEach-Object { 'txn-reject-' + $_ }
    $canScenarios += 0..7 | ForEach-Object { 'txn-life-' + $_ }
    $canScenarios += 0..1 | ForEach-Object { 'txn-tx-loss-' + $_ }
    $canScenarios += 0..4 | ForEach-Object { 'txn-submit-' + $_ }
    $canScenarios += 0..5 | ForEach-Object { 'txn-receipt-' + $_ }
    $canScenarios += 0..5 | ForEach-Object { 'txn-core-' + $_ }
    $canScenarios += 0..5 | ForEach-Object { 'txn-clear-' + $_ }
    foreach ($canScenario in $canScenarios) {
        & './build/can-host/can_link_service_host.exe' $canScenario
        if ($LASTEXITCODE -ne 0) { throw ('CAN service HOST failed: ' + $canScenario) }
    }
    Write-Output ('CAN service HOST: ' + $canScenarios.Count + ' scenarios PASS')
} finally {
    $env:PATH = $canServiceOldPath
    Pop-Location
}
