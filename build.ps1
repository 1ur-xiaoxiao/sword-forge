param([switch]$Test, [switch]$DebugBuild)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $compiler = Get-Command gcc -ErrorAction SilentlyContinue
    if ($compiler) { $compilerPath = $compiler.Source }
    elseif (Test-Path -LiteralPath 'D:\mingw64\bin\gcc.exe') { $compilerPath = 'D:\mingw64\bin\gcc.exe' }
    else { throw 'GCC was not found. Install MinGW-w64 and add its bin folder to PATH.' }
    New-Item -ItemType Directory -Path 'build','saves' -Force | Out-Null
    $common = @('src/data.c','src/game.c','src/forge.c','src/adventure.c','src/delve.c','src/save.c','src/game_api.c')
    $flags = @('-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-Wformat=2','-Iinclude','-finput-charset=UTF-8','-fexec-charset=UTF-8')
    if ($DebugBuild) { $flags += @('-O0','-g') } else { $flags += '-O2' }
    & $compilerPath @flags @common 'src/ui.c' 'src/main.c' '-o' 'build/forge_game.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Game compilation failed.' }
    Write-Host 'Built: build/forge_game.exe'
    & $compilerPath @flags @common 'src/web_server.c' '-o' 'build/sword_forge.exe' '-mwindows' '-lws2_32' '-lshell32'
    if ($LASTEXITCODE -ne 0) { throw 'Graphical game compilation failed.' }
    Write-Host 'Built: build/sword_forge.exe (graphical game, no console)'
    if ($Test) {
        & $compilerPath @flags @common 'tests/test_game.c' '-o' 'build/test_game.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed.' }
        & '.\build\test_game.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
        # Visual checks use a separate valid save fixture, never the player's save.
        & $compilerPath @flags @common 'tests/visual_fixture.c' '-o' 'build/visual_fixture.exe'
        if ($LASTEXITCODE -ne 0) { throw 'Visual fixture compilation failed.' }
    }
} finally { Pop-Location }
