param([switch]$Test)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $compiler = Get-Command gcc -ErrorAction SilentlyContinue
    if ($compiler) { $compilerPath = $compiler.Source }
    elseif (Test-Path -LiteralPath 'D:\mingw64\bin\gcc.exe') { $compilerPath = 'D:\mingw64\bin\gcc.exe' }
    else { throw 'MinGW-w64 GCC is required to rebuild.' }
    $resourceCompiler = Join-Path (Split-Path $compilerPath) 'windres.exe'
    if (!(Test-Path -LiteralPath $resourceCompiler)) { throw 'windres.exe was not found beside GCC.' }
    New-Item -ItemType Directory -Path 'build','outputs' -Force | Out-Null
    & $resourceCompiler '-I' $PSScriptRoot '-i' 'src/assets.rc' '-o' 'build/assets.o'
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
    $sources = @('src/data.c','src/game.c','src/forge.c','src/adventure.c','src/delve.c','src/save.c','src/game_api.c','src/web_server.c')
    $flags = @('-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-Wformat=2','-Iinclude','-finput-charset=UTF-8','-fexec-charset=UTF-8','-O2','-DFORGE_EMBEDDED_ASSETS','-static','-static-libgcc','-s')
    # Build to a separate filename so an already running game is never overwritten.
    & $compilerPath @flags @sources 'build/assets.o' '-o' 'outputs/SwordForge.exe' '-mwindows' '-lws2_32' '-lshell32'
    if ($LASTEXITCODE -ne 0) { throw 'Single-file game compilation failed.' }
    $gameName = -join ([char]0x5251,[char]0x7089,[char]0x7269,[char]0x8BED)
    Copy-Item -LiteralPath 'outputs/SwordForge.exe' -Destination "$gameName.exe" -Force
    Copy-Item -LiteralPath 'outputs/SwordForge.exe' -Destination "outputs/$gameName.exe" -Force
    Write-Host "Built single-file game: $gameName.exe"
    if ($Test) {
        $env:FORGE_TEST_EXE = Join-Path $PSScriptRoot 'outputs/SwordForge.exe'
        & python 'tests/test_web.py'
        if ($LASTEXITCODE -ne 0) { throw 'Packaged API tests failed.' }
    }
} finally {
    Remove-Item Env:FORGE_TEST_EXE -ErrorAction SilentlyContinue
    Pop-Location
}
