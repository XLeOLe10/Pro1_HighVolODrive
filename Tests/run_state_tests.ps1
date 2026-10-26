$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    & gcc -std=c99 -Wall -Wextra -Werror -I Tests/stubs -I Core/Inc -I Core/math Tests/test_axis_state.c Core/Src/axis_state.c Core/Src/open_foc.c Core/Src/current_monitor.c Core/math/Xmath.c -lm -o build/test_axis_state.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & .\build\test_axis_state.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host state tests failed' }
}
finally {
    Pop-Location
}
